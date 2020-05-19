// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "test", description = "run tests")]
pub struct TestCommand {
    #[argh(positional)]
    /// test suite component URL
    pub url: String,

    #[argh(option)]
    /// target device
    pub tests: Option<String>,

    #[argh(switch)]
    /// list tests in the Test Suite
    pub list: bool,
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["test"];

    #[test]
    fn test_command() {
        fn check(
            args: &[&str],
            expected_url: &String,
            expected_tests: Option<String>,
            expected_list: bool,
        ) {
            assert_eq!(
                TestCommand::from_args(CMD_NAME, args),
                Ok(TestCommand {
                    url: expected_url.to_string(),
                    tests: expected_tests,
                    list: expected_list
                })
            )
        }

        let url = "http://test.com".to_string();
        let list = "--list";
        let selector = "tests".to_string();
        let tests = "--tests";

        check(&[&url, &tests, &selector, list], &url, Some("tests".to_string()), true);
    }
}
