// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    ArtifactMetadataV0, Outcome, SuiteEntryV0, SuiteResult, TestCaseResultV0, TestRunResult,
    RUN_SUMMARY_NAME,
};
use std::collections::{HashMap, HashSet};
use std::fs::File;
use std::io::BufReader;
use std::path::{Path, PathBuf};
use valico::json_schema;

pub(crate) const RUN_SCHEMA: &str = include_str!("../schema/run_summary.schema.json");
pub(crate) const SUITE_SCHEMA: &str = include_str!("../schema/suite_summary.schema.json");

/// Parse the json files in a directory. Returns the parsed test run document and a list of
/// parsed suite results. This loads all the results in memory at once and shouldn't be used
/// outside of testing.
pub fn parse_json_in_output(path: &Path) -> (TestRunResult, Vec<SuiteResult>) {
    let mut suite_scope = json_schema::Scope::new();
    let suite_schema_json = serde_json::from_str(SUITE_SCHEMA).expect("parse json schema");
    let suite_schema =
        suite_scope.compile_and_return(suite_schema_json, false).expect("compile json schema");
    let mut run_scope = json_schema::Scope::new();
    let run_schema_json = serde_json::from_str(RUN_SCHEMA).expect("parse json schema");
    let run_schema =
        run_scope.compile_and_return(run_schema_json, false).expect("compile json schema");

    let summary_file =
        BufReader::new(File::open(path.join(RUN_SUMMARY_NAME)).expect("open summary file"));
    let run_result_value: serde_json::Value =
        serde_json::from_reader(summary_file).expect("deserialize run from file");
    if !run_schema.validate(&run_result_value).is_strictly_valid() {
        panic!("Run file does not conform with schema");
    }
    let run_result: TestRunResult =
        serde_json::from_value(run_result_value).expect("deserialize run from value");
    let suite_entries = match &run_result {
        TestRunResult::V0 { suites, .. } => suites,
    };
    let suite_results = suite_entries
        .iter()
        .map(|SuiteEntryV0 { summary }| {
            let suite_summary_file =
                BufReader::new(File::open(path.join(summary)).expect("open suite summary file"));
            let suite_result_value: serde_json::Value =
                serde_json::from_reader(suite_summary_file).expect("parse suite summary file");
            if !suite_schema.validate(&suite_result_value).is_strictly_valid() {
                panic!("Suite file {} does not conform with schema", summary);
            }
            serde_json::from_value::<SuiteResult>(suite_result_value)
                .expect("deserialize suite from value")
        })
        .collect::<Vec<_>>();
    (run_result, suite_results)
}

enum MatchOption<T> {
    AnyOrNone,
    None,
    Any,
    Specified(T),
}

macro_rules! assert_match_option {
    ($expected:expr, $actual:expr, $field:expr) => {
        match $expected {
            MatchOption::AnyOrNone => (),
            MatchOption::None => {
                assert_eq!(None, $actual, "Expected {} to be None but was {:?}", $field, $actual)
            }
            MatchOption::Any => {
                assert!($actual.is_some(), "Expected {} to contain a value but was None", $field)
            }
            MatchOption::Specified(val) => assert_eq!(
                Some(val),
                $actual,
                "Expected {} to be {:?} but was {:?}",
                $field,
                Some(val),
                $actual
            ),
        }
    };
}

/// Container that identifies the entity that is being verified in an assertion.
#[derive(Clone, Copy)]
enum EntityContext<'a> {
    Run,
    Suite(&'a ExpectedSuite),
    Case(&'a ExpectedSuite, &'a ExpectedTestCase),
}

/// Container that identifies the artifact that is being verified in an assertion.
#[derive(Clone, Copy)]
struct ArtifactContext<'a, 'b> {
    entity: &'a EntityContext<'b>,
    metadata: &'a ArtifactMetadataV0,
}

impl std::fmt::Display for EntityContext<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Run => write!(f, "TEST RUN"),
            Self::Suite(suite) => write!(f, "SUITE {}", suite.name),
            Self::Case(suite, case) => write!(f, "SUITE {}: CASE {}", suite.name, case.name),
        }
    }
}

impl std::fmt::Display for ArtifactContext<'_, '_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "Entity: {}, Metadata: {:?}", self.entity, self.metadata)
    }
}

/// A mapping from artifact metadata to assertions made on the artifact.
type ArtifactMetadataToAssertionMap = HashMap<ArtifactMetadataV0, ExpectedArtifact>;

/// Assert that the run results contained in `actual_run` and the directory specified by `root`
/// contain the results and artifacts in `expected_run`.
pub fn assert_run_result(root: &Path, actual_run: &TestRunResult, expected_run: &ExpectedTestRun) {
    let context = EntityContext::Run;
    let &TestRunResult::V0 {
        artifacts,
        artifact_dir,
        outcome,
        suites: _,
        start_time,
        duration_milliseconds,
    } = &actual_run;
    assert_match_option!(
        expected_run.duration_milliseconds,
        *duration_milliseconds,
        format!("Run duration for {}", context)
    );
    assert_match_option!(
        expected_run.start_time,
        *start_time,
        format!("Start time for {}", context)
    );
    assert_eq!(outcome, &expected_run.outcome, "Outcome for {}", context);
    assert_artifacts(root, &artifact_dir, &artifacts, &expected_run.artifacts, EntityContext::Run);
}

