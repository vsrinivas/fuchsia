// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

#[derive(FromArgs, Debug, PartialEq)]
/// Fuchsia Development Bridge
pub struct Fdb {
    #[argh(subcommand)]
    pub subcommand: Subcommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "start", description = "starting background daemon")]
pub struct StartCommand {}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "echo", description = "run echo test")]
pub struct EchoCommand {
    #[argh(positional)]
    /// text string to echo back and forth
    pub text: Option<String>,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "list", description = "list connected devices")]
pub struct ListCommand {}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum Subcommand {
    Start(StartCommand),
    Echo(EchoCommand),
    List(ListCommand),
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["fdb"];

    #[test]
    fn test_echo() {
        fn check(args: &[&str], expected_echo: &str) {
            assert_eq!(
                Fdb::from_args(CMD_NAME, args),
                Ok(Fdb {
                    subcommand: Subcommand::Echo(EchoCommand {
                        text: Some(expected_echo.to_string()),
                    })
                })
            )
        }

        let echo = "test-echo";

        check(&["echo", echo], echo);
    }

    #[test]
    fn test_start() {
        fn check(args: &[&str]) {
            assert_eq!(
                Fdb::from_args(CMD_NAME, args),
                Ok(Fdb { subcommand: Subcommand::Start(StartCommand {}) })
            )
        }

        check(&["start"]);
    }

    #[test]
    fn test_list() {
        fn check(args: &[&str]) {
            assert_eq!(
                Fdb::from_args(CMD_NAME, args),
                Ok(Fdb { subcommand: Subcommand::List(ListCommand {}) })
            )
        }

        check(&["list"]);
    }
}
