// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::output::{
    ArtifactType, DirectoryArtifactType, DirectoryWrite, DynArtifact, DynDirectoryArtifact,
    EntityId, ReportedOutcome, Reporter, Timestamp, ZxTime,
};
use anyhow::format_err;
use async_trait::async_trait;
use parking_lot::Mutex;
use std::collections::HashMap;
use std::fs::{DirBuilder, File};
use std::io::{BufWriter, Error, ErrorKind, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU32, Ordering};
use test_output_directory as directory;

const STDOUT_FILE: &str = "stdout.txt";
const STDERR_FILE: &str = "stderr.txt";
const SYSLOG_FILE: &str = "syslog.txt";
const REPORT_FILE: &str = "report.txt";
const RESTRICTED_LOG_FILE: &str = "restricted_logs.txt";
const CUSTOM_ARTIFACT_DIRECTORY: &str = "custom";
const DEBUG_ARTIFACT_DIRECTORY: &str = "debug";

const TEST_SUMMARY_TMP_FILE: &str = ".test_summary_tmp.json";

/// A reporter that saves results and artifacts to disk in the Fuchsia test output format.
pub struct DirectoryReporter {
    /// Root directory in which to place results.
    root: PathBuf,
    /// A mapping from ID to every test run, test suite, and test case. The test run entry
    /// is always contained in ID TEST_RUN_ID. Entries are added as new test cases and suites
    /// are found, and removed once they have been persisted.
    entries: Mutex<HashMap<EntityId, EntityEntry>>,
    /// Atomic counter used to generate unique names for custom artifact directories.
    name_counter: AtomicU32,
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
    outcome: directory::Outcome,
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
    pub fn new(root: PathBuf) -> Result<Self, Error> {
        let artifact_dir = artifact_dir_name(&EntityId::TestRun);

        ensure_directory_exists(root.as_path())?;

        let mut entries = HashMap::new();
        entries.insert(
            EntityId::TestRun,
            EntityEntry {
                name: "".to_string(),
                artifact_dir,
                artifacts: vec![],
                children: vec![],
                outcome: directory::Outcome::NotStarted,
                timer: MonotonicTimer::Unknown,
                approximate_host_start_time: None,
            },
        );
        let new_self = Self { root, entries: Mutex::new(entries), name_counter: AtomicU32::new(0) };
        new_self.persist_run_summary()?;
        Ok(new_self)
    }

    pub(super) fn add_report(&self, entity: &EntityId) -> Result<BufWriter<File>, Error> {
        self.new_artifact_inner(entity, directory::ArtifactType::Report)
    }

    fn persist_run_summary(&self) -> Result<(), Error> {
        let entry_lock = self.entries.lock();
        let run_entry =
            entry_lock.get(&EntityId::TestRun).expect("Run entry should always be present");
        // When saving a run summary, only save references to suite summaries that have already
        // been saved. This ensures that run_summary.json doesn't contain dangling references.
        // Here, since we clean up memory for suites that have already been saved to disk, the
        // set of ready suites is the subset of children of the TestRun that no longer exist in
        // the entry map.
        // TODO(satsukiu): consider a more explicit way of expressing these states.
        let ready_suites: Vec<_> = run_entry
            .children
            .iter()
            .filter(|entity_id| !entry_lock.contains_key(entity_id))
            .cloned()
            .collect();
        let serializable_run = construct_serializable_run(run_entry, ready_suites.as_slice());
        // Save to a temp file first then rename. This ensures we at least
        // have the old version if writing the new version fails.
        let tmp_path = self.root.join(TEST_SUMMARY_TMP_FILE);
        let mut summary_file = BufWriter::new(File::create(&tmp_path)?);
        serde_json::to_writer_pretty(&mut summary_file, &serializable_run)?;
        summary_file.flush()?;
        let final_path = self.root.join(directory::RUN_SUMMARY_NAME);
        let res = std::fs::rename(tmp_path, final_path);
        res
    }

    fn new_artifact_inner(
        &self,
        entity: &EntityId,
        artifact_type: directory::ArtifactType,
    ) -> Result<BufWriter<File>, Error> {
        let mut lock = self.entries.lock();
        let entry = lock
            .get_mut(entity)
            .expect("Attempting to create an artifact for an entity that does not exist");
        let name = filename_for_type(&artifact_type);

        let artifact_dir = self.root.join(&entry.artifact_dir);
        ensure_directory_exists(&artifact_dir)?;

        let artifact = BufWriter::new(File::create(artifact_dir.join(name))?);

        entry.artifacts.push((
            name.to_string(),
            directory::ArtifactMetadataV0 { artifact_type, component_moniker: None },
        ));
        Ok(artifact)
    }
}

