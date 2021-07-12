// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "stop",
    description = "Stops the component designated by the provided relative moniker",
    example = "To stop the component designated by the moniker:

    $ ffx component stop core/brightness_manager",
    note = "Stops the component designated by the provided moniker relative to the
    root of the component topology.
    This will resolve the component if it's not already resolved, and will stop the
    component if it is already running.",
    error_code(1, "Failed to stop the component with moniker <moniker>.")
)]
pub struct ComponentStopCommand {
    #[argh(positional)]
    /// moniker of the component
    pub moniker: String,
    #[argh(switch, long = "recursive", short = 'r')]
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
