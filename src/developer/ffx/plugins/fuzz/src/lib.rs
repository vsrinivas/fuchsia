// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::reader::{CommandReader, Reader, ShellReader},
    crate::shell::Shell,
    crate::writer::{OutputSink, StdioSink, Writer},
    anyhow::{Context as _, Result},
    errors::ffx_bail,
    ffx_core::ffx_plugin,
    ffx_fuzz_args::{FuzzCommand, Session},
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_developer_remotecontrol as remotecontrol, fidl_fuchsia_fuzzer as fuzz,
    selectors::{parse_selector, VerboseError},
    std::env,
};

mod autocomplete;
mod corpus;
mod diagnostics;
mod fuzzer;
mod input;
mod manager;
mod options;
mod reader;
mod shell;
mod util;
mod writer;

/// The `ffx fuzz` plugin.
///
/// This plugin is designed to connect to the `fuzz-manager` on a target device, and use it to
/// further connect to a fuzzer instance. It enables sending a sequence of commands to fuzzer to
/// perform various actions such as fuzzing, input minimization, corpus compaction, etc. It also
/// receives and displays fuzzer outputs
///
/// This plugin uses a number of async futures which may safely run concurrently, but not in
/// parallel. This assumption is fulfilled by `ffx` itself: See //src/developer/ffx/src/main.rs,
/// in which `main` has an attribute of `#[fuchsia_async::run_singlethreaded]`.
///
#[ffx_plugin("fuzzing")]
pub async fn fuzz(
    remote_control: remotecontrol::RemoteControlProxy,
    command: FuzzCommand,
) -> Result<()> {
    let mut writer = Writer::new(StdioSink::default());
    match command.as_session() {
        Session::Interactive => {
            writer.mute(false);
            writer.use_colors(true);
            run_session(remote_control, ShellReader::new(), &writer).await
        }
        Session::Quiet(commands) => {
            writer.mute(true);
            writer.use_colors(false);
            run_session(remote_control, CommandReader::new(commands), &writer).await
        }
        Session::Verbose(commands) => {
            writer.mute(false);
            writer.use_colors(false);
            run_session(remote_control, CommandReader::new(commands), &writer).await
        }
    }
}

async fn run_session<R: Reader, O: OutputSink>(
    rc: remotecontrol::RemoteControlProxy,
    reader: R,
    writer: &Writer<O>,
) -> Result<()> {
    let fuchsia_dir = match env::var("FUCHSIA_DIR") {
        Ok(fuchsia_dir) => fuchsia_dir,
        _ => {
            let err_msgs = vec![
                "FUCHSIA_DIR is not set.\n",
                "See `https://fuchsia.dev/fuchsia-src/get-started/get_fuchsia_source",
                "#set-up-environment-variables`\n",
                "for additional details on setting up a Fuchsia build environment.",
            ];
            ffx_bail!("{}", err_msgs.join(""));
        }
    };
    let (mut shell, server_end) =
        Shell::create(&fuchsia_dir, reader, writer).context("failed to create shell")?;
    let selector = format!("core/fuzz-manager:expose:{}", fuzz::ManagerMarker::DEBUG_NAME);
    let parsed = parse_selector::<VerboseError>(&selector).context("failed to parse selector")?;
    let result =
        rc.connect(parsed, server_end.into_channel()).await.context(fidl_name("Connect"))?;
    if let Err(e) = result {
        ffx_bail!("Failed to connect to fuzz-manager: {:?}", e);
    }
    shell.run().await;
    Ok(())
}

fn fidl_name(method: &str) -> String {
    format!("{}/{}", remotecontrol::RemoteControlMarker::DEBUG_NAME, method)
}
