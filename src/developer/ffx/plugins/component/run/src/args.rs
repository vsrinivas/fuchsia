// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "run",
    example = "To run the 'hello_world_rust' component:

    $ ffx component run \\
    fuchsia-pkg://fuchsia.com/hello-world#meta/hello-world-rust.cm",
    description = "Create and run a v2 component instance in an isolated realm",
    note = "The <url> must follow the format:

`fuchsia-pkg://fuchsia.com/<package>#meta/<component>.cm`"
)]

pub struct RunComponentCommand {
    #[argh(positional)]
    /// url of component to run
    pub url: String,

    #[argh(option, short = 'n')]
    /// specify a name for the component instance.
    /// if this flag is not set, the instance name is derived from the component URL.
    pub name: Option<String>,
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["run"];

    #[test]
    fn test_command() {
        let url = "http://test.com";
        let name = "test_instance";
        let args = &[url, "--name", name];
        assert_eq!(
            RunComponentCommand::from_args(CMD_NAME, args),
            Ok(RunComponentCommand { url: url.to_string(), name: Some(name.to_string()) })
        )
    }
}
