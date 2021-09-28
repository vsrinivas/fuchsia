// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod testing;

use serde::{Deserialize, Serialize};
use std::{collections::HashMap, path::PathBuf};

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
        /// A mapping from paths to artifacts to metadata associated with the artifact.
        /// The paths are relative to the root of the output directory.
        artifacts: HashMap<PathBuf, ArtifactMetadataV0>,
        outcome: Outcome,
        suites: Vec<SuiteEntryV0>,
        /// Approximate start time, as milliseconds since the epoch.
        #[serde(skip_serializing_if = "Option::is_none")]
        start_time: Option<u64>,
        #[serde(skip_serializing_if = "Option::is_none")]
        duration_milliseconds: Option<u64>,
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
        /// A mapping from paths to artifacts to metadata associated with the artifact.
        /// The paths are relative to the root of the output directory.
        artifacts: HashMap<PathBuf, ArtifactMetadataV0>,
        outcome: Outcome,
        name: String,
        cases: Vec<TestCaseResultV0>,
        /// Approximate start time, as milliseconds since the epoch.
        #[serde(skip_serializing_if = "Option::is_none")]
        start_time: Option<u64>,
        #[serde(skip_serializing_if = "Option::is_none")]
        duration_milliseconds: Option<u64>,
    },
}

/// A serializable test case result.
#[derive(Deserialize, Serialize, PartialEq, Eq, Debug)]
pub struct TestCaseResultV0 {
    /// A mapping from paths to artifacts to metadata associated with the artifact.
    /// The paths are relative to the root of the output directory.
    pub artifacts: HashMap<PathBuf, ArtifactMetadataV0>,
    pub outcome: Outcome,
    pub name: String,
    /// Approximate start time, as milliseconds since the epoch.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub start_time: Option<u64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub duration_milliseconds: Option<u64>,
}

/// Metadata associated with an artifact.
#[derive(Deserialize, Serialize, PartialEq, Eq, Debug, Clone, Hash)]
pub struct ArtifactMetadataV0 {
    /// The type of the artifact.
    pub artifact_type: ArtifactType,
    /// Moniker of the component which produced the artifact, if applicable.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub component_moniker: Option<String>,
}

/// Types of artifacts known to the test framework.
#[derive(Deserialize, Serialize, PartialEq, Eq, Debug, Clone, Hash)]
#[serde(rename_all = "UPPERCASE")]
pub enum ArtifactType {
    Syslog,
    Stdout,
    Stderr,
}

