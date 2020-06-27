// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, ffx_core::ffx_plugin, ffx_echo_args::EchoCommand as ParentEchoCommand,
    ffx_echo_daemon_args::EchoCommand, ffx_lib_args::Ffx, ffx_lib_sub_command::Subcommand,
    fidl_fuchsia_developer_bridge::DaemonProxy,
};

#[ffx_plugin()]
pub async fn echo(daemon_proxy: DaemonProxy, _cmd: EchoCommand) -> Result<(), Error> {
    // If you need access to args from a higher up subcommand - use argh::from_env()
    let app: Ffx = argh::from_env();
    let echo_text =
        if let Subcommand::FfxEcho(ParentEchoCommand { text: Some(t), .. }) = app.subcommand {
            t
        } else {
            "Ffx".to_string()
        };
    match daemon_proxy.echo_string(&echo_text).await {
        Ok(r) => {
            println!("SUCCESS: received {:?}", r);
            return Ok(());
        }
        Err(e) => panic!("ERROR: {:?}", e),
    }
}
