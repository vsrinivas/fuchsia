// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use ffx_core::ffx_plugin;
use ffx_emulator_common::config::FfxConfigWrapper;
use ffx_emulator_shutdown_args::ShutdownCommand;

#[ffx_plugin("emu.experimental")]
pub async fn shutdown(_cmd: ShutdownCommand) -> Result<(), anyhow::Error> {
    let _config = FfxConfigWrapper::new();
    todo!()
}
