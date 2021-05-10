// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::output::{
    ArtifactReporter, ArtifactType, CaseId, EntityId, ReportedOutcome, Reporter, SuiteId,
};
use parking_lot::Mutex;
use std::collections::HashMap;
use std::fs::{DirBuilder, File};
use std::io::Error;
use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};
use test_output_directory as directory;

const TEST_RUN_ID: u64 = 0;
const SUMMARY_FILE: &str = "run_summary.json";

/// A reporter that saves results and artifacts to disk in the Fuchsia test output format.
pub(super) struct DirectoryReporter {
    /// Root directory in which to place results.
    root: PathBuf,
    /// A mapping from ID to every test run, test suite, and test case. The test run entry
    /// is always contained in ID TEST_RUN_ID. Entries are added as new test cases and suites
    /// are found, and removed once they have been persisted.
    entries: Mutex<HashMap<u64, EntityEntry>>,
    /// The next id to issue to an entity. Ids are issued in increasing order.
    next_id: AtomicU64,
}

/// In-memory representation of either a test run, test suite, or test case.
struct EntityEntry {
    /// Name of the entity. Unused for a test run.
    name: String,
    /// A list of the children of an entity referenced by their id. Unused for a test case.
    children: Vec<u64>,
    /// Most recently known outcome for the entity.
    outcome: ReportedOutcome,
}

impl DirectoryReporter {
    /// Create a new `DirectoryReporter` that places results in the given `root` directory.
    pub(super) fn new(root: PathBuf) -> Result<Self, Error> {
        DirBuilder::new().recursive(true).create(&root)?;
        let mut entries = HashMap::new();
        entries.insert(
            TEST_RUN_ID,
            EntityEntry {
                name: "".to_string(),
                children: vec![],
                outcome: ReportedOutcome::Inconclusive,
            },
        );
        Ok(Self { root, entries: Mutex::new(entries), next_id: AtomicU64::new(TEST_RUN_ID + 1) })
    }

    /// Convert `id` into a u64 used as a key for the `entries` map.
    fn into_entry_id(id: &EntityId) -> u64 {
        match id {
            EntityId::TestRun => TEST_RUN_ID,
            EntityId::Suite(SuiteId(id)) | EntityId::Case(CaseId(id)) => *id,
        }
    }

    /// Create a new child entity. Parent id is either an id for a suite or TEST_RUN_ID.
    fn new_entity(&self, parent_id: u64, name: &str) -> Result<u64, Error> {
        let new_entity_id = self.next_id.fetch_add(1, Ordering::Relaxed);

        let mut entries = self.entries.lock();
        let parent = entries
            .get_mut(&parent_id)
            .expect("Attempting to create a child for an entity that does not exist");
        parent.children.push(new_entity_id);
        entries.insert(
            new_entity_id,
            EntityEntry {
                name: name.to_string(),
                children: vec![],
                outcome: ReportedOutcome::Inconclusive,
            },
        );

        Ok(new_entity_id)
    }
}

impl ArtifactReporter for DirectoryReporter {
    type Writer = File;

    fn new_artifact(
        &self,
        _entity: &EntityId,
        _artifact_type: &ArtifactType,
    ) -> Result<Self::Writer, Error> {
        unimplemented!()
    }
}

impl Reporter for DirectoryReporter {
    fn outcome(&self, entity: &EntityId, outcome: &ReportedOutcome) -> Result<(), Error> {
        let mut entries = self.entries.lock();
        let entry = entries
            .get_mut(&Self::into_entry_id(entity))
            .expect("Outcome reported for an entity that does not exist");
        entry.outcome = *outcome;
        Ok(())
    }

    fn new_case(&self, parent: &SuiteId, name: &str) -> Result<CaseId, Error> {
        let case_id = self.new_entity(parent.0, name)?;

        Ok(CaseId(case_id))
    }

    fn new_suite(&self, url: &str) -> Result<SuiteId, Error> {
        let suite_id = self.new_entity(TEST_RUN_ID, url)?;
        Ok(SuiteId(suite_id))
    }

    fn record(&self, entity: &EntityId) -> Result<(), Error> {
        match entity {
            EntityId::TestRun => {
                let run_entry = self
                    .entries
                    .lock()
                    .remove(&TEST_RUN_ID)
                    .expect("Run entry not found, was it already recorded?");
                let serializable_run = construct_serializable_run(run_entry);
                let summary_path = self.root.join(SUMMARY_FILE);
                let mut summary = File::create(summary_path)?;
                serde_json::to_writer_pretty(&mut summary, &serializable_run)?;
                summary.sync_all()
            }
            EntityId::Suite(SuiteId(suite_id)) => {
                let mut entries = self.entries.lock();
                let suite_entry = entries
                    .remove(suite_id)
                    .expect("Suite entry not found, was it already recorded?");
                let case_entries = suite_entry
                    .children
                    .iter()
                    .map(|case_id| {
                        entries.remove(case_id).expect("Test case referenced by suite not found")
                    })
                    .collect();
                let serializable_suite = construct_serializable_suite(suite_entry, case_entries);
                let summary_path = self.root.join(suite_json_name(*suite_id));
                let mut summary = File::create(summary_path)?;
                serde_json::to_writer_pretty(&mut summary, &serializable_suite)?;
                summary.sync_all()
            }
            // Cases are saved as part of suites.
            EntityId::Case(_) => Ok(()),
        }
    }
}

