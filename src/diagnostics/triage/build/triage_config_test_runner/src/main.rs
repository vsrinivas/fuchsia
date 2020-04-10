// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Error},
    structopt::StructOpt,
    triage_lib::{
        config::{self, ParseResult},
        validate::validate,
    },
};

#[derive(StructOpt, Debug)]
pub struct Options {
    #[structopt(long = "config")]
    config_files: Vec<String>,
}

fn main() -> Result<(), Error> {
    let options = Options::from_args();
    run_tests(options.config_files)
}

fn run_tests(config_files: Vec<String>) -> Result<(), Error> {
    if config_files.len() == 0 {
        bail!("Need at least 1 file to test");
    }

    let ParseResult { metrics, actions, tests } = config::initialize_for_validation(config_files)?;
    validate(&metrics, &actions, &tests)?;

    Ok(())
}

#[cfg(test)]
mod test {
    use super::run_tests;

    #[test]
    fn run_tests_fails_for_empty_file_list() {
        assert!(run_tests(HashMap::new()).is_err(), "Should fail on empty vec");
    }

    #[test]
    fn run_tests_fails_for_failing_validate() {
        let config_text = r#"{
            select: {},
            eval: { e: "1 == 1" },
            act: { equalact: {trigger: "e", print: "equal"}, },
            test: { trial1: {
                yes: [],
                no: ["equalact"],
                inspect: []
            }
        }}"#;
        let mut config_files = HashMap::new();
        config_files.insert(String::from("foo"), String::from(config_text));

        assert!(run_tests(config_files).is_err(), "run_tests should have failed");
    }

    #[test]
    fn run_tests_does_not_fail_for_passing_validate() {
        let config_text = r#"{
            select: {},
            eval: { e: "1 == 1" },
            act: { equalact: {trigger: "e", print: "equal"}, },
            test: { trial1: {
                yes: ["equalact"],
                no: [],
                inspect: []
            }
        }}"#;
        let mut config_files = HashMap::new();
        config_files.insert(String::from("foo"), String::from(config_text));

        assert!(run_tests(config_files).is_ok(), "run_tests should not have failed");
    }
}
