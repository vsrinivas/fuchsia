// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, ffx_flutter_sub_command::Subcommand};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "flutter",
    description = "Interact with Flutter components on the target.",
    note = "The `flutter` subcommand is an entry workflow and contains various subcommands
for flutter component management and interaction."
)]
pub struct FlutterCommand {
    #[argh(subcommand)]
    pub subcommand: Subcommand,
}