/// Assert that the suite results contained in `actual_suites` and the directory specified by `root`
/// contain the suites, results, artifacts, and test cases in `expected_suite`.
/// Note that this currently does not support duplicate suite names.
pub fn assert_suite_results(
    root: &Path,
    actual_suites: &Vec<SuiteResult>,
    expected_suites: &Vec<ExpectedSuite>,
) {
    assert_eq!(actual_suites.len(), expected_suites.len());
    let mut expected_suites_map = HashMap::new();
    for suite in expected_suites.iter() {
        expected_suites_map.insert(suite.name.clone(), suite);
    }
    assert_eq!(
        actual_suites.len(),
        expected_suites_map.len(),
        "Run contains multiple suites with the same name. \
        This is currently unsupported by assert_suite_results"
    );
    for suite in actual_suites.iter() {
        let suite_name = match suite {
            SuiteResult::V0 { name, .. } => name,
        };
        assert_suite_result(
            root,
            suite,
            expected_suites_map.get(suite_name).expect("No matching expected suite"),
        );
    }
}

/// Assert that the suite results contained in `actual_suite` and the directory specified by `root`
/// contain the results, artifacts, and test cases in `expected_suite`.
pub fn assert_suite_result(
    root: &Path,
    actual_suite: &SuiteResult,
    expected_suite: &ExpectedSuite,
) {
    let context = EntityContext::Suite(expected_suite);
    let &SuiteResult::V0 {
        artifacts,
        artifact_dir,
        outcome,
        name,
        cases,
        duration_milliseconds,
        start_time,
    } = &actual_suite;
    assert_eq!(outcome, &expected_suite.outcome, "Outcome for {}", context);
    assert_eq!(name, &expected_suite.name, "Name for {}", context);
    assert_match_option!(
        expected_suite.duration_milliseconds,
        *duration_milliseconds,
        format!("Duration for {}", context)
    );
    assert_match_option!(
        expected_suite.start_time,
        *start_time,
        format!("Start time for {}", context)
    );

    assert_artifacts(root, &artifact_dir, &artifacts, &expected_suite.artifacts, context);

    assert_eq!(cases.len(), expected_suite.cases.len());
    for case in cases.iter() {
        let expected_case = expected_suite.cases.get(&case.name);
        assert!(expected_case.is_some(), "Found unexpected case {} in {}", case.name, context);
        assert_case_result(root, case, expected_case.unwrap(), expected_suite);
    }
}

fn assert_case_result(
    root: &Path,
    actual_case: &TestCaseResultV0,
    expected_case: &ExpectedTestCase,
    parent_suite: &ExpectedSuite,
) {
    let context = EntityContext::Case(parent_suite, expected_case);
    assert_eq!(actual_case.name, expected_case.name, "Name for {}", context);
    assert_eq!(actual_case.outcome, expected_case.outcome, "Outcome for {}", context);
    assert_match_option!(
        expected_case.duration_milliseconds,
        actual_case.duration_milliseconds,
        format!("Duration for {}", context)
    );
    assert_match_option!(
        expected_case.start_time,
        actual_case.start_time,
        format!("Start time for {}", context)
    );
    assert_artifacts(
        root,
        &actual_case.artifact_dir,
        &actual_case.artifacts,
        &expected_case.artifacts,
        context,
    );
}

fn assert_artifacts(
    root: &Path,
    artifact_dir: &Path,
    actual_artifacts: &HashMap<PathBuf, ArtifactMetadataV0>,
    expected_artifacts: &ArtifactMetadataToAssertionMap,
    entity_context: EntityContext<'_>,
) {
    // TODO(satsukiu): add options so that the test author can explicitly declare whether
    // artifacts should be an exact match, should contain (and may contain more) artifacts,
    // or any number of artifacts is accesptable.
    // This skips artifact assertion for the typical case where verifying artifacts isn't
    // necessary and allows the author to avoid listing out every artifact that is generated
    // by the test.
    if expected_artifacts.is_empty() {
        return;
    }

    let actual_artifacts_by_metadata: HashMap<ArtifactMetadataV0, PathBuf> =
        actual_artifacts.iter().map(|(key, value)| (value.clone(), key.clone())).collect();
    // For now, artifact metadata should be unique for each artifact.
    assert_eq!(
        actual_artifacts_by_metadata.len(),
        actual_artifacts.len(),
        "Artifacts for {} do not have unique metadata. Actual artifacts: {:?}",
        entity_context,
        actual_artifacts
    );

    let expected_metadata: HashSet<_> = expected_artifacts.keys().collect();
    let actual_metadata: HashSet<_> = actual_artifacts_by_metadata.keys().collect();

    assert_eq!(
        expected_metadata, actual_metadata,
        "Artifacts for {} do not have matching metadata.",
        entity_context,
    );

    for (expected_metadata, expected_artifact) in expected_artifacts.iter() {
        let actual_filepath =
            artifact_dir.join(actual_artifacts_by_metadata.get(expected_metadata).unwrap());
        match expected_artifact {
            ExpectedArtifact::File { name, assertion_fn } => {
                assert_file(
                    &root.join(&actual_filepath),
                    name,
                    assertion_fn,
                    ArtifactContext { entity: &entity_context, metadata: expected_metadata },
                );
            }
            ExpectedArtifact::Directory { files, name } => {
                match name {
                    None => (),
                    Some(name) => assert_eq!(
                        name.as_str(),
                        actual_filepath.file_name().unwrap().to_str().unwrap(),
                        "Expected filename {} for artifact matching {:?} but got {}",
                        name,
                        expected_metadata,
                        actual_filepath.file_name().unwrap().to_str().unwrap()
                    ),
                }
                let actual_entries: HashSet<_> = std::fs::read_dir(root.join(&actual_filepath))
                    .expect("Failed to read directory artifact path")
                    .map(|entry| match entry {
                        Ok(dir_entry) if dir_entry.file_type().unwrap().is_file() => {
                            dir_entry.file_name().to_str().unwrap().to_string()
                        }
                        // TODO(fxbugdev/85528) - support directory artifacts with subdirectories
                        Ok(_) => panic!("Directory artifact with subdirectories unsupported"),
                        Err(e) => panic!("Error reading directory artifact: {:?}", e),
                    })
                    .collect();
                let expected_entries: HashSet<_> =
                    files.iter().map(|(name, _)| name.to_string()).collect();
                assert_eq!(
                    actual_entries, expected_entries,
                    "Expected files {:?} in directory artifact, got {:?}",
                    &expected_entries, &actual_entries
                );
                for (name, assertion) in files {
                    assert_file(
                        &root.join(&actual_filepath).join(name),
                        &None,
                        assertion,
                        ArtifactContext { entity: &entity_context, metadata: expected_metadata },
                    );
                }
            }
        }
    }
}

