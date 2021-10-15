// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "resolve",
    description = "Resolves a component instance",
    example = "To resolve the component designated by the provided moniker `/core/brightness_manager`:

    $ ffx component resolve /core/brightness_manager"
)]
pub struct ComponentResolveCommand {
    #[argh(positional)]
    /// A moniker to a component instance
    pub moniker: String,
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["resolve"];

    #[test]
    fn test_command() {
        fn check(args: &[&str], expected_moniker: String) {
            assert_eq!(
                ComponentResolveCommand::from_args(CMD_NAME, args),
                Ok(ComponentResolveCommand { moniker: expected_moniker })
            )
        }

        let test_moniker = "core/brightness_manager";
        check(&[test_moniker], test_moniker.to_string());
    }
}
