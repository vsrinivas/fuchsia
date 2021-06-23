// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "tunnel",
    description = "Establishes a port forward between the dart vm service port and a local host."
)]
pub struct TunnelCommand {
    #[argh(positional)]
    /// vmservice_port. The port exposed by the dart vm on a Fuchsia device.
    pub vm_service_port: String,

    #[argh(option)]
    /// name. The name of a Flutter application running on the target device.
    pub name: Option<String>,
}
