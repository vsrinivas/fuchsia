// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, ffx_core::ffx_plugin, ffx_echo_args::EchoCommand,
    fidl_fuchsia_developer_bridge::DaemonProxy,
};

#[ffx_plugin()]
pub async fn echo(daemon_proxy: DaemonProxy, cmd: EchoCommand) -> Result<(), Error> {
    match daemon_proxy
        .echo_string(match cmd.text {
            Some(ref t) => t,
            None => "Ffx",
        })
        .await
    {
        Ok(r) => {
            println!("SUCCESS: received {:?}", r);
            return Ok(());
        }
        Err(e) => panic!("ERROR: {:?}", e),
    }
}
