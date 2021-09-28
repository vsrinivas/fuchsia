// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::output::{
    ArtifactType, DynArtifact, EntityId, ReportedOutcome, Reporter, Timestamp, ZxTime,
};
use parking_lot::Mutex;
use std::collections::HashMap;
use std::fs::{DirBuilder, File};
use std::io::Error;
use std::path::{Path, PathBuf};
use test_output_directory as directory;

const STDOUT_FILE: &str = "stdout.txt";
const STDERR_FILE: &str = "stderr.txt";
const SYSLOG_FILE: &str = "syslog.txt";

/// A reporter that saves results and artifacts to disk in the Fuchsia test output format.
pub(super) struct DirectoryReporter {
    /// Root directory in which to place results.
    root: PathBuf,
    /// A mapping from ID to every test run, test suite, and test case. The test run entry
    /// is always contained in ID TEST_RUN_ID. Entries are added as new test cases and suites
    /// are found, and removed once they have been persisted.
    entries: Mutex<HashMap<EntityId, EntityEntry>>,
}

/// In-memory representation of either a test run, test suite, or test case.
struct EntityEntry {
    /// Name of the entity. Unused for a test run.
    name: String,
    /// A list of the children of an entity referenced by their id. Unused for a test case.
    children: Vec<EntityId>,
    /// Name of the artifact directory containing artifacts scoped to this entity.
    artifact_dir: PathBuf,
    /// A list of artifacts by filename.
    artifacts: Vec<(String, directory::ArtifactMetadataV0)>,
    /// Most recently known outcome for the entity.
    outcome: ReportedOutcome,
    /// The approximate UTC start time as measured by the host.
    approximate_host_start_time: Option<std::time::SystemTime>,
    /// Timer used to measure durations as the difference between monotonic timestamps on
    /// start and stop events.
    timer: MonotonicTimer,
}

enum MonotonicTimer {
    Unknown,
    /// Entity has started but not stopped.
    Started {
        /// Monotonic start timestamp reported by the target.
        mono_start_time: ZxTime,
    },
    /// Entity has completed running.
    Stopped {
        /// Runtime of the entity, calculated using timestamps reported by the target.
        mono_run_time: std::time::Duration,
    },
}

impl EntityEntry {
    /// Returns the start timestamp, if any, as milliseconds since the UNIX epoch.
    fn start_time_millis(&self) -> Option<u64> {
        self.approximate_host_start_time.map(|time| {
            time.duration_since(std::time::SystemTime::UNIX_EPOCH)
                .expect("host time after the epoch")
                .as_millis() as u64
        })
    }

    fn run_time_millis(&self) -> Option<u64> {
        match self.timer {
            MonotonicTimer::Unknown => None,
            MonotonicTimer::Started { .. } => None,
            MonotonicTimer::Stopped { mono_run_time, .. } => Some(mono_run_time.as_millis() as u64),
        }
    }
}

impl DirectoryReporter {
    /// Create a new `DirectoryReporter` that places results in the given `root` directory.
    pub(super) fn new(root: PathBuf) -> Result<Self, Error> {
        let artifact_dir = artifact_dir_name(&EntityId::TestRun);

        Self::ensure_directory_exists(root.as_path())?;

        let mut entries = HashMap::new();
        entries.insert(
            EntityId::TestRun,
            EntityEntry {
                name: "".to_string(),
                artifact_dir,
                artifacts: vec![],
                children: vec![],
                outcome: ReportedOutcome::Inconclusive,
                timer: MonotonicTimer::Unknown,
                approximate_host_start_time: None,
            },
        );
        Ok(Self { root, entries: Mutex::new(entries) })
    }

    fn ensure_directory_exists(absolute: &Path) -> Result<(), Error> {
        match absolute.exists() {
            true => Ok(()),
            false => DirBuilder::new().recursive(true).create(&absolute),
        }
    }
}

impl Reporter for DirectoryReporter {
    fn new_entity(&self, entity: &EntityId, name: &str) -> Result<(), Error> {
        let artifact_dir = artifact_dir_name(entity);

        let mut entries = self.entries.lock();
        let parent_id = match entity {
            EntityId::TestRun => panic!("Cannot create new test run"),
            EntityId::Suite(_) => EntityId::TestRun,
            EntityId::Case { suite, .. } => EntityId::Suite(*suite),
        };
        let parent = entries
            .get_mut(&parent_id)
            .expect("Attempting to create a child for an entity that does not exist");
        parent.children.push(*entity);
        entries.insert(
            *entity,
            EntityEntry {
                name: name.to_string(),
                artifact_dir,
                artifacts: vec![],
                children: vec![],
                outcome: ReportedOutcome::Inconclusive,
                timer: MonotonicTimer::Unknown,
                approximate_host_start_time: None,
            },
        );

        Ok(())
    }

