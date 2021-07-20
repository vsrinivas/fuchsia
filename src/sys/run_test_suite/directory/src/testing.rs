// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    ArtifactMetadataV0, Outcome, SuiteEntryV0, SuiteResult, TestCaseResultV0, TestRunResult,
    RUN_SUMMARY_NAME,
};
use std::collections::{HashMap, HashSet};
use std::fs::File;
use std::iter::FromIterator;
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
    expected_artifacts: &HashMap<String, (ArtifactMetadataV0, Box<dyn Fn(&str)>)>,
) {
    let actual_artifact_names: HashSet<String> = HashSet::from_iter(
        actual_artifacts.keys().map(|p| p.file_name().unwrap().to_str().unwrap().to_string()),
    );
    let expected_artifact_names: HashSet<String> =
        HashSet::from_iter(expected_artifacts.keys().map(|s| s.to_string()));
    assert_eq!(expected_artifact_names, actual_artifact_names);

    //for (artifact_path, (expected_metadata, assertion_fn)) in expected_artifacts.iter() {
    for (artifact_path, actual_metadata) in actual_artifacts.iter() {
        let absolute_artifact_path = root.join(artifact_path);
        let actual_contents = std::fs::read_to_string(&absolute_artifact_path);
        assert!(
            actual_contents.is_ok(),
            "Error reading artifact {:?}: {:?}",
            absolute_artifact_path,
            actual_contents.unwrap_err(),
        );

        let artifact_name = absolute_artifact_path.file_name().unwrap().to_str().unwrap();
        let (expected_metadata, assertion_fn) = expected_artifacts.get(artifact_name).unwrap();

        assert_eq!(
            expected_metadata, actual_metadata,
            "Metadata for {:?} does not match expected",
            absolute_artifact_path
        );
        (assertion_fn)(&actual_contents.unwrap());
    }
}

/// A version of a test run result that contains all output in memory. This should only be used
/// for making assertions in a test.
pub struct ExpectedTestRun {
    artifacts: HashMap<String, (ArtifactMetadataV0, Box<dyn Fn(&str)>)>,
    outcome: Outcome,
    start_time: MatchOption<u64>,
    duration_milliseconds: MatchOption<u64>,
}

/// A version of a suite run result that contains all output in memory. This should only be used
/// for making assertions in a test.
pub struct ExpectedSuite {
    artifacts: HashMap<String, (ArtifactMetadataV0, Box<dyn Fn(&str)>)>,
    name: String,
    outcome: Outcome,
    cases: HashMap<String, ExpectedTestCase>,
    start_time: MatchOption<u64>,
    duration_milliseconds: MatchOption<u64>,
}

/// A version of a test case result that contains all output in memory. This should only be used
/// for making assertions in a test.
pub struct ExpectedTestCase {
    artifacts: HashMap<String, (ArtifactMetadataV0, Box<dyn Fn(&str)>)>,
    name: String,
    outcome: Outcome,
    start_time: MatchOption<u64>,
    duration_milliseconds: MatchOption<u64>,
}

macro_rules! common_impl {
    {} => {
        /// Add an artifact matching the exact contents.
        pub fn with_artifact<S: AsRef<str>, T: AsRef<str>, U: Into<ArtifactMetadataV0>>(
            self, name: S, metadata: U, contents: T
        ) -> Self {
            let owned_expected = contents.as_ref().to_string();
            let owned_name = name.as_ref().to_string();
            self.with_matching_artifact(name, metadata, move |actual| {
                assert_eq!(
                    &owned_expected, actual,
                    "Mismatch in artifact '{}'. Expected: '{}', actual:'{}'",
                    owned_name, &owned_expected, actual
                )
            })
        }

        /// Add an artifact. `matcher` will be run against the contents of
        /// the actual artifact and may contain assertions.
        pub fn with_matching_artifact<S: AsRef<str>, F: 'static + Fn(&str), U: Into<ArtifactMetadataV0>>(
            mut self,
            name: S,
            metadata: U,
            matcher: F,
        ) -> Self {
            self.artifacts.insert(name.as_ref().to_string(), (metadata.into(), Box::new(matcher)));
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
            artifacts: HashMap::new(),
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
            artifacts: HashMap::new(),
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
            artifacts: HashMap::new(),
            name: name.as_ref().to_string(),
            outcome,
            start_time: MatchOption::AnyOrNone,
            duration_milliseconds: MatchOption::AnyOrNone,
        }
    }

    common_impl! {}
}
