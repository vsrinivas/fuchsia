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
    fuchsia-pkg://fuchsia.com/hello_world_rust#meta/hello_world_rust.cmx

To run the Remote Control Service:

    $ ffx component run \\
    fuchsia-pkg://fuchsia.com/remote-control#meta/remote-control-runner.cmx",
    description = "Run a component on the target",
    note = "Runs a specified v1 component on the target. The <url> must follow the
format:

`fuchsia-pkg://fuchsia.com/<package>#meta/<component>.cmx`."
)]

pub struct RunComponentCommand {
    #[argh(positional)]
    /// url of component to run
    pub url: String,
    #[argh(positional)]
    /// args for the component
    pub args: Vec<String>,
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["run"];

    #[test]
    fn test_command() {
        fn check(args: &[&str], expected_url: String, expected_args: Vec<String>) {
            assert_eq!(
                RunComponentCommand::from_args(CMD_NAME, args),
                Ok(RunComponentCommand { url: expected_url, args: expected_args })
            )
        }

        let test_url = "http://test.com";
        let arg1 = "test1";
        let arg2 = "test2";
        let args = vec![arg1.to_string(), arg2.to_string()];

        check(&[test_url, arg1, arg2], test_url.to_string(), args);
    }
}
