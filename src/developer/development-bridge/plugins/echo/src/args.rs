// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "echo", description = "run echo test")]
pub struct EchoCommand {
    #[argh(positional)]
    /// text string to echo back and forth
    pub text: Option<String>,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_echo() {
        fn check(args: &[&str], expected_echo: &str) {
            assert_eq!(
                EchoCommand::from_args(&["echo"], args),
                Ok(EchoCommand { text: Some(expected_echo.to_string()) })
            )
        }

        let echo = "test-echo";
        check(&[echo], echo);
    }
}
