// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use ffx_core::ffx_plugin;
use ffx_emulator_show_args::ShowCommand;

#[ffx_plugin("emu.experimental")]
pub async fn show(_cmd: ShowCommand) -> Result<(), anyhow::Error> {
    todo!()
}
