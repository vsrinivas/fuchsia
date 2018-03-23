// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_SCENIC_FIDL_HELPERS_H_
#define LIB_UI_SCENIC_FIDL_HELPERS_H_

#include <string>

#include <fuchsia/cpp/gfx.h>
#include <fuchsia/cpp/images.h>
#include <fuchsia/cpp/ui.h>

namespace scenic_lib {

constexpr float kZeroesFloat3[3] = {0.f, 0.f, 0.f};
constexpr float kOnesFloat3[3] = {1.f, 1.f, 1.f};
// A quaterion that has no rotation.
constexpr float kQuaternionDefault[4] = {0.f, 0.f, 0.f, 1.f};

// Helper function for wrapping Scenic ops as Mozart commands.
ui::Command NewCommand(gfx::Command command);

// Resource creation.
gfx::Command NewCreateMemoryCommand(uint32_t id,
                                    zx::vmo vmo,
                                    images::MemoryType memory_type);
gfx::Command NewCreateImageCommand(uint32_t id,
                                   uint32_t memory_id,
                                   uint32_t memory_offset,
                                   images::ImageInfo info);
gfx::Command NewCreateImageCommand(uint32_t id,
                                   uint32_t memory_id,
                                   uint32_t memory_offset,
                                   images::PixelFormat format,
                                   images::ColorSpace color_space,
                                   images::Tiling tiling,
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t stride);
gfx::Command NewCreateImagePipeCommand(
    uint32_t id,
    ::fidl::InterfaceRequest<images::ImagePipe> request);
gfx::Command NewCreateBufferCommand(uint32_t id,
                                    uint32_t memory_id,
                                    uint32_t memory_offset,
                                    uint32_t num_bytes);

gfx::Command NewCreateDisplayCompositorCommand(uint32_t id);
gfx::Command NewCreateLayerStackCommand(uint32_t id);
gfx::Command NewCreateLayerCommand(uint32_t id);

gfx::Command NewCreateSceneCommand(uint32_t id);
gfx::Command NewCreateCameraCommand(uint32_t id, uint32_t scene_id);
gfx::Command NewCreateRendererCommand(uint32_t id);
gfx::Command NewCreateAmbientLightCommand(uint32_t id);
gfx::Command NewCreateDirectionalLightCommand(uint32_t id);

gfx::Command NewCreateCircleCommand(uint32_t id, float radius);
gfx::Command NewCreateRectangleCommand(uint32_t id, float width, float height);
gfx::Command NewCreateRoundedRectangleCommand(uint32_t id,
                                              float width,
                                              float height,
                                              float top_left_radius,
                                              float top_right_radius,
                                              float bottom_right_radius,
                                              float bottom_left_radius);

// Variant of NewCreateCircleCommand that uses a variable radius instead of a
// constant one set at construction time.
gfx::Command NewCreateVarCircleCommand(uint32_t id, uint32_t radius_var_id);
// Variant of NewCreateRectangleCommand that uses a variable width/height
// instead of constant ones set at construction time.
gfx::Command NewCreateVarRectangleCommand(uint32_t id,
                                          uint32_t width_var_id,
                                          uint32_t height_var_id);
// Variant of NewCreateRoundedRectangleCommand that uses a variable
// width/height/etc. instead of constant ones set at construction time.
gfx::Command NewCreateVarRoundedRectangleCommand(
    uint32_t id,
    uint32_t width_var_id,
    uint32_t height_var_id,
    uint32_t top_left_radius_var_id,
    uint32_t top_right_radius_var_id,
    uint32_t bottom_left_radius_var_id,
    uint32_t bottom_right_radius_var_id);

gfx::Command NewCreateMeshCommand(uint32_t id);
gfx::Command NewCreateMaterialCommand(uint32_t id);
gfx::Command NewCreateClipNodeCommand(uint32_t id);
gfx::Command NewCreateEntityNodeCommand(uint32_t id);
gfx::Command NewCreateShapeNodeCommand(uint32_t id);
gfx::Command NewCreateVariableCommand(uint32_t id, gfx::Value value);

gfx::Command NewReleaseResourceCommand(uint32_t id);

// Export & Import operations.
gfx::Command NewExportResourceCommand(uint32_t resource_id,
                                      zx::eventpair export_token);
gfx::Command NewImportResourceCommand(uint32_t resource_id,
                                      gfx::ImportSpec spec,
                                      zx::eventpair import_token);

// Exports the resource and returns an import token in |out_import_token|
// which allows it to be imported into other sessions.
gfx::Command NewExportResourceCommandAsRequest(uint32_t resource_id,
                                               zx::eventpair* out_import_token);

// Imports the resource and returns an export token in |out_export_token|
// by which another session can export a resource to associate with this import.
gfx::Command NewImportResourceCommandAsRequest(uint32_t resource_id,
                                               gfx::ImportSpec import_spec,
                                               zx::eventpair* out_export_token);

// Node operations.
gfx::Command NewAddChildCommand(uint32_t node_id, uint32_t child_id);
gfx::Command NewAddPartCommand(uint32_t node_id, uint32_t part_id);
gfx::Command NewDetachCommand(uint32_t node_id);
gfx::Command NewDetachChildrenCommand(uint32_t node_id);
gfx::Command NewSetTranslationCommand(uint32_t node_id,
                                      const float translation[3]);
gfx::Command NewSetTranslationCommand(uint32_t node_id, uint32_t variable_id);
gfx::Command NewSetScaleCommand(uint32_t node_id, const float scale[3]);
gfx::Command NewSetScaleCommand(uint32_t node_id, uint32_t variable_id);
gfx::Command NewSetRotationCommand(uint32_t node_id, const float quaternion[4]);
gfx::Command NewSetRotationCommand(uint32_t node_id, uint32_t variable_id);
gfx::Command NewSetAnchorCommand(uint32_t node_id, const float anchor[3]);
gfx::Command NewSetAnchorCommand(uint32_t node_id, uint32_t variable_id);

gfx::Command NewSetShapeCommand(uint32_t node_id, uint32_t shape_id);
gfx::Command NewSetMaterialCommand(uint32_t node_id, uint32_t material_id);
gfx::Command NewSetClipCommand(uint32_t node_id,
                               uint32_t clip_id,
                               bool clip_to_self);
gfx::Command NewSetTagCommand(uint32_t node_id, uint32_t tag_value);
gfx::Command NewSetHitTestBehaviorCommand(
    uint32_t node_id,
    gfx::HitTestBehavior hit_test_behavior);

// Camera and lighting operations.
gfx::Command NewSetCameraCommand(uint32_t renderer_id, uint32_t camera_id);
gfx::Command NewSetCameraTransformCommand(uint32_t camera_id,
                                          const float eye_position[3],
                                          const float eye_look_at[3],
                                          const float eye_up[3]);
gfx::Command NewSetCameraProjectionCommand(uint32_t camera_id,
                                           const float fovy);

gfx::Command NewSetCameraPoseBufferCommand(uint32_t camera_id,
                                           uint32_t buffer_id,
                                           uint32_t num_entries,
                                           uint64_t base_time,
                                           uint64_t time_interval);

gfx::Command NewSetLightColorCommand(uint32_t light_id, const float rgb[3]);
gfx::Command NewSetLightColorCommand(uint32_t light_id, uint32_t variable_id);
gfx::Command NewSetLightDirectionCommand(uint32_t light_id,
                                         const float direction[3]);
gfx::Command NewSetLightDirectionCommand(uint32_t light_id,
                                         uint32_t variable_id);
gfx::Command NewAddLightCommand(uint32_t scene_id, uint32_t light_id);
gfx::Command NewDetachLightCommand(uint32_t light_id);
gfx::Command NewDetachLightsCommand(uint32_t scene_id);

// Material operations.
gfx::Command NewSetTextureCommand(uint32_t node_id, uint32_t image_id);
gfx::Command NewSetColorCommand(uint32_t node_id,
                                uint8_t red,
                                uint8_t green,
                                uint8_t blue,
                                uint8_t alpha);

// Mesh operations.
gfx::MeshVertexFormat NewMeshVertexFormat(gfx::ValueType position_type,
                                          gfx::ValueType normal_type,
                                          gfx::ValueType tex_coord_type);
// These arguments are documented in commands.fidl; see BindMeshBuffersCommand.
gfx::Command NewBindMeshBuffersCommand(uint32_t mesh_id,
                                       uint32_t index_buffer_id,
                                       gfx::MeshIndexFormat index_format,
                                       uint64_t index_offset,
                                       uint32_t index_count,
                                       uint32_t vertex_buffer_id,
                                       gfx::MeshVertexFormatPtr vertex_format,
                                       uint64_t vertex_offset,
                                       uint32_t vertex_count,
                                       const float bounding_box_min[3],
                                       const float bounding_box_max[3]);

// Layer / LayerStack / Compositor operations.
gfx::Command NewAddLayerCommand(uint32_t layer_stack_id, uint32_t layer_id);
gfx::Command NewSetLayerStackCommand(uint32_t compositor_id,
                                     uint32_t layer_stack_id);
gfx::Command NewSetRendererCommand(uint32_t layer_id, uint32_t renderer_id);
gfx::Command NewSetRendererParamCommand(uint32_t renderer_id,
                                        gfx::RendererParam param);
gfx::Command NewSetSizeCommand(uint32_t node_id, const float size[2]);

// Event operations.
gfx::Command NewSetEventMaskCommand(uint32_t resource_id, uint32_t event_mask);

// Diagnostic operations.
gfx::Command NewSetLabelCommand(uint32_t resource_id, const std::string& label);

// Debugging operations.
gfx::Command NewSetDisableClippingCommand(uint32_t resource_id,
                                          bool disable_clipping);

// Basic types.
gfx::FloatValue NewFloatValue(float value);
gfx::Vector2Value NewVector2Value(const float value[2]);
gfx::Vector2Value NewVector2Value(uint32_t variable_id);
gfx::Vector3Value NewVector3Value(const float value[3]);
gfx::Vector3Value NewVector3Value(uint32_t variable_id);
gfx::Vector4Value NewVector4Value(const float value[4]);
gfx::Vector4Value NewVector4Value(uint32_t variable_id);
gfx::QuaternionValue NewQuaternionValue(const float value[4]);
gfx::QuaternionValue NewQuaternionValue(uint32_t variable_id);
gfx::Matrix4Value NewMatrix4Value(const float value[16]);
gfx::Matrix4Value NewMatrix4Value(uint32_t variable_id);
gfx::ColorRgbValue NewColorRgbValue(float red, float green, float blue);
gfx::ColorRgbValue NewColorRgbValue(uint32_t variable_id);
gfx::ColorRgbaValue NewColorRgbaValue(const uint8_t value[4]);
gfx::ColorRgbaValue NewColorRgbaValue(uint32_t variable_id);
gfx::QuaternionValue NewQuaternionValue(const float value[4]);

}  // namespace scenic_lib

#endif  // LIB_UI_SCENIC_FIDL_HELPERS_H_
