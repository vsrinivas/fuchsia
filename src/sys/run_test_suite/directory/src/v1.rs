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
        path::{Path, PathBuf},
    },
    test_list::TestTag,
};

#[derive(Serialize, Deserialize, PartialEq, Eq, Debug)]
struct SerializableCommon<'a> {
    #[serde(skip_serializing_if = "Option::is_none")]
    name: Option<Cow<'a, str>>,
    artifacts: Cow<'a, HashMap<PathBuf, ArtifactMetadata>>,
    artifact_dir: Cow<'a, Path>,
    outcome: MaybeUnknown<Outcome>,
    #[serde(skip_serializing_if = "Option::is_none")]
    start_time: Option<u64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    duration_milliseconds: Option<u64>,
}

#[derive(Serialize, Deserialize, PartialEq, Eq, Debug)]
struct SerializableTestRun<'a> {
    #[serde(flatten)]
    common: SerializableCommon<'a>,
    suites: Vec<SerializableSuite<'a>>,
}

#[derive(Serialize, Deserialize, PartialEq, Eq, Debug)]
struct SerializableSuite<'a> {
    #[serde(flatten)]
    common: SerializableCommon<'a>,
    cases: Vec<SerializableTestCase<'a>>,
    tags: Cow<'a, Vec<TestTag>>,
}

#[derive(Serialize, Deserialize, PartialEq, Eq, Debug)]
struct SerializableTestCase<'a> {
    #[serde(flatten)]
    common: SerializableCommon<'a>,
}

#[derive(Serialize, Deserialize, PartialEq, Eq, Debug)]
enum SchemaId {
    #[serde(rename = "https://fuchsia.dev/schema/ffx_test/run_summary-8d1dd964.json")]
    V1,
}

#[derive(Serialize, Deserialize, PartialEq, Eq, Debug)]
struct VersionedEnvelope<'a> {
    data: SerializableTestRun<'a>,
    schema_id: SchemaId,
}

enum NameOption {
    Omit,
    Include,
}

fn make_serializable_common<'a>(
    common: &'a CommonResult,
    omit_name: NameOption,
) -> SerializableCommon<'a> {
    SerializableCommon {
        name: match omit_name {
            NameOption::Omit => None,
            NameOption::Include => Some(Cow::Borrowed(&common.name)),
        },
        artifacts: Cow::Borrowed(&common.artifact_dir.artifacts),
        artifact_dir: Cow::Borrowed(&common.artifact_dir.root.file_name().unwrap().as_ref()),
        outcome: common.outcome.clone(),
        start_time: common.start_time,
        duration_milliseconds: common.duration_milliseconds,
    }
}

fn make_serializable_suite<'a, 'b>(suite: &'a SuiteResult<'b>) -> SerializableSuite<'a> {
    SerializableSuite {
        common: make_serializable_common(&*suite.common, NameOption::Include),
        cases: suite
            .cases
            .iter()
            .map(|case| SerializableTestCase {
                common: make_serializable_common(&*case.common, NameOption::Include),
            })
            .collect(),
        tags: Cow::Borrowed(&suite.tags),
    }
}

/// Saves a summary of test results in the experimental format.
pub(crate) fn save_summary<'a, 'b>(
    root_path: &'a Path,
    result: &TestRunResult<'b>,
) -> Result<(), Error> {
    let serializable_run = SerializableTestRun {
        common: make_serializable_common(&*result.common, NameOption::Omit),
        suites: result.suites.iter().map(make_serializable_suite).collect(),
    };

    let enveloped = VersionedEnvelope { data: serializable_run, schema_id: SchemaId::V1 };

    let mut file = BufWriter::new(File::create(root_path.join(RUN_SUMMARY_NAME))?);
    serde_json::to_writer_pretty(&mut file, &enveloped)?;
    file.flush()
}

fn from_serializable_common(
    root_path: &Path,
    serializable: SerializableCommon<'static>,
) -> CommonResult {
    CommonResult {
        name: serializable.name.unwrap_or_else(|| Cow::Borrowed(RUN_NAME)).into_owned(),
        artifact_dir: ArtifactSubDirectory {
            version: SchemaVersion::V1,
            root: root_path.join(serializable.artifact_dir),
            artifacts: serializable.artifacts.into_owned(),
        },
        outcome: serializable.outcome,
        start_time: serializable.start_time,
        duration_milliseconds: serializable.duration_milliseconds,
    }
}

