// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate fidl_fuchsia_ui_gfx;

use self::fidl_fuchsia_ui_gfx::Command as GfxCommand;
use self::fidl_fuchsia_ui_gfx::{
    AddChildCmd, CreateResourceCmd, ReleaseResourceCmd, ResourceArgs, SetMaterialCmd, SetTextureCmd,
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

pub fn add_child(node_id: u32, child_id: u32) -> Command {
    let cmd = AddChildCmd { node_id, child_id };
    Command::Gfx(GfxCommand::AddChild(cmd))
}
