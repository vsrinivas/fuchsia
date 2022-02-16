// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod macros;
pub mod testing;

use serde::{Deserialize, Serialize};
use std::{collections::HashMap, path::PathBuf};

/// Filename of the top level summary json.
pub const RUN_SUMMARY_NAME: &str = "run_summary.json";

enumerable_enum! {
    /// A serializable version of a test outcome.
    #[derive(Deserialize, Serialize, PartialEq, Eq, Debug, Clone, Copy)]
    #[serde(rename_all = "SCREAMING_SNAKE_CASE")]
    Outcome {
        NotStarted,
        Passed,
        Failed,
        Inconclusive,
        Timedout,
        Error,
        Skipped,
    }
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
        /// The paths are given relative to |artifact_dir|. Artifacts are always placed
        /// in the top level directory of |artifact_dir|. Note that some artifacts are
        /// themselves directories. In this case, the root directory of the artifact is
        /// a direct child of |artifact_dir|.
        artifacts: HashMap<PathBuf, ArtifactMetadataV0>,
        /// Path to the directory containing artifacts for the suite. The path is given
        /// relative to the root of the output directory. |artifact_dir| is always in the root
        /// output directory; it will never be in a subdirectory.
        artifact_dir: PathBuf,
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
        /// The paths are given relative to |artifact_dir|. Artifacts are always placed
        /// in the top level directory of |artifact_dir|. Note that some artifacts are
        /// themselves directories. In this case, the root directory of the artifact is
        /// a direct child of |artifact_dir|.
        artifacts: HashMap<PathBuf, ArtifactMetadataV0>,
        /// Path to the directory containing artifacts for the suite. The path is given
        /// relative to the root of the output directory. |artifact_dir| is always in the root
        /// output directory; it will never be in a subdirectory.
        artifact_dir: PathBuf,
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
    /// The paths are given relative to |artifact_dir|. Artifacts are always placed
    /// in the top level directory of |artifact_dir|. Note that some artifacts are
    /// themselves directories. In this case, the root directory of the artifact is
    /// a direct child of |artifact_dir|.
    pub artifacts: HashMap<PathBuf, ArtifactMetadataV0>,
    /// Path to the directory containing artifacts for the suite. The path is given
    /// relative to the root of the output directory. |artifact_dir| is always in the root
    /// output directory; it will never be in a subdirectory.
    pub artifact_dir: PathBuf,
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

enumerable_enum! {
    /// Types of artifacts known to the test framework.
    #[derive(Deserialize, Serialize, PartialEq, Eq, Debug, Clone, Copy, Hash)]
    #[serde(rename_all = "SCREAMING_SNAKE_CASE")]
    ArtifactType {
        Syslog,
        /// Unexpected high severity logs that caused a test to fail.
        RestrictedLog,
        Stdout,
        Stderr,
        /// A directory containing custom artifacts produced by a component in the test.
        Custom,
        /// A human readable report generated by the test framework.
        Report,
        /// Debug data. For example, profraw or symbolizer output.
        Debug,
    }
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
            artifact_dir: Path::new("a").to_path_buf(),
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
            "artifact_dir": "a",
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
            artifact_dir: Path::new("a").to_path_buf(),
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
            "artifact_dir": "a",
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
        let run_schema = serde_json::from_str(testing::RUN_SCHEMA).expect("parse json schema");
        let mut scope = json_schema::Scope::new();
        let compiled_schema =
            scope.compile_and_return(run_schema, false).expect("compile json schema");

        let mut cases = vec![
            TestRunResult::V0 {
                artifacts: hashmap! {},
                artifact_dir: Path::new("a").to_path_buf(),
                outcome: Outcome::Skipped,
                suites: vec![],
                duration_milliseconds: None,
                start_time: None,
            },
            TestRunResult::V0 {
                artifacts: hashmap! {
                    Path::new("b.txt").to_path_buf() => ArtifactType::Syslog.into(),
                },
                artifact_dir: Path::new("a").to_path_buf(),
                outcome: Outcome::Skipped,
                suites: vec![SuiteEntryV0 { summary: "suite-summary.json".to_string() }],
                duration_milliseconds: None,
                start_time: None,
            },
            TestRunResult::V0 {
                artifacts: hashmap! {
                    Path::new("b.txt").to_path_buf() => ArtifactType::Stderr.into(),
                    Path::new("d.txt").to_path_buf() => ArtifactMetadataV0 {
                        artifact_type: ArtifactType::Syslog,
                        component_moniker: Some("component".to_string())
                    },
                },
                artifact_dir: Path::new("a").to_path_buf(),
                outcome: Outcome::Skipped,
                suites: vec![
                    SuiteEntryV0 { summary: "suite-summary-1.json".to_string() },
                    SuiteEntryV0 { summary: "suite-summary-2.json".to_string() },
                ],
                duration_milliseconds: Some(65),
                start_time: Some(01),
            },
        ];
        for outcome in Outcome::all_variants() {
            cases.push(TestRunResult::V0 {
                artifacts: hashmap! {},
                artifact_dir: Path::new("a").to_path_buf(),
                outcome,
                suites: vec![],
                duration_milliseconds: None,
                start_time: None,
            });
        }
        for artifact_type in ArtifactType::all_variants() {
            cases.push(TestRunResult::V0 {
                artifacts: hashmap! {
                    Path::new("a").to_path_buf() => artifact_type.into(),
                },
                artifact_dir: Path::new("a").to_path_buf(),
                outcome: Outcome::Skipped,
                suites: vec![],
                duration_milliseconds: None,
                start_time: None,
            });
        }

        for case in cases.iter() {
            let serialized = serde_json::to_value(case).expect("serialize test run");
            let validate_result = compiled_schema.validate(&serialized);
            if !validate_result.is_strictly_valid() {
                panic!(
                    "Run did not conform with schema: {:#?}, {:#?}, {:#?}",
                    case, serialized, validate_result
                );
            }
        }
    }

    #[test]
    fn suite_conforms_to_schema() {
        let suite_schema = serde_json::from_str(testing::SUITE_SCHEMA).expect("parse json schema");
        let mut scope = json_schema::Scope::new();
        let compiled_schema =
            scope.compile_and_return(suite_schema, false).expect("compile json schema");

        let mut cases = vec![
            SuiteResult::V0 {
                artifacts: hashmap! {},
                artifact_dir: Path::new("a").to_path_buf(),
                outcome: Outcome::Passed,
                name: "my test suite".to_string(),
                cases: vec![],
                duration_milliseconds: None,
                start_time: None,
            },
            SuiteResult::V0 {
                artifacts: hashmap! {},
                artifact_dir: Path::new("b").to_path_buf(),
                outcome: Outcome::Failed,
                name: "another suite".to_string(),
                cases: vec![TestCaseResultV0 {
                    artifacts: hashmap! {},
                    artifact_dir: Path::new("b-case").to_path_buf(),
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
                    Path::new("a.txt").to_path_buf() => ArtifactType::Stderr.into(),
                },
                artifact_dir: Path::new("c").to_path_buf(),
                outcome: Outcome::Failed,
                name: "another suite".to_string(),
                cases: vec![
                    TestCaseResultV0 {
                        artifacts: hashmap! {
                            Path::new("b.txt").to_path_buf() => ArtifactType::Stdout.into(),
                            Path::new("c.txt").to_path_buf() => ArtifactMetadataV0 {
                                artifact_type: ArtifactType::Syslog,
                                component_moniker: Some("component".to_string())
                            },
                        },
                        artifact_dir: Path::new("case-0").to_path_buf(),
                        outcome: Outcome::Timedout,
                        name: "test case".to_string(),
                        duration_milliseconds: None,
                        start_time: Some(37),
                    },
                    TestCaseResultV0 {
                        artifacts: hashmap![
                            Path::new("d.txt").to_path_buf() => ArtifactType::Stdout.into(),
                            Path::new("e.txt").to_path_buf() => ArtifactType::Stdout.into(),
                        ],
                        artifact_dir: Path::new("case-1").to_path_buf(),
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
        for outcome in Outcome::all_variants() {
            cases.push(SuiteResult::V0 {
                artifacts: hashmap! {},
                artifact_dir: Path::new("a").to_path_buf(),
                name: "a suite".to_string(),
                outcome,
                cases: vec![TestCaseResultV0 {
                    artifacts: hashmap! {},
                    artifact_dir: Path::new("case-0").to_path_buf(),
                    outcome,
                    name: "test case".to_string(),
                    duration_milliseconds: None,
                    start_time: Some(37),
                }],
                duration_milliseconds: None,
                start_time: None,
            });
        }
        for artifact_type in ArtifactType::all_variants() {
            cases.push(SuiteResult::V0 {
                artifacts: hashmap! {
                    Path::new("a").to_path_buf() => artifact_type.into(),
                },
                artifact_dir: Path::new("a").to_path_buf(),
                name: "a suite".to_string(),
                outcome: Outcome::Error,
                cases: vec![TestCaseResultV0 {
                    artifacts: hashmap! {
                        Path::new("a").to_path_buf() => artifact_type.into(),
                    },
                    artifact_dir: Path::new("case-0").to_path_buf(),
                    outcome: Outcome::Error,
                    name: "test case".to_string(),
                    duration_milliseconds: None,
                    start_time: Some(37),
                }],
                duration_milliseconds: None,
                start_time: None,
            });
        }

        for case in cases.iter() {
            let serialized = serde_json::to_value(case).expect("serialize test run");
            let validate_result = compiled_schema.validate(&serialized);
            if !validate_result.is_strictly_valid() {
                panic!(
                    "Run did not conform with schema: {:#?}, {:#?}, {:#?}",
                    case, serialized, validate_result
                );
            }
        }
    }
}
