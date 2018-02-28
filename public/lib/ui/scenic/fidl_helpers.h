// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_SCENIC_FIDL_HELPERS_H_
#define LIB_UI_SCENIC_FIDL_HELPERS_H_

#include <string>

#include "lib/ui/scenic/fidl/commands.fidl.h"

namespace scenic_lib {

constexpr float kZeroesFloat3[3] = {0.f, 0.f, 0.f};
constexpr float kOnesFloat3[3] = {1.f, 1.f, 1.f};
// A quaterion that has no rotation.
constexpr float kQuaternionDefault[4] = {0.f, 0.f, 0.f, 1.f};

// Helper function for wrapping Scenic ops as Mozart commands.
ui::CommandPtr NewCommand(ui::gfx::CommandPtr command);

// Resource creation.
ui::gfx::CommandPtr NewCreateMemoryCommand(uint32_t id,
                                zx::vmo vmo,
                                ui::gfx::MemoryType memory_type);
ui::gfx::CommandPtr NewCreateImageCommand(uint32_t id,
                               uint32_t memory_id,
                               uint32_t memory_offset,
                               ui::gfx::ImageInfoPtr info);
ui::gfx::CommandPtr NewCreateImageCommand(uint32_t id,
                               uint32_t memory_id,
                               uint32_t memory_offset,
                               ui::gfx::ImageInfo::PixelFormat format,
                               ui::gfx::ImageInfo::ColorSpace color_space,
                               ui::gfx::ImageInfo::Tiling tiling,
                               uint32_t width,
                               uint32_t height,
                               uint32_t stride);
ui::gfx::CommandPtr NewCreateImagePipeCommand(
    uint32_t id,
    ::f1dl::InterfaceRequest<ui::gfx::ImagePipe> request);
ui::gfx::CommandPtr NewCreateBufferCommand(uint32_t id,
                                uint32_t memory_id,
                                uint32_t memory_offset,
                                uint32_t num_bytes);

ui::gfx::CommandPtr NewCreateDisplayCompositorCommand(uint32_t id);
ui::gfx::CommandPtr NewCreateLayerStackCommand(uint32_t id);
ui::gfx::CommandPtr NewCreateLayerCommand(uint32_t id);

ui::gfx::CommandPtr NewCreateSceneCommand(uint32_t id);
ui::gfx::CommandPtr NewCreateCameraCommand(uint32_t id, uint32_t scene_id);
ui::gfx::CommandPtr NewCreateRendererCommand(uint32_t id);
ui::gfx::CommandPtr NewCreateAmbientLightCommand(uint32_t id);
ui::gfx::CommandPtr NewCreateDirectionalLightCommand(uint32_t id);

ui::gfx::CommandPtr NewCreateCircleCommand(uint32_t id, float radius);
ui::gfx::CommandPtr NewCreateRectangleCommand(uint32_t id, float width, float height);
ui::gfx::CommandPtr NewCreateRoundedRectangleCommand(uint32_t id,
                                          float width,
                                          float height,
                                          float top_left_radius,
                                          float top_right_radius,
                                          float bottom_right_radius,
                                          float bottom_left_radius);

// Variant of NewCreateCircleCommand that uses a variable radius instead of a
// constant one set at construction time.
ui::gfx::CommandPtr NewCreateVarCircleCommand(uint32_t id, uint32_t radius_var_id);
// Variant of NewCreateRectangleCommand that uses a variable width/height instead of
// constant ones set at construction time.
ui::gfx::CommandPtr NewCreateVarRectangleCommand(uint32_t id,
                                      uint32_t width_var_id,
                                      uint32_t height_var_id);
// Variant of NewCreateRoundedRectangleCommand that uses a variable width/height/etc.
// instead of constant ones set at construction time.
ui::gfx::CommandPtr NewCreateVarRoundedRectangleCommand(
    uint32_t id,
    uint32_t width_var_id,
    uint32_t height_var_id,
    uint32_t top_left_radius_var_id,
    uint32_t top_right_radius_var_id,
    uint32_t bottom_left_radius_var_id,
    uint32_t bottom_right_radius_var_id);

ui::gfx::CommandPtr NewCreateMeshCommand(uint32_t id);
ui::gfx::CommandPtr NewCreateMaterialCommand(uint32_t id);
ui::gfx::CommandPtr NewCreateClipNodeCommand(uint32_t id);
ui::gfx::CommandPtr NewCreateEntityNodeCommand(uint32_t id);
ui::gfx::CommandPtr NewCreateShapeNodeCommand(uint32_t id);
ui::gfx::CommandPtr NewCreateVariableCommand(uint32_t id, ui::gfx::ValuePtr value);

ui::gfx::CommandPtr NewReleaseResourceCommand(uint32_t id);

// Export & Import operations.
ui::gfx::CommandPtr NewExportResourceCommand(uint32_t resource_id,
                                  zx::eventpair export_token);
ui::gfx::CommandPtr NewImportResourceCommand(uint32_t resource_id,
                                  ui::gfx::ImportSpec spec,
                                  zx::eventpair import_token);

// Exports the resource and returns an import token in |out_import_token|
// which allows it to be imported into other sessions.
ui::gfx::CommandPtr NewExportResourceCommandAsRequest(uint32_t resource_id,
                                           zx::eventpair* out_import_token);

// Imports the resource and returns an export token in |out_export_token|
// by which another session can export a resource to associate with this import.
ui::gfx::CommandPtr NewImportResourceCommandAsRequest(uint32_t resource_id,
                                           ui::gfx::ImportSpec import_spec,
                                           zx::eventpair* out_export_token);

// Node operations.
ui::gfx::CommandPtr NewAddChildCommand(uint32_t node_id, uint32_t child_id);
ui::gfx::CommandPtr NewAddPartCommand(uint32_t node_id, uint32_t part_id);
ui::gfx::CommandPtr NewDetachCommand(uint32_t node_id);
ui::gfx::CommandPtr NewDetachChildrenCommand(uint32_t node_id);
ui::gfx::CommandPtr NewSetTranslationCommand(uint32_t node_id, const float translation[3]);
ui::gfx::CommandPtr NewSetTranslationCommand(uint32_t node_id, uint32_t variable_id);
ui::gfx::CommandPtr NewSetScaleCommand(uint32_t node_id, const float scale[3]);
ui::gfx::CommandPtr NewSetScaleCommand(uint32_t node_id, uint32_t variable_id);
ui::gfx::CommandPtr NewSetRotationCommand(uint32_t node_id, const float quaternion[4]);
ui::gfx::CommandPtr NewSetRotationCommand(uint32_t node_id, uint32_t variable_id);
ui::gfx::CommandPtr NewSetAnchorCommand(uint32_t node_id, const float anchor[3]);
ui::gfx::CommandPtr NewSetAnchorCommand(uint32_t node_id, uint32_t variable_id);

ui::gfx::CommandPtr NewSetShapeCommand(uint32_t node_id, uint32_t shape_id);
ui::gfx::CommandPtr NewSetMaterialCommand(uint32_t node_id, uint32_t material_id);
ui::gfx::CommandPtr NewSetClipCommand(uint32_t node_id,
                           uint32_t clip_id,
                           bool clip_to_self);
ui::gfx::CommandPtr NewSetTagCommand(uint32_t node_id, uint32_t tag_value);
ui::gfx::CommandPtr NewSetHitTestBehaviorCommand(
    uint32_t node_id,
    ui::gfx::HitTestBehavior hit_test_behavior);

// Camera and lighting operations.
ui::gfx::CommandPtr NewSetCameraCommand(uint32_t renderer_id, uint32_t camera_id);
ui::gfx::CommandPtr NewSetCameraTransformCommand(uint32_t camera_id,
                                                 const float eye_position[3],
                                                 const float eye_look_at[3],
                                                 const float eye_up[3]);
ui::gfx::CommandPtr NewSetCameraProjectionCommand(uint32_t camera_id,
                                                  const float fovy);

ui::gfx::CommandPtr NewSetCameraPoseBufferCommand(uint32_t camera_id,
                                       uint32_t buffer_id,
                                       uint32_t num_entries,
                                       uint64_t base_time,
                                       uint64_t time_interval);

ui::gfx::CommandPtr NewSetLightColorCommand(uint32_t light_id, const float rgb[3]);
ui::gfx::CommandPtr NewSetLightColorCommand(uint32_t light_id, uint32_t variable_id);
ui::gfx::CommandPtr NewSetLightDirectionCommand(uint32_t light_id,
                                     const float direction[3]);
ui::gfx::CommandPtr NewSetLightDirectionCommand(uint32_t light_id, uint32_t variable_id);
ui::gfx::CommandPtr NewAddLightCommand(uint32_t scene_id, uint32_t light_id);
ui::gfx::CommandPtr NewDetachLightCommand(uint32_t light_id);
ui::gfx::CommandPtr NewDetachLightsCommand(uint32_t scene_id);

// Material operations.
ui::gfx::CommandPtr NewSetTextureCommand(uint32_t node_id, uint32_t image_id);
ui::gfx::CommandPtr NewSetColorCommand(uint32_t node_id,
                            uint8_t red,
                            uint8_t green,
                            uint8_t blue,
                            uint8_t alpha);

// Mesh operations.
ui::gfx::MeshVertexFormatPtr NewMeshVertexFormat(
    ui::gfx::ValueType position_type,
    ui::gfx::ValueType normal_type,
    ui::gfx::ValueType tex_coord_type);
// These arguments are documented in commands.fidl; see BindMeshBuffersCommand.
ui::gfx::CommandPtr NewBindMeshBuffersCommand(uint32_t mesh_id,
                                   uint32_t index_buffer_id,
                                   ui::gfx::MeshIndexFormat index_format,
                                   uint64_t index_offset,
                                   uint32_t index_count,
                                   uint32_t vertex_buffer_id,
                                   ui::gfx::MeshVertexFormatPtr vertex_format,
                                   uint64_t vertex_offset,
                                   uint32_t vertex_count,
                                   const float bounding_box_min[3],
                                   const float bounding_box_max[3]);

// Layer / LayerStack / Compositor operations.
ui::gfx::CommandPtr NewAddLayerCommand(uint32_t layer_stack_id, uint32_t layer_id);
ui::gfx::CommandPtr NewSetLayerStackCommand(uint32_t compositor_id,
                                 uint32_t layer_stack_id);
ui::gfx::CommandPtr NewSetRendererCommand(uint32_t layer_id, uint32_t renderer_id);
ui::gfx::CommandPtr NewSetRendererParamCommand(uint32_t renderer_id,
                                    ui::gfx::RendererParamPtr param);
ui::gfx::CommandPtr NewSetSizeCommand(uint32_t node_id, const float size[2]);

// Event operations.
ui::gfx::CommandPtr NewSetEventMaskCommand(uint32_t resource_id, uint32_t event_mask);

// Diagnostic operations.
ui::gfx::CommandPtr NewSetLabelCommand(uint32_t resource_id, const std::string& label);

// Debugging operations.
ui::gfx::CommandPtr NewSetDisableClippingCommand(uint32_t resource_id,
                                      bool disable_clipping);

// Basic types.
ui::gfx::FloatValuePtr NewFloatValue(float value);
ui::gfx::Vector2ValuePtr NewVector2Value(const float value[2]);
ui::gfx::Vector2ValuePtr NewVector2Value(uint32_t variable_id);
ui::gfx::Vector3ValuePtr NewVector3Value(const float value[3]);
ui::gfx::Vector3ValuePtr NewVector3Value(uint32_t variable_id);
ui::gfx::Vector4ValuePtr NewVector4Value(const float value[4]);
ui::gfx::Vector4ValuePtr NewVector4Value(uint32_t variable_id);
ui::gfx::QuaternionValuePtr NewQuaternionValue(const float value[4]);
ui::gfx::QuaternionValuePtr NewQuaternionValue(uint32_t variable_id);
ui::gfx::Matrix4ValuePtr NewMatrix4Value(const float value[16]);
ui::gfx::Matrix4ValuePtr NewMatrix4Value(uint32_t variable_id);
ui::gfx::ColorRgbValuePtr NewColorRgbValue(float red, float green, float blue);
ui::gfx::ColorRgbValuePtr NewColorRgbValue(uint32_t variable_id);
ui::gfx::ColorRgbaValuePtr NewColorRgbaValue(const uint8_t value[4]);
ui::gfx::ColorRgbaValuePtr NewColorRgbaValue(uint32_t variable_id);
ui::gfx::QuaternionValuePtr NewQuaternionValue(const float value[4]);

}  // namespace scenic_lib

#endif  // LIB_UI_SCENIC_FIDL_HELPERS_H_
