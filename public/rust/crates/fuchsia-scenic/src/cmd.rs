// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate fidl_fuchsia_ui_gfx;

use self::fidl_fuchsia_ui_gfx::Command as GfxCommand;
use self::fidl_fuchsia_ui_gfx::{AddChildCmd, ColorRgba, ColorRgbaValue, CreateResourceCmd,
                                DetachCmd, ImportResourceCmd, ImportSpec, Quaternion,
                                QuaternionValue, ReleaseResourceCmd, ResourceArgs, SetColorCmd,
                                SetMaterialCmd, SetRotationCmd, SetScaleCmd, SetShapeCmd,
                                SetTextureCmd, SetTranslationCmd, Vec3, Vector3Value};
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

pub fn release_resource(id: u32) -> Command {
    let cmd = ReleaseResourceCmd { id };
    Command::Gfx(GfxCommand::ReleaseResource(cmd))
}

pub fn set_color(material_id: u32, value: ColorRgba) -> Command {
    let cmd = SetColorCmd {
        material_id,
        color: ColorRgbaValue {
            value,
            variable_id: 0,
        },
    };
    Command::Gfx(GfxCommand::SetColor(cmd))
}

pub fn set_texture(material_id: u32, texture_id: u32) -> Command {
    let cmd = SetTextureCmd {
        material_id,
        texture_id,
    };
    Command::Gfx(GfxCommand::SetTexture(cmd))
}

pub fn set_material(node_id: u32, material_id: u32) -> Command {
    let cmd = SetMaterialCmd {
        node_id,
        material_id,
    };
    Command::Gfx(GfxCommand::SetMaterial(cmd))
}

pub fn set_shape(node_id: u32, shape_id: u32) -> Command {
    let cmd = SetShapeCmd { node_id, shape_id };
    Command::Gfx(GfxCommand::SetShape(cmd))
}

pub fn set_translation(id: u32, x: f32, y: f32, z: f32) -> Command {
    let cmd = SetTranslationCmd {
        id,
        value: Vector3Value {
            value: Vec3 { x, y, z },
            variable_id: 0,
        },
    };
    Command::Gfx(GfxCommand::SetTranslation(cmd))
}

pub fn set_scale(id: u32, x: f32, y: f32, z: f32) -> Command {
    let cmd = SetScaleCmd {
        id,
        value: Vector3Value {
            value: Vec3 { x, y, z },
            variable_id: 0,
        },
    };
    Command::Gfx(GfxCommand::SetScale(cmd))
}

pub fn set_rotation(id: u32, x: f32, y: f32, z: f32, w: f32) -> Command {
    let cmd = SetRotationCmd {
        id,
        value: QuaternionValue {
            value: Quaternion { x, y, z, w },
            variable_id: 0,
        },
    };
    Command::Gfx(GfxCommand::SetRotation(cmd))
}

pub fn add_child(node_id: u32, child_id: u32) -> Command {
    let cmd = AddChildCmd { node_id, child_id };
    Command::Gfx(GfxCommand::AddChild(cmd))
}

pub fn detach(id: u32) -> Command {
    let cmd = DetachCmd { id };
    Command::Gfx(GfxCommand::Detach(cmd))
}
