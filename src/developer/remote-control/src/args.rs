// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

#[derive(Debug, Eq, FromArgs, PartialEq)]
/// Remote control services
pub struct RemoteControl {
    #[argh(subcommand)]
    // Remote control services
    pub cmd: Command,
}

#[derive(Debug, Eq, FromArgs, PartialEq)]
#[argh(subcommand)]
pub enum Command {
    DiagnosticsBridge(DiagnosticsBridge),
    RemoteControl(RemoteControlCmd),
}

#[derive(Debug, Eq, FromArgs, PartialEq)]
#[argh(subcommand, name = "diagnostics-bridge", description = "starts the diagnostics bridge")]
pub struct DiagnosticsBridge {}

#[derive(Debug, Eq, FromArgs, PartialEq)]
#[argh(subcommand, name = "remote-control", description = "starts the remote control service")]
pub struct RemoteControlCmd {}
