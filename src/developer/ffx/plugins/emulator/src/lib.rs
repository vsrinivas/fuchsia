// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, ffx_core::ffx_plugin, ffx_emulator_args::EmulatorCommand};

#[ffx_plugin("vdl.experimental")]
pub fn emulator(_cmd: EmulatorCommand) -> Result<()> {
    println!("Hello, FEMU");
    Ok(())
}
