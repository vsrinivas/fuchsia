// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::output::{
    ArtifactType, DirectoryArtifactType, DirectoryWrite, DynArtifact, DynDirectoryArtifact,
    EntityId, EntityInfo, ReportedOutcome, Reporter, Timestamp, ZxTime,
};
use anyhow::format_err;
use async_trait::async_trait;
use parking_lot::Mutex;
use std::borrow::Cow;
use std::collections::HashMap;
use std::fs::{DirBuilder, File};
use std::io::{BufWriter, Error, ErrorKind};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU32, Ordering};
use test_list::TestTag;
use test_output_directory as directory;

pub use directory::SchemaVersion;

const STDOUT_FILE: &str = "stdout.txt";
const STDERR_FILE: &str = "stderr.txt";
const SYSLOG_FILE: &str = "syslog.txt";
const REPORT_FILE: &str = "report.txt";
const RESTRICTED_LOG_FILE: &str = "restricted_logs.txt";
const CUSTOM_ARTIFACT_DIRECTORY: &str = "custom";
const DEBUG_ARTIFACT_DIRECTORY: &str = "debug";

const SAVE_AFTER_SUITE_COUNT: u32 = 10;

/// A reporter that saves results and artifacts to disk in the Fuchsia test output format.
pub struct DirectoryReporter {
    /// Root directory in which to place results.
    output_directory: directory::OutputDirectoryBuilder,
    /// A mapping from ID to every test run, test suite, and test case. The test run entry
    /// is always contained in ID TEST_RUN_ID. Entries are added as new test cases and suites
    /// are found, and removed once they have been persisted.
    entries: Mutex<HashMap<EntityId, EntityEntry>>,
    /// Atomic counter used to generate unique names for custom artifact directories.
    name_counter: AtomicU32,
    /// Counter for number of completed suites. Used to save complete results after
    /// SAVE_AFTER_SUITE_COUNT suites complete.
    suites_finished_counter: AtomicU32,
}

/// In-memory representation of either a test run, test suite, or test case.
struct EntityEntry {
    common: directory::CommonResult,
    /// A list of the children of an entity referenced by their id. Unused for a test case.
    children: Vec<EntityId>,
    /// Timer used to measure durations as the difference between monotonic timestamps on
    /// start and stop events.
    timer: MonotonicTimer,
    tags: Option<Vec<TestTag>>,
}

enum MonotonicTimer {
    Unknown,
    /// Entity has started but not stopped.
    Started {
        /// Monotonic start timestamp reported by the target.
        mono_start_time: ZxTime,
    },
    /// Entity has completed running.
    Stopped,
}

impl DirectoryReporter {
    /// Create a new `DirectoryReporter` that places results in the given `root` directory.
    pub fn new(root: PathBuf, schema_version: SchemaVersion) -> Result<Self, Error> {
        let output_directory = directory::OutputDirectoryBuilder::new(root, schema_version)?;

        let mut entries = HashMap::new();
        entries.insert(
            EntityId::TestRun,
            EntityEntry {
                common: directory::CommonResult {
                    name: "".to_string(),
                    artifact_dir: output_directory.new_artifact_dir()?,
                    outcome: directory::Outcome::NotStarted.into(),
                    start_time: None,
                    duration_milliseconds: None,
                },
                children: vec![],
                timer: MonotonicTimer::Unknown,
                tags: None,
            },
        );
        let new_self = Self {
            output_directory,
            entries: Mutex::new(entries),
            name_counter: AtomicU32::new(0),
            suites_finished_counter: AtomicU32::new(1),
        };
        new_self.persist_summary()?;
        Ok(new_self)
    }

    pub(super) fn add_report(&self, entity: &EntityId) -> Result<BufWriter<File>, Error> {
        self.new_artifact_inner(entity, directory::ArtifactType::Report)
    }

