// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::pbm::update_engine_with_pbm;
use ffx_core::ffx_plugin;
use ffx_emulator_common::config::FfxConfigWrapper;
use ffx_emulator_start_args::StartCommand;

mod pbm;

#[ffx_plugin("emu.experimental")]
pub async fn start(cmd: StartCommand) -> Result<(), anyhow::Error> {
    let _config = FfxConfigWrapper::new();
    update_engine_with_pbm(&cmd).await.ok();
    todo!()
}
