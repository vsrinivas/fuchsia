// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::reader::{CommandReader, Reader, ShellReader},
    crate::shell::Shell,
    crate::writer::{OutputSink, StdioSink, Writer},
    anyhow::Result,
    errors::ffx_bail,
    ffx_core::ffx_plugin,
    ffx_fuzz_args::{FuzzCommand, Session},
    fidl_fuchsia_developer_remotecontrol as rcs,
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

#[cfg(test)]
mod test_fixtures;

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
pub async fn fuzz(remote_control: rcs::RemoteControlProxy, command: FuzzCommand) -> Result<()> {
    let session = command.as_session();
    let (is_tty, muted, use_colors) = match session {
        Session::Interactive => (true, false, true),
        Session::Quiet(_) => (false, true, false),
        Session::Verbose(_) => (false, false, false),
    };
    let mut writer = Writer::new(StdioSink { is_tty });
    writer.mute(muted);
    writer.use_colors(use_colors);
    match session {
        Session::Interactive => run_session(remote_control, ShellReader::new(), &writer).await,
        Session::Quiet(commands) => {
            run_session(remote_control, CommandReader::new(commands), &writer).await
        }
        Session::Verbose(commands) => {
            run_session(remote_control, CommandReader::new(commands), &writer).await
        }
    }
}

async fn run_session<R: Reader, O: OutputSink>(
    rc: rcs::RemoteControlProxy,
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
    let mut shell = Shell::new(&fuchsia_dir, rc, reader, writer);
    shell.run().await
}
