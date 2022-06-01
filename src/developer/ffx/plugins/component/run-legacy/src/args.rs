// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "run-legacy",
    example = "To run the 'hello_world_rust' component:

    $ ffx component run-legacy \\
    fuchsia-pkg://fuchsia.com/hello_world_rust#meta/hello_world_rust.cmx",
    description = "Runs a legacy (CMX) component instance on the target",
    note = "To learn more, see https://fuchsia.dev/go/components/url"
)]

pub struct RunComponentCommand {
    #[argh(positional)]
    /// url of component to run
    pub url: String,
    #[argh(positional)]
    /// args for the component
    pub args: Vec<String>,
    #[argh(switch, long = "background", short = 'b')]
    /// switch to turn on background info
    pub background: bool,
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["run-legacy"];

    #[test]
    fn test_command() {
        fn check(
            args: &[&str],
            expected_url: String,
            expected_args: Vec<String>,
            expected_background: bool,
        ) {
            assert_eq!(
                RunComponentCommand::from_args(CMD_NAME, args),
                Ok(RunComponentCommand {
                    url: expected_url,
                    args: expected_args,
                    background: expected_background
                })
            )
        }

        let test_url = "http://test.com";
        let arg1 = "test1";
        let arg2 = "test2";
        let args = vec![arg1.to_string(), arg2.to_string()];
        let background = "--background";

        check(&[test_url, arg1, arg2, background], test_url.to_string(), args.clone(), true);
        check(&[test_url, arg1, arg2], test_url.to_string(), args.clone(), false);
        check(&[test_url, background, arg1, arg2], test_url.to_string(), args.clone(), true);
        check(&[background, test_url, arg1, arg2], test_url.to_string(), args.clone(), true);
    }
}