fn assert_file(
    file_path: &Path,
    name: &Option<String>,
    assertion_fn: &Box<dyn Fn(&str)>,
    artifact_context: ArtifactContext<'_, '_>,
) {
    match name {
        None => (),
        Some(name) => assert_eq!(
            name.as_str(),
            file_path.file_name().unwrap().to_str().unwrap(),
            "Got incorrect filename while checking file for artifact {}",
            artifact_context
        ),
    }
    let actual_contents = std::fs::read_to_string(&file_path);
    (assertion_fn)(&actual_contents.unwrap());
}

/// The expected contents of an artifact.
enum ExpectedArtifact {
    /// An artifact contained in a single file, such as stdout.
    File {
        /// If given, the expected name of the file.
        name: Option<String>,
        /// Assertion run against the contents of the file.
        assertion_fn: Box<dyn Fn(&str)>,
    },
    /// An artifact consisting of files in a directory.
    Directory {
        /// List of expected files, as (name, assertion) pairs. The name
        /// is the expected name of the file, and the assertion fn is run
        /// against the contents of the file.
        files: Vec<(String, Box<dyn Fn(&str)>)>,
        /// If given, the expected name of the directory.
        name: Option<String>,
    },
}

/// Contents of an expected directory artifact.
pub struct ExpectedDirectory {
    files: Vec<(String, Box<dyn Fn(&str)>)>,
}

impl ExpectedDirectory {
    /// Create a new empty expected directory.
    pub fn new() -> Self {
        Self { files: vec![] }
    }

    /// Add a file with expected |contents|.
    pub fn with_file(self, name: impl AsRef<str>, contents: impl AsRef<str>) -> Self {
        let owned_expected = contents.as_ref().to_string();
        let owned_name = name.as_ref().to_string();
        self.with_matching_file(name, move |actual| {
            assert_eq!(
                &owned_expected, actual,
                "Mismatch in contents of file {}. Expected: '{}', actual:'{}'",
                owned_name, &owned_expected, actual
            )
        })
    }

    pub fn with_matching_file(
        mut self,
        name: impl AsRef<str>,
        matcher: impl 'static + Fn(&str),
    ) -> Self {
        self.files.push((name.as_ref().to_string(), Box::new(matcher)));
        self
    }
}

/// A version of a test run result that contains all output in memory. This should only be used
/// for making assertions in a test.
pub struct ExpectedTestRun {
    artifacts: ArtifactMetadataToAssertionMap,
    outcome: Outcome,
    start_time: MatchOption<u64>,
    duration_milliseconds: MatchOption<u64>,
}

/// A version of a suite run result that contains all output in memory. This should only be used
/// for making assertions in a test.
pub struct ExpectedSuite {
    artifacts: ArtifactMetadataToAssertionMap,
    name: String,
    outcome: Outcome,
    cases: HashMap<String, ExpectedTestCase>,
    start_time: MatchOption<u64>,
    duration_milliseconds: MatchOption<u64>,
}

/// A version of a test case result that contains all output in memory. This should only be used
/// for making assertions in a test.
pub struct ExpectedTestCase {
    artifacts: ArtifactMetadataToAssertionMap,
    name: String,
    outcome: Outcome,
    start_time: MatchOption<u64>,
    duration_milliseconds: MatchOption<u64>,
}