#[async_trait]
impl Reporter for DirectoryReporter {
    async fn new_entity(&self, entity: &EntityId, name: &str) -> Result<(), Error> {
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
                outcome: directory::Outcome::NotStarted,
                timer: MonotonicTimer::Unknown,
                approximate_host_start_time: None,
            },
        );

        Ok(())
    }

    async fn entity_started(&self, entity: &EntityId, timestamp: Timestamp) -> Result<(), Error> {
        let mut entries = self.entries.lock();
        let entry =
            entries.get_mut(entity).expect("Outcome reported for an entity that does not exist");
        entry.approximate_host_start_time = Some(std::time::SystemTime::now());
        entry.outcome = directory::Outcome::Inconclusive;
        match (&entry.timer, timestamp) {
            (MonotonicTimer::Unknown, Timestamp::Given(mono_start_time)) => {
                entry.timer = MonotonicTimer::Started { mono_start_time };
            }
            _ => (),
        }
        Ok(())
    }

    async fn entity_stopped(
        &self,
        entity: &EntityId,
        outcome: &ReportedOutcome,
        timestamp: Timestamp,
    ) -> Result<(), Error> {
        let mut entries = self.entries.lock();
        let entry =
            entries.get_mut(entity).expect("Outcome reported for an entity that does not exist");
        entry.outcome = into_serializable_outcome(*outcome);

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
    async fn entity_finished(&self, entity: &EntityId) -> Result<(), Error> {
        match entity {
            EntityId::TestRun => self.persist_run_summary(),
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
                match persist_suite_summary(&summary_path, serializable_suite) {
                    Ok(()) => {
                        drop(entries); // drop lock as persist_run_summary acquires it again.
                        self.persist_run_summary()
                    }
                    Err(e) => {
                        // If saving suite_summary fails, remove the reference to it from the run
                        // entry. This way, if saving the run summary succeeds, it won't point to
                        // a file that doesn't exist. We'll still return an error since this
                        // indicates only a partial result as been recorded.
                        entries
                            .get_mut(&EntityId::TestRun)
                            .expect("Run entry should always exist")
                            .children
                            .retain(|entity_id| *entity_id != EntityId::Suite(*suite_id));
                        Err(e)
                    }
                }
            }
            // Cases are saved as part of suites.
            EntityId::Case { .. } => Ok(()),
        }
    }

    async fn new_artifact(
        &self,
        entity: &EntityId,
        artifact_type: &ArtifactType,
    ) -> Result<Box<DynArtifact>, Error> {
        let file = self.new_artifact_inner(entity, (*artifact_type).into())?;
        Ok(Box::new(file))
    }

    async fn new_directory_artifact(
        &self,
        entity: &EntityId,
        artifact_type: &DirectoryArtifactType,
        component_moniker: Option<String>,
    ) -> Result<Box<DynDirectoryArtifact>, Error> {
        let mut lock = self.entries.lock();
        let entry = lock
            .get_mut(entity)
            .expect("Attempting to create an artifact for an entity that does not exist");
        let name = format!(
            "{}-{}",
            prefix_for_directory_type(artifact_type),
            self.name_counter.fetch_add(1, Ordering::Relaxed),
        );
        let artifact_dir = self.root.join(&entry.artifact_dir).join(&name);
        ensure_directory_exists(&artifact_dir)?;

        entry.artifacts.push((
            name,
            directory::ArtifactMetadataV0 {
                artifact_type: (*artifact_type).into(),
                component_moniker,
            },
        ));

        Ok(Box::new(DirectoryDirectoryWriter { path: artifact_dir.into() }))
    }
}

/// A |DirectoryWrite| implementation that creates files in a set directory.
struct DirectoryDirectoryWriter {
    path: PathBuf,
}

