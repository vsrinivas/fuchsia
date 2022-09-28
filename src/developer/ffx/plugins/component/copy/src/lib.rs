// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, ffx_component_copy_args::CopyComponentCommand, ffx_core::ffx_plugin};

#[ffx_plugin("copy")]
pub async fn copy(cmd: CopyComponentCommand) -> Result<()> {
    println!("{:?}", cmd);
    println!("Welcome to this function! I am not sure why you are here, but this feature is currently in progress");
    Ok(())
}