macro_rules! common_impl {
    {} => {
        /// Add an artifact matching the exact contents. Artifacts are checked by finding
        /// an entry matching the given metadata, then checking the contents of the corresponding
        /// file. If |name| is provided, the name of the file is verified. Artifacts are keyed by
        /// metadata rather than by name as the names of files are not guaranteed to be stable.
        pub fn with_artifact<S, T, U>(
            self, metadata: U, name: Option<S>, contents: T
        ) -> Self
        where
            S: AsRef<str>,
            T: AsRef<str>,
            U: Into<ArtifactMetadataV0>
        {
            let owned_expected = contents.as_ref().to_string();
            let metadata = metadata.into();
            let metadata_clone = metadata.clone();
            self.with_matching_artifact(metadata, name, move |actual| {
                assert_eq!(
                    &owned_expected, actual,
                    "Mismatch in artifact with metadata {:?}. Expected: '{}', actual:'{}'",
                    metadata_clone, &owned_expected, actual
                )
            })
        }

        /// Add an artifact matching the exact contents. Artifacts are checked by finding
        /// an entry matching the given metadata, then running |matcher| against the contents of
        /// the file. If |name| is provided, the name of the file is verified. Artifacts are keyed
        /// by metadata rather than by name as the names of files are not guaranteed to be stable.
        pub fn with_matching_artifact<S, F, U>(
            mut self,
            metadata: U,
            name: Option<S>,
            matcher: F,
        ) -> Self
        where
            S: AsRef<str>,
            F: 'static + Fn(&str),
            U: Into<ArtifactMetadataV0>
        {
            self.artifacts.insert(
                metadata.into(),
                ExpectedArtifact::File {
                    name: name.map(|s| s.as_ref().to_string()),
                    assertion_fn: Box::new(matcher),
                }
            );
            self
        }

        /// Add a directory based artifact containing the entries described in |directory|.
        pub fn with_directory_artifact<S, U>(
            mut self,
            metadata: U,
            name: Option<S>,
            directory: ExpectedDirectory,
        ) -> Self
        where
            S: AsRef<str>,
            U: Into<ArtifactMetadataV0>
        {
            self.artifacts.insert(
                metadata.into(),
                ExpectedArtifact::Directory {
                    name: name.map(|s| s.as_ref().to_string()),
                    files: directory.files,
                }
            );
            self
        }

        /// Verify an exact start time.
        pub fn with_start_time(mut self, millis: u64) -> Self {
            self.start_time = MatchOption::Specified(millis);
            self
        }

        /// Verify an exact run duration.
        pub fn with_run_duration(mut self, millis: u64) -> Self {
            self.duration_milliseconds = MatchOption::Specified(millis);
            self
        }

        /// Verify that a start time is present.
        pub fn with_any_start_time(mut self) -> Self {
            self.start_time = MatchOption::Any;
            self
        }

        /// Verify that a run duration is present.
        pub fn with_any_run_duration(mut self) -> Self {
            self.duration_milliseconds = MatchOption::Any;
            self
        }

        /// Verify that no start time is present.
        pub fn with_no_start_time(mut self) -> Self {
            self.start_time = MatchOption::None;
            self
        }

        /// Verify that no run duration is present.
        pub fn with_no_run_duration(mut self) -> Self {
            self.duration_milliseconds = MatchOption::None;
            self
        }
    };
}

impl ExpectedTestRun {
    /// Create a new `ExpectedTestRun` with the given `outcome`.
    pub fn new(outcome: Outcome) -> Self {
        Self {
            artifacts: ArtifactMetadataToAssertionMap::new(),
            outcome,
            start_time: MatchOption::AnyOrNone,
            duration_milliseconds: MatchOption::AnyOrNone,
        }
    }

    common_impl! {}
}

impl ExpectedSuite {
    /// Create a new `ExpectedTestRun` with the given `name` and `outcome`.
    pub fn new<S: AsRef<str>>(name: S, outcome: Outcome) -> Self {
        Self {
            artifacts: ArtifactMetadataToAssertionMap::new(),
            name: name.as_ref().to_string(),
            outcome,
            cases: HashMap::new(),
            start_time: MatchOption::AnyOrNone,
            duration_milliseconds: MatchOption::AnyOrNone,
        }
    }

    /// Add a test case to the suite.
    pub fn with_case(mut self, case: ExpectedTestCase) -> Self {
        self.cases.insert(case.name.clone(), case);
        self
    }

    common_impl! {}
}

impl ExpectedTestCase {
    /// Create a new `ExpectedTestCase` with the given `name` and `outcome`.
    pub fn new<S: AsRef<str>>(name: S, outcome: Outcome) -> Self {
        Self {
            artifacts: ArtifactMetadataToAssertionMap::new(),
            name: name.as_ref().to_string(),
            outcome,
            start_time: MatchOption::AnyOrNone,
            duration_milliseconds: MatchOption::AnyOrNone,
        }
    }

    common_impl! {}
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::ArtifactType;
    use maplit::hashmap;

    fn make_tempdir<F: Fn(&Path)>(initialize_fn: F) -> tempfile::TempDir {
        let dir = tempfile::TempDir::new().unwrap();
        initialize_fn(dir.path());
        dir
    }

