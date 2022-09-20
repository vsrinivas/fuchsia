// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_core::ffx_plugin;
pub use ffx_package_far_args::{ExtractSubCommand, FarCommand, FarSubCommand};
use ffx_writer::Writer;

mod extract;

#[ffx_plugin("ffx_package")]
pub async fn cmd_far(
    cmd: FarCommand,
    #[ffx(machine = Vec<T:Serialize>)] mut writer: Writer,
) -> Result<()> {
    match cmd.subcommand {
        FarSubCommand::Extract(subcmd) => extract::extract_impl(subcmd, &mut writer),
    }
}
