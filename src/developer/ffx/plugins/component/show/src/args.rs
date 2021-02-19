// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "show",
    description = "Show useful information about a component",
    example = "To show information about a component with full url:

    $ ffx component show fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm

    To show information about a component with partial url:

    $ ffx component show appmgr.cm

    To show information about a component with name:

    $ ffx component show appmgr",
    note = "Show useful information about a component including url, merkle root,
exposed/incoming/outgoing services, etc. The command expects a <url/name> which is
the partial url or name of the component.",
    error_code(
        1,
        "Error! Failed to get information about the component. The component may not exist."
    )
)]
pub struct ComponentShowCommand {
    #[argh(positional)]
    /// partial url or name of the component
    pub filter: String,
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["show"];

    #[test]
    fn test_command() {
        fn check(args: &[&str], expected_filter: String) {
            assert_eq!(
                ComponentShowCommand::from_args(CMD_NAME, args),
                Ok(ComponentShowCommand { filter: expected_filter })
            )
        }

        let test_filter = "http://test.com";
        check(&[test_filter], test_filter.to_string());
    }
}
