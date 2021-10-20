// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "remote",
    description = "Start and manage Fuchsia emulators on a remote workstation"
)]
/// This is a placeholder for a new feature in active development. Please stand by...
// Connect to <host>, run a build using fx from <dir>, fetch the artifacts and
// start the emulator. Alternatively, start the emulator on <host>,
// and open an WebRTC connection to it using local browser.
pub struct RemoteCommand {
    /// the hostname to connect to
    #[argh(positional)]
    pub host: String,

    /// defaults to ~/fuchsia, the path to the FUCHSIA_DIR on <host>
    #[argh(option, default = "default_dir()")]
    pub dir: String,

    /// do not build, just pull artifacts already present
    #[argh(switch)]
    pub no_build: bool,

    /// stream output from remote emulator using WebRTC instead of fetching artifacts
    #[argh(switch)]
    pub stream: bool,

    /// only tunnel, do not start remote emulator
    #[argh(switch)]
    pub no_emu: bool,

    /// do not use turn configuration for remote emulator
    #[argh(switch)]
    pub no_turn: bool,

    /// do not open https://web-femu.appspot.com, just run remote emulator
    #[argh(switch)]
    pub no_open: bool,

    /// do not start remote virtual display, use DPY instead
    #[argh(option, default = "default_display()")]
    pub display: String,

    /// port used on local machine to connect with remote emulator over HTTP (default: 8080)
    #[argh(option, default = "default_port()")]
    pub port: u16,

    /// arguments to pass to the emulator
    #[argh(positional)]
    pub args: Vec<String>,

    /// running in fuchsia sdk (not inside the fuchsia code repository)
    #[argh(switch)]
    pub sdk: bool,
}

fn default_port() -> u16 {
    8080
}

fn default_display() -> String {
    "xvfb-run".to_string()
}

fn default_dir() -> String {
    "~/fuchsia".to_string()
}