    #[test]
    fn assert_run_result_ok() {
        let cases: Vec<(tempfile::TempDir, TestRunResult, ExpectedTestRun)> = vec![
            (
                make_tempdir(|_| ()),
                TestRunResult::V0 {
                    artifacts: hashmap! {},
                    artifact_dir: Path::new("a").to_path_buf(),
                    outcome: Outcome::Passed,
                    suites: vec![],
                    start_time: Some(64),
                    duration_milliseconds: Some(128),
                },
                ExpectedTestRun::new(Outcome::Passed).with_any_start_time().with_any_run_duration(),
            ),
            (
                make_tempdir(|_| ()),
                TestRunResult::V0 {
                    artifacts: hashmap! {},
                    artifact_dir: Path::new("a").to_path_buf(),
                    outcome: Outcome::Passed,
                    suites: vec![],
                    start_time: Some(64),
                    duration_milliseconds: Some(128),
                },
                ExpectedTestRun::new(Outcome::Passed).with_start_time(64).with_run_duration(128),
            ),
            (
                make_tempdir(|_| ()),
                TestRunResult::V0 {
                    artifacts: hashmap! {},
                    artifact_dir: Path::new("a").to_path_buf(),
                    outcome: Outcome::Passed,
                    suites: vec![],
                    start_time: None,
                    duration_milliseconds: None,
                },
                ExpectedTestRun::new(Outcome::Passed).with_no_start_time().with_no_run_duration(),
            ),
            (
                make_tempdir(|path| {
                    std::fs::create_dir(path.join("a")).unwrap();
                    std::fs::write(path.join("a/b.txt"), "hello").unwrap();
                }),
                TestRunResult::V0 {
                    artifacts: hashmap! {
                        Path::new("b.txt").to_path_buf() => ArtifactType::Syslog.into()
                    },
                    artifact_dir: Path::new("a").to_path_buf(),
                    outcome: Outcome::Passed,
                    suites: vec![],
                    start_time: None,
                    duration_milliseconds: None,
                },
                ExpectedTestRun::new(Outcome::Passed).with_artifact(
                    ArtifactType::Syslog,
                    Option::<&str>::None,
                    "hello",
                ),
            ),
            (
                make_tempdir(|path| {
                    std::fs::create_dir(path.join("a")).unwrap();
                    std::fs::write(path.join("a/b.txt"), "hello").unwrap();
                }),
                TestRunResult::V0 {
                    artifacts: hashmap! {
                        Path::new("b.txt").to_path_buf() => ArtifactType::Syslog.into()
                    },
                    artifact_dir: Path::new("a").to_path_buf(),
                    outcome: Outcome::Passed,
                    suites: vec![],
                    start_time: None,
                    duration_milliseconds: None,
                },
                ExpectedTestRun::new(Outcome::Passed).with_artifact(
                    ArtifactType::Syslog,
                    "b.txt".into(),
                    "hello",
                ),
            ),
        ];

        for (actual_dir, actual_test_run, expected_test_run) in cases.into_iter() {
            assert_run_result(actual_dir.path(), &actual_test_run, &expected_test_run);
        }
    }

    #[test]
    #[should_panic(expected = "Outcome for TEST RUN")]
    fn assert_run_outcome_mismatch() {
        let dir = make_tempdir(|_| ());
        assert_run_result(
            dir.path(),
            &TestRunResult::V0 {
                artifacts: hashmap! {},
                artifact_dir: Path::new("a").to_path_buf(),
                outcome: Outcome::Failed,
                suites: vec![],
                start_time: None,
                duration_milliseconds: None,
            },
            &ExpectedTestRun::new(Outcome::Passed),
        );
    }

    #[test]
    #[should_panic(expected = "Start time for TEST RUN")]
    fn assert_run_start_time_mismatch() {
        let dir = make_tempdir(|_| ());
        assert_run_result(
            dir.path(),
            &TestRunResult::V0 {
                artifacts: hashmap! {},
                artifact_dir: Path::new("a").to_path_buf(),
                outcome: Outcome::Failed,
                suites: vec![],
                start_time: Some(64),
                duration_milliseconds: None,
            },
            &ExpectedTestRun::new(Outcome::Passed).with_start_time(23),
        );
    }

    #[test]
    #[should_panic(expected = "Run duration for TEST RUN")]
    fn assert_run_duration_mismatch() {
        let dir = make_tempdir(|_| ());
        assert_run_result(
            dir.path(),
            &TestRunResult::V0 {
                artifacts: hashmap! {},
                artifact_dir: Path::new("a").to_path_buf(),
                outcome: Outcome::Failed,
                suites: vec![],
                start_time: None,
                duration_milliseconds: None,
            },
            &ExpectedTestRun::new(Outcome::Passed).with_run_duration(23),
        );
    }

    #[test]
    #[should_panic]
    fn assert_run_artifact_mismatch() {
        let dir = make_tempdir(|_| ());
        assert_run_result(
            dir.path(),
            &TestRunResult::V0 {
                artifacts: hashmap! {
                    Path::new("missing").to_path_buf() => ArtifactType::Syslog.into()
                },
                artifact_dir: Path::new("a").to_path_buf(),
                outcome: Outcome::Failed,
                suites: vec![],
                start_time: None,
                duration_milliseconds: None,
            },
            &ExpectedTestRun::new(Outcome::Failed).with_artifact(
                ArtifactType::Stderr,
                "stderr.txt".into(),
                "",
            ),
        );
    }

