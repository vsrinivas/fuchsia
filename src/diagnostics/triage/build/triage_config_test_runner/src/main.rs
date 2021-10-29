// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Error},
    structopt::StructOpt,
    triage::ActionTagDirective,
    triage_app_lib::file_io::config_from_files,
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

    let parse_result = config_from_files(&config_files, &ActionTagDirective::AllowAll)?;
    parse_result.validate()?;

    Ok(())
}

#[cfg(test)]
mod test {
    use super::run_tests;

    #[fuchsia::test]
    fn run_tests_fails_for_empty_file_list() {
        assert!(run_tests(vec![]).is_err(), "Should fail on empty vec");
    }

    #[fuchsia::test]
    fn run_tests_fails_for_failing_validate() {
        assert!(run_tests(vec!["foo.triage".to_string()]).is_err(), "run_tests should have failed");
    }
}