    fn persist_summary(&self) -> Result<(), Error> {
        let entry_lock = self.entries.lock();
        let run_entry =
            entry_lock.get(&EntityId::TestRun).expect("Run entry should always be present");

        let mut run_result =
            directory::TestRunResult { common: Cow::Borrowed(&run_entry.common), suites: vec![] };

        for suite_entity_id in run_entry.children.iter() {
            let suite_entry =
                entry_lock.get(suite_entity_id).expect("Nonexistant suite referenced");
            let mut suite_result = directory::SuiteResult {
                common: Cow::Borrowed(&suite_entry.common),
                cases: vec![],
                tags: suite_entry.tags.as_ref().map(Cow::Borrowed).unwrap_or(Cow::Owned(vec![])),
            };
            for case_entity_id in suite_entry.children.iter() {
                suite_result.cases.push(directory::TestCaseResult {
                    common: Cow::Borrowed(
                        &entry_lock
                            .get(case_entity_id)
                            .expect("Nonexistant case referenced")
                            .common,
                    ),
                })
            }

            run_result.suites.push(suite_result);
        }

        self.output_directory.save_summary(&run_result)
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
        let file = entry
            .common
            .artifact_dir
            .new_artifact(artifact_type, filename_for_type(&artifact_type))?;
        Ok(BufWriter::new(file))
    }
}

#[async_trait]
impl Reporter for DirectoryReporter {
    async fn new_entity(&self, entity: &EntityId, name: &str) -> Result<(), Error> {
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
                common: directory::CommonResult {
                    name: name.to_string(),
                    artifact_dir: self.output_directory.new_artifact_dir()?,
                    outcome: directory::Outcome::NotStarted.into(),
                    start_time: None,
                    duration_milliseconds: None,
                },
                children: vec![],
                timer: MonotonicTimer::Unknown,
                tags: None,
            },
        );

        Ok(())
    }

    async fn set_entity_info(&self, entity: &EntityId, info: &EntityInfo) {
        let mut entries = self.entries.lock();
        let entry = entries.get_mut(entity).expect("Setting info for entity that does not exist");
        entry.tags = info.tags.clone();
    }

    async fn entity_started(&self, entity: &EntityId, timestamp: Timestamp) -> Result<(), Error> {
        let mut entries = self.entries.lock();
        let entry =
            entries.get_mut(entity).expect("Outcome reported for an entity that does not exist");
        entry.common.start_time = Some(
            std::time::SystemTime::now()
                .duration_since(std::time::SystemTime::UNIX_EPOCH)
                .unwrap()
                .as_millis() as u64,
        );
        entry.common.outcome = directory::Outcome::Inconclusive.into();
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
        entry.common.outcome = into_serializable_outcome(*outcome).into();

        if let (MonotonicTimer::Started { mono_start_time }, Timestamp::Given(mono_end_time)) =
            (&entry.timer, timestamp)
        {
            entry.common.duration_milliseconds = Some(
                mono_end_time
                    .checked_sub(*mono_start_time)
                    .expect("end time must be after start time")
                    .as_millis() as u64,
            );
            entry.timer = MonotonicTimer::Stopped;
        }
        Ok(())
    }

    /// Finalize and persist the outcome and artifacts for an entity. This should only be
    /// called once per entity.
    async fn entity_finished(&self, entity: &EntityId) -> Result<(), Error> {
        match entity {
            EntityId::TestRun => self.persist_summary()?,
            EntityId::Suite(_) => {
                let num_saved_suites = self.suites_finished_counter.fetch_add(1, Ordering::Relaxed);
                if num_saved_suites % SAVE_AFTER_SUITE_COUNT == 0 {
                    self.persist_summary()?;
                }
            }
            // Cases are saved as part of suites.
            EntityId::Case { .. } => (),
        }
        Ok(())
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
        let subdir = entry.common.artifact_dir.new_directory_artifact(
            directory::ArtifactMetadata {
                artifact_type: directory::MaybeUnknown::Known((*artifact_type).into()),
                component_moniker,
            },
            name,
        )?;

        Ok(Box::new(DirectoryDirectoryWriter { path: subdir }))
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
            if !parent.exists() {
                DirBuilder::new().recursive(true).create(&parent)?;
            }
        }

        let file = BufWriter::new(File::create(new_path)?);
        Ok(Box::new(file))
    }
}