fn suite_json_name(suite_id: u64) -> String {
    format!("{:?}.json", suite_id)
}

/// Construct a serializable version of a test run.
fn construct_serializable_run(run_entry: EntityEntry) -> directory::TestRunResult {
    let suites = run_entry
        .children
        .iter()
        .map(|suite_id| directory::SuiteEntryV0 { summary: suite_json_name(*suite_id) })
        .collect();
    directory::TestRunResult::V0 { outcome: into_serializable_outcome(run_entry.outcome), suites }
}

/// Construct a serializable version of a test suite.
fn construct_serializable_suite(
    suite_entry: EntityEntry,
    case_entries: Vec<EntityEntry>,
) -> directory::SuiteResult {
    let cases = case_entries
        .into_iter()
        .map(|case_entry| directory::TestCaseResultV0 {
            outcome: into_serializable_outcome(case_entry.outcome),
            name: case_entry.name,
        })
        .collect::<Vec<_>>();
    directory::SuiteResult::V0 {
        outcome: into_serializable_outcome(suite_entry.outcome),
        cases,
        name: suite_entry.name,
    }
}

fn into_serializable_outcome(outcome: ReportedOutcome) -> directory::Outcome {
    match outcome {
        ReportedOutcome::Passed => directory::Outcome::Passed,
        ReportedOutcome::Failed => directory::Outcome::Failed,
        ReportedOutcome::Inconclusive => directory::Outcome::Inconclusive,
        ReportedOutcome::Timedout => directory::Outcome::Timedout,
        ReportedOutcome::Error => directory::Outcome::Error,
        ReportedOutcome::Skipped => directory::Outcome::Skipped,
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::output::RunReporter;
    use std::path::Path;
    use tempfile::tempdir;

    /// Parse the json files in a directory. Returns the parsed test run document and a list of
    /// parsed suite results.
    fn parse_json_in_output(
        path: &Path,
    ) -> (directory::TestRunResult, Vec<directory::SuiteResult>) {
        let summary_file = File::open(path.join(SUMMARY_FILE)).expect("open summary file");
        let run_result: directory::TestRunResult =
            serde_json::from_reader(summary_file).expect("parse summary file");
        let suite_entries = match &run_result {
            directory::TestRunResult::V0 { suites, .. } => suites,
        };
        let suite_results = suite_entries
            .iter()
            .map(|directory::SuiteEntryV0 { summary }| {
                let suite_summary_file =
                    File::open(path.join(summary)).expect("open suite summary file");
                serde_json::from_reader::<_, directory::SuiteResult>(suite_summary_file)
                    .expect("parse suite summary file")
            })
            .collect::<Vec<_>>();
        (run_result, suite_results)
    }

    fn assert_run_result(actual_run: &directory::TestRunResult, expected_run: &ExpectedTestRun) {
        let &directory::TestRunResult::V0 { outcome, .. } = &actual_run;
        assert_eq!(outcome, &expected_run.outcome);
    }

    fn assert_suite_results(
        actual_suites: &Vec<directory::SuiteResult>,
        expected_suites: &Vec<ExpectedSuite>,
    ) {
        assert_eq!(actual_suites.len(), expected_suites.len());
        let mut expected_suites_map = HashMap::new();
        for suite in expected_suites.iter() {
            expected_suites_map.insert(suite.name.clone(), suite);
        }
        for suite in actual_suites.iter() {
            let suite_name = match suite {
                directory::SuiteResult::V0 { name, .. } => name,
            };
            assert_suite_result(
                suite,
                expected_suites_map.get(suite_name).expect("No matching expected suite"),
            );
        }
    }

    fn assert_suite_result(actual_suite: &directory::SuiteResult, expected_suite: &ExpectedSuite) {
        let &directory::SuiteResult::V0 { outcome, name, cases } = &actual_suite;
        assert_eq!(outcome, &expected_suite.outcome);
        assert_eq!(name, &expected_suite.name);

        assert_eq!(cases.len(), expected_suite.cases.len());
        for case in cases.iter() {
            assert_case_result(case, expected_suite.cases.get(&case.name).unwrap());
        }
    }

    fn assert_case_result(
        actual_case: &directory::TestCaseResultV0,
        expected_case: &ExpectedTestCase,
    ) {
        assert_eq!(actual_case.name, expected_case.name);
        assert_eq!(actual_case.outcome, expected_case.outcome);
    }

    struct ExpectedTestRun {
        outcome: directory::Outcome,
    }

    struct ExpectedSuite {
        name: String,
        outcome: directory::Outcome,
        cases: HashMap<String, ExpectedTestCase>,
    }

    struct ExpectedTestCase {
        name: String,
        outcome: directory::Outcome,
    }

    impl ExpectedTestRun {
        fn new(outcome: directory::Outcome) -> Self {
            Self { outcome }
        }
    }

    impl ExpectedSuite {
        fn new<S: AsRef<str>>(name: S, outcome: directory::Outcome) -> Self {
            Self { name: name.as_ref().to_string(), outcome, cases: HashMap::new() }
        }

        fn with_case(mut self, case: ExpectedTestCase) -> Self {
            self.cases.insert(case.name.clone(), case);
            self
        }
    }

    impl ExpectedTestCase {
        fn new<S: AsRef<str>>(name: S, outcome: directory::Outcome) -> Self {
            Self { name: name.as_ref().to_string(), outcome }
        }
    }

    #[test]
    fn no_artifacts() {
        let dir = tempdir().expect("create temp directory");
        let run_reporter = RunReporter::new(dir.path().to_path_buf()).expect("create run reporter");
        for suite_no in 0..3 {
            let suite_reporter = run_reporter
                .new_suite(&format!("suite-{:?}", suite_no))
                .expect("create suite reporter");
            for case_no in 0..3 {
                let case_reporter = suite_reporter
                    .new_case(&format!("case-{:?}-{:?}", suite_no, case_no))
                    .expect("create suite reporter");
                case_reporter.outcome(&ReportedOutcome::Passed).expect("set case outcome");
            }
            suite_reporter.outcome(&ReportedOutcome::Failed).expect("set suite outcome");
            suite_reporter.record().expect("record suite");
        }
        run_reporter.outcome(&ReportedOutcome::Timedout).expect("set run outcome");
        run_reporter.record().expect("record run");

        // assert on directory
        let (run_result, suite_results) = parse_json_in_output(dir.path());
        assert_run_result(&run_result, &ExpectedTestRun::new(directory::Outcome::Timedout));
        assert_suite_results(
            &suite_results,
            &vec![
                ExpectedSuite::new("suite-0", directory::Outcome::Failed)
                    .with_case(ExpectedTestCase::new("case-0-0", directory::Outcome::Passed))
                    .with_case(ExpectedTestCase::new("case-0-1", directory::Outcome::Passed))
                    .with_case(ExpectedTestCase::new("case-0-2", directory::Outcome::Passed)),
                ExpectedSuite::new("suite-1", directory::Outcome::Failed)
                    .with_case(ExpectedTestCase::new("case-1-0", directory::Outcome::Passed))
                    .with_case(ExpectedTestCase::new("case-1-1", directory::Outcome::Passed))
                    .with_case(ExpectedTestCase::new("case-1-2", directory::Outcome::Passed)),
                ExpectedSuite::new("suite-2", directory::Outcome::Failed)
                    .with_case(ExpectedTestCase::new("case-2-0", directory::Outcome::Passed))
                    .with_case(ExpectedTestCase::new("case-2-1", directory::Outcome::Passed))
                    .with_case(ExpectedTestCase::new("case-2-2", directory::Outcome::Passed)),
            ],
        );
    }

    #[test]
    fn duplicate_suite_names_ok() {
        let dir = tempdir().expect("create temp directory");
        let run_reporter = RunReporter::new(dir.path().to_path_buf()).expect("create run reporter");

        let success_suite_reporter = run_reporter.new_suite("suite").expect("create new suite");
        success_suite_reporter.outcome(&ReportedOutcome::Passed).expect("report suite outcome");
        success_suite_reporter.record().expect("record suite");

        let failed_suite_reporter = run_reporter.new_suite("suite").expect("create new suite");
        failed_suite_reporter.outcome(&ReportedOutcome::Failed).expect("report suite outcome");
        failed_suite_reporter.record().expect("record suite");

        run_reporter.outcome(&ReportedOutcome::Failed).expect("report run outcome");
        run_reporter.record().expect("record run");

        let (run_result, suite_results) = parse_json_in_output(dir.path());
        assert_run_result(&run_result, &ExpectedTestRun::new(directory::Outcome::Failed));

        assert_eq!(suite_results.len(), 2);
        // names of the suites are identical, so we rely on the outcome to differentiate them.
        let expected_success_suite = ExpectedSuite::new("suite", directory::Outcome::Passed);
        let expected_failed_suite = ExpectedSuite::new("suite", directory::Outcome::Failed);

        if let directory::SuiteResult::V0 { outcome: directory::Outcome::Passed, .. } =
            suite_results[0]
        {
            assert_suite_result(&suite_results[0], &expected_success_suite);
            assert_suite_result(&suite_results[1], &expected_failed_suite);
        } else {
            assert_suite_result(&suite_results[0], &expected_failed_suite);
            assert_suite_result(&suite_results[1], &expected_success_suite);
        }
    }
}