    #[test]
    fn assert_suite_result_ok() {
        let cases: Vec<(tempfile::TempDir, SuiteResult, ExpectedSuite)> = vec![
            (
                make_tempdir(|_| ()),
                SuiteResult::V0 {
                    artifacts: hashmap! {},
                    artifact_dir: Path::new("a").to_path_buf(),
                    outcome: Outcome::Passed,
                    name: "suite".into(),
                    cases: vec![],
                    start_time: Some(64),
                    duration_milliseconds: Some(128),
                },
                ExpectedSuite::new("suite", Outcome::Passed)
                    .with_any_start_time()
                    .with_any_run_duration(),
            ),
            (
                make_tempdir(|_| ()),
                SuiteResult::V0 {
                    artifacts: hashmap! {},
                    artifact_dir: Path::new("a").to_path_buf(),
                    outcome: Outcome::Passed,
                    name: "suite".into(),
                    cases: vec![],
                    start_time: Some(64),
                    duration_milliseconds: Some(128),
                },
                ExpectedSuite::new("suite", Outcome::Passed)
                    .with_start_time(64)
                    .with_run_duration(128),
            ),
            (
                make_tempdir(|_| ()),
                SuiteResult::V0 {
                    artifacts: hashmap! {},
                    artifact_dir: Path::new("a").to_path_buf(),
                    outcome: Outcome::Passed,
                    name: "suite".into(),
                    cases: vec![],
                    start_time: None,
                    duration_milliseconds: None,
                },
                ExpectedSuite::new("suite", Outcome::Passed)
                    .with_no_start_time()
                    .with_no_run_duration(),
            ),
            (
                make_tempdir(|path| {
                    std::fs::create_dir(path.join("a")).unwrap();
                    std::fs::write(path.join("a/b.txt"), "hello").unwrap();
                }),
                SuiteResult::V0 {
                    artifacts: hashmap! {
                        Path::new("b.txt").to_path_buf() => ArtifactType::Syslog.into()
                    },
                    artifact_dir: Path::new("a").to_path_buf(),
                    outcome: Outcome::Passed,
                    name: "suite".into(),
                    cases: vec![],
                    start_time: None,
                    duration_milliseconds: None,
                },
                ExpectedSuite::new("suite", Outcome::Passed).with_artifact(
                    ArtifactType::Syslog,
                    "b.txt".into(),
                    "hello",
                ),
            ),
            (
                make_tempdir(|_| ()),
                SuiteResult::V0 {
                    artifacts: hashmap! {},
                    artifact_dir: Path::new("a").to_path_buf(),
                    outcome: Outcome::Failed,
                    name: "suite".into(),
                    cases: vec![TestCaseResultV0 {
                        artifacts: hashmap! {},
                        artifact_dir: Path::new("a").to_path_buf(),
                        outcome: Outcome::Passed,
                        name: "case".into(),
                        start_time: None,
                        duration_milliseconds: None,
                    }],
                    start_time: None,
                    duration_milliseconds: None,
                },
                ExpectedSuite::new("suite", Outcome::Failed).with_case(
                    ExpectedTestCase::new("case", Outcome::Passed)
                        .with_no_run_duration()
                        .with_no_start_time(),
                ),
            ),
        ];

        for (actual_dir, actual_suite, expected_suite) in cases.into_iter() {
            assert_suite_result(actual_dir.path(), &actual_suite, &expected_suite);
        }
    }

    #[test]
    #[should_panic(expected = "Outcome for SUITE suite")]
    fn assert_suite_outcome_mismatch() {
        let dir = make_tempdir(|_| ());
        assert_suite_result(
            dir.path(),
            &SuiteResult::V0 {
                artifacts: hashmap! {},
                artifact_dir: Path::new("a").to_path_buf(),
                outcome: Outcome::Passed,
                name: "suite".into(),
                cases: vec![],
                start_time: Some(64),
                duration_milliseconds: Some(128),
            },
            &ExpectedSuite::new("suite", Outcome::Failed),
        );
    }

    #[test]
    #[should_panic(expected = "Start time for SUITE suite")]
    fn assert_suite_start_time_mismatch() {
        let dir = make_tempdir(|_| ());
        assert_suite_result(
            dir.path(),
            &SuiteResult::V0 {
                artifacts: hashmap! {},
                artifact_dir: Path::new("a").to_path_buf(),
                outcome: Outcome::Passed,
                name: "suite".into(),
                cases: vec![],
                start_time: None,
                duration_milliseconds: Some(128),
            },
            &ExpectedSuite::new("suite", Outcome::Passed).with_any_start_time(),
        );
    }

    #[test]
    #[should_panic(expected = "Duration for SUITE suite")]
    fn assert_suite_duration_mismatch() {
        let dir = make_tempdir(|_| ());
        assert_suite_result(
            dir.path(),
            &SuiteResult::V0 {
                artifacts: hashmap! {},
                artifact_dir: Path::new("a").to_path_buf(),
                outcome: Outcome::Passed,
                name: "suite".into(),
                cases: vec![],
                start_time: None,
                duration_milliseconds: Some(128),
            },
            &ExpectedSuite::new("suite", Outcome::Passed).with_run_duration(32),
        );
    }

    #[test]
    #[should_panic]
    fn assert_suite_artifact_mismatch() {
        let dir = make_tempdir(|_| ());
        assert_suite_result(
            dir.path(),
            &SuiteResult::V0 {
                artifacts: hashmap! {},
                artifact_dir: Path::new("a").to_path_buf(),
                outcome: Outcome::Failed,
                name: "suite".into(),
                cases: vec![],
                start_time: None,
                duration_milliseconds: None,
            },
            &ExpectedSuite::new("suite", Outcome::Passed).with_artifact(
                ArtifactType::Stderr,
                Option::<&str>::None,
                "missing contents",
            ),
        );
    }

