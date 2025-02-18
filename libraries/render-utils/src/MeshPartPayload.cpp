//
//  MeshPartPayload.cpp
//  interface/src/renderer
//
//  Created by Sam Gateau on 10/3/15.
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "MeshPartPayload.h"

#include <PerfStat.h>

#include "DeferredLightingEffect.h"
#include "Model.h"

using namespace render;

namespace render {
template <> const ItemKey payloadGetKey(const MeshPartPayload::Pointer& payload) {
    if (payload) {
        return payload->getKey();
    }
    return ItemKey::Builder::opaqueShape(); // for lack of a better idea
}

template <> const Item::Bound payloadGetBound(const MeshPartPayload::Pointer& payload) {
    if (payload) {
        return payload->getBound();
    }
    return Item::Bound();
}

template <> const ShapeKey shapeGetShapeKey(const MeshPartPayload::Pointer& payload) {
    if (payload) {
        return payload->getShapeKey();
    }
    return ShapeKey::Builder::invalid();
}

template <> void payloadRender(const MeshPartPayload::Pointer& payload, RenderArgs* args) {
    return payload->render(args);
}
}

MeshPartPayload::MeshPartPayload(const std::shared_ptr<const model::Mesh>& mesh, int partIndex, model::MaterialPointer material, const Transform& transform, const Transform& offsetTransform) {

    updateMeshPart(mesh, partIndex);
    updateMaterial(material);
    updateTransform(transform, offsetTransform);
}

void MeshPartPayload::updateMeshPart(const std::shared_ptr<const model::Mesh>& drawMesh, int partIndex) {
    _drawMesh = drawMesh;
    if (_drawMesh) {
        auto vertexFormat = _drawMesh->getVertexFormat();
        _hasColorAttrib = vertexFormat->hasAttribute(gpu::Stream::COLOR);
        _drawPart = _drawMesh->getPartBuffer().get<model::Mesh::Part>(partIndex);
        _localBound = _drawMesh->evalPartBound(partIndex);
    }
}

void MeshPartPayload::updateTransform(const Transform& transform, const Transform& offsetTransform) {
    _transform = transform;
    _offsetTransform = offsetTransform;
    Transform::mult(_drawTransform, _transform, _offsetTransform);
    _worldBound = _localBound;
    _worldBound.transform(_drawTransform);
}

void MeshPartPayload::updateMaterial(model::MaterialPointer drawMaterial) {
    _drawMaterial = drawMaterial;
}

ItemKey MeshPartPayload::getKey() const {
    ItemKey::Builder builder;
    builder.withTypeShape();

    if (_drawMaterial) {
        auto matKey = _drawMaterial->getKey();
        if (matKey.isTranslucent()) {
            builder.withTransparent();
        }
    }

    return builder.build();
}

Item::Bound MeshPartPayload::getBound() const {
    return _worldBound;
}

ShapeKey MeshPartPayload::getShapeKey() const {
    model::MaterialKey drawMaterialKey;
    if (_drawMaterial) {
        drawMaterialKey = _drawMaterial->getKey();
    }

    ShapeKey::Builder builder;
    if (drawMaterialKey.isTranslucent()) {
        builder.withTranslucent();
    }
    if (drawMaterialKey.isNormalMap()) {
        builder.withTangents();
    }
    if (drawMaterialKey.isMetallicMap()) {
        builder.withSpecular();
    }
    if (drawMaterialKey.isLightmapMap()) {
        builder.withLightmap();
    }
    return builder.build();
}

void MeshPartPayload::drawCall(gpu::Batch& batch) const {
    batch.drawIndexed(gpu::TRIANGLES, _drawPart._numIndices, _drawPart._startIndex);
}

