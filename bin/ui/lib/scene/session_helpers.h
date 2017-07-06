// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/services/scene/session.fidl.h"

namespace mozart {

constexpr float kZeroesFloat3[3] = {0.f, 0.f, 0.f};
constexpr float kOnesFloat3[3] = {1.f, 1.f, 1.f};
// A quaterion that has no rotation.
constexpr float kQuaternionDefault[4] = {0.f, 0.f, 0.f, 1.f};

// Resource creation.
mozart2::OpPtr NewCreateMemoryOp(uint32_t id,
                                 mx::vmo vmo,
                                 mozart2::MemoryType memory_type);
mozart2::OpPtr NewCreateImageOp(uint32_t id,
                                uint32_t memory_id,
                                uint32_t memory_offset,
                                mozart2::ImageInfoPtr info);
mozart2::OpPtr NewCreateImageOp(uint32_t id,
                                uint32_t memory_id,
                                uint32_t memory_offset,
                                mozart2::ImageInfo::PixelFormat format,
                                mozart2::ImageInfo::ColorSpace color_space,
                                mozart2::ImageInfo::Tiling tiling,
                                uint32_t width,
                                uint32_t height,
                                uint32_t stride);
mozart2::OpPtr NewCreateImagePipeOp(
    uint32_t id,
    ::fidl::InterfaceRequest<mozart2::ImagePipe> request);
mozart2::OpPtr NewCreateBufferOp(uint32_t id,
                                 uint32_t memory_id,
                                 uint32_t memory_offset,
                                 uint32_t num_bytes);
mozart2::OpPtr NewCreateSceneOp(uint32_t id);
mozart2::OpPtr NewCreateCameraOp(uint32_t id, uint32_t scene_id);
mozart2::OpPtr NewCreateDisplayRendererOp(uint32_t id);

mozart2::OpPtr NewCreateCircleOp(uint32_t id, float radius);
mozart2::OpPtr NewCreateRectangleOp(uint32_t id, float width, float height);
mozart2::OpPtr NewCreateRoundedRectangleOp(uint32_t id,
                                           float width,
                                           float height,
                                           float top_left_radius,
                                           float top_right_radius,
                                           float bottom_right_radius,
                                           float bottom_left_radius);

// Variant of NewCreateCircleOp that uses a variable radius instead of a
// constant one set at construction time.
mozart2::OpPtr NewCreateVarCircleOp(uint32_t id, uint32_t radius_var_id);
// Variant of NewCreateRectangleOp that uses a variable width/height instead of
// constant ones set at construction time.
mozart2::OpPtr NewCreateVarRectangleOp(uint32_t id,
                                       uint32_t width_var_id,
                                       uint32_t height_var_id);
// Variant of NewCreateRoundedRectangleOp that uses a variable width/height/etc.
// instead of constant ones set at construction time.
mozart2::OpPtr NewCreateVarRoundedRectangleOp(
    uint32_t id,
    uint32_t width_var_id,
    uint32_t height_var_id,
    uint32_t top_left_radius_var_id,
    uint32_t top_right_radius_var_id,
    uint32_t bottom_left_radius_var_id,
    uint32_t bottom_right_radius_var_id);

mozart2::OpPtr NewCreateMaterialOp(uint32_t id);
mozart2::OpPtr NewCreateClipNodeOp(uint32_t id);
mozart2::OpPtr NewCreateEntityNodeOp(uint32_t id);
mozart2::OpPtr NewCreateShapeNodeOp(uint32_t id);
mozart2::OpPtr NewCreateVariableFloatOp(uint32_t id, float inital_val);

mozart2::OpPtr NewReleaseResourceOp(uint32_t id);

// Export & Import operations.
mozart2::OpPtr NewExportResourceOp(uint32_t resource_id,
                                   mx::eventpair export_token);
mozart2::OpPtr NewImportResourceOp(uint32_t resource_id,
                                   mozart2::ImportSpec spec,
                                   mx::eventpair import_token);

// Exports the resource and returns an import token in |out_import_token|
// which allows it to be imported into other sessions.
mozart2::OpPtr NewExportResourceOpAsRequest(uint32_t resource_id,
                                            mx::eventpair* out_import_token);

// Imports the resource and returns an export token in |out_export_token|
// by which another session can export a resource to associate with this import.
mozart2::OpPtr NewImportResourceOpAsRequest(uint32_t resource_id,
                                            mozart2::ImportSpec import_spec,
                                            mx::eventpair* out_export_token);

// Node operations.
mozart2::OpPtr NewAddChildOp(uint32_t node_id, uint32_t child_id);
mozart2::OpPtr NewAddPartOp(uint32_t node_id, uint32_t part_id);
mozart2::OpPtr NewDetachOp(uint32_t node_id);
mozart2::OpPtr NewDetachChildrenOp(uint32_t node_id);
mozart2::OpPtr NewSetTranslationOp(uint32_t node_id,
                                   const float translation[3]);
mozart2::OpPtr NewSetScaleOp(uint32_t node_id, const float scale[3]);
mozart2::OpPtr NewSetRotationOp(uint32_t node_id, const float quaternion[4]);
mozart2::OpPtr NewSetAnchorOp(uint32_t node_id, const float anchor[3]);

mozart2::OpPtr NewSetShapeOp(uint32_t node_id, uint32_t shape_id);
mozart2::OpPtr NewSetMaterialOp(uint32_t node_id, uint32_t material_id);
mozart2::OpPtr NewSetClipOp(uint32_t node_id,
                            uint32_t clip_id,
                            bool clip_to_self);
mozart2::OpPtr NewSetTagOp(uint32_t node_id, uint32_t tag_value);

// Camera and lighting operations.
mozart2::OpPtr NewSetCameraOp(uint32_t renderer_id, uint32_t camera_id);
mozart2::OpPtr NewSetCameraProjectionOp(uint32_t camera_id,
                                        const float eye_position[3],
                                        const float eye_look_at[3],
                                        const float eye_up[3],
                                        float fovy);

// Material operations.
mozart2::OpPtr NewSetTextureOp(uint32_t node_id, uint32_t image_id);
mozart2::OpPtr NewSetColorOp(uint32_t node_id,
                             uint8_t red,
                             uint8_t green,
                             uint8_t blue,
                             uint8_t alpha);

// Basic types.

mozart2::FloatValuePtr NewFloatValue(float value);
mozart2::Vector2ValuePtr NewVector2Value(const float value[2]);
mozart2::Vector3ValuePtr NewVector3Value(const float value[3]);
mozart2::Vector4ValuePtr NewVector4Value(const float value[4]);
mozart2::Matrix4ValuePtr NewMatrix4Value(const float value[16]);
mozart2::ColorRgbaValuePtr NewColorRgbaValue(const uint8_t value[4]);
mozart2::QuaternionValuePtr NewQuaternionValue(const float value[4]);

}  // namespace mozart
