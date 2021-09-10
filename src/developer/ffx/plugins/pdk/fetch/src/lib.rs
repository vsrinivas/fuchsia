// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_core::ffx_plugin;
use ffx_pdk_fetch_args::FetchCommand;

#[ffx_plugin("ffx_pdk")]
pub async fn cmd_update(cmd: FetchCommand) -> Result<()> {
    print!("Fetch command: {:#?}", cmd);
    print!("This plugin is now yet implemented.");
    Ok(())
}
