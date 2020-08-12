// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;

#[ffx_core::ffx_plugin()]
pub async fn debug(cmd: ffx_overnet_plugin_args::OvernetCommand) -> Result<(), Error> {
    onet_lib::run_onet(onet_lib::Opts { command: cmd.command }).await
}
