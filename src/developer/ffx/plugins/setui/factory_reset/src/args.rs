// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "factory_reset")]
/// get or set factory reset settings
pub struct FactoryReset {
    /// when set to 'true', factory reset can be performed on the device
    #[argh(option, short = 'l')]
    pub is_local_reset_allowed: Option<bool>,
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["factory_reset"];

    #[test]
    fn test_factory_reset_cmd() {
        // Test input arguments are generated to according struct.
        let allowed = "true";
        let args = &["-l", allowed];
        assert_eq!(
            FactoryReset::from_args(CMD_NAME, args),
            Ok(FactoryReset { is_local_reset_allowed: Some(true) })
        )
    }
}