void MeshPartPayload::bindMesh(gpu::Batch& batch) const {
    batch.setIndexBuffer(gpu::UINT32, (_drawMesh->getIndexBuffer()._buffer), 0);

    batch.setInputFormat((_drawMesh->getVertexFormat()));

    batch.setInputStream(0, _drawMesh->getVertexStream());

    // TODO: Get rid of that extra call
    if (!_hasColorAttrib) {
        batch._glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

void MeshPartPayload::bindMaterial(gpu::Batch& batch, const ShapePipeline::LocationsPointer locations) const {
    if (!_drawMaterial) {
        return;
    }

    auto textureCache = DependencyManager::get<TextureCache>();

    batch.setUniformBuffer(ShapePipeline::Slot::MATERIAL_GPU, _drawMaterial->getSchemaBuffer());

    auto materialKey = _drawMaterial->getKey();
    auto textureMaps = _drawMaterial->getTextureMaps();
    glm::mat4 texcoordTransform[2];

    // Albedo
    if (materialKey.isAlbedoMap()) {
        auto albedoMap = textureMaps[model::MaterialKey::ALBEDO_MAP];
        if (albedoMap && albedoMap->isDefined()) {
            batch.setResourceTexture(ShapePipeline::Slot::ALBEDO_MAP, albedoMap->getTextureView());

            if (!albedoMap->getTextureTransform().isIdentity()) {
                albedoMap->getTextureTransform().getMatrix(texcoordTransform[0]);
            }
        } else {
            batch.setResourceTexture(ShapePipeline::Slot::ALBEDO_MAP, textureCache->getGrayTexture());
        }
    } else {
        batch.setResourceTexture(ShapePipeline::Slot::ALBEDO_MAP, textureCache->getWhiteTexture());
    }

    // Roughness map
    if (materialKey.isRoughnessMap()) {
        auto roughnessMap = textureMaps[model::MaterialKey::ROUGHNESS_MAP];
        if (roughnessMap && roughnessMap->isDefined()) {
            batch.setResourceTexture(ShapePipeline::Slot::ROUGHNESS_MAP, roughnessMap->getTextureView());

            // texcoord are assumed to be the same has albedo
        } else {
            batch.setResourceTexture(ShapePipeline::Slot::ROUGHNESS_MAP, textureCache->getWhiteTexture());
        }
    } else {
        batch.setResourceTexture(ShapePipeline::Slot::ROUGHNESS_MAP, textureCache->getWhiteTexture());
    }

    // Normal map
    if (materialKey.isNormalMap()) {
        auto normalMap = textureMaps[model::MaterialKey::NORMAL_MAP];
        if (normalMap && normalMap->isDefined()) {
            batch.setResourceTexture(ShapePipeline::Slot::NORMAL_MAP, normalMap->getTextureView());

            // texcoord are assumed to be the same has albedo
        } else {
            batch.setResourceTexture(ShapePipeline::Slot::NORMAL_MAP, textureCache->getBlueTexture());
        }
    } else {
        batch.setResourceTexture(ShapePipeline::Slot::NORMAL_MAP, nullptr);
    }

    // Metallic map
    if (materialKey.isMetallicMap()) {
        auto specularMap = textureMaps[model::MaterialKey::METALLIC_MAP];
        if (specularMap && specularMap->isDefined()) {
            batch.setResourceTexture(ShapePipeline::Slot::METALLIC_MAP, specularMap->getTextureView());

            // texcoord are assumed to be the same has albedo
        } else {
            batch.setResourceTexture(ShapePipeline::Slot::METALLIC_MAP, textureCache->getBlackTexture());
        }
    } else {
        batch.setResourceTexture(ShapePipeline::Slot::METALLIC_MAP, nullptr);
    }

    // Occlusion map
    if (materialKey.isOcclusionMap()) {
        auto specularMap = textureMaps[model::MaterialKey::OCCLUSION_MAP];
        if (specularMap && specularMap->isDefined()) {
            batch.setResourceTexture(ShapePipeline::Slot::OCCLUSION_MAP, specularMap->getTextureView());

            // texcoord are assumed to be the same has albedo
        } else {
            batch.setResourceTexture(ShapePipeline::Slot::OCCLUSION_MAP, textureCache->getWhiteTexture());
        }
    } else {
        batch.setResourceTexture(ShapePipeline::Slot::OCCLUSION_MAP, nullptr);
    }

    // Emissive / Lightmap
    if (materialKey.isLightmapMap()) {
        auto lightmapMap = textureMaps[model::MaterialKey::LIGHTMAP_MAP];

        if (lightmapMap && lightmapMap->isDefined()) {
            batch.setResourceTexture(ShapePipeline::Slot::EMISSIVE_LIGHTMAP_MAP, lightmapMap->getTextureView());

            auto lightmapOffsetScale = lightmapMap->getLightmapOffsetScale();
            batch._glUniform2f(locations->emissiveParams, lightmapOffsetScale.x, lightmapOffsetScale.y);

            if (!lightmapMap->getTextureTransform().isIdentity()) {
                lightmapMap->getTextureTransform().getMatrix(texcoordTransform[1]);
            }
        } else {
            batch.setResourceTexture(ShapePipeline::Slot::EMISSIVE_LIGHTMAP_MAP, textureCache->getGrayTexture());
        }
    } else if (materialKey.isEmissiveMap()) {
        auto emissiveMap = textureMaps[model::MaterialKey::EMISSIVE_MAP];

        if (emissiveMap && emissiveMap->isDefined()) {
            batch.setResourceTexture(ShapePipeline::Slot::EMISSIVE_LIGHTMAP_MAP, emissiveMap->getTextureView());
        } else {
            batch.setResourceTexture(ShapePipeline::Slot::EMISSIVE_LIGHTMAP_MAP, textureCache->getBlackTexture());
        }
    } else {
        batch.setResourceTexture(ShapePipeline::Slot::EMISSIVE_LIGHTMAP_MAP, nullptr);
    }

    // Texcoord transforms ?
    if (locations->texcoordMatrices >= 0) {
        batch._glUniformMatrix4fv(locations->texcoordMatrices, 2, false, (const float*)&texcoordTransform);
    }
}

void MeshPartPayload::bindTransform(gpu::Batch& batch, const ShapePipeline::LocationsPointer locations, bool canCauterize) const {
    batch.setModelTransform(_drawTransform);
}


void MeshPartPayload::render(RenderArgs* args) const {
    PerformanceTimer perfTimer("MeshPartPayload::render");

    gpu::Batch& batch = *(args->_batch);

    auto locations = args->_pipeline->locations;
    assert(locations);

    // Bind the model transform and the skinCLusterMatrices if needed
    bindTransform(batch, locations);

    //Bind the index buffer and vertex buffer and Blend shapes if needed
    bindMesh(batch);

    // apply material properties
    bindMaterial(batch, locations);

    if (args) {
        args->_details._materialSwitches++;
    }

    // Draw!
    {
        PerformanceTimer perfTimer("batch.drawIndexed()");
        drawCall(batch);
    }

    if (args) {
        const int INDICES_PER_TRIANGLE = 3;
        args->_details._trianglesRendered += _drawPart._numIndices / INDICES_PER_TRIANGLE;
    }
}

namespace render {
template <> const ItemKey payloadGetKey(const ModelMeshPartPayload::Pointer& payload) {
    if (payload) {
        return payload->getKey();
    }
    return ItemKey::Builder::opaqueShape(); // for lack of a better idea
}

template <> const Item::Bound payloadGetBound(const ModelMeshPartPayload::Pointer& payload) {
    if (payload) {
        return payload->getBound();
    }
    return Item::Bound();
}

template <> const ShapeKey shapeGetShapeKey(const ModelMeshPartPayload::Pointer& payload) {
    if (payload) {
        return payload->getShapeKey();
    }
    return ShapeKey::Builder::invalid();
}

template <> void payloadRender(const ModelMeshPartPayload::Pointer& payload, RenderArgs* args) {
    return payload->render(args);
}
}

ModelMeshPartPayload::ModelMeshPartPayload(Model* model, int _meshIndex, int partIndex, int shapeIndex, const Transform& transform, const Transform& offsetTransform) :
    _model(model),
    _meshIndex(_meshIndex),
    _shapeID(shapeIndex) {

    assert(_model && _model->isLoaded());
    auto& modelMesh = _model->getGeometry()->getGeometry()->getMeshes().at(_meshIndex);
    updateMeshPart(modelMesh, partIndex);

    updateTransform(transform, offsetTransform);
    initCache();
}

void ModelMeshPartPayload::initCache() {
    assert(_model->isLoaded());

    if (_drawMesh) {
        auto vertexFormat = _drawMesh->getVertexFormat();
        _hasColorAttrib = vertexFormat->hasAttribute(gpu::Stream::COLOR);
        _isSkinned = vertexFormat->hasAttribute(gpu::Stream::SKIN_CLUSTER_WEIGHT) && vertexFormat->hasAttribute(gpu::Stream::SKIN_CLUSTER_INDEX);

        const FBXGeometry& geometry = _model->getFBXGeometry();
        const FBXMesh& mesh = geometry.meshes.at(_meshIndex);

        _isBlendShaped = !mesh.blendshapes.isEmpty();
    }

    auto networkMaterial = _model->getGeometry()->getGeometry()->getShapeMaterial(_shapeID);
    if (networkMaterial) {
        _drawMaterial = networkMaterial;
    };

}


void ModelMeshPartPayload::notifyLocationChanged() {

}

void ModelMeshPartPayload::updateTransformForSkinnedMesh(const Transform& transform, const Transform& offsetTransform, const QVector<glm::mat4>& clusterMatrices) {
    ModelMeshPartPayload::updateTransform(transform, offsetTransform);

    if (clusterMatrices.size() > 0) {
        _worldBound = AABox();
        for (auto& clusterMatrix : clusterMatrices) {
            AABox clusterBound = _localBound;
            clusterBound.transform(clusterMatrix);
            _worldBound += clusterBound;
        }

        // clusterMatrix has world rotation but not world translation.
        _worldBound.translate(transform.getTranslation());
    }
}

ItemKey ModelMeshPartPayload::getKey() const {
    ItemKey::Builder builder;
    builder.withTypeShape();

    if (!_model->isVisible()) {
        builder.withInvisible();
    }

    if (_isBlendShaped || _isSkinned) {
        builder.withDeformed();
    }

    if (_drawMaterial) {
        auto matKey = _drawMaterial->getKey();
        if (matKey.isTranslucent()) {
            builder.withTransparent();
        }
    }

    return builder.build();
}

ShapeKey ModelMeshPartPayload::getShapeKey() const {
    assert(_model->isLoaded());
    const FBXGeometry& geometry = _model->getFBXGeometry();
    const auto& networkMeshes = _model->getGeometry()->getGeometry()->getMeshes();

    // guard against partially loaded meshes
    if (_meshIndex >= (int)networkMeshes.size() || _meshIndex >= (int)geometry.meshes.size() || _meshIndex >= (int)_model->_meshStates.size()) {
        return ShapeKey::Builder::invalid();
    }

    const FBXMesh& mesh = geometry.meshes.at(_meshIndex);

    // if our index is ever out of range for either meshes or networkMeshes, then skip it, and set our _meshGroupsKnown
    // to false to rebuild out mesh groups.
    if (_meshIndex < 0 || _meshIndex >= (int)networkMeshes.size() || _meshIndex > geometry.meshes.size()) {
        _model->_meshGroupsKnown = false; // regenerate these lists next time around.
        _model->_readyWhenAdded = false; // in case any of our users are using scenes
        _model->invalidCalculatedMeshBoxes(); // if we have to reload, we need to assume our mesh boxes are all invalid
        return ShapeKey::Builder::invalid();
    }


    int vertexCount = mesh.vertices.size();
    if (vertexCount == 0) {
        // sanity check
        return ShapeKey::Builder::invalid(); // FIXME
    }


    model::MaterialKey drawMaterialKey;
    if (_drawMaterial) {
        drawMaterialKey = _drawMaterial->getKey();
    }

    bool isTranslucent = drawMaterialKey.isTranslucent();
    bool hasTangents = drawMaterialKey.isNormalMap() && !mesh.tangents.isEmpty();
    bool hasSpecular = drawMaterialKey.isMetallicMap();
    bool hasLightmap = drawMaterialKey.isLightmapMap();

    bool isSkinned = _isSkinned;
    bool wireframe = _model->isWireframe();

    if (wireframe) {
        isTranslucent = hasTangents = hasSpecular = hasLightmap = isSkinned = false;
    }

    ShapeKey::Builder builder;
    if (isTranslucent) {
        builder.withTranslucent();
    }
    if (hasTangents) {
        builder.withTangents();
    }
    if (hasSpecular) {
        builder.withSpecular();
    }
    if (hasLightmap) {
        builder.withLightmap();
    }
    if (isSkinned) {
        builder.withSkinned();
    }
    if (wireframe) {
        builder.withWireframe();
    }
    return builder.build();
}

void ModelMeshPartPayload::bindMesh(gpu::Batch& batch) const {
    if (!_isBlendShaped) {
        batch.setIndexBuffer(gpu::UINT32, (_drawMesh->getIndexBuffer()._buffer), 0);

        batch.setInputFormat((_drawMesh->getVertexFormat()));

        batch.setInputStream(0, _drawMesh->getVertexStream());
    } else {
        batch.setIndexBuffer(gpu::UINT32, (_drawMesh->getIndexBuffer()._buffer), 0);

        batch.setInputFormat((_drawMesh->getVertexFormat()));

        batch.setInputBuffer(0, _model->_blendedVertexBuffers[_meshIndex], 0, sizeof(glm::vec3));
        batch.setInputBuffer(1, _model->_blendedVertexBuffers[_meshIndex], _drawMesh->getNumVertices() * sizeof(glm::vec3), sizeof(glm::vec3));
        batch.setInputStream(2, _drawMesh->getVertexStream().makeRangedStream(2));
    }

    // TODO: Get rid of that extra call
    if (!_hasColorAttrib) {
        batch._glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

void ModelMeshPartPayload::bindTransform(gpu::Batch& batch, const ShapePipeline::LocationsPointer locations, bool canCauterize) const {
    // Still relying on the raw data from the model
    const Model::MeshState& state = _model->_meshStates.at(_meshIndex);

    Transform transform;
    if (state.clusterBuffer) {
        if (canCauterize && _model->getCauterizeBones()) {
            batch.setUniformBuffer(ShapePipeline::Slot::SKINNING_GPU, state.cauterizedClusterBuffer);
        } else {
            batch.setUniformBuffer(ShapePipeline::Slot::SKINNING_GPU, state.clusterBuffer);
        }
    } else {
        if (canCauterize && _model->getCauterizeBones()) {
            transform = Transform(state.cauterizedClusterMatrices[0]);
        } else {
            transform = Transform(state.clusterMatrices[0]);
        }
    }

    transform.preTranslate(_transform.getTranslation());
    batch.setModelTransform(transform);
}


void ModelMeshPartPayload::render(RenderArgs* args) const {
    PerformanceTimer perfTimer("ModelMeshPartPayload::render");

    if (!_model->_readyWhenAdded || !_model->_isVisible) {
        return; // bail asap
    }

    gpu::Batch& batch = *(args->_batch);

    if (!getShapeKey().isValid()) {
        return;
    }

    // render the part bounding box
#ifdef DEBUG_BOUNDING_PARTS
    {
        AABox partBounds = getPartBounds(_meshIndex, partIndex);

        glm::vec4 cubeColor(1.0f, 1.0f, 0.0f, 1.0f);
        if (isSkinned) {
            cubeColor = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f);
        } else if (args->_viewFrustum->boxIntersectsFrustum(partBounds)) {
            cubeColor = glm::vec4(1.0f, 0.0f, 1.0f, 1.0f);
        }

        Transform transform;
        transform.setTranslation(partBounds.calcCenter());
        transform.setScale(partBounds.getDimensions());
        batch.setModelTransform(transform);
        DependencyManager::get<GeometryCache>()->renderWireCube(batch, 1.0f, cubeColor);
    }
#endif //def DEBUG_BOUNDING_PARTS

    auto locations =  args->_pipeline->locations;
    assert(locations);

    // Bind the model transform and the skinCLusterMatrices if needed
    bool canCauterize = args->_renderMode != RenderArgs::SHADOW_RENDER_MODE;
    _model->updateClusterMatrices(_transform.getTranslation(), _transform.getRotation());
    bindTransform(batch, locations, canCauterize);

    //Bind the index buffer and vertex buffer and Blend shapes if needed
    bindMesh(batch);

    // apply material properties
    bindMaterial(batch, locations);

    if (args) {
        args->_details._materialSwitches++;
    }

    // Draw!
    {
        PerformanceTimer perfTimer("batch.drawIndexed()");
        drawCall(batch);
    }

    if (args) {
        const int INDICES_PER_TRIANGLE = 3;
        args->_details._trianglesRendered += _drawPart._numIndices / INDICES_PER_TRIANGLE;
    }
}