    fn entity_started(&self, entity: &EntityId, timestamp: Timestamp) -> Result<(), Error> {
        let mut entries = self.entries.lock();
        let entry =
            entries.get_mut(entity).expect("Outcome reported for an entity that does not exist");
        entry.approximate_host_start_time = Some(std::time::SystemTime::now());
        match (&entry.timer, timestamp) {
            (MonotonicTimer::Unknown, Timestamp::Given(mono_start_time)) => {
                entry.timer = MonotonicTimer::Started { mono_start_time };
            }
            _ => (),
        }
        Ok(())
    }

    fn entity_stopped(
        &self,
        entity: &EntityId,
        outcome: &ReportedOutcome,
        timestamp: Timestamp,
    ) -> Result<(), Error> {
        let mut entries = self.entries.lock();
        let entry =
            entries.get_mut(entity).expect("Outcome reported for an entity that does not exist");
        entry.outcome = *outcome;

        if let (MonotonicTimer::Started { mono_start_time }, Timestamp::Given(mono_end_time)) =
            (&entry.timer, timestamp)
        {
            entry.timer = MonotonicTimer::Stopped {
                mono_run_time: mono_end_time
                    .checked_sub(*mono_start_time)
                    .expect("end time must be after start time"),
            };
        }
        Ok(())
    }

    /// Finalize and persist the outcome and artifacts for an entity. This should only be
    /// called once per entity.
    fn entity_finished(&self, entity: &EntityId) -> Result<(), Error> {
        match entity {
            EntityId::TestRun => {
                let run_entry = self
                    .entries
                    .lock()
                    .remove(&EntityId::TestRun)
                    .expect("Run entry not found, was it already recorded?");
                let serializable_run = construct_serializable_run(run_entry);
                let summary_path = self.root.join(directory::RUN_SUMMARY_NAME);
                let mut summary = File::create(summary_path)?;
                serde_json::to_writer_pretty(&mut summary, &serializable_run)?;
                summary.sync_all()
            }
            EntityId::Suite(suite_id) => {
                let mut entries = self.entries.lock();
                let suite_entry = entries
                    .remove(entity)
                    .expect("Suite entry not found, was it already recorded?");
                let case_entries = suite_entry
                    .children
                    .iter()
                    .map(|case_id| {
                        entries.remove(case_id).expect("Test case referenced by suite not found")
                    })
                    .collect();
                let serializable_suite = construct_serializable_suite(suite_entry, case_entries);
                let summary_path = self.root.join(suite_json_name(suite_id.0));
                let mut summary = File::create(summary_path)?;
                serde_json::to_writer_pretty(&mut summary, &serializable_suite)?;
                summary.sync_all()
            }
            // Cases are saved as part of suites.
            EntityId::Case { .. } => Ok(()),
        }
    }

    fn new_artifact(
        &self,
        entity: &EntityId,
        artifact_type: &ArtifactType,
    ) -> Result<Box<DynArtifact>, Error> {
        let mut lock = self.entries.lock();
        let entry = lock
            .get_mut(entity)
            .expect("Attempting to create an artifact for an entity that does not exist");
        let name = filename_for_type(artifact_type);

        let artifact_dir = self.root.join(&entry.artifact_dir);
        Self::ensure_directory_exists(&artifact_dir)?;

        let artifact = File::create(artifact_dir.join(name))?;

        entry.artifacts.push((
            name.to_string(),
            directory::ArtifactMetadataV0 {
                artifact_type: (*artifact_type).into(),
                component_moniker: None,
            },
        ));
        Ok(Box::new(artifact))
    }
}

fn artifact_dir_name(entity_id: &EntityId) -> PathBuf {
    match entity_id {
        EntityId::TestRun => "artifact-run".into(),
        EntityId::Suite(suite) => format!("artifact-{:?}", suite.0).into(),
        EntityId::Case { suite, case } => format!("artifact-{:?}-{:?}", suite.0, case.0).into(),
    }
}

fn suite_json_name(suite_id: u32) -> String {
    format!("{:?}.json", suite_id)
}

fn filename_for_type(artifact_type: &ArtifactType) -> &'static str {
    match artifact_type {
        ArtifactType::Stdout => STDOUT_FILE,
        ArtifactType::Stderr => STDERR_FILE,
        ArtifactType::Syslog => SYSLOG_FILE,
    }
}

