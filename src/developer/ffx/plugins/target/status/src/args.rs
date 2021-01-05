// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(Debug, Default, FromArgs, PartialEq)]
#[argh(
    subcommand,
    name = "status",
    description = "Display status information for the target",
    note = "Displays a detailed runtime status information about the target.

The default output is intended for a human reader. This output can be
decorated with machine readable labels (--label) and descriptions of
each field (--desc).

The 'label' fields in the machine readable output (--json) will remain
stable across software updates and is not localized (compare to 'title'
which may change or be localized). The 'value' field will be one of:
'null', 'bool', 'string', or a list of strings.",
    error_code(1, "Timeout retrieving target status.")
)]
pub struct TargetStatus {
    /// display descriptions of entries
    #[argh(switch)]
    pub desc: bool,

    /// display label of entries
    #[argh(switch)]
    pub label: bool,

    /// formats output as json objects
    #[argh(switch)]
    pub json: bool,

    /// display version
    #[argh(switch)]
    pub version: bool,
}