fn from_serializable_suite(
    root_path: &Path,
    serializable: SerializableSuite<'static>,
) -> SuiteResult<'static> {
    SuiteResult {
        common: Cow::Owned(from_serializable_common(root_path, serializable.common)),
        cases: serializable
            .cases
            .into_iter()
            .map(|case| TestCaseResult {
                common: Cow::Owned(from_serializable_common(root_path, case.common)),
            })
            .collect(),
        tags: serializable.tags,
    }
}

/// Retrieve a test result summary from the given directory.
pub(crate) fn parse_from_directory(root_path: &Path) -> Result<TestRunResult<'static>, Error> {
    let summary_file = BufReader::new(File::open(root_path.join(RUN_SUMMARY_NAME))?);
    let envelope: VersionedEnvelope<'static> = serde_json::from_reader(summary_file)?;

    Ok(TestRunResult {
        common: Cow::Owned(from_serializable_common(root_path, envelope.data.common)),
        suites: envelope
            .data
            .suites
            .into_iter()
            .map(|suite| from_serializable_suite(root_path, suite))
            .collect(),
    })
}

#[cfg(test)]
pub fn validate_against_schema(root_path: &Path) {
    const RUN_SCHEMA: &str =
        include_str!("../../../../../sdk/schema/ffx_test/run_summary-8d1dd964.json");
    const COMMON_SCHEMA: &str = include_str!("../../../../../sdk/schema/common-00000000.json");
    let mut run_scope = valico::json_schema::Scope::new();
    let common_schema_json = serde_json::from_str(COMMON_SCHEMA).expect("parse common schema");
    let _ = run_scope.compile(common_schema_json, false).expect("compile common schema");
    let run_schema_json = serde_json::from_str(RUN_SCHEMA).expect("parse json schema");
    let run_schema =
        run_scope.compile_and_return(run_schema_json, false).expect("compile json schema");

    let summary_file =
        BufReader::new(File::open(root_path.join(RUN_SUMMARY_NAME)).expect("open summary file"));
    let run_result_value: serde_json::Value =
        serde_json::from_reader(summary_file).expect("deserialize run from file");
    let validation = run_schema.validate(&run_result_value);
    if !validation.is_strictly_valid() {
        panic!("Run file does not conform with schema: {:#?}", validation);
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use serde_json::{from_str, json, to_string, Value};

    #[test]
    fn run_version_serialized() {
        // This verifies version is serialized.

        let envelope = VersionedEnvelope {
            data: SerializableTestRun {
                common: SerializableCommon {
                    name: None,
                    artifacts: Cow::Owned(HashMap::new()),
                    artifact_dir: Cow::Owned(Path::new("artifacts").to_path_buf()),
                    outcome: MaybeUnknown::Known(Outcome::Passed),
                    start_time: None,
                    duration_milliseconds: None,
                },
                suites: vec![],
            },
            schema_id: SchemaId::V1,
        };

        let serialized = to_string(&envelope).expect("serialize result");
        let value = from_str::<Value>(&serialized).expect("deserialize result");

        let expected = json!({
            "schema_id": "https://fuchsia.dev/schema/ffx_test/run_summary-8d1dd964.json",
            "data": {
                "artifacts": {},
                "artifact_dir": "artifacts",
                "outcome": "PASSED",
                "suites": []
            }
        });

        assert_eq!(value, expected);
    }

    #[test]
    fn run_version_mismatch() {
        let wrong_version_json = json!({
            "schema_id": "https://fuchsia.dev/schema/fake-schema",
            "data": {
                "artifacts": {},
                "artifact_dir": "artifacts",
                "outcome": "PASSED",
                "suites": []
            }
        });

        let serialized = to_string(&wrong_version_json).expect("serialize result");

        assert!(from_str::<SerializableTestRun<'static>>(&serialized).unwrap_err().is_data());
    }
}
