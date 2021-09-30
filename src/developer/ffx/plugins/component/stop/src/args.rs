// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "stop",
    description = "Stops a component instance",
    example = "To stop the component instance designated by the moniker `/core/brightness_manager`:

    $ ffx component stop /core/brightness_manager",
    note = "To learn more about running components, visit https://fuchsia.dev/fuchsia-src/development/components/run"
)]
pub struct ComponentStopCommand {
    #[argh(positional)]
    /// A moniker to a component instance
    pub moniker: String,
    #[argh(switch, short = 'r')]
    /// whether or not to stop the component recursively
    pub recursive: bool,
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["stop"];

    #[test]
    fn test_command() {
        fn check(args: &[&str], expected_moniker: String, expected_recursive: bool) {
            assert_eq!(
                ComponentStopCommand::from_args(CMD_NAME, args),
                Ok(ComponentStopCommand {
                    moniker: expected_moniker,
                    recursive: expected_recursive
                })
            )
        }

        let test_moniker = "core/brightness_manager";
        let test_recursive = false;
        check(&[test_moniker], test_moniker.to_string(), test_recursive);
    }
}
