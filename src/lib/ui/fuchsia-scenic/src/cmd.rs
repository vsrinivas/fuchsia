// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_ui_gfx::Command as GfxCommand;
use fidl_fuchsia_ui_gfx::{
    AddChildCmd, AddLayerCmd, AddPartCmd, ColorRgb, ColorRgbValue, ColorRgba, ColorRgbaValue,
    CreateResourceCmd, DetachCmd, DetachLightCmd, DetachLightsCmd, Quaternion, QuaternionValue,
    ReleaseResourceCmd, RemoveAllLayersCmd, RemoveLayerCmd, ResourceArgs, SceneAddAmbientLightCmd,
    SceneAddDirectionalLightCmd, SceneAddPointLightCmd, SetAnchorCmd, SetCameraCmd, SetClipCmd,
    SetColorCmd, SetEventMaskCmd, SetLayerStackCmd, SetLightColorCmd, SetLightDirectionCmd,
    SetMaterialCmd, SetRendererCmd, SetRotationCmd, SetScaleCmd, SetShapeCmd, SetSizeCmd,
    SetTextureCmd, SetTranslationCmd, SetViewPropertiesCmd, Vec2, Vec3, Vector2Value, Vector3Value,
    ViewProperties,
};
use fidl_fuchsia_ui_scenic::Command;

pub fn create_resource(id: u32, resource: ResourceArgs) -> Command {
    let cmd = CreateResourceCmd { id, resource };
    Command::Gfx(GfxCommand::CreateResource(cmd))
}

pub fn release_resource(id: u32) -> Command {
    let cmd = ReleaseResourceCmd { id };
    Command::Gfx(GfxCommand::ReleaseResource(cmd))
}

pub fn set_event_mask(id: u32, event_mask: u32) -> Command {
    let cmd = SetEventMaskCmd { id, event_mask };
    Command::Gfx(GfxCommand::SetEventMask(cmd))
}

pub fn set_clip(node_id: u32, clip_id: u32, clip_to_self: bool) -> Command {
    let cmd = SetClipCmd { node_id, clip_id, clip_to_self };
    Command::Gfx(GfxCommand::SetClip(cmd))
}

pub fn set_color(material_id: u32, value: ColorRgba) -> Command {
    let cmd = SetColorCmd { material_id, color: ColorRgbaValue { value, variable_id: 0 } };
    Command::Gfx(GfxCommand::SetColor(cmd))
}

pub fn set_texture(material_id: u32, texture_id: u32) -> Command {
    let cmd = SetTextureCmd { material_id, texture_id };
    Command::Gfx(GfxCommand::SetTexture(cmd))
}

pub fn set_material(node_id: u32, material_id: u32) -> Command {
    let cmd = SetMaterialCmd { node_id, material_id };
    Command::Gfx(GfxCommand::SetMaterial(cmd))
}

pub fn set_shape(node_id: u32, shape_id: u32) -> Command {
    let cmd = SetShapeCmd { node_id, shape_id };
    Command::Gfx(GfxCommand::SetShape(cmd))
}

pub fn set_translation(id: u32, x: f32, y: f32, z: f32) -> Command {
    let cmd =
        SetTranslationCmd { id, value: Vector3Value { value: Vec3 { x, y, z }, variable_id: 0 } };
    Command::Gfx(GfxCommand::SetTranslation(cmd))
}

pub fn set_size(id: u32, x: f32, y: f32) -> Command {
    let cmd = SetSizeCmd { id, value: Vector2Value { value: Vec2 { x, y }, variable_id: 0 } };
    Command::Gfx(GfxCommand::SetSize(cmd))
}

pub fn set_anchor(id: u32, x: f32, y: f32, z: f32) -> Command {
    let cmd = SetAnchorCmd { id, value: Vector3Value { value: Vec3 { x, y, z }, variable_id: 0 } };
    Command::Gfx(GfxCommand::SetAnchor(cmd))
}

pub fn set_scale(id: u32, x: f32, y: f32, z: f32) -> Command {
    let cmd = SetScaleCmd { id, value: Vector3Value { value: Vec3 { x, y, z }, variable_id: 0 } };
    Command::Gfx(GfxCommand::SetScale(cmd))
}

