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
        let opts : Vec<&str> = Vec::new();
        assert_command!(@assertion [json, text], $command, $golden, args, opts);
    };

    (
        command: $command:expr,
        golden_basename: $golden:ident,
        args: [$($arg:expr),*],
        test_opts: [$($opt:expr),*]
    ) => {
        use crate::tests::macros::CommandAssertion;
        let args : Vec<&str> = vec![$($arg),*];
        let opts: Vec<&str> = vec![$($opt),*];
        assert_command!(@assertion [json, text], $command, $golden, args, opts);
    };

    (@assertion
        [$($format:ident),*],
        $command:expr,
        $golden:ident,
        $args:ident,
        $test_opts:ident
    ) => {
        $({
            let expected = include_str!(
                concat!("../../test_data/", stringify!($golden), ".", stringify!($format)));
            let mut assertion = CommandAssertion::new($command, expected, stringify!($format))
                .add_args($args.clone());
            for opt in &$test_opts {
                match &opt[..] {
                    "with_retries" => assertion.with_retries(),
                    "remove_observer" => assertion.remove_observer(),
                    _ => panic!("unkown test option"),
                }
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
    remove_observer: bool,
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
            remove_observer: false,
        }
    }

    pub fn add_args<T: Into<String>>(mut self, args: Vec<T>) -> Self {
        self.args = args.into_iter().map(|arg| arg.into()).collect::<Vec<_>>();
        self
    }

    pub fn with_retries(&mut self) {
        self.max_retries = 100;
    }

    pub fn remove_observer(&mut self) {
        self.remove_observer = true;
    }

    pub async fn assert(self) {
        let mut retries = 0;
        let mut command_line = vec!["--format", &self.format, &self.command];
        command_line.append(&mut self.args.iter().map(|s| s.as_ref()).collect::<Vec<_>>());
        loop {
            let mut result =
                utils::execute_command(&command_line[..]).await.expect("execute command");
            if self.remove_observer {
                result = self.do_remove_observer(result);
            }
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

    fn do_remove_observer(&self, result: String) -> String {
        match &self.format[..] {
            "json" => {
                // Removes the entry in the vector for the archivist-for-embedding.cmx
                let mut result_json: serde_json::Value =
                    serde_json::from_str(&result).expect("expected json");
                match result_json.as_array_mut() {
                    None => result,
                    Some(arr) => {
                        arr.retain(|value| {
                            value
                                .get("moniker")
                                .and_then(|val| val.as_str())
                                .map(|val| {
                                    // TODO(fxbug.dev/58074) remove observer.cmx
                                    !(val.ends_with("archivist-for-embedding.cmx")
                                        || val.ends_with("observer.cmx"))
                                })
                                .unwrap_or(true)
                        });
                        serde_json::to_string_pretty(&result_json).unwrap()
                    }
                }
            }
            "text" => {
                // Removes the chunk of text that belongs to archivist-for-embedding.cmx
                let lines = result.lines().collect::<Vec<_>>();
                match lines.iter().enumerate().find(|(_, line)| {
                    // TODO(fxbug.dev/58074) remove observer.cmx
                    line.ends_with("archivist-for-embedding.cmx:")
                        || line.ends_with("observer.cmx:")
                }) {
                    None => result,
                    Some((found_index, _)) => {
                        let next_index = lines
                            .iter()
                            .enumerate()
                            .find(|(i, line)| line.ends_with(".cmx:") && *i > found_index)
                            .map(|(i, _)| i)
                            .unwrap_or(lines.len() - 1);
                        let mut result = lines[0..found_index].to_vec();
                        result.extend(&lines[next_index..]);
                        result.join("\n")
                    }
                }
            }
            _ => panic!("unknown format"),
        }
    }
}
