// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "tunnel",
    description = "Establishes a port forward between the local host and the dart vm service port.",
    note = "Determines the vm_service_port in the Flutter runner on the target Fuchsia device. An ssh
tunnel is then established between localhost(127.0.0.1) at an available port and the target device at
the vm_service_port.

A url for the location of the listening socket is printed out for users."
)]
pub struct TunnelCommand {}
