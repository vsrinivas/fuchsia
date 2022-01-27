// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "night_mode")]
/// get or set night mode settings
pub struct NightMode {
    /// when 'true', enables night mode
    #[argh(option, short = 'n')]
    pub night_mode_enabled: Option<bool>,
}
