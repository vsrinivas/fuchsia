// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        ArtifactMetadata, ArtifactSubDirectory, CommonResult, MaybeUnknown, Outcome, SchemaVersion,
        SuiteResult, TestCaseResult, TestRunResult, RUN_NAME, RUN_SUMMARY_NAME,
    },
    serde::{Deserialize, Serialize},
    std::{
        borrow::Cow,
        collections::HashMap,
        fs::File,
        io::{BufReader, BufWriter, Error, Write},
        ops::Deref,
        path::{Path, PathBuf},
    },
    test_list::TestTag,
};

/// This enum exists purely to ensure a "version: 0" field is serialized.
#[derive(Serialize, Deserialize, PartialEq, Eq, Debug)]
enum Version {
    #[serde(rename = "0")]
    V0,
}

#[derive(Serialize, Deserialize, PartialEq, Eq, Debug, Clone)]
struct SuiteEntry {
    /// Location of the summary file for this suite.
    summary: String,
}

#[derive(Serialize, Deserialize, PartialEq, Eq, Debug)]
struct SerializableTestRun<'a> {
    artifacts: Cow<'a, HashMap<PathBuf, ArtifactMetadata>>,
    artifact_dir: Cow<'a, Path>,
    outcome: MaybeUnknown<Outcome>,
    suites: Vec<SuiteEntry>,
    #[serde(skip_serializing_if = "Option::is_none")]
    start_time: Option<u64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    duration_milliseconds: Option<u64>,
    version: Version,
}

#[derive(Serialize, Deserialize, PartialEq, Eq, Debug)]
struct SerializableSuite<'a> {
    artifacts: Cow<'a, HashMap<PathBuf, ArtifactMetadata>>,
    artifact_dir: Cow<'a, Path>,
    outcome: MaybeUnknown<Outcome>,
    name: Cow<'a, String>,
    cases: Vec<SerializableTestCase<'a>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    start_time: Option<u64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    duration_milliseconds: Option<u64>,
    #[serde(skip_serializing_if = "Vec::is_empty", default)]
    tags: Cow<'a, Vec<TestTag>>,
    version: Version,
}

#[derive(Serialize, Deserialize, PartialEq, Eq, Debug)]
struct SerializableTestCase<'a> {
    artifacts: Cow<'a, HashMap<PathBuf, ArtifactMetadata>>,
    artifact_dir: Cow<'a, Path>,
    outcome: MaybeUnknown<Outcome>,
    name: Cow<'a, String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    start_time: Option<u64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    duration_milliseconds: Option<u64>,
}

fn make_serializable_suite<'a, 'b>(suite: &'a SuiteResult<'b>) -> SerializableSuite<'a> {
    let cases = suite
        .cases
        .iter()
        .map(|test_case| SerializableTestCase {
            artifacts: Cow::Borrowed(&test_case.common.artifact_dir.artifacts),
            artifact_dir: Cow::Borrowed(
                &test_case.common.artifact_dir.root.file_name().unwrap().as_ref(),
            ),
            outcome: test_case.common.outcome.clone(),
            name: Cow::Borrowed(&test_case.common.name),
            start_time: test_case.common.start_time,
            duration_milliseconds: test_case.common.duration_milliseconds,
        })
        .collect();

    let suite_common = suite.common.deref();
    SerializableSuite {
        artifacts: Cow::Borrowed(&suite_common.artifact_dir.artifacts),
        artifact_dir: Cow::Borrowed(suite_common.artifact_dir.root.file_name().unwrap().as_ref()),
        outcome: suite_common.outcome.clone(),
        name: Cow::Borrowed(&suite_common.name),
        cases,
        start_time: suite_common.start_time,
        duration_milliseconds: suite_common.duration_milliseconds,
        version: Version::V0,
        tags: Cow::Borrowed(suite.tags.deref()),
    }
}

/// Saves a summary of test results in the experimental format.
pub(crate) fn save_summary<'a, 'b>(
    root: &'a Path,
    result: &TestRunResult<'b>,
) -> Result<(), Error> {
    let mut suite_entries = vec![];
    for suite in result.suites.iter() {
        let serializable = make_serializable_suite(suite.deref());
        let filename = suite.deref().summary_file_hint.to_owned();
        let mut file =
            BufWriter::new(File::create(root.join(suite.deref().summary_file_hint.deref()))?);
        serde_json::to_writer_pretty(&mut file, &serializable)?;
        file.flush()?;
        suite_entries.push(SuiteEntry { summary: filename.to_string() });
    }

    // next, save run summary
    let serializable_run = SerializableTestRun {
        artifacts: Cow::Borrowed(&result.common.artifact_dir.artifacts),
        artifact_dir: Cow::Borrowed(result.common.artifact_dir.root.file_name().unwrap().as_ref()),
        outcome: result.common.outcome.clone(),
        suites: suite_entries,
        start_time: result.common.start_time,
        duration_milliseconds: result.common.duration_milliseconds,
        version: Version::V0,
    };

    let mut file = BufWriter::new(File::create(root.join(RUN_SUMMARY_NAME))?);
    serde_json::to_writer_pretty(&mut file, &serializable_run)?;
    file.flush()
}