    #[test]
    #[should_panic(expected = "Found unexpected case")]
    fn assert_suite_case_mismatch() {
        let dir = make_tempdir(|_| ());
        assert_suite_result(
            dir.path(),
            &SuiteResult::V0 {
                artifacts: hashmap! {},
                artifact_dir: Path::new("a").to_path_buf(),
                outcome: Outcome::Failed,
                name: "suite".into(),
                cases: vec![TestCaseResultV0 {
                    artifacts: hashmap! {},
                    artifact_dir: Path::new("a").to_path_buf(),
                    outcome: Outcome::Passed,
                    name: "case".into(),
                    start_time: None,
                    duration_milliseconds: None,
                }],
                start_time: None,
                duration_milliseconds: None,
            },
            &ExpectedSuite::new("suite", Outcome::Failed)
                .with_case(ExpectedTestCase::new("wrong name", Outcome::Passed)),
        );
    }

    #[test]
    fn assert_artifacts_ok() {
        let cases: Vec<(tempfile::TempDir, TestRunResult, ExpectedTestRun)> = vec![
            (
                make_tempdir(|_| ()),
                TestRunResult::V0 {
                    artifacts: hashmap! {},
                    artifact_dir: Path::new("a").to_path_buf(),
                    outcome: Outcome::Passed,
                    suites: vec![],
                    start_time: None,
                    duration_milliseconds: None,
                },
                ExpectedTestRun::new(Outcome::Passed),
            ),
            (
                make_tempdir(|path| {
                    std::fs::create_dir(path.join("a")).unwrap();
                    std::fs::write(path.join("a/b.txt"), "hello").unwrap();
                }),
                TestRunResult::V0 {
                    artifacts: hashmap! {
                        "b.txt".into() => ArtifactType::Stderr.into(),
                    },
                    artifact_dir: Path::new("a").to_path_buf(),
                    outcome: Outcome::Passed,
                    suites: vec![],
                    start_time: None,
                    duration_milliseconds: None,
                },
                ExpectedTestRun::new(Outcome::Passed).with_artifact(
                    ArtifactType::Stderr,
                    Option::<&str>::None,
                    "hello",
                ),
            ),
            (
                make_tempdir(|path| {
                    std::fs::create_dir(path.join("a")).unwrap();
                    std::fs::write(path.join("a/b.txt"), "hello").unwrap();
                }),
                TestRunResult::V0 {
                    artifacts: hashmap! {
                        "b.txt".into() => ArtifactType::Stderr.into(),
                    },
                    artifact_dir: Path::new("a").to_path_buf(),
                    outcome: Outcome::Passed,
                    suites: vec![],
                    start_time: None,
                    duration_milliseconds: None,
                },
                ExpectedTestRun::new(Outcome::Passed).with_artifact(
                    ArtifactType::Stderr,
                    Some("b.txt"),
                    "hello",
                ),
            ),
            (
                make_tempdir(|path| {
                    std::fs::create_dir(path.join("a")).unwrap();
                    std::fs::write(path.join("a/b.txt"), "hello").unwrap();
                }),
                TestRunResult::V0 {
                    artifacts: hashmap! {
                        "b.txt".into() => ArtifactType::Stderr.into(),
                    },
                    artifact_dir: Path::new("a").to_path_buf(),
                    outcome: Outcome::Passed,
                    suites: vec![],
                    start_time: None,
                    duration_milliseconds: None,
                },
                ExpectedTestRun::new(Outcome::Passed).with_matching_artifact(
                    ArtifactType::Stderr,
                    Some("b.txt"),
                    |content| assert_eq!(content, "hello"),
                ),
            ),
            (
                make_tempdir(|path| {
                    std::fs::create_dir(path.join("a")).unwrap();
                    std::fs::write(path.join("a/b.txt"), "hello").unwrap();
                }),
                TestRunResult::V0 {
                    artifacts: hashmap! {
                        "b.txt".into() => ArtifactMetadataV0 {
                            artifact_type: ArtifactType::Syslog,
                            component_moniker: Some("moniker".into())
                        },
                    },
                    artifact_dir: Path::new("a").to_path_buf(),
                    outcome: Outcome::Passed,
                    suites: vec![],
                    start_time: None,
                    duration_milliseconds: None,
                },
                ExpectedTestRun::new(Outcome::Passed).with_artifact(
                    ArtifactMetadataV0 {
                        artifact_type: ArtifactType::Syslog,
                        component_moniker: Some("moniker".into()),
                    },
                    Option::<&str>::None,
                    "hello",
                ),
            ),
            (
                make_tempdir(|path| {
                    std::fs::create_dir(path.join("a")).unwrap();
                    std::fs::create_dir(path.join("a/b")).unwrap();
                    std::fs::write(path.join("a/b/c.txt"), "hello c").unwrap();
                    std::fs::write(path.join("a/b/d.txt"), "hello d").unwrap();
                }),
                TestRunResult::V0 {
                    artifacts: hashmap! {
                        "b".into() => ArtifactType::Custom.into(),
                    },
                    artifact_dir: Path::new("a").to_path_buf(),
                    outcome: Outcome::Passed,
                    suites: vec![],
                    start_time: None,
                    duration_milliseconds: None,
                },
                ExpectedTestRun::new(Outcome::Passed).with_directory_artifact(
                    ArtifactType::Custom,
                    Some("b"),
                    ExpectedDirectory::new()
                        .with_file("c.txt", "hello c")
                        .with_matching_file("d.txt", |contents| assert_eq!(contents, "hello d")),
                ),
            ),
        ];

        for (actual_dir, actual_run, expected_run) in cases.into_iter() {
            assert_run_result(actual_dir.path(), &actual_run, &expected_run);
        }
    }

