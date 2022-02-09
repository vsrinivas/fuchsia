// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use fidl_fuchsia_settings::DoNotDisturbSettings;

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq, Clone, Copy)]
#[argh(subcommand, name = "do_not_disturb")]
/// get or set DnD settings
pub struct DoNotDisturb {
    /// when set to 'true', allows the device to enter do not disturb mode
    #[argh(option, short = 'u')]
    pub user_dnd: Option<bool>,

    /// when set to 'true', forces the device into do not disturb mode
    #[argh(option, short = 'n')]
    pub night_mode_dnd: Option<bool>,
}

impl From<DoNotDisturb> for DoNotDisturbSettings {
    fn from(src: DoNotDisturb) -> DoNotDisturbSettings {
        DoNotDisturbSettings {
            user_initiated_do_not_disturb: src.user_dnd,
            night_mode_initiated_do_not_disturb: src.night_mode_dnd,
            ..DoNotDisturbSettings::EMPTY
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["do_not_disturb"];

    #[test]
    fn test_dnd_cmd() {
        // Test input arguments are generated to according struct.
        let user = "true";
        let args = &["-u", user];
        assert_eq!(
            DoNotDisturb::from_args(CMD_NAME, args),
            Ok(DoNotDisturb { user_dnd: Some(true), night_mode_dnd: None })
        )
    }
}
