// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod testing;

use serde::{Deserialize, Serialize};
use std::path::PathBuf;

/// Filename of the top level summary json.
pub const RUN_SUMMARY_NAME: &str = "run_summary.json";

/// A serializable version of a test outcome.
#[derive(Deserialize, Serialize, PartialEq, Eq, Debug)]
#[serde(rename_all = "UPPERCASE")]
pub enum Outcome {
    Passed,
    Failed,
    Inconclusive,
    Timedout,
    Error,
    Skipped,
}

/// A serializable test run result.
/// This contains overall results and artifacts scoped to a test run, and
/// a list of filenames for finding serialized suite results.
#[derive(Deserialize, Serialize, PartialEq, Eq, Debug)]
#[serde(tag = "version")]
pub enum TestRunResult {
    #[serde(rename = "0")]
    V0 {
        /// Paths to artifacts scoped at the run level. The paths are relative to the root of
        /// the output directory.
        artifacts: Vec<PathBuf>,
        outcome: Outcome,
        suites: Vec<SuiteEntryV0>,
    },
}

/// A suite listing in the test run summary.
#[derive(Deserialize, Serialize, PartialEq, Eq, Debug)]
pub struct SuiteEntryV0 {
    /// Location of the summary file for this suite.
    pub summary: String,
}

/// A serializable suite run result.
/// Contains overall results and artifacts scoped to a suite run, and
/// results and artifacts scoped to any test run within it.
#[derive(Deserialize, Serialize, PartialEq, Eq, Debug)]
#[serde(tag = "version")]
pub enum SuiteResult {
    #[serde(rename = "0")]
    V0 {
        /// Paths to artifacts scoped at the run level. The paths are relative to the root of
        /// the output directory.
        artifacts: Vec<PathBuf>,
        outcome: Outcome,
        name: String,
        cases: Vec<TestCaseResultV0>,
    },
}

/// A serializable test case result.
#[derive(Deserialize, Serialize, PartialEq, Eq, Debug)]
pub struct TestCaseResultV0 {
    /// Paths to artifacts scoped at the run level. The paths are relative to the root of
    /// the output directory.
    pub artifacts: Vec<PathBuf>,
    pub outcome: Outcome,
    pub name: String,
}

#[cfg(test)]
mod test {
    use super::*;
    use serde_json::{from_str, json, to_string, Value};

    #[test]
    fn run_version_serialized() {
        // This is a sanity check that verifies version is serialized.
        let run_result =
            TestRunResult::V0 { artifacts: vec![], outcome: Outcome::Inconclusive, suites: vec![] };

        let serialized = to_string(&run_result).expect("serialize result");
        let value = from_str::<Value>(&serialized).expect("deserialize result");

        let expected = json!({
            "version": "0",
            "artifacts": [],
            "outcome": "INCONCLUSIVE",
            "suites": [],
        });

        assert_eq!(value, expected);
    }

    #[test]
    fn run_version_mismatch() {
        let wrong_version_json = json!({
            "version": "10",
            "artifacts": [],
            "outcome": "INCONCLUSIVE",
            "suites": [],
        });

        let serialized = to_string(&wrong_version_json).expect("serialize result");

        assert!(from_str::<TestRunResult>(&serialized).unwrap_err().is_data());
    }

    #[test]
    fn suite_version_serialized() {
        let suite_result = SuiteResult::V0 {
            artifacts: vec![],
            outcome: Outcome::Inconclusive,
            cases: vec![],
            name: "suite".to_string(),
        };

        let serialized = to_string(&suite_result).expect("serialize result");
        let value = from_str::<Value>(&serialized).expect("deserialize result");

        let expected = json!({
            "version": "0",
            "artifacts": [],
            "outcome": "INCONCLUSIVE",
            "cases": [],
            "name": "suite",
        });

        assert_eq!(value, expected);
    }

    #[test]
    fn suite_version_mismatch() {
        let wrong_version_json = json!({
            "version": "10",
            "artifacts": [],
            "outcome": "INCONCLUSIVE",
            "cases": [],
            "name": "suite",
        });

        let serialized = to_string(&wrong_version_json).expect("serialize result");

        assert!(from_str::<SuiteResult>(&serialized).unwrap_err().is_data());
    }
}
