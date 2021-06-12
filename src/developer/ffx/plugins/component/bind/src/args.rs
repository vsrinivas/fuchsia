// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "bind",
    description = "Binds to the v2 component designated by the provided relative moniker",
    example = "To bind to the v2 component designated by the moniker:

    $ ffx component bind core/brightness_manager",
    note = "Binds to the v2 component designated by the provided relative moniker
    relative to the v2 component to which the protocol is scoped.
    This will resolve the v2 component if it's not already resolved, and will start the v2 component
    if it isn't already running. ",
    error_code(1, "Failed to bind to the v2 component with moniker <moniker>.")
)]
pub struct ComponentBindCommand {
    #[argh(positional)]
    /// moniker of the component
    pub moniker: String,
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["bind"];

    #[test]
    fn test_command() {
        fn check(args: &[&str], expected_moniker: String) {
            assert_eq!(
                ComponentBindCommand::from_args(CMD_NAME, args),
                Ok(ComponentBindCommand { moniker: expected_moniker })
            )
        }

        let test_moniker = "core/brightness_manager";
        check(&[test_moniker], test_moniker.to_string());
    }
}