    #[test]
    #[should_panic(expected = "Artifacts for TEST RUN")]
    fn assert_artifacts_missing() {
        let dir = make_tempdir(|_| ());
        assert_run_result(
            dir.path(),
            &TestRunResult::V0 {
                artifacts: hashmap! {},
                artifact_dir: Path::new("a").to_path_buf(),
                outcome: Outcome::Passed,
                suites: vec![],
                start_time: None,
                duration_milliseconds: None,
            },
            &ExpectedTestRun::new(Outcome::Passed).with_artifact(
                ArtifactType::Syslog,
                Some("missing"),
                "missing contents",
            ),
        );
    }

    #[test]
    #[should_panic(expected = "Artifacts for TEST RUN")]
    fn assert_artifacts_extra_artifact() {
        let dir = make_tempdir(|path| {
            std::fs::create_dir(path.join("a")).unwrap();
            std::fs::write(path.join("a/b.txt"), "hello").unwrap();
            std::fs::write(path.join("a/c.txt"), "hello").unwrap();
        });
        assert_run_result(
            dir.path(),
            &TestRunResult::V0 {
                artifacts: hashmap! {
                    "b.txt".into() => ArtifactType::Stdout.into(),
                    "c.txt".into() => ArtifactType::Stderr.into(),
                },
                artifact_dir: Path::new("a").to_path_buf(),
                outcome: Outcome::Passed,
                suites: vec![],
                start_time: None,
                duration_milliseconds: None,
            },
            &ExpectedTestRun::new(Outcome::Passed).with_artifact(
                ArtifactType::Stderr,
                "c.txt".into(),
                "hello",
            ),
        );
    }

    #[test]
    #[should_panic]
    fn assert_artifacts_content_not_equal() {
        let dir = make_tempdir(|path| {
            std::fs::create_dir(path.join("a")).unwrap();
            std::fs::write(path.join("a/b.txt"), "wrong content").unwrap();
        });
        assert_run_result(
            dir.path(),
            &TestRunResult::V0 {
                artifacts: hashmap! {
                    "b.txt".into() => ArtifactType::Syslog.into()
                },
                artifact_dir: Path::new("a").to_path_buf(),
                outcome: Outcome::Passed,
                suites: vec![],
                start_time: None,
                duration_milliseconds: None,
            },
            &ExpectedTestRun::new(Outcome::Passed).with_artifact(
                ArtifactType::Syslog,
                Option::<&str>::None,
                "expected content",
            ),
        );
    }

    #[test]
    #[should_panic]
    fn assert_artifacts_content_does_not_match() {
        let dir = make_tempdir(|path| {
            std::fs::create_dir(path.join("a")).unwrap();
            std::fs::write(path.join("a/b.txt"), "wrong content").unwrap();
        });
        assert_run_result(
            dir.path(),
            &TestRunResult::V0 {
                artifacts: hashmap! {
                    "b.txt".into() => ArtifactType::Syslog.into()
                },
                artifact_dir: Path::new("a").to_path_buf(),
                outcome: Outcome::Passed,
                suites: vec![],
                start_time: None,
                duration_milliseconds: None,
            },
            &ExpectedTestRun::new(Outcome::Passed).with_matching_artifact(
                ArtifactType::Syslog,
                Option::<&str>::None,
                |content| assert_eq!(content, "expected content"),
            ),
        );
    }

    #[test]
    #[should_panic]
    fn assert_artifacts_directory_mismatch() {
        let dir = make_tempdir(|path| {
            std::fs::create_dir(path.join("a")).unwrap();
            std::fs::create_dir(path.join("a/b")).unwrap();
            std::fs::write(path.join("a/b/c.txt"), "unexpected file").unwrap();
        });
        assert_run_result(
            dir.path(),
            &TestRunResult::V0 {
                artifacts: hashmap! {
                    "b".into() => ArtifactType::Custom.into()
                },
                artifact_dir: Path::new("a").to_path_buf(),
                outcome: Outcome::Passed,
                suites: vec![],
                start_time: None,
                duration_milliseconds: None,
            },
            &ExpectedTestRun::new(Outcome::Passed).with_directory_artifact(
                ArtifactType::Custom,
                Option::<&str>::None,
                ExpectedDirectory::new(),
            ),
        );
    }

    #[test]
    fn assert_artifacts_not_checked_if_unspecified() {
        let dir = make_tempdir(|path| {
            std::fs::create_dir(path.join("a")).unwrap();
            std::fs::create_dir(path.join("a/b")).unwrap();
            std::fs::write(path.join("a/b/c.txt"), "unexpected file").unwrap();
        });
        assert_run_result(
            dir.path(),
            &TestRunResult::V0 {
                artifacts: hashmap! {
                    "b".into() => ArtifactType::Custom.into()
                },
                artifact_dir: Path::new("a").to_path_buf(),
                outcome: Outcome::Passed,
                suites: vec![],
                start_time: None,
                duration_milliseconds: None,
            },
            &ExpectedTestRun::new(Outcome::Passed),
        );
    }
}
