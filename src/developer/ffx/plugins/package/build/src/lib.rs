// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, errors::ffx_error, ffx_core::ffx_plugin, package_tool::cmd_package_build};

pub use ffx_package_build_args::PackageBuildCommand;

#[ffx_plugin()]
pub async fn cmd_package(cmd: PackageBuildCommand) -> Result<()> {
    cmd_package_build(cmd).await.map_err(|err| ffx_error!(err))?;
    Ok(())
}
