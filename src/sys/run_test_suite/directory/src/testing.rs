// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    Outcome, SuiteEntryV0, SuiteResult, TestCaseResultV0, TestRunResult, RUN_SUMMARY_NAME,
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

/// Assert that the run results contained in `actual_run` and the directory specified by `root`
/// contain the results and artifacts in `expected_run`.
pub fn assert_run_result(root: &Path, actual_run: &TestRunResult, expected_run: &ExpectedTestRun) {
    let &TestRunResult::V0 { artifacts, outcome, .. } = &actual_run;
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
    let &SuiteResult::V0 { artifacts, outcome, name, cases } = &actual_suite;
    assert_eq!(outcome, &expected_suite.outcome);
    assert_eq!(name, &expected_suite.name);

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
    assert_artifacts(root, &actual_case.artifacts, &expected_case.artifacts);
}

fn assert_artifacts(
    root: &Path,
    actual_artifact_list: &Vec<PathBuf>,
    expected_artifact_contents: &HashMap<String, Box<dyn Fn(&str)>>,
) {
    let actual_artifact_names: HashSet<String> = HashSet::from_iter(
        actual_artifact_list.iter().map(|p| p.file_name().unwrap().to_str().unwrap().to_string()),
    );
    let expected_artifact_names: HashSet<String> =
        HashSet::from_iter(expected_artifact_contents.keys().map(|s| s.to_string()));
    assert_eq!(expected_artifact_names, actual_artifact_names);

    for artifact_path in actual_artifact_list.iter() {
        let absolute_artifact_path = root.join(artifact_path);
        let artifact_name = absolute_artifact_path.file_name().unwrap().to_str().unwrap();
        let artifact_contents =
            std::fs::read_to_string(&absolute_artifact_path).expect("read artifact file");
        let assertion_fn = expected_artifact_contents.get(artifact_name).unwrap();

        (assertion_fn)(&artifact_contents);
    }
}

/// A version of a test run result that contains all output in memory. This should only be used
/// for making assertions in a test.
pub struct ExpectedTestRun {
    artifacts: HashMap<String, Box<dyn Fn(&str)>>,
    outcome: Outcome,
}

/// A version of a suite run result that contains all output in memory. This should only be used
/// for making assertions in a test.
pub struct ExpectedSuite {
    artifacts: HashMap<String, Box<dyn Fn(&str)>>,
    name: String,
    outcome: Outcome,
    cases: HashMap<String, ExpectedTestCase>,
}

/// A version of a test case result that contains all output in memory. This should only be used
/// for making assertions in a test.
pub struct ExpectedTestCase {
    artifacts: HashMap<String, Box<dyn Fn(&str)>>,
    name: String,
    outcome: Outcome,
}

impl ExpectedTestRun {
    /// Create a new `ExpectedTestRun` with the given `outcome`.
    pub fn new(outcome: Outcome) -> Self {
        Self { artifacts: HashMap::new(), outcome }
    }

    /// Add an artifact scoped to the test run.
    pub fn with_artifact<S: AsRef<str>, T: AsRef<str>>(self, name: S, contents: T) -> Self {
        let owned_expected = contents.as_ref().to_string();
        let owned_name = name.as_ref().to_string();
        self.with_matching_artifact(name, move |actual| {
            assert_eq!(
                &owned_expected, actual,
                "Mismatch in artifact '{}'. Expected: '{}', actual:'{}'",
                owned_name, &owned_expected, actual
            )
        })
    }

    /// Add an artifact scoped to the test run. `matcher` will be run against the contents of
    /// the actual artifact and may contain assertions.
    pub fn with_matching_artifact<S: AsRef<str>, F: 'static + Fn(&str)>(
        mut self,
        name: S,
        matcher: F,
    ) -> Self {
        self.artifacts.insert(name.as_ref().to_string(), Box::new(matcher));
        self
    }
}

impl ExpectedSuite {
    /// Create a new `ExpectedTestRun` with the given `name` and `outcome`.
    pub fn new<S: AsRef<str>>(name: S, outcome: Outcome) -> Self {
        Self {
            artifacts: HashMap::new(),
            name: name.as_ref().to_string(),
            outcome,
            cases: HashMap::new(),
        }
    }

    /// Add a test case to the suite.
    pub fn with_case(mut self, case: ExpectedTestCase) -> Self {
        self.cases.insert(case.name.clone(), case);
        self
    }

    /// Add an artifact scoped to the test suite.
    pub fn with_artifact<S: AsRef<str>, T: AsRef<str>>(self, name: S, contents: T) -> Self {
        let owned_expected = contents.as_ref().to_string();
        let owned_name = name.as_ref().to_string();
        self.with_matching_artifact(name, move |actual| {
            assert_eq!(
                &owned_expected, actual,
                "Mismatch in artifact '{}'. Expected: '{}', actual:'{}'",
                owned_name, &owned_expected, actual
            )
        })
    }

    /// Add an artifact scoped to the test suite. `matcher` will be run against the contents of
    /// the actual artifact and may contain assertions.
    pub fn with_matching_artifact<S: AsRef<str>, F: 'static + Fn(&str)>(
        mut self,
        name: S,
        matcher: F,
    ) -> Self {
        self.artifacts.insert(name.as_ref().to_string(), Box::new(matcher));
        self
    }
}

impl ExpectedTestCase {
    /// Create a new `ExpectedTestCase` with the given `name` and `outcome`.
    pub fn new<S: AsRef<str>>(name: S, outcome: Outcome) -> Self {
        Self { artifacts: HashMap::new(), name: name.as_ref().to_string(), outcome }
    }

    /// Add an artifact scoped to the test case.
    pub fn with_artifact<S: AsRef<str>, T: AsRef<str>>(self, name: S, contents: T) -> Self {
        let owned_expected = contents.as_ref().to_string();
        let owned_name = name.as_ref().to_string();
        self.with_matching_artifact(name, move |actual| {
            assert_eq!(
                &owned_expected, actual,
                "Mismatch in artifact '{}'. Expected: '{}', actual:'{}'",
                owned_name, &owned_expected, actual
            )
        })
    }

    /// Add an artifact scoped to the test case. `matcher` will be run against the contents of
    /// the actual artifact and may contain assertions.
    pub fn with_matching_artifact<S: AsRef<str>, F: 'static + Fn(&str)>(
        mut self,
        name: S,
        matcher: F,
    ) -> Self {
        self.artifacts.insert(name.as_ref().to_string(), Box::new(matcher));
        self
    }
}