/// Retrieve a test result summary from the given directory.
pub(crate) fn parse_from_directory(root: &Path) -> Result<TestRunResult<'static>, Error> {
    let summary_file = BufReader::new(File::open(root.join(RUN_SUMMARY_NAME))?);
    let serializable_run: SerializableTestRun<'static> = serde_json::from_reader(summary_file)?;

    let mut suites = vec![];
    for suite_entry in serializable_run.suites {
        let suite_summary = BufReader::new(File::open(root.join(&suite_entry.summary))?);
        let serializable_suite: SerializableSuite<'static> =
            serde_json::from_reader(suite_summary)?;
        let suite = SuiteResult {
            common: Cow::Owned(CommonResult {
                name: serializable_suite.name.into_owned(),
                artifact_dir: ArtifactSubDirectory {
                    version: SchemaVersion::UnstablePrototype,
                    root: root.join(serializable_suite.artifact_dir),
                    artifacts: serializable_suite.artifacts.into_owned(),
                },
                outcome: serializable_suite.outcome,
                start_time: serializable_suite.start_time,
                duration_milliseconds: serializable_suite.duration_milliseconds,
            }),
            tags: serializable_suite.tags,
            summary_file_hint: Cow::Owned(suite_entry.summary),
            cases: serializable_suite
                .cases
                .into_iter()
                .map(|serialiable| from_serializable_test_case(root, serialiable))
                .collect(),
        };

        suites.push(suite);
    }

    Ok(TestRunResult {
        common: Cow::Owned(CommonResult {
            name: RUN_NAME.to_string(),
            artifact_dir: ArtifactSubDirectory {
                version: SchemaVersion::UnstablePrototype,
                root: root.join(serializable_run.artifact_dir),
                artifacts: serializable_run.artifacts.into_owned(),
            },
            outcome: serializable_run.outcome,
            start_time: serializable_run.start_time,
            duration_milliseconds: serializable_run.duration_milliseconds,
        }),
        suites,
    })
}

fn from_serializable_test_case(
    root: &Path,
    serializable: SerializableTestCase<'static>,
) -> TestCaseResult<'static> {
    TestCaseResult {
        common: Cow::Owned(CommonResult {
            name: serializable.name.into_owned(),
            artifact_dir: ArtifactSubDirectory {
                version: SchemaVersion::UnstablePrototype,
                root: root.join(serializable.artifact_dir),
                artifacts: serializable.artifacts.into_owned(),
            },
            outcome: serializable.outcome,
            start_time: serializable.start_time,
            duration_milliseconds: serializable.duration_milliseconds,
        }),
    }
}

#[cfg(test)]
pub fn validate_against_schema(root: &Path) {
    const RUN_SCHEMA: &str = include_str!("../schema/run_summary.schema.json");
    const SUITE_SCHEMA: &str = include_str!("../schema/suite_summary.schema.json");
    let mut suite_scope = valico::json_schema::Scope::new();
    let suite_schema_json = serde_json::from_str(SUITE_SCHEMA).expect("parse json schema");
    let suite_schema =
        suite_scope.compile_and_return(suite_schema_json, false).expect("compile json schema");
    let mut run_scope = valico::json_schema::Scope::new();
    let run_schema_json = serde_json::from_str(RUN_SCHEMA).expect("parse json schema");
    let run_schema =
        run_scope.compile_and_return(run_schema_json, false).expect("compile json schema");

    let summary_file =
        BufReader::new(File::open(root.join(RUN_SUMMARY_NAME)).expect("open summary file"));
    let run_result_value: serde_json::Value =
        serde_json::from_reader(summary_file).expect("deserialize run from file");
    if !run_schema.validate(&run_result_value).is_strictly_valid() {
        panic!("Run file does not conform with schema");
    }
    let run_result: SerializableTestRun<'static> =
        serde_json::from_value(run_result_value).expect("convert to internal");
    for suite_entry in run_result.suites {
        let suite_summary_file =
            BufReader::new(File::open(root.join(suite_entry.summary)).expect("open summary file"));
        let suite_result_value: serde_json::Value =
            serde_json::from_reader(suite_summary_file).expect("deserialize suite from file");
        if !suite_schema.validate(&suite_result_value).is_strictly_valid() {
            panic!("Run file does not conform with schema");
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use serde_json::{from_str, json, to_string, Value};

    #[test]
    fn run_version_serialized() {
        // This verifies version is serialized.

        let run_result = SerializableTestRun {
            artifacts: Cow::Owned(HashMap::new()),
            artifact_dir: Cow::Owned("a".into()),
            outcome: Outcome::Inconclusive.into(),
            suites: vec![],
            start_time: None,
            duration_milliseconds: None,
            version: Version::V0,
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

        assert!(from_str::<SerializableTestRun<'static>>(&serialized).unwrap_err().is_data());
    }

    #[test]
    fn suite_version_serialized() {
        let suite_result = SerializableSuite {
            artifacts: Cow::Owned(HashMap::new()),
            artifact_dir: Cow::Owned("a".into()),
            outcome: Outcome::Inconclusive.into(),
            name: Cow::Owned("suite".into()),
            cases: vec![],
            start_time: None,
            duration_milliseconds: None,
            tags: Cow::Owned(vec![]),
            version: Version::V0,
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

        assert!(from_str::<SerializableSuite<'static>>(&serialized).unwrap_err().is_data());
    }
}
