// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::reader::{CommandReader, ShellReader},
    crate::shell::Shell,
    crate::writer::{StdioSink, Writer},
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_fuzz_args::{FuzzCommand, Session},
    fidl_fuchsia_developer_remotecontrol as rcs,
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
pub async fn fuzz(rc: rcs::RemoteControlProxy, command: FuzzCommand) -> Result<()> {
    let session = command.as_session();
    let (is_tty, muted, use_colors) = match session {
        Session::Interactive(_) => (true, false, true),
        Session::Quiet(_) => (false, true, false),
        Session::Verbose(_) => (false, false, false),
    };
    let mut writer = Writer::new(StdioSink { is_tty });
    writer.mute(muted);
    writer.use_colors(use_colors);
    match session {
        Session::Interactive(json_file) => {
            Shell::new(json_file, rc, ShellReader::new(), &writer).run().await
        }
        Session::Quiet(commands) => {
            Shell::new(None, rc, CommandReader::new(commands), &writer).run().await
        }
        Session::Verbose(commands) => {
            Shell::new(None, rc, CommandReader::new(commands), &writer).run().await
        }
    }
}
