// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_ui_gfx::Command as GfxCommand;
use fidl_fuchsia_ui_gfx::{
    AddChildCmd, AddPartCmd, ColorRgba, ColorRgbaValue, CreateResourceCmd, DetachCmd,
    ExportResourceCmd, ImportResourceCmd, ImportSpec, Quaternion, QuaternionValue,
    ReleaseResourceCmd, ResourceArgs, SetAnchorCmd, SetClipCmd, SetColorCmd, SetEventMaskCmd,
    SetMaterialCmd, SetRotationCmd, SetScaleCmd, SetShapeCmd, SetTextureCmd, SetTranslationCmd,
    SetViewPropertiesCmd, Vec3, Vector3Value, ViewProperties,
};
use fidl_fuchsia_ui_scenic::Command;
use fuchsia_zircon::EventPair;

pub fn create_resource(id: u32, resource: ResourceArgs) -> Command {
    let cmd = CreateResourceCmd { id, resource };
    Command::Gfx(GfxCommand::CreateResource(cmd))
}

pub fn import_resource(id: u32, token: EventPair, spec: ImportSpec) -> Command {
    let cmd = ImportResourceCmd { id, token, spec };
    Command::Gfx(GfxCommand::ImportResource(cmd))
}

pub fn export_resource(id: u32, token: EventPair) -> Command {
    let cmd = ExportResourceCmd { id, token };
    Command::Gfx(GfxCommand::ExportResource(cmd))
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
