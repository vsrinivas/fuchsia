// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::output::{
    noop::NoopDirectoryWriter, ArtifactType, DirectoryArtifactType, DynArtifact,
    DynDirectoryArtifact, EntityId, EntityInfo, ReportedOutcome, Reporter, Timestamp,
};
use async_trait::async_trait;
use fuchsia_async as fasync;
use parking_lot::Mutex;
use std::{
    collections::HashMap,
    io::{Error, Write},
    sync::{atomic::AtomicU32, Arc},
    time::Duration,
};
use tracing::error;

mod writer;
pub use writer::ShellWriterView;
use writer::{ShellWriterHandle, ShellWriterHandleInner};

/// Duration after which to emit an excessive duration log.
const EXCESSIVE_DURATION: Duration = Duration::from_secs(60);
/// Buffer stdout and stderr for this duration before dumping to console.
const STDIO_BUFFERING_DURATION: Duration = Duration::from_secs(5);
/// Dump stdout and stderr to console if it exceeds this size.
const STDIO_BUFFER_SIZE: usize = 4096;

/// A reporter that outputs results and artifacts to a single stream, usually stdout.
/// This reporter is intended to provide "live" updates to a developer watching while
/// tests are executed.
pub struct ShellReporter<W: 'static + Write + Send + Sync> {
    /// Arc around the writer and state, used to dispense more handles.
    inner: Arc<Mutex<ShellWriterHandleInner<W>>>,
    /// Map containing known information about each entity.
    entity_state_map: Mutex<HashMap<EntityId, EntityState>>,
    /// Number of completed suites, used to output
    completed_suites: AtomicU32,
    /// Size of the buffer used for stdio.
    stdio_buffer_size: usize,
    /// Length of time to buffer stdio before printing it to console.
    stdio_buffer_duration: Duration,
}

/// All known state needed by a |ShellReporter| to display results.
struct EntityState {
    name: String,
    excessive_duration_task: Option<fasync::Task<()>>,
    children: Vec<EntityId>,
    restricted_logs: Option<ShellWriterView<Vec<u8>>>,
    run_state: EntityRunningState,
}

enum EntityRunningState {
    NotRunning,
    Started,
    Finished(ReportedOutcome),
}

impl EntityState {
    fn new<S: Into<String>>(name: S) -> Self {
        Self {
            name: name.into(),
            excessive_duration_task: None,
            children: vec![],
            restricted_logs: None,
            run_state: EntityRunningState::NotRunning,
        }
    }

    fn name(&self) -> &str {
        &self.name
    }
}

impl ShellReporter<Vec<u8>> {
    pub fn new_expose_writer_for_test() -> (Self, ShellWriterView<Vec<u8>>) {
        let inner = Arc::new(Mutex::new(ShellWriterHandleInner::new(vec![])));
        let mut entity_state_map = HashMap::new();
        entity_state_map.insert(EntityId::TestRun, EntityState::new("TEST RUN"));
        (
            Self {
                inner: inner.clone(),
                entity_state_map: Mutex::new(entity_state_map),
                completed_suites: AtomicU32::new(0),
                // Disable buffering for most tests to simplify testing.
                stdio_buffer_duration: Duration::ZERO,
                stdio_buffer_size: 0,
            },
            ShellWriterView::new(inner),
        )
    }
}

impl<W: 'static + Write + Send + Sync> ShellReporter<W> {
    pub fn new(inner: W) -> Self {
        let inner = Arc::new(Mutex::new(ShellWriterHandleInner::new(inner)));
        let mut entity_state_map = HashMap::new();
        entity_state_map.insert(EntityId::TestRun, EntityState::new("TEST RUN"));
        Self {
            inner,
            entity_state_map: Mutex::new(entity_state_map),
            completed_suites: AtomicU32::new(0),
            stdio_buffer_duration: STDIO_BUFFERING_DURATION,
            stdio_buffer_size: STDIO_BUFFER_SIZE,
        }
    }

    fn new_writer_handle(&self, prefix: Option<String>) -> ShellWriterHandle<W> {
        ShellWriterHandle::new_handle(Arc::clone(&self.inner), prefix)
    }

    fn num_known_suites(entity_map: &HashMap<EntityId, EntityState>) -> usize {
        entity_map.keys().filter(|id| matches!(id, EntityId::Suite(_))).count()
    }
}

