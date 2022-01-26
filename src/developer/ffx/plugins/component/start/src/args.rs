// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "start",
    description = "Starts a component",
    example = "To start the component instance designated by the moniker `/core/brightness_manager`:

    $ ffx component start /core/brightness_manager",
    note = "To learn more about running components, visit https://fuchsia.dev/fuchsia-src/development/components/run"
)]
pub struct ComponentStartCommand {
    #[argh(positional)]
    /// A moniker to a component instance
    pub moniker: String,
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["start"];

    #[test]
    fn test_command() {
        fn check(args: &[&str], expected_moniker: String) {
            assert_eq!(
                ComponentStartCommand::from_args(CMD_NAME, args),
                Ok(ComponentStartCommand { moniker: expected_moniker })
            )
        }

        let test_moniker = "core/brightness_manager";
        check(&[test_moniker], test_moniker.to_string());
    }
}
