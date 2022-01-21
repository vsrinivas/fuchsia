// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "privacy")]
/// get or set privacy settings
pub struct Privacy {
    /// when 'true', is considered to be user giving consent to have their data shared with product
    /// owner, e.g. for metrics collection and crash reporting
    #[argh(option, short = 'u')]
    pub user_data_sharing_consent: Option<bool>,
}