impl DirectoryWrite for DirectoryDirectoryWriter {
    fn new_file(&self, path: &Path) -> Result<Box<DynArtifact>, Error> {
        let new_path = self.path.join(path);
        // The path must be relative to the parent directory (no absolute paths) and cannot have
        // any parent components (which can escape the parent directory).
        if !new_path.starts_with(&self.path)
            || new_path.components().any(|c| match c {
                std::path::Component::ParentDir => true,
                _ => false,
            })
        {
            return Err(Error::new(
                ErrorKind::Other,
                format_err!(
                    "Path {:?} results in destination {:?} that may be outside of {:?}",
                    path,
                    new_path,
                    self.path
                ),
            ));
        }
        if let Some(parent) = new_path.parent() {
            ensure_directory_exists(parent)?;
        }

        let file = BufWriter::new(File::create(new_path)?);
        Ok(Box::new(file))
    }
}

fn persist_suite_summary(path: &Path, suite_result: directory::SuiteResult) -> Result<(), Error> {
    let mut summary = BufWriter::new(File::create(path)?);
    serde_json::to_writer_pretty(&mut summary, &suite_result)?;
    summary.flush()
}

fn ensure_directory_exists(dir: &Path) -> Result<(), Error> {
    match dir.exists() {
        true => Ok(()),
        false => DirBuilder::new().recursive(true).create(&dir),
    }
}

fn artifact_dir_name(entity_id: &EntityId) -> PathBuf {
    match entity_id {
        EntityId::TestRun => "artifact-run".into(),
        EntityId::Suite(suite) => format!("artifact-{:?}", suite.0).into(),
        EntityId::Case { suite, case } => format!("artifact-{:?}-{:?}", suite.0, case.0).into(),
    }
}

fn prefix_for_directory_type(artifact_type: &DirectoryArtifactType) -> &'static str {
    match artifact_type {
        DirectoryArtifactType::Custom => CUSTOM_ARTIFACT_DIRECTORY,
        DirectoryArtifactType::Debug => DEBUG_ARTIFACT_DIRECTORY,
    }
}

fn suite_json_name(suite_id: u32) -> String {
    format!("{:?}.json", suite_id)
}

fn filename_for_type(artifact_type: &directory::ArtifactType) -> &'static str {
    match artifact_type {
        directory::ArtifactType::Stdout => STDOUT_FILE,
        directory::ArtifactType::Stderr => STDERR_FILE,
        directory::ArtifactType::Syslog => SYSLOG_FILE,
        directory::ArtifactType::RestrictedLog => RESTRICTED_LOG_FILE,
        directory::ArtifactType::Report => REPORT_FILE,
        directory::ArtifactType::Custom => unreachable!("Custom artifact is not a file"),
        directory::ArtifactType::Debug => {
            unreachable!("Debug artifacts must be placed in a directory")
        }
    }
}

