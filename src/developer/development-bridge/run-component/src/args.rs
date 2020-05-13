// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "run-component", description = "run component")]
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
    const CMD_NAME: &'static [&'static str] = &["run-component"];

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