#[async_trait]
impl<W: 'static + Write + Send + Sync> Reporter for ShellReporter<W> {
    async fn new_entity(&self, entity: &EntityId, name: &str) -> Result<(), Error> {
        let mut map = self.entity_state_map.lock();
        map.insert(entity.clone(), EntityState::new(name));
        if let EntityId::Case { suite, .. } = entity {
            map.get_mut(&EntityId::Suite(*suite)).unwrap().children.push(entity.clone());
        }
        Ok(())
    }

    async fn set_entity_info(&self, _entity: &EntityId, _info: &EntityInfo) {}

    async fn entity_started(&self, entity: &EntityId, _: Timestamp) -> Result<(), Error> {
        let mut writer = self.new_writer_handle(None);
        let mut entity_map_lock = self.entity_state_map.lock();
        let mut entity_entry = entity_map_lock.get_mut(entity).unwrap();
        entity_entry.run_state = EntityRunningState::Started;
        let name = entity_entry.name().to_string();
        match entity {
            EntityId::TestRun => (),
            EntityId::Suite(_) => writeln!(writer, "Running test '{}'", name)?,
            EntityId::Case { .. } => {
                writeln!(writer, "[RUNNING]\t{}", name)?;
                entity_entry.excessive_duration_task = Some(fasync::Task::spawn(async move {
                    fasync::Timer::new(EXCESSIVE_DURATION).await;
                    writeln!(
                        writer,
                        "[duration - {}]:\tStill running after {:?} seconds",
                        name,
                        EXCESSIVE_DURATION.as_secs()
                    )
                    .unwrap_or_else(|e| error!("Failed to write: {:?}", e));
                }));
            }
        }
        Ok(())
    }

    async fn entity_stopped(
        &self,
        entity: &EntityId,
        outcome: &ReportedOutcome,
        _: Timestamp,
    ) -> Result<(), Error> {
        let mut writer = self.new_writer_handle(None);
        let mut entity_map_lock = self.entity_state_map.lock();
        entity_map_lock.get_mut(entity).unwrap().run_state = EntityRunningState::Finished(*outcome);
        let entity_entry = entity_map_lock.get_mut(entity).unwrap();
        let name = entity_entry.name().to_string();

        // cancel any tasks for reporting excessive duration
        let _ = entity_entry.excessive_duration_task.take();
        match entity {
            EntityId::TestRun => (),
            EntityId::Suite(_) => (),
            EntityId::Case { .. } => {
                // We don't list error result as it indicates the test didn't finish.
                if *outcome != ReportedOutcome::Error {
                    writeln!(writer, "[{}]\t{}", outcome, name)?;
                }
            }
        }
        Ok(())
    }

    async fn entity_finished(&self, entity: &EntityId) -> Result<(), Error> {
        let mut writer = self.new_writer_handle(None);
        let entity_map_lock = self.entity_state_map.lock();
        let entity_entry = entity_map_lock.get(entity).unwrap();
        let name = entity_entry.name().to_string();
        let outcome = match &entity_entry.run_state {
            EntityRunningState::Finished(outcome) => *outcome,
            _ => ReportedOutcome::Inconclusive,
        };
        let children: Vec<_> = entity_entry.children.iter().cloned().collect();
        match entity {
            EntityId::TestRun => (),
            EntityId::Suite(_) => {
                if matches!(entity_entry.run_state, EntityRunningState::NotRunning) {
                    // no need to output a report if the test wasn't even started.
                    return Ok(());
                }

                let cases: Vec<_> =
                    children.iter().map(|child| entity_map_lock.get(child).unwrap()).collect();
                let executed: Vec<_> = cases
                    .iter()
                    .filter(|case| match &case.run_state {
                        EntityRunningState::Started => true,
                        EntityRunningState::Finished(ReportedOutcome::Skipped) => false,
                        EntityRunningState::Finished(_) => true,
                        EntityRunningState::NotRunning => false,
                    })
                    .collect();
                let mut failed: Vec<_> = cases
                    .iter()
                    .filter(|case| {
                        matches!(
                            &case.run_state,
                            EntityRunningState::Finished(
                                ReportedOutcome::Failed | ReportedOutcome::Timedout
                            )
                        )
                    })
                    .map(|case| case.name())
                    .collect();
                failed.sort();
                let mut not_finished: Vec<_> = cases
                    .iter()
                    .filter(|case| {
                        matches!(
                            &case.run_state,
                            EntityRunningState::Started
                                | EntityRunningState::Finished(ReportedOutcome::Error)
                        )
                    })
                    .map(|case| case.name())
                    .collect();
                not_finished.sort();
                let num_passed = cases
                    .iter()
                    .filter(|case| {
                        matches!(
                            &case.run_state,
                            EntityRunningState::Finished(ReportedOutcome::Passed)
                        )
                    })
                    .count();
                let num_skipped = cases
                    .iter()
                    .filter(|case| {
                        matches!(
                            &case.run_state,
                            EntityRunningState::Finished(ReportedOutcome::Skipped)
                        )
                    })
                    .count();

                let suite_number =
                    self.completed_suites.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
                if suite_number > 0 {
                    writeln!(
                        writer,
                        "\nTest run count {}/{}",
                        suite_number + 1,
                        Self::num_known_suites(&*entity_map_lock),
                    )?;
                }
                writeln!(writer)?;
                if !failed.is_empty() {
                    writeln!(writer, "Failed tests: {}", failed.join(", "))?;
                }
                if !not_finished.is_empty() {
                    writeln!(writer, "\nThe following test(s) never completed:")?;
                    for t in not_finished {
                        writeln!(writer, "{}", t)?;
                    }
                }
                match num_skipped {
                    0 => writeln!(
                        writer,
                        "{} out of {} tests passed...",
                        num_passed,
                        executed.len()
                    )?,
                    skipped => writeln!(
                        writer,
                        "{} out of {} attempted tests passed, {} tests skipped...",
                        num_passed,
                        executed.len(),
                        skipped,
                    )?,
                }
                if let Some(restricted_logs) = &entity_entry.restricted_logs {
                    writeln!(writer, "\nTest {} produced unexpected high-severity logs:", &name)?;
                    writeln!(writer, "----------------xxxxx----------------")?;
                    writer.write_all(restricted_logs.lock().as_slice())?;
                    writeln!(writer, "\n----------------xxxxx----------------")?;
                    writeln!(writer, "Failing this test. See: https://fuchsia.dev/fuchsia-src/development/diagnostics/test_and_logs#restricting_log_severity\n")?;
                }
                match outcome {
                    ReportedOutcome::Cancelled => {
                        writeln!(writer, "{} was cancelled before completion.", &name)?
                    }
                    ReportedOutcome::DidNotFinish => {
                        writeln!(writer, "{} did not complete successfully.", &name)?
                    }
                    other => writeln!(writer, "{} completed with result: {}", &name, other)?,
                }
            }
            EntityId::Case { .. } => (),
        }
        Ok(())
    }

    async fn new_artifact(
        &self,
        entity: &EntityId,
        artifact_type: &ArtifactType,
    ) -> Result<Box<DynArtifact>, Error> {
        let mut lock = self.entity_state_map.lock();
        let mut entity = lock.get_mut(entity).unwrap();
        let name = entity.name();

        Ok(match artifact_type {
            ArtifactType::Stdout => Box::new(test_diagnostics::StdoutBuffer::new(
                self.stdio_buffer_duration,
                self.new_writer_handle(Some(format!("[stdout - {}]\n", name))),
                self.stdio_buffer_size,
            )),
            ArtifactType::Stderr => Box::new(test_diagnostics::StdoutBuffer::new(
                self.stdio_buffer_duration,
                self.new_writer_handle(Some(format!("[stderr - {}]\n", name))),
                self.stdio_buffer_size,
            )),
            ArtifactType::Syslog => Box::new(self.new_writer_handle(None)),
            ArtifactType::RestrictedLog => {
                // Restricted logs are saved for reporting when the entity completes.
                let log_buffer = Arc::new(Mutex::new(ShellWriterHandleInner::new(vec![])));
                entity.restricted_logs = Some(ShellWriterView::new(log_buffer.clone()));
                Box::new(ShellWriterHandle::new_handle(log_buffer, None))
            }
        })
    }

    async fn new_directory_artifact(
        &self,
        _entity: &EntityId,
        _artifact_type: &DirectoryArtifactType,
        _component_moniker: Option<String>,
    ) -> Result<Box<DynDirectoryArtifact>, Error> {
        // For the purpose of live reporting we don't display anything from a directory.
        Ok(Box::new(NoopDirectoryWriter))
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use {
        crate::output::{CaseId, RunReporter, SuiteId},
        std::io::Write,
    };

    #[fuchsia::test]
    async fn report_case_events() {
        let (shell_reporter, output) = ShellReporter::new_expose_writer_for_test();
        let run_reporter = RunReporter::new(shell_reporter);
        let suite_reporter =
            run_reporter.new_suite("test-suite", &SuiteId(0)).await.expect("create suite");
        suite_reporter.started(Timestamp::Unknown).await.expect("case started");
        let mut expected = "Running test 'test-suite'\n".to_string();
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        let case_1_reporter =
            suite_reporter.new_case("case-1", &CaseId(0)).await.expect("create case");
        let case_2_reporter =
            suite_reporter.new_case("case-2", &CaseId(1)).await.expect("create case");

        case_1_reporter.started(Timestamp::Unknown).await.expect("case started");
        expected.push_str("[RUNNING]\tcase-1\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        case_2_reporter.started(Timestamp::Unknown).await.expect("case started");
        expected.push_str("[RUNNING]\tcase-2\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        case_1_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("stop case");
        expected.push_str("[PASSED]\tcase-1\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        case_2_reporter
            .stopped(&ReportedOutcome::Failed, Timestamp::Unknown)
            .await
            .expect("stop case");
        expected.push_str("[FAILED]\tcase-2\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        case_1_reporter.finished().await.expect("finish case");
        case_2_reporter.finished().await.expect("finish case");
        suite_reporter
            .stopped(&ReportedOutcome::Failed, Timestamp::Unknown)
            .await
            .expect("stop suite");
        suite_reporter.finished().await.expect("finish suite");

        expected.push_str("\n");
        expected.push_str("Failed tests: case-2\n");
        expected.push_str("1 out of 2 tests passed...\n");
        expected.push_str("test-suite completed with result: FAILED\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);
    }

    #[fuchsia::test]
    async fn report_case_skipped() {
        let (shell_reporter, output) = ShellReporter::new_expose_writer_for_test();
        let run_reporter = RunReporter::new(shell_reporter);
        let suite_reporter =
            run_reporter.new_suite("test-suite", &SuiteId(0)).await.expect("create suite");
        suite_reporter.started(Timestamp::Unknown).await.expect("case started");
        let mut expected = "Running test 'test-suite'\n".to_string();
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        let case_1_reporter =
            suite_reporter.new_case("case-1", &CaseId(0)).await.expect("create case");
        let case_2_reporter =
            suite_reporter.new_case("case-2", &CaseId(1)).await.expect("create case");

        case_1_reporter.started(Timestamp::Unknown).await.expect("case started");
        expected.push_str("[RUNNING]\tcase-1\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        case_1_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("stop case");
        expected.push_str("[PASSED]\tcase-1\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        case_2_reporter
            .stopped(&ReportedOutcome::Skipped, Timestamp::Unknown)
            .await
            .expect("stop case");
        expected.push_str("[SKIPPED]\tcase-2\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        case_1_reporter.finished().await.expect("finish case");
        case_2_reporter.finished().await.expect("finish case");
        suite_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("stop suite");
        suite_reporter.finished().await.expect("finish suite");

        expected.push_str("\n");
        expected.push_str("1 out of 1 attempted tests passed, 1 tests skipped...\n");
        expected.push_str("test-suite completed with result: PASSED\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);
    }

    #[fuchsia::test]
    async fn syslog_artifacts() {
        let (shell_reporter, output) = ShellReporter::new_expose_writer_for_test();
        let run_reporter = RunReporter::new(shell_reporter);
        let suite_reporter =
            run_reporter.new_suite("test-suite", &SuiteId(0)).await.expect("create suite");
        suite_reporter.started(Timestamp::Unknown).await.expect("case started");
        let mut syslog_writer =
            suite_reporter.new_artifact(&ArtifactType::Syslog).await.expect("create syslog");

        writeln!(syslog_writer, "[log] test syslog").expect("write to syslog");
        let mut expected = "Running test 'test-suite'\n".to_string();
        expected.push_str("[log] test syslog\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected);

        suite_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("stop suite");
        writeln!(syslog_writer, "[log] more test syslog").expect("write to syslog");
        expected.push_str("[log] more test syslog\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected);

        suite_reporter.finished().await.expect("finish suite");
        expected.push_str("\n");
        expected.push_str("0 out of 0 tests passed...\n");
        expected.push_str("test-suite completed with result: PASSED\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);
    }

    #[fuchsia::test]
    async fn report_retricted_logs() {
        let (shell_reporter, output) = ShellReporter::new_expose_writer_for_test();
        let run_reporter = RunReporter::new(shell_reporter);
        let suite_reporter =
            run_reporter.new_suite("test-suite", &SuiteId(0)).await.expect("create suite");
        suite_reporter.started(Timestamp::Unknown).await.expect("case started");

        let case_reporter =
            suite_reporter.new_case("case-0", &CaseId(0)).await.expect("create case");
        case_reporter.started(Timestamp::Unknown).await.expect("case started");
        case_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("stop case");
        case_reporter.finished().await.expect("finish case");

        let mut expected = "Running test 'test-suite'\n".to_string();
        expected.push_str("[RUNNING]\tcase-0\n");
        expected.push_str("[PASSED]\tcase-0\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        suite_reporter
            .stopped(&ReportedOutcome::Failed, Timestamp::Unknown)
            .await
            .expect("stop suite");
        let mut restricted_log = suite_reporter
            .new_artifact(&ArtifactType::RestrictedLog)
            .await
            .expect("create restricted log");
        write!(restricted_log, "suite restricted log").expect("write to restricted log");
        drop(restricted_log);

        suite_reporter.finished().await.expect("finish suite");
        expected.push_str("\n");
        expected.push_str("1 out of 1 tests passed...\n");
        expected.push_str("\nTest test-suite produced unexpected high-severity logs:\n");
        expected.push_str("----------------xxxxx----------------\n");
        expected.push_str("suite restricted log\n\n");
        expected.push_str("----------------xxxxx----------------\n");
        expected.push_str("Failing this test. See: https://fuchsia.dev/fuchsia-src/development/diagnostics/test_and_logs#restricting_log_severity\n");
        expected.push_str("\ntest-suite completed with result: FAILED\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);
    }

    #[fuchsia::test]
    async fn stdout_artifacts() {
        let (shell_reporter, output) = ShellReporter::new_expose_writer_for_test();
        let run_reporter = RunReporter::new(shell_reporter);
        let suite_reporter =
            run_reporter.new_suite("test-suite", &SuiteId(0)).await.expect("create suite");
        suite_reporter.started(Timestamp::Unknown).await.expect("case started");

        let case_0_reporter =
            suite_reporter.new_case("case-0", &CaseId(0)).await.expect("create case");
        let case_1_reporter =
            suite_reporter.new_case("case-1", &CaseId(1)).await.expect("create case");
        case_0_reporter.started(Timestamp::Unknown).await.expect("start case");
        case_1_reporter.started(Timestamp::Unknown).await.expect("start case");
        let mut expected = "Running test 'test-suite'\n".to_string();
        expected.push_str("[RUNNING]\tcase-0\n[RUNNING]\tcase-1\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        let mut case_0_stdout =
            case_0_reporter.new_artifact(&ArtifactType::Stdout).await.expect("create artifact");
        let mut case_1_stdout =
            case_1_reporter.new_artifact(&ArtifactType::Stdout).await.expect("create artifact");

        writeln!(case_0_stdout, "stdout from case 0").expect("write to stdout");
        writeln!(case_1_stdout, "stdout from case 1").expect("write to stdout");

        expected.push_str("[stdout - case-0]\nstdout from case 0\n");
        expected.push_str("[stdout - case-1]\nstdout from case 1\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        case_0_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("stop case");
        case_1_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("stop case");
        expected.push_str("[PASSED]\tcase-0\n");
        expected.push_str("[PASSED]\tcase-1\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        case_0_reporter.finished().await.expect("finish case");
        case_1_reporter.finished().await.expect("finish case");
        suite_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("stop suite");

        suite_reporter.finished().await.expect("finish suite");
        expected.push_str("\n");
        expected.push_str("2 out of 2 tests passed...\n");
        expected.push_str("test-suite completed with result: PASSED\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);
    }

    #[fuchsia::test]
    async fn report_unfinished() {
        let (shell_reporter, output) = ShellReporter::new_expose_writer_for_test();
        let run_reporter = RunReporter::new(shell_reporter);
        let suite_reporter =
            run_reporter.new_suite("test-suite", &SuiteId(0)).await.expect("create suite");
        suite_reporter.started(Timestamp::Unknown).await.expect("suite started");

        let case_reporter =
            suite_reporter.new_case("case-0", &CaseId(0)).await.expect("create case");
        case_reporter.started(Timestamp::Unknown).await.expect("case started");
        case_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("stop case");
        case_reporter.finished().await.expect("finish case");
        let mut expected = "Running test 'test-suite'\n".to_string();
        expected.push_str("[RUNNING]\tcase-0\n[PASSED]\tcase-0\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        // add a case that finishes with ERROR
        let unfinished_case_1 =
            suite_reporter.new_case("case-1", &CaseId(1)).await.expect("create case");
        unfinished_case_1.started(Timestamp::Unknown).await.expect("case started");
        unfinished_case_1
            .stopped(&ReportedOutcome::Error, Timestamp::Unknown)
            .await
            .expect("stop case");
        unfinished_case_1.finished().await.expect("finish case");
        expected.push_str("[RUNNING]\tcase-1\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        // add a case that doesn't finish at all
        let unfinished_case_2 =
            suite_reporter.new_case("case-2", &CaseId(2)).await.expect("create case");
        unfinished_case_2.started(Timestamp::Unknown).await.expect("case started");
        unfinished_case_2.finished().await.expect("finish case");
        expected.push_str("[RUNNING]\tcase-2\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        suite_reporter
            .stopped(&ReportedOutcome::Failed, Timestamp::Unknown)
            .await
            .expect("stop suite");
        suite_reporter.finished().await.expect("finish suite");
        expected.push_str("\n");
        expected.push_str("\nThe following test(s) never completed:\n");
        expected.push_str("case-1\n");
        expected.push_str("case-2\n");
        expected.push_str("1 out of 3 tests passed...\n");
        expected.push_str("test-suite completed with result: FAILED\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);
    }

    #[fuchsia::test]
    async fn report_cancelled_suite() {
        let (shell_reporter, output) = ShellReporter::new_expose_writer_for_test();
        let run_reporter = RunReporter::new(shell_reporter);
        let suite_reporter =
            run_reporter.new_suite("test-suite", &SuiteId(0)).await.expect("create suite");
        suite_reporter.started(Timestamp::Unknown).await.expect("suite started");

        let case_reporter =
            suite_reporter.new_case("case", &CaseId(0)).await.expect("create new case");
        case_reporter.started(Timestamp::Unknown).await.expect("case started");
        case_reporter.finished().await.expect("case finished");
        suite_reporter
            .stopped(&ReportedOutcome::Cancelled, Timestamp::Unknown)
            .await
            .expect("stop suite");
        suite_reporter.finished().await.expect("case finished");

        let mut expected = "Running test 'test-suite'\n".to_string();
        expected.push_str("[RUNNING]\tcase\n");
        expected.push_str("\n");
        expected.push_str("\nThe following test(s) never completed:\n");
        expected.push_str("case\n");
        expected.push_str("0 out of 1 tests passed...\n");
        expected.push_str("test-suite was cancelled before completion.\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);
    }

    #[fuchsia::test]
    async fn report_suite_did_not_finish() {
        let (shell_reporter, output) = ShellReporter::new_expose_writer_for_test();
        let run_reporter = RunReporter::new(shell_reporter);
        let suite_reporter =
            run_reporter.new_suite("test-suite", &SuiteId(0)).await.expect("create suite");
        suite_reporter.started(Timestamp::Unknown).await.expect("suite started");

        let case_reporter =
            suite_reporter.new_case("case", &CaseId(0)).await.expect("create new case");
        case_reporter.started(Timestamp::Unknown).await.expect("case started");
        case_reporter.finished().await.expect("case finished");
        suite_reporter
            .stopped(&ReportedOutcome::DidNotFinish, Timestamp::Unknown)
            .await
            .expect("stop suite");
        suite_reporter.finished().await.expect("case finished");

        let mut expected = "Running test 'test-suite'\n".to_string();
        expected.push_str("[RUNNING]\tcase\n");
        expected.push_str("\n");
        expected.push_str("\nThe following test(s) never completed:\n");
        expected.push_str("case\n");
        expected.push_str("0 out of 1 tests passed...\n");
        expected.push_str("test-suite did not complete successfully.\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);
    }
}