pub fn set_rotation(id: u32, x: f32, y: f32, z: f32, w: f32) -> Command {
    let cmd = SetRotationCmd {
        id,
        value: QuaternionValue { value: Quaternion { x, y, z, w }, variable_id: 0 },
    };
    Command::Gfx(GfxCommand::SetRotation(cmd))
}

pub fn add_child(node_id: u32, child_id: u32) -> Command {
    let cmd = AddChildCmd { node_id, child_id };
    Command::Gfx(GfxCommand::AddChild(cmd))
}

pub fn add_part(node_id: u32, part_id: u32) -> Command {
    let cmd = AddPartCmd { node_id, part_id };
    Command::Gfx(GfxCommand::AddPart(cmd))
}

pub fn detach(id: u32) -> Command {
    let cmd = DetachCmd { id };
    Command::Gfx(GfxCommand::Detach(cmd))
}

pub fn set_view_properties(view_holder_id: u32, properties: ViewProperties) -> Command {
    let cmd = SetViewPropertiesCmd { view_holder_id, properties };
    Command::Gfx(GfxCommand::SetViewProperties(cmd))
}

pub fn add_layer(layer_stack_id: u32, layer_id: u32) -> Command {
    let cmd = AddLayerCmd { layer_stack_id, layer_id };
    Command::Gfx(GfxCommand::AddLayer(cmd))
}

pub fn remove_layer(layer_stack_id: u32, layer_id: u32) -> Command {
    let cmd = RemoveLayerCmd { layer_stack_id, layer_id };
    Command::Gfx(GfxCommand::RemoveLayer(cmd))
}

pub fn remove_all_layers(layer_stack_id: u32) -> Command {
    let cmd = RemoveAllLayersCmd { layer_stack_id };
    Command::Gfx(GfxCommand::RemoveAllLayers(cmd))
}

pub fn set_layer_stack(compositor_id: u32, layer_stack_id: u32) -> Command {
    let cmd = SetLayerStackCmd { compositor_id, layer_stack_id };
    Command::Gfx(GfxCommand::SetLayerStack(cmd))
}

pub fn set_renderer(layer_id: u32, renderer_id: u32) -> Command {
    let cmd = SetRendererCmd { layer_id, renderer_id };
    Command::Gfx(GfxCommand::SetRenderer(cmd))
}

pub fn set_camera(renderer_id: u32, camera_id: u32) -> Command {
    let cmd = SetCameraCmd { renderer_id, camera_id };
    Command::Gfx(GfxCommand::SetCamera(cmd))
}

pub fn set_light_color(light_id: u32, value: ColorRgb) -> Command {
    let cmd = SetLightColorCmd { light_id, color: ColorRgbValue { value, variable_id: 0 } };
    Command::Gfx(GfxCommand::SetLightColor(cmd))
}

pub fn set_light_direction(light_id: u32, x: f32, y: f32, z: f32) -> Command {
    let cmd = SetLightDirectionCmd {
        light_id,
        direction: Vector3Value { value: Vec3 { x, y, z }, variable_id: 0 },
    };
    Command::Gfx(GfxCommand::SetLightDirection(cmd))
}

pub fn scene_add_directional_light(scene_id: u32, light_id: u32) -> Command {
    let cmd = SceneAddDirectionalLightCmd { scene_id, light_id };
    Command::Gfx(GfxCommand::Scene_AddDirectionalLight(cmd))
}

pub fn scene_add_point_light(scene_id: u32, light_id: u32) -> Command {
    let cmd = SceneAddPointLightCmd { scene_id, light_id };
    Command::Gfx(GfxCommand::Scene_AddPointLight(cmd))
}

pub fn scene_add_ambient_light(scene_id: u32, light_id: u32) -> Command {
    let cmd = SceneAddAmbientLightCmd { scene_id, light_id };
    Command::Gfx(GfxCommand::Scene_AddAmbientLight(cmd))
}

pub fn detach_light(light_id: u32) -> Command {
    let cmd = DetachLightCmd { light_id };
    Command::Gfx(GfxCommand::DetachLight(cmd))
}

pub fn detach_lights(scene_id: u32) -> Command {
    let cmd = DetachLightsCmd { scene_id };
    Command::Gfx(GfxCommand::DetachLights(cmd))
}
