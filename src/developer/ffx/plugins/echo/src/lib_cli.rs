// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, ffx_core::ffx_plugin, ffx_echo_args::EchoCommand as ParentEchoCommand,
    ffx_echo_cli_args::EchoCommand, ffx_lib_args::Ffx, ffx_lib_sub_command::Subcommand,
};

#[ffx_plugin()]
pub async fn echo(cmd: EchoCommand) -> Result<(), Error> {
    // If you need access to args from a higher up subcommand - use argh::from_env()
    let app: Ffx = argh::from_env();
    let echo_text =
        if let Subcommand::FfxEcho(ParentEchoCommand { text: Some(t), .. }) = app.subcommand {
            t
        } else {
            "Ffx".to_string()
        };
    let times = cmd.times.unwrap_or(1);
    for _ in 0..times {
        println!("CLI ECHO: {}", &echo_text);
    }
    Ok(())
}
