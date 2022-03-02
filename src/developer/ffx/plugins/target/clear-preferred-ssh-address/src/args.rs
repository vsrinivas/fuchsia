// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq, Clone)]
#[argh(
    subcommand,
    name = "clear-preferred-ssh-address",
    description = "Clears a previously configured SSH address",
    note = "Clears a preferred SSH address that was set using the
set-preferred-ssh-address command on the default target. Executing this command
severs any existing connection to the target and establishes a new connection.
The newly selected address is chosen using the standard address selection
logic."
)]
pub struct ClearPreferredSshAddressCommand {}