fn prefix_for_directory_type(artifact_type: &DirectoryArtifactType) -> &'static str {
    match artifact_type {
        DirectoryArtifactType::Custom => CUSTOM_ARTIFACT_DIRECTORY,
        DirectoryArtifactType::Debug => DEBUG_ARTIFACT_DIRECTORY,
    }
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
    use fixture::fixture;
    use futures::future::Future;
    use std::ops::Deref;
    use tempfile::tempdir;
    use test_output_directory::testing::{
        assert_run_result, assert_suite_result, ExpectedDirectory, ExpectedSuite, ExpectedTestCase,
        ExpectedTestRun,
    };

    async fn version_variants<F, Fut>(_name: &str, test_fn: F)
    where
        F: Fn(SchemaVersion) -> Fut,
        Fut: Future<Output = ()>,
    {
        for schema in SchemaVersion::all_variants() {
            test_fn(schema).await;
        }
    }

    #[fixture(version_variants)]
    #[fuchsia::test]
    async fn no_artifacts(version: SchemaVersion) {
        let dir = tempdir().expect("create temp directory");
        const CASE_TIMES: [(ZxTime, ZxTime); 3] = [
            (ZxTime::from_nanos(0x1100000), ZxTime::from_nanos(0x2100000)),
            (ZxTime::from_nanos(0x1200000), ZxTime::from_nanos(0x2200000)),
            (ZxTime::from_nanos(0x1300000), ZxTime::from_nanos(0x2300000)),
        ];
        const SUITE_TIMES: (ZxTime, ZxTime) =
            (ZxTime::from_nanos(0x1000000), ZxTime::from_nanos(0x2400000));

        let run_reporter = RunReporter::new(
            DirectoryReporter::new(dir.path().to_path_buf(), version).expect("create run reporter"),
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

        assert_run_result(
            dir.path(),
            &ExpectedTestRun::new(directory::Outcome::Timedout)
                .with_suite(
                    ExpectedSuite::new("suite-0", directory::Outcome::Failed)
                        .with_case(ExpectedTestCase::new("case-0-0", directory::Outcome::Passed))
                        .with_case(ExpectedTestCase::new("case-0-1", directory::Outcome::Passed))
                        .with_case(ExpectedTestCase::new("case-0-2", directory::Outcome::Passed)),
                )
                .with_suite(
                    ExpectedSuite::new("suite-1", directory::Outcome::Failed)
                        .with_case(ExpectedTestCase::new("case-1-0", directory::Outcome::Passed))
                        .with_case(ExpectedTestCase::new("case-1-1", directory::Outcome::Passed))
                        .with_case(ExpectedTestCase::new("case-1-2", directory::Outcome::Passed)),
                )
                .with_suite(
                    ExpectedSuite::new("suite-2", directory::Outcome::Failed)
                        .with_case(ExpectedTestCase::new("case-2-0", directory::Outcome::Passed))
                        .with_case(ExpectedTestCase::new("case-2-1", directory::Outcome::Passed))
                        .with_case(ExpectedTestCase::new("case-2-2", directory::Outcome::Passed)),
                ),
        );
    }

    #[fixture(version_variants)]
    #[fuchsia::test]
    async fn artifacts_per_entity(version: SchemaVersion) {
        let dir = tempdir().expect("create temp directory");
        let run_reporter = RunReporter::new(
            DirectoryReporter::new(dir.path().to_path_buf(), version).expect("create run reporter"),
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

        assert_run_result(
            dir.path(),
            &ExpectedTestRun::new(directory::Outcome::Passed)
                .with_artifact(
                    directory::ArtifactType::Stdout,
                    STDOUT_FILE.into(),
                    "stdout from run\n",
                )
                .with_suite(
                    ExpectedSuite::new("suite-1", directory::Outcome::Passed)
                        .with_case(
                            ExpectedTestCase::new("case-1-0", directory::Outcome::Passed)
                                .with_artifact(
                                    directory::ArtifactType::Stdout,
                                    STDOUT_FILE.into(),
                                    "stdout from case 0\n",
                                ),
                        )
                        .with_case(
                            ExpectedTestCase::new("case-1-1", directory::Outcome::Passed)
                                .with_artifact(
                                    directory::ArtifactType::Stdout,
                                    STDOUT_FILE.into(),
                                    "stdout from case 1\n",
                                ),
                        )
                        .with_case(
                            ExpectedTestCase::new("case-1-2", directory::Outcome::Passed)
                                .with_artifact(
                                    directory::ArtifactType::Stdout,
                                    STDOUT_FILE.into(),
                                    "stdout from case 2\n",
                                ),
                        )
                        .with_artifact(
                            directory::ArtifactType::Stdout,
                            STDOUT_FILE.into(),
                            "stdout from suite\n",
                        ),
                ),
        );
    }

    #[fixture(version_variants)]
    #[fuchsia::test]
    async fn empty_directory_artifacts(version: SchemaVersion) {
        let dir = tempdir().expect("create temp directory");

        let run_reporter = RunReporter::new(
            DirectoryReporter::new(dir.path().to_path_buf(), version).expect("create run reporter"),
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

        assert_run_result(
            dir.path(),
            &ExpectedTestRun::new(directory::Outcome::Passed)
                .with_directory_artifact(
                    directory::ArtifactType::Custom,
                    Option::<&str>::None,
                    ExpectedDirectory::new(),
                )
                .with_suite(
                    ExpectedSuite::new("suite-1", directory::Outcome::Passed)
                        .with_directory_artifact(
                            directory::ArtifactMetadata {
                                artifact_type: directory::ArtifactType::Custom.into(),
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
                        ),
                ),
        );
    }

    #[fixture(version_variants)]
    #[fuchsia::test]
    async fn directory_artifacts(version: SchemaVersion) {
        let dir = tempdir().expect("create temp directory");

        let run_reporter = RunReporter::new(
            DirectoryReporter::new(dir.path().to_path_buf(), version).expect("create run reporter"),
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

        assert_run_result(
            dir.path(),
            &ExpectedTestRun::new(directory::Outcome::Passed)
                .with_directory_artifact(
                    directory::ArtifactType::Custom,
                    Option::<&str>::None,
                    ExpectedDirectory::new().with_file("run-artifact", "run artifact content\n"),
                )
                .with_suite(
                    ExpectedSuite::new("suite-1", directory::Outcome::Passed)
                        .with_directory_artifact(
                            directory::ArtifactMetadata {
                                artifact_type: directory::ArtifactType::Custom.into(),
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
                        ),
                ),
        );
    }

    #[fixture(version_variants)]
    #[fuchsia::test]
    async fn ensure_paths_cannot_escape_directory(version: SchemaVersion) {
        let dir = tempdir().expect("create temp directory");

        let run_reporter = RunReporter::new(
            DirectoryReporter::new(dir.path().to_path_buf(), version).expect("create run reporter"),
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

    #[fixture(version_variants)]
    #[fuchsia::test]
    async fn duplicate_suite_names_ok(version: SchemaVersion) {
        let dir = tempdir().expect("create temp directory");
        let run_reporter = RunReporter::new(
            DirectoryReporter::new(dir.path().to_path_buf(), version).expect("create run reporter"),
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

        let saved_run_result = directory::TestRunResult::from_dir(dir.path()).expect("parse dir");
        assert_eq!(
            saved_run_result.common.deref().outcome,
            directory::MaybeUnknown::Known(directory::Outcome::Failed)
        );

        assert_eq!(saved_run_result.suites.len(), 2);
        // names of the suites are identical, so we rely on the outcome to differentiate them.
        let expected_success_suite = ExpectedSuite::new("suite", directory::Outcome::Passed)
            .with_artifact(
                directory::ArtifactType::Stdout,
                STDOUT_FILE.into(),
                "stdout from passed suite\n",
            );
        let expected_failed_suite = ExpectedSuite::new("suite", directory::Outcome::Failed);

        let suite_results = saved_run_result.suites;

        if suite_results[0].common.deref().outcome
            == directory::MaybeUnknown::Known(directory::Outcome::Passed)
        {
            assert_suite_result(dir.path(), &suite_results[0], &expected_success_suite);
            assert_suite_result(dir.path(), &suite_results[1], &expected_failed_suite);
        } else {
            assert_suite_result(dir.path(), &suite_results[0], &expected_failed_suite);
            assert_suite_result(dir.path(), &suite_results[1], &expected_success_suite);
        }
    }

    #[fixture(version_variants)]
    #[fuchsia::test]
    async fn intermediate_results_persisted(version: SchemaVersion) {
        // This test verifies that the results of the test run are persisted after every
        // few suites complete. This ensures that at least some results will be saved even if
        // ffx test crashes.
        let dir = tempdir().expect("create temp directory");
        let run_reporter = RunReporter::new(
            DirectoryReporter::new(dir.path().to_path_buf(), version).expect("create run reporter"),
        );

        assert_run_result(dir.path(), &ExpectedTestRun::new(directory::Outcome::NotStarted));

        run_reporter.started(Timestamp::Unknown).await.expect("start test run");

        // Create one suite which isn't finished. It should be present in intermediate results too.
        let incomplete_suite_reporter =
            run_reporter.new_suite("incomplete", &SuiteId(99)).await.expect("create new suite");

        for i in 0..SAVE_AFTER_SUITE_COUNT {
            let suite_reporter = run_reporter
                .new_suite(&format!("suite-{:?}", i), &SuiteId(i))
                .await
                .expect("create new suite");
            suite_reporter.started(Timestamp::Unknown).await.expect("start suite");
            suite_reporter
                .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
                .await
                .expect("stop suite");
            suite_reporter.finished().await.expect("finish suite");
        }

        let mut intermediate_run = ExpectedTestRun::new(directory::Outcome::Inconclusive)
            .with_suite(ExpectedSuite::new("incomplete", directory::Outcome::NotStarted));
        for i in 0..SAVE_AFTER_SUITE_COUNT {
            intermediate_run = intermediate_run.with_suite(ExpectedSuite::new(
                &format!("suite-{:?}", i),
                directory::Outcome::Passed,
            ));
        }

        assert_run_result(dir.path(), &intermediate_run);

        incomplete_suite_reporter.finished().await.expect("finish suite");

        run_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("stop test run");
        run_reporter.finished().await.expect("finish test run");

        let mut final_run = ExpectedTestRun::new(directory::Outcome::Passed)
            .with_suite(ExpectedSuite::new("incomplete", directory::Outcome::NotStarted));
        for i in 0..SAVE_AFTER_SUITE_COUNT {
            final_run = final_run.with_suite(ExpectedSuite::new(
                &format!("suite-{:?}", i),
                directory::Outcome::Passed,
            ));
        }

        assert_run_result(dir.path(), &final_run);
    }

    #[fixture(version_variants)]
    #[fuchsia::test]
    async fn early_finish_ok(version: SchemaVersion) {
        // This test verifies that a suite is saved if finished() is called before an outcome is
        // reported. This could happen if some error causes test execution to terminate early.
        let dir = tempdir().expect("create temp directory");
        let run_reporter = RunReporter::new(
            DirectoryReporter::new(dir.path().to_path_buf(), version).expect("create run reporter"),
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

        assert_run_result(
            dir.path(),
            &ExpectedTestRun::new(directory::Outcome::Inconclusive)
                .with_suite(
                    ExpectedSuite::new("suite", directory::Outcome::Inconclusive)
                        .with_case(ExpectedTestCase::new("case", directory::Outcome::Inconclusive)),
                )
                .with_suite(ExpectedSuite::new("no-start-suite", directory::Outcome::NotStarted)),
        );
    }
}
