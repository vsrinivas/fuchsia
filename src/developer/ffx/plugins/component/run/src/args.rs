// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "run",
    description = "Creates and starts a component instance in an existing collection
within the component topology.",
    example = "To create a component instance from the `hello-world-rust` component URL:

    $ ffx component run /core/ffx-laboratory:hello-world fuchsia-pkg://fuchsia.com/hello-world#meta/hello-world-rust.cm",
    note = "This command is a shorthand for the following:

    $ ffx component create <moniker> <component-url>
    $ ffx component start <moniker>

To learn more about running components, see https://fuchsia.dev/go/components/run"
)]

pub struct RunComponentCommand {
    #[argh(positional)]
    /// moniker of a component instance in an existing collection.
    /// The component instance will be added to the collection.
    pub moniker: String,

    #[argh(positional)]
    // NOTE: this is optional to support the deprecated command:
    // ffx component run <url>, in which case `moniker` above will be
    // set with the URL.
    /// url of the component to create and then start.
    pub url: Option<String>,

    #[argh(option, short = 'n')]
    /// deprecated. specify a name for the component instance.
    /// if this flag is not set, the instance name is derived from the component URL.
    pub name: Option<String>,

    #[argh(switch, short = 'r')]
    /// destroy and recreate the component instance if it already exists
    pub recreate: bool,

    #[argh(switch, short = 'f')]
    /// start printing logs from the started component after it has started
    pub follow_logs: bool,
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["run"];

    #[test]
    fn test_command() {
        let url = "http://test.com";
        let name = "test_instance";
        let args = &[url, "--name", name, "--recreate"];
        assert_eq!(
            RunComponentCommand::from_args(CMD_NAME, args),
            Ok(RunComponentCommand {
                moniker: url.to_string(),
                url: None,
                name: Some(name.to_string()),
                recreate: true,
                follow_logs: false,
            })
        );
        let args = &[url, "--name", name, "--recreate", "--follow-logs"];
        assert_eq!(
            RunComponentCommand::from_args(CMD_NAME, args),
            Ok(RunComponentCommand {
                moniker: url.to_string(),
                url: None,
                name: Some(name.to_string()),
                recreate: true,
                follow_logs: true,
            })
        )
    }
}
