// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use ffx_core::ffx_plugin;
use ffx_emulator_list_args::ListCommand;

#[ffx_plugin("emu.experimental")]
pub async fn list(_cmd: ListCommand) -> Result<(), anyhow::Error> {
    todo!()
}