/// Construct a serializable version of a test run.
fn construct_serializable_run(run_entry: EntityEntry) -> directory::TestRunResult {
    let duration_milliseconds = run_entry.run_time_millis();
    let start_time = run_entry.start_time_millis();
    let EntityEntry { children, artifacts, artifact_dir, outcome, .. } = run_entry;

    let suites = children
        .iter()
        .map(|suite_id| {
            let raw_id = match suite_id {
                EntityId::Suite(suite) => suite.0,
                _ => panic!("Test run child should be a suite"),
            };
            directory::SuiteEntryV0 { summary: suite_json_name(raw_id) }
        })
        .collect();
    directory::TestRunResult::V0 {
        artifacts: artifacts
            .into_iter()
            .map(|(name, metadata)| (artifact_dir.join(name), metadata))
            .collect(),
        outcome: into_serializable_outcome(outcome),
        suites,
        duration_milliseconds,
        start_time,
    }
}

/// Construct a serializable version of a test suite.
fn construct_serializable_suite(
    suite_entry: EntityEntry,
    case_entries: Vec<EntityEntry>,
) -> directory::SuiteResult {
    let cases = case_entries
        .into_iter()
        .map(|case_entry| {
            let duration_milliseconds = case_entry.run_time_millis();
            let start_time = case_entry.start_time_millis();
            let EntityEntry { artifact_dir, artifacts, outcome, name, .. } = case_entry;
            directory::TestCaseResultV0 {
                artifacts: artifacts
                    .into_iter()
                    .map(|(name, metadata)| (artifact_dir.join(name), metadata))
                    .collect(),
                outcome: into_serializable_outcome(outcome),
                duration_milliseconds,
                start_time,
                name,
            }
        })
        .collect::<Vec<_>>();
    let duration_milliseconds = suite_entry.run_time_millis();
    let start_time = suite_entry.start_time_millis();
    let EntityEntry { artifact_dir, artifacts, outcome, name, .. } = suite_entry;
    directory::SuiteResult::V0 {
        artifacts: artifacts
            .into_iter()
            .map(|(name, metadata)| (artifact_dir.join(name), metadata))
            .collect(),
        outcome: into_serializable_outcome(outcome),
        cases,
        duration_milliseconds,
        start_time,
        name,
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
    use crate::output::{CaseId, RunReporter, SuiteId};
    use tempfile::tempdir;
    use test_output_directory::testing::{
        assert_run_result, assert_suite_result, assert_suite_results, parse_json_in_output,
        ExpectedSuite, ExpectedTestCase, ExpectedTestRun,
    };

    #[test]
    fn no_artifacts() {
        let dir = tempdir().expect("create temp directory");
        const CASE_TIMES: [(ZxTime, ZxTime); 3] = [
            (ZxTime::from_nanos(0x1100000), ZxTime::from_nanos(0x2100000)),
            (ZxTime::from_nanos(0x1200000), ZxTime::from_nanos(0x2200000)),
            (ZxTime::from_nanos(0x1300000), ZxTime::from_nanos(0x2300000)),
        ];
        const SUITE_TIMES: (ZxTime, ZxTime) =
            (ZxTime::from_nanos(0x1000000), ZxTime::from_nanos(0x2400000));

        let run_reporter = RunReporter::new(dir.path().to_path_buf()).expect("create run reporter");
        for suite_no in 0..3 {
            let suite_reporter = run_reporter
                .new_suite(&format!("suite-{:?}", suite_no), &SuiteId(suite_no))
                .expect("create suite reporter");
            suite_reporter.started(Timestamp::Given(SUITE_TIMES.0)).expect("start suite");
            for case_no in 0..3 {
                let case_reporter = suite_reporter
                    .new_case(&format!("case-{:?}-{:?}", suite_no, case_no), &CaseId(case_no))
                    .expect("create suite reporter");
                case_reporter
                    .started(Timestamp::Given(CASE_TIMES[case_no as usize].0))
                    .expect("start case");
                case_reporter
                    .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
                    .expect("stop case");
            }
            suite_reporter
                .stopped(&ReportedOutcome::Failed, Timestamp::Unknown)
                .expect("set suite outcome");
            suite_reporter.finished().expect("record suite");
        }
        run_reporter
            .stopped(&ReportedOutcome::Timedout, Timestamp::Unknown)
            .expect("set run outcome");
        run_reporter.finished().expect("record run");

        // assert on directory
        let (run_result, suite_results) = parse_json_in_output(dir.path());
        assert_run_result(
            dir.path(),
            &run_result,
            &ExpectedTestRun::new(directory::Outcome::Timedout),
        );
        assert_suite_results(
            dir.path(),
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
    fn artifacts_per_entity() {
        let dir = tempdir().expect("create temp directory");
        let run_reporter = RunReporter::new(dir.path().to_path_buf()).expect("create run reporter");
        let suite_reporter =
            run_reporter.new_suite("suite-1", &SuiteId(0)).expect("create new suite");
        run_reporter.started(Timestamp::Unknown).expect("start run");
        for case_no in 0..3 {
            let case_reporter = suite_reporter
                .new_case(&format!("case-1-{:?}", case_no), &CaseId(case_no))
                .expect("create new case");
            case_reporter.started(Timestamp::Unknown).expect("start case");
            let mut artifact =
                case_reporter.new_artifact(&ArtifactType::Stdout).expect("create case artifact");
            writeln!(artifact, "stdout from case {:?}", case_no).expect("write to artifact");
            case_reporter
                .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
                .expect("report case outcome");
        }

        let mut suite_artifact =
            suite_reporter.new_artifact(&ArtifactType::Stdout).expect("create suite artifact");
        writeln!(suite_artifact, "stdout from suite").expect("write to artifact");
        suite_reporter.started(Timestamp::Unknown).expect("start suite");
        suite_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .expect("report suite outcome");
        suite_reporter.finished().expect("record suite");
        drop(suite_artifact); // want to flush contents

        let mut run_artifact =
            run_reporter.new_artifact(&ArtifactType::Stdout).expect("create run artifact");
        writeln!(run_artifact, "stdout from run").expect("write to artifact");
        run_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .expect("record run outcome");
        run_reporter.finished().expect("record run");
        drop(run_artifact); // want to flush contents

        let (run_result, suite_results) = parse_json_in_output(dir.path());
        assert_run_result(
            dir.path(),
            &run_result,
            &ExpectedTestRun::new(directory::Outcome::Passed).with_artifact(
                STDOUT_FILE,
                directory::ArtifactType::Stdout,
                "stdout from run\n",
            ),
        );
        assert_suite_results(
            dir.path(),
            &suite_results,
            &vec![ExpectedSuite::new("suite-1", directory::Outcome::Passed)
                .with_case(
                    ExpectedTestCase::new("case-1-0", directory::Outcome::Passed).with_artifact(
                        STDOUT_FILE,
                        directory::ArtifactType::Stdout,
                        "stdout from case 0\n",
                    ),
                )
                .with_case(
                    ExpectedTestCase::new("case-1-1", directory::Outcome::Passed).with_artifact(
                        STDOUT_FILE,
                        directory::ArtifactType::Stdout,
                        "stdout from case 1\n",
                    ),
                )
                .with_case(
                    ExpectedTestCase::new("case-1-2", directory::Outcome::Passed).with_artifact(
                        STDOUT_FILE,
                        directory::ArtifactType::Stdout,
                        "stdout from case 2\n",
                    ),
                )
                .with_artifact(
                    STDOUT_FILE,
                    directory::ArtifactType::Stdout,
                    "stdout from suite\n",
                )],
        );
    }

    #[test]
    fn duplicate_suite_names_ok() {
        let dir = tempdir().expect("create temp directory");
        let run_reporter = RunReporter::new(dir.path().to_path_buf()).expect("create run reporter");

        let success_suite_reporter =
            run_reporter.new_suite("suite", &SuiteId(0)).expect("create new suite");
        success_suite_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .expect("report suite outcome");
        success_suite_reporter
            .new_artifact(&ArtifactType::Stdout)
            .expect("create new artifact")
            .write_all(b"stdout from passed suite\n")
            .expect("write to artifact");
        success_suite_reporter.finished().expect("record suite");

        let failed_suite_reporter =
            run_reporter.new_suite("suite", &SuiteId(1)).expect("create new suite");
        failed_suite_reporter
            .stopped(&ReportedOutcome::Failed, Timestamp::Unknown)
            .expect("report suite outcome");
        failed_suite_reporter.finished().expect("record suite");

        run_reporter
            .stopped(&ReportedOutcome::Failed, Timestamp::Unknown)
            .expect("report run outcome");
        run_reporter.finished().expect("record run");

        let (run_result, suite_results) = parse_json_in_output(dir.path());
        assert_run_result(
            dir.path(),
            &run_result,
            &ExpectedTestRun::new(directory::Outcome::Failed),
        );

        assert_eq!(suite_results.len(), 2);
        // names of the suites are identical, so we rely on the outcome to differentiate them.
        let expected_success_suite = ExpectedSuite::new("suite", directory::Outcome::Passed)
            .with_artifact(
                STDOUT_FILE,
                directory::ArtifactType::Stdout,
                "stdout from passed suite\n",
            );
        let expected_failed_suite = ExpectedSuite::new("suite", directory::Outcome::Failed);

        if let directory::SuiteResult::V0 { outcome: directory::Outcome::Passed, .. } =
            suite_results[0]
        {
            assert_suite_result(dir.path(), &suite_results[0], &expected_success_suite);
            assert_suite_result(dir.path(), &suite_results[1], &expected_failed_suite);
        } else {
            assert_suite_result(dir.path(), &suite_results[0], &expected_failed_suite);
            assert_suite_result(dir.path(), &suite_results[1], &expected_success_suite);
        }
    }
}