impl From<ArtifactType> for ArtifactMetadataV0 {
    fn from(other: ArtifactType) -> Self {
        Self { artifact_type: other, component_moniker: None }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use maplit::hashmap;
    use serde_json::{from_str, json, to_string, Value};
    use std::path::Path;
    use valico::json_schema;

    #[test]
    fn run_version_serialized() {
        // This is a sanity check that verifies version is serialized.
        let run_result = TestRunResult::V0 {
            artifacts: HashMap::new(),
            outcome: Outcome::Inconclusive,
            suites: vec![],
            duration_milliseconds: None,
            start_time: None,
        };

        let serialized = to_string(&run_result).expect("serialize result");
        let value = from_str::<Value>(&serialized).expect("deserialize result");

        let expected = json!({
            "version": "0",
            "artifacts": {},
            "outcome": "INCONCLUSIVE",
            "suites": [],
        });

        assert_eq!(value, expected);
    }

    #[test]
    fn run_version_mismatch() {
        let wrong_version_json = json!({
            "version": "10",
            "artifacts": {},
            "outcome": "INCONCLUSIVE",
            "suites": [],
        });

        let serialized = to_string(&wrong_version_json).expect("serialize result");

        assert!(from_str::<TestRunResult>(&serialized).unwrap_err().is_data());
    }

    #[test]
    fn suite_version_serialized() {
        let suite_result = SuiteResult::V0 {
            artifacts: HashMap::new(),
            outcome: Outcome::Inconclusive,
            cases: vec![],
            name: "suite".to_string(),
            duration_milliseconds: None,
            start_time: None,
        };

        let serialized = to_string(&suite_result).expect("serialize result");
        let value = from_str::<Value>(&serialized).expect("deserialize result");

        let expected = json!({
            "version": "0",
            "artifacts": {},
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
            "artifacts": {},
            "outcome": "INCONCLUSIVE",
            "cases": [],
            "name": "suite",
        });

        let serialized = to_string(&wrong_version_json).expect("serialize result");

        assert!(from_str::<SuiteResult>(&serialized).unwrap_err().is_data());
    }

    #[test]
    fn run_conforms_to_schema() {
        const RUN_SCHEMA: &str = include_str!("../schema/run_summary.schema.json");
        let run_schema = serde_json::from_str(RUN_SCHEMA).expect("parse json schema");
        let mut scope = json_schema::Scope::new();
        let compiled_schema =
            scope.compile_and_return(run_schema, false).expect("compile json schema");

        let cases = vec![
            TestRunResult::V0 {
                artifacts: hashmap! {},
                outcome: Outcome::Skipped,
                suites: vec![],
                duration_milliseconds: None,
                start_time: None,
            },
            TestRunResult::V0 {
                artifacts: hashmap! {
                    Path::new("a/b.txt").to_path_buf() => ArtifactType::Syslog.into(),
                },
                outcome: Outcome::Skipped,
                suites: vec![SuiteEntryV0 { summary: "suite-summary.json".to_string() }],
                duration_milliseconds: None,
                start_time: None,
            },
            TestRunResult::V0 {
                artifacts: hashmap! {
                    Path::new("a/b.txt").to_path_buf() => ArtifactType::Stderr.into(),
                    Path::new("c/d.txt").to_path_buf() => ArtifactMetadataV0 {
                        artifact_type: ArtifactType::Syslog,
                        component_moniker: Some("component".to_string())
                    },
                },
                outcome: Outcome::Skipped,
                suites: vec![
                    SuiteEntryV0 { summary: "suite-summary-1.json".to_string() },
                    SuiteEntryV0 { summary: "suite-summary-2.json".to_string() },
                ],
                duration_milliseconds: Some(65),
                start_time: Some(01),
            },
        ];

        for case in cases.iter() {
            let serialized = serde_json::to_value(case).expect("serialize test run");
            let validate_result = compiled_schema.validate(&serialized);
            if !validate_result.is_strictly_valid() {
                panic!(
                    "Run did not conform with schema: {:?}, {:?}, {:?}",
                    case, serialized, validate_result
                );
            }
        }
    }

    #[test]
    fn suite_conforms_to_schema() {
        const SUITE_SCHEMA: &str = include_str!("../schema/suite_summary.schema.json");
        let suite_schema = serde_json::from_str(SUITE_SCHEMA).expect("parse json schema");
        let mut scope = json_schema::Scope::new();
        let compiled_schema =
            scope.compile_and_return(suite_schema, false).expect("compile json schema");

        let cases = vec![
            SuiteResult::V0 {
                artifacts: hashmap! {},
                outcome: Outcome::Passed,
                name: "my test suite".to_string(),
                cases: vec![],
                duration_milliseconds: None,
                start_time: None,
            },
            SuiteResult::V0 {
                artifacts: hashmap! {},
                outcome: Outcome::Failed,
                name: "another suite".to_string(),
                cases: vec![TestCaseResultV0 {
                    artifacts: hashmap! {},
                    outcome: Outcome::Inconclusive,
                    name: "test case".to_string(),
                    duration_milliseconds: Some(12),
                    start_time: Some(100),
                }],
                duration_milliseconds: Some(80),
                start_time: Some(200),
            },
            SuiteResult::V0 {
                artifacts: hashmap! {
                    Path::new("suite/a.txt").to_path_buf() => ArtifactType::Stderr.into(),
                },
                outcome: Outcome::Failed,
                name: "another suite".to_string(),
                cases: vec![
                    TestCaseResultV0 {
                        artifacts: hashmap! {
                            Path::new("case-0/b.txt").to_path_buf() => ArtifactType::Stdout.into(),
                            Path::new("case-0/c.txt").to_path_buf() => ArtifactMetadataV0 {
                                artifact_type: ArtifactType::Syslog,
                                component_moniker: Some("component".to_string())
                            },
                        },
                        outcome: Outcome::Timedout,
                        name: "test case".to_string(),
                        duration_milliseconds: None,
                        start_time: Some(37),
                    },
                    TestCaseResultV0 {
                        artifacts: hashmap![
                            Path::new("case-1/d.txt").to_path_buf() => ArtifactType::Stdout.into(),
                            Path::new("case-1/e.txt").to_path_buf() => ArtifactType::Stdout.into(),
                        ],
                        outcome: Outcome::Error,
                        name: "test case 2".to_string(),
                        duration_milliseconds: Some(37),
                        start_time: None,
                    },
                ],
                duration_milliseconds: Some(37),
                start_time: None,
            },
        ];

        for case in cases.iter() {
            let serialized = serde_json::to_value(case).expect("serialize test run");
            let validate_result = compiled_schema.validate(&serialized);
            if !validate_result.is_strictly_valid() {
                panic!(
                    "Run did not conform with schema: {:?}, {:?}, {:?}",
                    case, serialized, validate_result
                );
            }
        }
    }
}