/// Construct a serializable version of a test run.
fn construct_serializable_run(
    run_entry: &EntityEntry,
    ready_suites: &[EntityId],
) -> directory::TestRunResult {
    let duration_milliseconds = run_entry.run_time_millis();
    let start_time = run_entry.start_time_millis();

    let suites = ready_suites
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
        artifacts: run_entry
            .artifacts
            .iter()
            .map(|(name, metadata)| (Path::new(&name).to_path_buf(), metadata.clone()))
            .collect(),
        artifact_dir: run_entry.artifact_dir.clone(),
        outcome: run_entry.outcome,
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
                    .map(|(name, metadata)| (name.into(), metadata.clone()))
                    .collect(),
                artifact_dir,
                outcome,
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
            .map(|(name, metadata)| (name.into(), metadata.clone()))
            .collect(),
        artifact_dir,
        outcome,
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
        ReportedOutcome::Cancelled => directory::Outcome::Inconclusive,
        ReportedOutcome::DidNotFinish => directory::Outcome::Inconclusive,
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::output::{CaseId, RunReporter, SuiteId};
    use tempfile::tempdir;
    use test_output_directory::testing::{
        assert_run_result, assert_suite_result, assert_suite_results, parse_json_in_output,
        ExpectedDirectory, ExpectedSuite, ExpectedTestCase, ExpectedTestRun,
    };

    #[fuchsia::test]
    async fn no_artifacts() {
        let dir = tempdir().expect("create temp directory");
        const CASE_TIMES: [(ZxTime, ZxTime); 3] = [
            (ZxTime::from_nanos(0x1100000), ZxTime::from_nanos(0x2100000)),
            (ZxTime::from_nanos(0x1200000), ZxTime::from_nanos(0x2200000)),
            (ZxTime::from_nanos(0x1300000), ZxTime::from_nanos(0x2300000)),
        ];
        const SUITE_TIMES: (ZxTime, ZxTime) =
            (ZxTime::from_nanos(0x1000000), ZxTime::from_nanos(0x2400000));

        let run_reporter = RunReporter::new(
            DirectoryReporter::new(dir.path().to_path_buf()).expect("create run reporter"),
        );
        for suite_no in 0..3 {
            let suite_reporter = run_reporter
                .new_suite(&format!("suite-{:?}", suite_no), &SuiteId(suite_no))
                .await
                .expect("create suite reporter");
            suite_reporter.started(Timestamp::Given(SUITE_TIMES.0)).await.expect("start suite");
            for case_no in 0..3 {
                let case_reporter = suite_reporter
                    .new_case(&format!("case-{:?}-{:?}", suite_no, case_no), &CaseId(case_no))
                    .await
                    .expect("create suite reporter");
                case_reporter
                    .started(Timestamp::Given(CASE_TIMES[case_no as usize].0))
                    .await
                    .expect("start case");
                case_reporter
                    .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
                    .await
                    .expect("stop case");
            }
            suite_reporter
                .stopped(&ReportedOutcome::Failed, Timestamp::Unknown)
                .await
                .expect("set suite outcome");
            suite_reporter.finished().await.expect("record suite");
        }
        run_reporter
            .stopped(&ReportedOutcome::Timedout, Timestamp::Unknown)
            .await
            .expect("set run outcome");
        run_reporter.finished().await.expect("record run");

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

    #[fuchsia::test]
    async fn artifacts_per_entity() {
        let dir = tempdir().expect("create temp directory");
        let run_reporter = RunReporter::new(
            DirectoryReporter::new(dir.path().to_path_buf()).expect("create run reporter"),
        );
        let suite_reporter =
            run_reporter.new_suite("suite-1", &SuiteId(0)).await.expect("create new suite");
        run_reporter.started(Timestamp::Unknown).await.expect("start run");
        for case_no in 0..3 {
            let case_reporter = suite_reporter
                .new_case(&format!("case-1-{:?}", case_no), &CaseId(case_no))
                .await
                .expect("create new case");
            case_reporter.started(Timestamp::Unknown).await.expect("start case");
            let mut artifact = case_reporter
                .new_artifact(&ArtifactType::Stdout)
                .await
                .expect("create case artifact");
            writeln!(artifact, "stdout from case {:?}", case_no).expect("write to artifact");
            case_reporter
                .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
                .await
                .expect("report case outcome");
        }

        let mut suite_artifact = suite_reporter
            .new_artifact(&ArtifactType::Stdout)
            .await
            .expect("create suite artifact");
        writeln!(suite_artifact, "stdout from suite").expect("write to artifact");
        suite_reporter.started(Timestamp::Unknown).await.expect("start suite");
        suite_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("report suite outcome");
        suite_reporter.finished().await.expect("record suite");
        drop(suite_artifact); // want to flush contents

        let mut run_artifact =
            run_reporter.new_artifact(&ArtifactType::Stdout).await.expect("create run artifact");
        writeln!(run_artifact, "stdout from run").expect("write to artifact");
        run_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("record run outcome");
        run_reporter.finished().await.expect("record run");
        drop(run_artifact); // want to flush contents

        let (run_result, suite_results) = parse_json_in_output(dir.path());
        assert_run_result(
            dir.path(),
            &run_result,
            &ExpectedTestRun::new(directory::Outcome::Passed).with_artifact(
                directory::ArtifactType::Stdout,
                STDOUT_FILE.into(),
                "stdout from run\n",
            ),
        );
        assert_suite_results(
            dir.path(),
            &suite_results,
            &vec![ExpectedSuite::new("suite-1", directory::Outcome::Passed)
                .with_case(
                    ExpectedTestCase::new("case-1-0", directory::Outcome::Passed).with_artifact(
                        directory::ArtifactType::Stdout,
                        STDOUT_FILE.into(),
                        "stdout from case 0\n",
                    ),
                )
                .with_case(
                    ExpectedTestCase::new("case-1-1", directory::Outcome::Passed).with_artifact(
                        directory::ArtifactType::Stdout,
                        STDOUT_FILE.into(),
                        "stdout from case 1\n",
                    ),
                )
                .with_case(
                    ExpectedTestCase::new("case-1-2", directory::Outcome::Passed).with_artifact(
                        directory::ArtifactType::Stdout,
                        STDOUT_FILE.into(),
                        "stdout from case 2\n",
                    ),
                )
                .with_artifact(
                    directory::ArtifactType::Stdout,
                    STDOUT_FILE.into(),
                    "stdout from suite\n",
                )],
        );
    }

    #[fuchsia::test]
    async fn empty_directory_artifacts() {
        let dir = tempdir().expect("create temp directory");

        let run_reporter = RunReporter::new(
            DirectoryReporter::new(dir.path().to_path_buf()).expect("create run reporter"),
        );
        run_reporter.started(Timestamp::Unknown).await.expect("start run");
        let _run_directory_artifact = run_reporter
            .new_directory_artifact(&DirectoryArtifactType::Custom, None)
            .await
            .expect("Create run directory artifact");

        let suite_reporter =
            run_reporter.new_suite("suite-1", &SuiteId(0)).await.expect("create new suite");
        suite_reporter.started(Timestamp::Unknown).await.expect("start suite");
        let _suite_directory_artifact = suite_reporter
            .new_directory_artifact(&DirectoryArtifactType::Custom, Some("suite-moniker".into()))
            .await
            .expect("create suite directory artifact");

        let case_reporter =
            suite_reporter.new_case("case-1-1", &CaseId(1)).await.expect("create new case");
        case_reporter.started(Timestamp::Unknown).await.expect("start case");
        let _case_directory_artifact = case_reporter
            .new_directory_artifact(&DirectoryArtifactType::Custom, None)
            .await
            .expect("create suite directory artifact");
        case_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("report case outcome");
        case_reporter.finished().await.expect("Case finished");

        suite_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("report suite outcome");
        suite_reporter.finished().await.expect("record suite");

        run_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("record run outcome");
        run_reporter.finished().await.expect("record run");

        let (run_result, suite_results) = parse_json_in_output(dir.path());
        assert_run_result(
            dir.path(),
            &run_result,
            &ExpectedTestRun::new(directory::Outcome::Passed).with_directory_artifact(
                directory::ArtifactType::Custom,
                Option::<&str>::None,
                ExpectedDirectory::new(),
            ),
        );
        assert_suite_results(
            dir.path(),
            &suite_results,
            &vec![ExpectedSuite::new("suite-1", directory::Outcome::Passed)
                .with_directory_artifact(
                    directory::ArtifactMetadataV0 {
                        artifact_type: directory::ArtifactType::Custom,
                        component_moniker: Some("suite-moniker".into()),
                    },
                    Option::<&str>::None,
                    ExpectedDirectory::new(),
                )
                .with_case(
                    ExpectedTestCase::new("case-1-1", directory::Outcome::Passed)
                        .with_directory_artifact(
                            directory::ArtifactType::Custom,
                            Option::<&str>::None,
                            ExpectedDirectory::new(),
                        ),
                )],
        );
    }

    #[fuchsia::test]
    async fn directory_artifacts() {
        let dir = tempdir().expect("create temp directory");

        let run_reporter = RunReporter::new(
            DirectoryReporter::new(dir.path().to_path_buf()).expect("create run reporter"),
        );
        run_reporter.started(Timestamp::Unknown).await.expect("start run");
        let run_directory_artifact = run_reporter
            .new_directory_artifact(&DirectoryArtifactType::Custom, None)
            .await
            .expect("Create run directory artifact");
        let mut run_artifact_file = run_directory_artifact
            .new_file("run-artifact".as_ref())
            .expect("Create file in run directory artifact");
        writeln!(run_artifact_file, "run artifact content").expect("write to run artifact");
        drop(run_artifact_file); // force flushing

        let suite_reporter =
            run_reporter.new_suite("suite-1", &SuiteId(0)).await.expect("create new suite");
        suite_reporter.started(Timestamp::Unknown).await.expect("start suite");
        let suite_directory_artifact = suite_reporter
            .new_directory_artifact(&DirectoryArtifactType::Custom, Some("suite-moniker".into()))
            .await
            .expect("create suite directory artifact");
        let mut suite_artifact_file = suite_directory_artifact
            .new_file("suite-artifact".as_ref())
            .expect("Create file in suite directory artifact");
        writeln!(suite_artifact_file, "suite artifact content").expect("write to suite artifact");
        drop(suite_artifact_file); // force flushing

        let case_reporter =
            suite_reporter.new_case("case-1-1", &CaseId(1)).await.expect("create new case");
        case_reporter.started(Timestamp::Unknown).await.expect("start case");
        let case_directory_artifact = case_reporter
            .new_directory_artifact(&DirectoryArtifactType::Custom, None)
            .await
            .expect("create suite directory artifact");
        let mut case_artifact_file = case_directory_artifact
            .new_file("case-artifact".as_ref())
            .expect("Create file in case directory artifact");
        writeln!(case_artifact_file, "case artifact content").expect("write to case artifact");
        drop(case_artifact_file); // force flushing
        case_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("report case outcome");
        case_reporter.finished().await.expect("Case finished");

        suite_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("report suite outcome");
        suite_reporter.finished().await.expect("record suite");

        run_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("record run outcome");
        run_reporter.finished().await.expect("record run");

        let (run_result, suite_results) = parse_json_in_output(dir.path());
        assert_run_result(
            dir.path(),
            &run_result,
            &ExpectedTestRun::new(directory::Outcome::Passed).with_directory_artifact(
                directory::ArtifactType::Custom,
                Option::<&str>::None,
                ExpectedDirectory::new().with_file("run-artifact", "run artifact content\n"),
            ),
        );
        assert_suite_results(
            dir.path(),
            &suite_results,
            &vec![ExpectedSuite::new("suite-1", directory::Outcome::Passed)
                .with_directory_artifact(
                    directory::ArtifactMetadataV0 {
                        artifact_type: directory::ArtifactType::Custom,
                        component_moniker: Some("suite-moniker".into()),
                    },
                    Option::<&str>::None,
                    ExpectedDirectory::new()
                        .with_file("suite-artifact", "suite artifact content\n"),
                )
                .with_case(
                    ExpectedTestCase::new("case-1-1", directory::Outcome::Passed)
                        .with_directory_artifact(
                            directory::ArtifactType::Custom,
                            Option::<&str>::None,
                            ExpectedDirectory::new()
                                .with_file("case-artifact", "case artifact content\n"),
                        ),
                )],
        );
    }

    #[fuchsia::test]
    async fn ensure_paths_cannot_escape_directory() {
        let dir = tempdir().expect("create temp directory");

        let run_reporter = RunReporter::new(
            DirectoryReporter::new(dir.path().to_path_buf()).expect("create run reporter"),
        );

        let directory = run_reporter
            .new_directory_artifact(&DirectoryArtifactType::Custom, None)
            .await
            .expect("make custom directory");
        assert!(directory.new_file("file.txt".as_ref()).is_ok());

        assert!(directory.new_file("this/is/a/file/path.txt".as_ref()).is_ok());

        assert!(directory.new_file("../file.txt".as_ref()).is_err());
        assert!(directory.new_file("/file.txt".as_ref()).is_err());
        assert!(directory.new_file("../this/is/a/file/path.txt".as_ref()).is_err());
        assert!(directory.new_file("/this/is/a/file/path.txt".as_ref()).is_err());
        assert!(directory.new_file("/../file.txt".as_ref()).is_err());
        assert!(directory.new_file("fail/../../file.txt".as_ref()).is_err());
    }

    #[fuchsia::test]
    async fn duplicate_suite_names_ok() {
        let dir = tempdir().expect("create temp directory");
        let run_reporter = RunReporter::new(
            DirectoryReporter::new(dir.path().to_path_buf()).expect("create run reporter"),
        );

        let success_suite_reporter =
            run_reporter.new_suite("suite", &SuiteId(0)).await.expect("create new suite");
        success_suite_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("report suite outcome");
        success_suite_reporter
            .new_artifact(&ArtifactType::Stdout)
            .await
            .expect("create new artifact")
            .write_all(b"stdout from passed suite\n")
            .expect("write to artifact");
        success_suite_reporter.finished().await.expect("record suite");

        let failed_suite_reporter =
            run_reporter.new_suite("suite", &SuiteId(1)).await.expect("create new suite");
        failed_suite_reporter
            .stopped(&ReportedOutcome::Failed, Timestamp::Unknown)
            .await
            .expect("report suite outcome");
        failed_suite_reporter.finished().await.expect("record suite");

        run_reporter
            .stopped(&ReportedOutcome::Failed, Timestamp::Unknown)
            .await
            .expect("report run outcome");
        run_reporter.finished().await.expect("record run");

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
                directory::ArtifactType::Stdout,
                STDOUT_FILE.into(),
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

    #[fuchsia::test]
    async fn intermediate_results_persisted() {
        // This test verifies that the results of the test run are persisted after each test suite
        // finishes. This allows intermediate results to be read even if the command is killed
        // before completion.
        let dir = tempdir().expect("create temp directory");
        let run_reporter = RunReporter::new(
            DirectoryReporter::new(dir.path().to_path_buf()).expect("create run reporter"),
        );

        let (initial_run_result, initial_suite_results) = parse_json_in_output(dir.path());
        assert_run_result(
            dir.path(),
            &initial_run_result,
            &ExpectedTestRun::new(directory::Outcome::NotStarted),
        );
        assert!(initial_suite_results.is_empty());

        run_reporter.started(Timestamp::Unknown).await.expect("start test run");

        let suite_reporter =
            run_reporter.new_suite("suite", &SuiteId(0)).await.expect("create new suite");
        suite_reporter.started(Timestamp::Unknown).await.expect("start suite");
        suite_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("stop suite");
        suite_reporter.finished().await.expect("finish suite");

        let (intermediate_run_result, intermediate_suite_results) =
            parse_json_in_output(dir.path());
        assert_run_result(
            dir.path(),
            &intermediate_run_result,
            &ExpectedTestRun::new(directory::Outcome::Inconclusive),
        );
        assert_suite_results(
            dir.path(),
            &intermediate_suite_results,
            &vec![ExpectedSuite::new("suite", directory::Outcome::Passed)],
        );

        run_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("stop test run");
        run_reporter.finished().await.expect("finish test run");

        let (final_run_result, final_suite_results) = parse_json_in_output(dir.path());
        assert_run_result(
            dir.path(),
            &final_run_result,
            &ExpectedTestRun::new(directory::Outcome::Passed),
        );
        assert_suite_results(
            dir.path(),
            &final_suite_results,
            &vec![ExpectedSuite::new("suite", directory::Outcome::Passed)],
        );
    }

    #[fuchsia::test]
    async fn early_finish_ok() {
        // This test verifies that a suite is saved if finished() is called before an outcome is
        // reported. This could happen if some error causes test execution to terminate early.
        let dir = tempdir().expect("create temp directory");
        let run_reporter = RunReporter::new(
            DirectoryReporter::new(dir.path().to_path_buf()).expect("create run reporter"),
        );

        run_reporter.started(Timestamp::Unknown).await.expect("start test run");

        // Add a suite and case that start, but don't stop.
        let suite_reporter =
            run_reporter.new_suite("suite", &SuiteId(0)).await.expect("create new suite");
        suite_reporter.started(Timestamp::Unknown).await.expect("start suite");
        let case_reporter = suite_reporter.new_case("case", &CaseId(0)).await.expect("create case");
        case_reporter.started(Timestamp::Unknown).await.expect("start case");
        // finish run without reporting result
        case_reporter.finished().await.expect("finish case");
        suite_reporter.finished().await.expect("finish suite");

        // Add a suite that doesn't start.
        let no_start_suite_reporter =
            run_reporter.new_suite("no-start-suite", &SuiteId(1)).await.expect("create new suite");
        no_start_suite_reporter.finished().await.expect("finish suite");

        run_reporter.finished().await.expect("finish test run");

        let (run_result, suite_results) = parse_json_in_output(dir.path());
        assert_run_result(
            dir.path(),
            &run_result,
            &ExpectedTestRun::new(directory::Outcome::Inconclusive),
        );
        assert_suite_results(
            dir.path(),
            &suite_results,
            &vec![
                ExpectedSuite::new("suite", directory::Outcome::Inconclusive)
                    .with_case(ExpectedTestCase::new("case", directory::Outcome::Inconclusive)),
                ExpectedSuite::new("no-start-suite", directory::Outcome::NotStarted),
            ],
        );
    }

    fn get_suite_entries(test_result: &directory::TestRunResult) -> &[directory::SuiteEntryV0] {
        match test_result {
            directory::TestRunResult::V0 { suites, .. } => suites.as_slice(),
        }
    }

    #[fuchsia::test]
    async fn intermediate_results_dont_reference_unsaved_suites() {
        // This test verifies that intermediate results in run_summary.json do not reference
        // suites that haven't been saved yet.
        let dir = tempdir().expect("create temp directory");
        let run_reporter = RunReporter::new(
            DirectoryReporter::new(dir.path().to_path_buf()).expect("create run reporter"),
        );
        run_reporter.started(Timestamp::Unknown).await.expect("start test run");

        let suite_reporter_0 =
            run_reporter.new_suite("suite-0", &SuiteId(0)).await.expect("create new suite");
        let suite_reporter_1 =
            run_reporter.new_suite("suite-1", &SuiteId(1)).await.expect("create new suite");

        let (initial_run_result, initial_suite_results) = parse_json_in_output(dir.path());
        assert!(get_suite_entries(&initial_run_result).is_empty());
        assert_run_result(
            dir.path(),
            &initial_run_result,
            &ExpectedTestRun::new(directory::Outcome::NotStarted),
        );
        assert!(initial_suite_results.is_empty());

        // creating artifacts doesn't record the suite, so it shouldn't affect what's recorded
        // in the json summaries.
        let mut artifact =
            suite_reporter_0.new_artifact(&ArtifactType::Stderr).await.expect("create artifact");
        writeln!(artifact, "contents").expect("write to artifact");
        drop(artifact);
        let (intermediate_run_result, intermediate_suite_results) =
            parse_json_in_output(dir.path());
        assert!(get_suite_entries(&intermediate_run_result).is_empty());
        assert_run_result(
            dir.path(),
            &intermediate_run_result,
            &ExpectedTestRun::new(directory::Outcome::NotStarted),
        );
        assert!(intermediate_suite_results.is_empty());

        // after recording one suite, the other suite should not be referenced
        suite_reporter_0.finished().await.expect("finish suite");
        let (intermediate_run_result, intermediate_suite_results) =
            parse_json_in_output(dir.path());
        assert_eq!(get_suite_entries(&intermediate_run_result).len(), 1);
        assert_run_result(
            dir.path(),
            &intermediate_run_result,
            &ExpectedTestRun::new(directory::Outcome::Inconclusive),
        );
        assert_suite_results(
            dir.path(),
            &intermediate_suite_results,
            &vec![ExpectedSuite::new("suite-0", directory::Outcome::NotStarted)],
        );

        suite_reporter_1.finished().await.expect("finish suite");
        run_reporter.finished().await.expect("finish run");
        let (final_run_result, final_suite_results) = parse_json_in_output(dir.path());
        assert_run_result(
            dir.path(),
            &final_run_result,
            &ExpectedTestRun::new(directory::Outcome::Inconclusive),
        );
        assert_suite_results(
            dir.path(),
            &final_suite_results,
            &vec![
                ExpectedSuite::new("suite-0", directory::Outcome::NotStarted),
                ExpectedSuite::new("suite-1", directory::Outcome::NotStarted),
            ],
        );
    }

    #[fuchsia::test]
    async fn drop_suites_that_fail_to_save() {
        // This test verifies that when a suite fails to save, it is not referenced in
        // run_summary.json, which is needed to keep the output self conistent.
        let dir = tempdir().expect("create temp directory");
        let run_reporter = RunReporter::new(
            DirectoryReporter::new(dir.path().to_path_buf()).expect("create run reporter"),
        );

        run_reporter.started(Timestamp::Unknown).await.expect("start test run");

        // Add a suite, and make it fail to persist.
        let failing_suite_reporter =
            run_reporter.new_suite("failing", &SuiteId(0)).await.expect("create new suite");
        failing_suite_reporter.started(Timestamp::Unknown).await.expect("start suite");
        failing_suite_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("stop suite");
        // induce the failure by creating a directory where the suite summary should go.
        std::fs::create_dir(dir.path().join(suite_json_name(0))).expect("create dir");
        assert!(
            failing_suite_reporter.finished().await.is_err(),
            "persisting suite reults should fail"
        );

        // Add a suite that succeeds.
        let suite_reporter =
            run_reporter.new_suite("suite", &SuiteId(1)).await.expect("create new suite");
        suite_reporter.started(Timestamp::Unknown).await.expect("start suite");
        suite_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("start suite");
        suite_reporter.finished().await.expect("finish suite");

        run_reporter.stopped(&ReportedOutcome::Passed, Timestamp::Unknown).await.expect("stop run");
        run_reporter.finished().await.expect("finish test run");

        // Only json files for suites we successfully saved should be referenced in the
        // run summary.
        let (run_result, suite_results) = parse_json_in_output(dir.path());
        match &run_result {
            directory::TestRunResult::V0 { suites, .. } => assert_eq!(suites.len(), 1),
        }
        assert_run_result(
            dir.path(),
            &run_result,
            &ExpectedTestRun::new(directory::Outcome::Passed),
        );
        assert_suite_results(
            dir.path(),
            &suite_results,
            &vec![ExpectedSuite::new("suite", directory::Outcome::Passed)],
        );
    }
}
