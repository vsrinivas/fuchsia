// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;

#[ffx_core::ffx_plugin()]
pub async fn onet(cmd: ffx_overnet_plugin_args::OvernetCommand) -> Result<(), Error> {
    // todo(fxb/108692) remove this use of the global hoist when we put the main one in the environment context
    // instead.
    onet_tool::run_onet(hoist::hoist(), onet_tool::Opts { command: cmd.command }).await
}
