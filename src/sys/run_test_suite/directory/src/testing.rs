// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    ArtifactMetadataV0, Outcome, SuiteEntryV0, SuiteResult, TestCaseResultV0, TestRunResult,
    RUN_SUMMARY_NAME,
};
use std::collections::HashMap;
use std::fs::File;
use std::path::{Path, PathBuf};

/// Parse the json files in a directory. Returns the parsed test run document and a list of
/// parsed suite results. This loads all the results in memory at once and shouldn't be used
/// outside of testing.
pub fn parse_json_in_output(path: &Path) -> (TestRunResult, Vec<SuiteResult>) {
    let summary_file = File::open(path.join(RUN_SUMMARY_NAME)).expect("open summary file");
    let run_result: TestRunResult =
        serde_json::from_reader(summary_file).expect("parse summary file");
    let suite_entries = match &run_result {
        TestRunResult::V0 { suites, .. } => suites,
    };
    let suite_results = suite_entries
        .iter()
        .map(|SuiteEntryV0 { summary }| {
            let suite_summary_file =
                File::open(path.join(summary)).expect("open suite summary file");
            serde_json::from_reader::<_, SuiteResult>(suite_summary_file)
                .expect("parse suite summary file")
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
    ($expected:expr, $actual:expr, $field:literal) => {
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

/// A mapping from artifact metadata to assertions made on the artifact.
/// The value is a (name, assertion_fn) tuple. When a name is present it is used to verify the
/// filename of the artifact. assertion_fn is run against the contents of the file.
type ArtifactMetadataToAssertionMap =
    HashMap<ArtifactMetadataV0, (Option<String>, Box<dyn Fn(&str)>)>;

/// Assert that the run results contained in `actual_run` and the directory specified by `root`
/// contain the results and artifacts in `expected_run`.
pub fn assert_run_result(root: &Path, actual_run: &TestRunResult, expected_run: &ExpectedTestRun) {
    let &TestRunResult::V0 { artifacts, outcome, suites: _, start_time, duration_milliseconds } =
        &actual_run;
    assert_match_option!(
        expected_run.duration_milliseconds,
        *duration_milliseconds,
        "run duration"
    );
    assert_match_option!(expected_run.start_time, *start_time, "run start time");
    assert_eq!(outcome, &expected_run.outcome);
    assert_artifacts(root, &artifacts, &expected_run.artifacts);
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
    let &SuiteResult::V0 { artifacts, outcome, name, cases, duration_milliseconds, start_time } =
        &actual_suite;
    assert_eq!(outcome, &expected_suite.outcome);
    assert_eq!(name, &expected_suite.name);
    assert_match_option!(
        expected_suite.duration_milliseconds,
        *duration_milliseconds,
        "suite duration"
    );
    assert_match_option!(expected_suite.start_time, *start_time, "suite start time");

    assert_artifacts(root, &artifacts, &expected_suite.artifacts);

    assert_eq!(cases.len(), expected_suite.cases.len());
    for case in cases.iter() {
        assert_case_result(root, case, expected_suite.cases.get(&case.name).unwrap());
    }
}

fn assert_case_result(
    root: &Path,
    actual_case: &TestCaseResultV0,
    expected_case: &ExpectedTestCase,
) {
    assert_eq!(actual_case.name, expected_case.name);
    assert_eq!(actual_case.outcome, expected_case.outcome);
    assert_match_option!(
        expected_case.duration_milliseconds,
        actual_case.duration_milliseconds,
        "case duration"
    );
    assert_match_option!(expected_case.start_time, actual_case.start_time, "case start time");
    assert_artifacts(root, &actual_case.artifacts, &expected_case.artifacts);
}

fn assert_artifacts(
    root: &Path,
    actual_artifacts: &HashMap<PathBuf, ArtifactMetadataV0>,
    expected_artifacts: &ArtifactMetadataToAssertionMap,
) {
    let actual_artifacts_by_metadata: HashMap<ArtifactMetadataV0, PathBuf> =
        actual_artifacts.iter().map(|(key, value)| (value.clone(), key.clone())).collect();
    // For now, artifact metadata should be unique for each artifact.
    assert_eq!(actual_artifacts_by_metadata.len(), actual_artifacts.len());

    assert_eq!(actual_artifacts_by_metadata.len(), expected_artifacts.len());

    for (expected_metadata, (expected_name, assertion_fn)) in expected_artifacts.iter() {
        let actual_filepath = actual_artifacts_by_metadata.get(expected_metadata);
        assert!(
            actual_filepath.is_some(),
            "Expected artifact matching {:?} to exist",
            expected_metadata
        );
        let actual_filepath = actual_filepath.unwrap();
        match expected_name {
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
        let absolute_artifact_path = root.join(actual_filepath);
        let actual_contents = std::fs::read_to_string(&absolute_artifact_path);
        (assertion_fn)(&actual_contents.unwrap());
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

        /// /// Add an artifact matching the exact contents. Artifacts are checked by finding
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
                (name.map(|s| s.as_ref().to_string()), Box::new(matcher))
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
