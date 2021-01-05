// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq, Clone)]
#[argh(
    subcommand,
    name = "off",
    description = "Powers off a target",
    note = "Power off a target. Uses the 'fuchsia.hardware.power.statecontrol.Admin'
FIDL API to send the power off command.

The 'fuchsia.hardware.power.statecontrol.Admin' is exposed via the 'appmgr'
component. To verify that the target exposes this service, `ffx component
select` or `ffx component knock` can be used.",
    error_code(1, "Timeout while powering off target.")
)]
pub struct OffCommand {}
