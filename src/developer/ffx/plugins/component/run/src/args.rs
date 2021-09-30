// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "run",
    description = "Creates and binds to a component instance",
    example = "To create a component instance from the `hello-world-rust` component URL:

    $ ffx component run fuchsia-pkg://fuchsia.com/hello-world#meta/hello-world-rust.cm",
    note = "This command is a shorthand for the following:

    $ ffx component create /core/ffx-laboratory:<instance-name> <component-url>
    $ ffx component bind /core/ffx-laboratory:<instance-name>

To learn more about running components, visit https://fuchsia.dev/fuchsia-src/development/components/run"
)]

pub struct RunComponentCommand {
    #[argh(positional)]
    /// url of component to run
    pub url: String,

    #[argh(option, short = 'n')]
    /// specify a name for the component instance.
    /// if this flag is not set, the instance name is derived from the component URL.
    pub name: Option<String>,

    #[argh(switch, short = 'r')]
    /// destroy and recreate the component instance if it already exists
    pub recreate: bool,
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
                url: url.to_string(),
                name: Some(name.to_string()),
                recreate: true
            })
        )
    }
}
