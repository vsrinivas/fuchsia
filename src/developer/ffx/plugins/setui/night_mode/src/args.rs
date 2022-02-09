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

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["night_mode"];

    #[test]
    fn test_night_mode_cmd() {
        // Test input arguments are generated to according struct.
        let enabled = "true";
        let args = &["-n", enabled];
        assert_eq!(
            NightMode::from_args(CMD_NAME, args),
            Ok(NightMode { night_mode_enabled: Some(true) })
        )
    }
}
