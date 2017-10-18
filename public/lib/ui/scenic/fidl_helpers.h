// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "lib/ui/scenic/fidl/session.fidl.h"

namespace scenic_lib {

constexpr float kZeroesFloat3[3] = {0.f, 0.f, 0.f};
constexpr float kOnesFloat3[3] = {1.f, 1.f, 1.f};
// A quaterion that has no rotation.
constexpr float kQuaternionDefault[4] = {0.f, 0.f, 0.f, 1.f};

// Resource creation.
scenic::OpPtr NewCreateMemoryOp(uint32_t id,
                                zx::vmo vmo,
                                scenic::MemoryType memory_type);
scenic::OpPtr NewCreateImageOp(uint32_t id,
                               uint32_t memory_id,
                               uint32_t memory_offset,
                               scenic::ImageInfoPtr info);
scenic::OpPtr NewCreateImageOp(uint32_t id,
                               uint32_t memory_id,
                               uint32_t memory_offset,
                               scenic::ImageInfo::PixelFormat format,
                               scenic::ImageInfo::ColorSpace color_space,
                               scenic::ImageInfo::Tiling tiling,
                               uint32_t width,
                               uint32_t height,
                               uint32_t stride);
scenic::OpPtr NewCreateImagePipeOp(
    uint32_t id,
    ::fidl::InterfaceRequest<scenic::ImagePipe> request);
scenic::OpPtr NewCreateBufferOp(uint32_t id,
                                uint32_t memory_id,
                                uint32_t memory_offset,
                                uint32_t num_bytes);

scenic::OpPtr NewCreateDisplayCompositorOp(uint32_t id);
scenic::OpPtr NewCreateLayerStackOp(uint32_t id);
scenic::OpPtr NewCreateLayerOp(uint32_t id);

scenic::OpPtr NewCreateSceneOp(uint32_t id);
scenic::OpPtr NewCreateCameraOp(uint32_t id, uint32_t scene_id);
scenic::OpPtr NewCreateRendererOp(uint32_t id);

scenic::OpPtr NewCreateCircleOp(uint32_t id, float radius);
scenic::OpPtr NewCreateRectangleOp(uint32_t id, float width, float height);
scenic::OpPtr NewCreateRoundedRectangleOp(uint32_t id,
                                          float width,
                                          float height,
                                          float top_left_radius,
                                          float top_right_radius,
                                          float bottom_right_radius,
                                          float bottom_left_radius);

// Variant of NewCreateCircleOp that uses a variable radius instead of a
// constant one set at construction time.
scenic::OpPtr NewCreateVarCircleOp(uint32_t id, uint32_t radius_var_id);
// Variant of NewCreateRectangleOp that uses a variable width/height instead of
// constant ones set at construction time.
scenic::OpPtr NewCreateVarRectangleOp(uint32_t id,
                                      uint32_t width_var_id,
                                      uint32_t height_var_id);
// Variant of NewCreateRoundedRectangleOp that uses a variable width/height/etc.
// instead of constant ones set at construction time.
scenic::OpPtr NewCreateVarRoundedRectangleOp(
    uint32_t id,
    uint32_t width_var_id,
    uint32_t height_var_id,
    uint32_t top_left_radius_var_id,
    uint32_t top_right_radius_var_id,
    uint32_t bottom_left_radius_var_id,
    uint32_t bottom_right_radius_var_id);

scenic::OpPtr NewCreateMeshOp(uint32_t id);
scenic::OpPtr NewCreateMaterialOp(uint32_t id);
scenic::OpPtr NewCreateClipNodeOp(uint32_t id);
scenic::OpPtr NewCreateEntityNodeOp(uint32_t id);
scenic::OpPtr NewCreateShapeNodeOp(uint32_t id);
scenic::OpPtr NewCreateVariableFloatOp(uint32_t id, float inital_val);

scenic::OpPtr NewReleaseResourceOp(uint32_t id);

// Export & Import operations.
scenic::OpPtr NewExportResourceOp(uint32_t resource_id,
                                  zx::eventpair export_token);
scenic::OpPtr NewImportResourceOp(uint32_t resource_id,
                                  scenic::ImportSpec spec,
                                  zx::eventpair import_token);

// Exports the resource and returns an import token in |out_import_token|
// which allows it to be imported into other sessions.
scenic::OpPtr NewExportResourceOpAsRequest(uint32_t resource_id,
                                           zx::eventpair* out_import_token);

// Imports the resource and returns an export token in |out_export_token|
// by which another session can export a resource to associate with this import.
scenic::OpPtr NewImportResourceOpAsRequest(uint32_t resource_id,
                                           scenic::ImportSpec import_spec,
                                           zx::eventpair* out_export_token);

// Node operations.
scenic::OpPtr NewAddChildOp(uint32_t node_id, uint32_t child_id);
scenic::OpPtr NewAddPartOp(uint32_t node_id, uint32_t part_id);
scenic::OpPtr NewDetachOp(uint32_t node_id);
scenic::OpPtr NewDetachChildrenOp(uint32_t node_id);
scenic::OpPtr NewSetTranslationOp(uint32_t node_id, const float translation[3]);
scenic::OpPtr NewSetScaleOp(uint32_t node_id, const float scale[3]);
scenic::OpPtr NewSetRotationOp(uint32_t node_id, const float quaternion[4]);
scenic::OpPtr NewSetAnchorOp(uint32_t node_id, const float anchor[3]);

scenic::OpPtr NewSetShapeOp(uint32_t node_id, uint32_t shape_id);
scenic::OpPtr NewSetMaterialOp(uint32_t node_id, uint32_t material_id);
scenic::OpPtr NewSetClipOp(uint32_t node_id,
                           uint32_t clip_id,
                           bool clip_to_self);
scenic::OpPtr NewSetTagOp(uint32_t node_id, uint32_t tag_value);
scenic::OpPtr NewSetHitTestBehaviorOp(
    uint32_t node_id,
    scenic::HitTestBehavior hit_test_behavior);

// Camera and lighting operations.
scenic::OpPtr NewSetCameraOp(uint32_t renderer_id, uint32_t camera_id);
scenic::OpPtr NewSetCameraProjectionOp(uint32_t camera_id,
                                       const float eye_position[3],
                                       const float eye_look_at[3],
                                       const float eye_up[3],
                                       float fovy);

// Material operations.
scenic::OpPtr NewSetTextureOp(uint32_t node_id, uint32_t image_id);
scenic::OpPtr NewSetColorOp(uint32_t node_id,
                            uint8_t red,
                            uint8_t green,
                            uint8_t blue,
                            uint8_t alpha);

// Mesh operations.
scenic::MeshVertexFormatPtr NewMeshVertexFormat(
    scenic::ValueType position_type,
    scenic::ValueType normal_type,
    scenic::ValueType tex_coord_type);
// These arguments are documented in ops.fidl; see BindMeshBuffersOp.
scenic::OpPtr NewBindMeshBuffersOp(uint32_t mesh_id,
                                   uint32_t index_buffer_id,
                                   scenic::MeshIndexFormat index_format,
                                   uint64_t index_offset,
                                   uint32_t index_count,
                                   uint32_t vertex_buffer_id,
                                   scenic::MeshVertexFormatPtr vertex_format,
                                   uint64_t vertex_offset,
                                   uint32_t vertex_count,
                                   const float bounding_box_min[3],
                                   const float bounding_box_max[3]);

// Layer / LayerStack / Compositor operations.
scenic::OpPtr NewAddLayerOp(uint32_t layer_stack_id, uint32_t layer_id);
scenic::OpPtr NewSetLayerStackOp(uint32_t compositor_id,
                                 uint32_t layer_stack_id);
scenic::OpPtr NewSetRendererOp(uint32_t layer_id, uint32_t renderer_id);
scenic::OpPtr NewSetRendererParamOp(uint32_t renderer_id,
                                    scenic::RendererParamPtr param);
scenic::OpPtr NewSetSizeOp(uint32_t node_id, const float size[2]);

// Event operations.
scenic::OpPtr NewSetEventMaskOp(uint32_t resource_id, uint32_t event_mask);

// Diagnostic operations.
scenic::OpPtr NewSetLabelOp(uint32_t resource_id, const std::string& label);

// Debugging operations.
scenic::OpPtr NewSetDisableClippingOp(uint32_t resource_id,
                                      bool disable_clipping);

// Basic types.

scenic::FloatValuePtr NewFloatValue(float value);
scenic::Vector2ValuePtr NewVector2Value(const float value[2]);
scenic::Vector3ValuePtr NewVector3Value(const float value[3]);
scenic::Vector4ValuePtr NewVector4Value(const float value[4]);
scenic::Matrix4ValuePtr NewMatrix4Value(const float value[16]);
scenic::ColorRgbaValuePtr NewColorRgbaValue(const uint8_t value[4]);
scenic::QuaternionValuePtr NewQuaternionValue(const float value[4]);

}  // namespace scenic_lib
