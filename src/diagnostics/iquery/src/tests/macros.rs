// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {crate::tests::utils, fuchsia_async as fasync, fuchsia_zircon::DurationNum};

#[macro_export]
macro_rules! assert_command {
    (command: $command:expr, golden_basename: $golden:ident, args: [$($arg:expr),*]) => {
        use crate::tests::macros::CommandAssertion;
        let args : Vec<&str> = vec![$($arg),*];
        assert_command!(@assertion [json, text], $command, $golden, args, false);
    };

    (
        command: $command:expr,
        golden_basename: $golden:ident,
        args: [$($arg:expr),*],
        with_retries
    ) => {
        use crate::tests::macros::CommandAssertion;
        let args : Vec<&str> = vec![$($arg),*];
        assert_command!(@assertion [json, text], $command, $golden, args, true);
    };

    (@assertion
        [$($format:ident),*],
        $command:expr,
        $golden:ident,
        $args:ident,
        $with_retries:expr
    ) => {
        $({
            let expected = include_str!(
                concat!("../../test_data/", stringify!($golden), ".", stringify!($format)));
            let mut assertion = CommandAssertion::new($command, expected, stringify!($format))
                .add_args($args.clone());
            if $with_retries {
                assertion = assertion.with_retries();
            }
            assertion.assert().await;
        })*
    };
}

#[derive(Clone, Debug)]
pub struct CommandAssertion {
    command: String,
    args: Vec<String>,
    max_retries: usize,
    expected: String,
    format: String,
}

impl CommandAssertion {
    pub fn new(
        command: impl Into<String>,
        expected: impl Into<String>,
        format: impl Into<String>,
    ) -> Self {
        Self {
            command: command.into(),
            args: Vec::new(),
            max_retries: 0,
            expected: expected.into(),
            format: format.into(),
        }
    }

    pub fn add_args<T: Into<String>>(mut self, args: Vec<T>) -> Self {
        self.args = args.into_iter().map(|arg| arg.into()).collect::<Vec<_>>();
        self
    }

    pub fn with_retries(mut self) -> Self {
        self.max_retries = 100;
        self
    }

    pub async fn assert(self) {
        let mut retries = 0;
        let mut command_line = vec!["--format", &self.format, &self.command];
        command_line.append(&mut self.args.iter().map(|s| s.as_ref()).collect::<Vec<_>>());
        loop {
            let result = utils::execute_command(&command_line[..]).await.expect("execute command");
            if retries >= self.max_retries {
                utils::assert_result(&result, &self.expected);
                break;
            }
            if utils::result_equals_expected(&result, &self.expected) {
                break;
            }
            fasync::Timer::new(fasync::Time::after(100.millis())).await;
            retries += 1;
        }
    }
}
