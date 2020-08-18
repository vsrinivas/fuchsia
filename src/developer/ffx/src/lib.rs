// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, ffx_core::ffx_plugin, ffx_lib_args::Ffx, std::env, std::process::Command};

#[ffx_plugin()]
pub async fn help(_cmd: Ffx) -> Result<()> {
    let ffx_path = env::current_exe()?;
    Command::new(ffx_path).arg("help").spawn()?;
    Ok(())
}
