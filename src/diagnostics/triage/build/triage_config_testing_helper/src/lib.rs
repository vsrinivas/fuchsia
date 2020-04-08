// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Error},
    std::collections::HashMap,
    triage_lib::{
        config::{initialize_for_validation, ParseResult},
        validate::validate,
    },
};

/// Public entry point for testing.
///
/// This method should only be called by generated code
pub fn run_tests(files: Vec<ConfigFile>) -> Result<(), Error> {
    if files.len() == 0 {
        bail!("Need at least 1 file to test");
    }
    let mut config_files = HashMap::new();
    for file in files {
        config_files.insert(file.name, String::from(file.contents));
    }

    let ParseResult { metrics, actions, tests } = initialize_for_validation(config_files)?;
    validate(&metrics, &actions, &tests)?;

    Ok(())
}

#[derive(Debug)]
pub struct ConfigFile {
    pub name: String,
    pub contents: &'static str,
}

impl ConfigFile {
    pub fn new(name: String, contents: &'static str) -> ConfigFile {
        ConfigFile { name: name, contents: contents }
    }
}

#[cfg(test)]
mod test {
    use super::{run_tests, ConfigFile};

    #[test]
    fn can_create_config_file_from_raw_string() {
        let raw_string = r#"{
        test: {
          foo: {
            yes: ["some_disk", "more_disk"],
            no: [],
            inspect: [{
                path: "data",
                contents: {root: {stats: {total_bytes: 10, used_bytes: 9}}}
            }]
          }
        }}"#;
        let _config_file = ConfigFile::new(String::from("foo"), raw_string);
    }

    #[test]
    fn run_tests_fails_for_empty_file_list() {
        assert!(run_tests(vec![]).is_err(), "Should fail on empty vec");
    }

    #[test]
    fn run_tests_fails_for_failing_validate() {
        let raw_string = r#"{
            select: {},
            eval: { e: "1 == 1" },
            act: { equalact: {trigger: "e", print: "equal"}, },
            test: { trial1: {
                yes: [],
                no: ["equalact"],
                inspect: []
            }
        }}"#;
        let config_file = ConfigFile::new(String::from("foo"), raw_string);
        assert!(run_tests(vec![config_file]).is_err(), "run_tests should have failed");
    }

    #[test]
    fn run_tests_does_not_fail_for_passing_validate() {
        let raw_string = r#"{
            select: {},
            eval: { e: "1 == 1" },
            act: { equalact: {trigger: "e", print: "equal"}, },
            test: { trial1: {
                yes: ["equalact"],
                no: [],
                inspect: []
            }
        }}"#;
        let config_file = ConfigFile::new(String::from("foo"), raw_string);
        assert!(run_tests(vec![config_file]).is_ok(), "run_tests should not have failed");
    }
}
