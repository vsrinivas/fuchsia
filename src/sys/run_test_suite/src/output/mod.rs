// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async_trait::async_trait;
use std::io::{Error, Write};
use std::path::Path;
use test_list::TestTag;

mod directory;
mod directory_with_stdout;
mod line;
mod memory;
mod mux;
mod noop;
mod shell;
pub use line::AnsiFilterWriter;

pub use directory::{DirectoryReporter, SchemaVersion};
pub use directory_with_stdout::DirectoryWithStdoutReporter;
use fidl_fuchsia_test_manager as ftest_manager;
pub use memory::{InMemoryArtifact, InMemoryDirectoryWriter, InMemoryReporter};
pub use mux::MultiplexedReporter;
pub use noop::NoopReporter;
pub use shell::{ShellReporter, ShellWriterView};

pub type DynArtifact = dyn 'static + Write + Send + Sync;
pub type DynDirectoryArtifact = dyn 'static + DirectoryWrite + Send + Sync;
type DynReporter = dyn 'static + Reporter + Send + Sync;
type ArtifactWrapperFn = dyn Fn(&ArtifactType, Box<DynArtifact>) -> Box<DynArtifact>;

/// A reporter for structured results scoped to a test run.
/// To report results and artifacts for a test run, a user should create a `RunReporter`,
/// and create child `SuiteReporter`s and `CaseReporter`s beneath it to report results and
/// artifacts scoped to suites and cases. When the run is finished, the user should call
/// `record` to ensure the results are persisted.
///
/// `RunReporter`, `SuiteReporter`, and `CaseReporter` are wrappers around `Reporter`
/// implementations that statically ensure that some assumptions made by `Reporter` implemtnations
/// are upheld (for example, that an entity marked finished at most once).
pub struct RunReporter {
    /// Inner `Reporter` implementation used to record results.
    reporter: Box<DynReporter>,
    /// Method used to wrap artifact Writers. For example, this may add ANSI filtering
    /// to an artifact.
    artifact_wrapper: Box<ArtifactWrapperFn>,
}

/// A reporter for structured results scoped to a test suite. Note this may not outlive
/// the `RunReporter` from which it is created.
pub struct SuiteReporter<'a> {
    reporter: &'a DynReporter,
    artifact_wrapper: &'a ArtifactWrapperFn,
    suite_id: SuiteId,
}

/// A reporter for structured results scoped to a single test case. Note this may not outlive
/// the `SuiteReporter` from which it is created.
pub struct CaseReporter<'a> {
    reporter: &'a DynReporter,
    artifact_wrapper: &'a ArtifactWrapperFn,
    entity_id: EntityId,
}

impl RunReporter {
    /// Create a `RunReporter`, where stdout, stderr, syslog, and restricted log artifact types
    /// are sanitized for ANSI escape sequences before being passed along to |reporter|.
    pub fn new_ansi_filtered<R: 'static + Reporter + Send + Sync>(reporter: R) -> Self {
        let reporter = Box::new(reporter);
        let artifact_wrapper =
            Box::new(|artifact_type: &ArtifactType, artifact: Box<DynArtifact>| {
                match artifact_type {
                    // All the artifact types are enumerated here as we expect future artifacts
                    // should not be filtered.
                    ArtifactType::Stdout
                    | ArtifactType::Stderr
                    | ArtifactType::Syslog
                    | ArtifactType::RestrictedLog => {
                        Box::new(AnsiFilterWriter::new(artifact)) as Box<DynArtifact>
                    }
                }
            });
        Self { reporter, artifact_wrapper }
    }

    /// Create a `RunReporter`.
    pub fn new<R: 'static + Reporter + Send + Sync>(reporter: R) -> Self {
        let reporter = Box::new(reporter);
        let artifact_wrapper =
            Box::new(|_type: &ArtifactType, artifact: Box<DynArtifact>| artifact);
        Self { reporter, artifact_wrapper }
    }

    #[cfg(test)]
    fn new_for_test<R: 'static + Reporter + Send + Sync>(inner: R) -> Self {
        let artifact_wrapper =
            Box::new(|_type: &ArtifactType, artifact: Box<DynArtifact>| artifact);
        Self { reporter: Box::new(inner), artifact_wrapper }
    }

    /// Create a new artifact scoped to the test run.
    pub async fn new_artifact(
        &self,
        artifact_type: &ArtifactType,
    ) -> Result<Box<DynArtifact>, Error> {
        self.reporter
            .new_artifact(&EntityId::TestRun, artifact_type)
            .await
            .map(|artifact| (self.artifact_wrapper)(artifact_type, artifact))
    }

    /// Create a new directory artifact scoped to the test run.
    pub async fn new_directory_artifact(
        &self,
        artifact_type: &DirectoryArtifactType,
        component_moniker: Option<String>,
    ) -> Result<Box<DynDirectoryArtifact>, Error> {
        self.reporter
            .new_directory_artifact(&EntityId::TestRun, artifact_type, component_moniker)
            .await
    }

    /// Record that the test run has started.
    pub async fn started(&self, timestamp: Timestamp) -> Result<(), Error> {
        self.reporter.entity_started(&EntityId::TestRun, timestamp).await
    }

    /// Record the outcome of the test run.
    pub async fn stopped(
        &self,
        outcome: &ReportedOutcome,
        timestamp: Timestamp,
    ) -> Result<(), Error> {
        self.reporter.entity_stopped(&EntityId::TestRun, outcome, timestamp).await
    }

    /// Record a new suite under the test run.
    pub async fn new_suite(
        &self,
        url: &str,
        suite_id: &SuiteId,
    ) -> Result<SuiteReporter<'_>, Error> {
        self.reporter.new_entity(&EntityId::Suite(*suite_id), url).await?;
        Ok(SuiteReporter {
            reporter: self.reporter.as_ref(),
            artifact_wrapper: self.artifact_wrapper.as_ref(),
            suite_id: *suite_id,
        })
    }

    /// Finalize and persist the test run.
    pub async fn finished(self) -> Result<(), Error> {
        self.reporter.entity_finished(&EntityId::TestRun).await
    }
}

impl<'a> SuiteReporter<'a> {
    /// Create a new artifact scoped to the suite.
    pub async fn new_artifact(
        &self,
        artifact_type: &ArtifactType,
    ) -> Result<Box<DynArtifact>, Error> {
        self.reporter
            .new_artifact(&EntityId::Suite(self.suite_id), artifact_type)
            .await
            .map(|artifact| (self.artifact_wrapper)(artifact_type, artifact))
    }

    /// Create a new directory artifact scoped to the suite.
    pub async fn new_directory_artifact(
        &self,
        artifact_type: &DirectoryArtifactType,
        component_moniker: Option<String>,
    ) -> Result<Box<DynDirectoryArtifact>, Error> {
        self.reporter
            .new_directory_artifact(
                &EntityId::Suite(self.suite_id),
                artifact_type,
                component_moniker,
            )
            .await
    }

    /// Record that the suite has started.
    pub async fn started(&self, timestamp: Timestamp) -> Result<(), Error> {
        self.reporter.entity_started(&EntityId::Suite(self.suite_id), timestamp).await
    }

    /// Record the outcome of the suite.
    pub async fn stopped(
        &self,
        outcome: &ReportedOutcome,
        timestamp: Timestamp,
    ) -> Result<(), Error> {
        self.reporter.entity_stopped(&EntityId::Suite(self.suite_id), outcome, timestamp).await
    }

    /// Record a new suite under the suite.
    pub async fn new_case(&self, name: &str, case_id: &CaseId) -> Result<CaseReporter<'_>, Error> {
        let entity_id = EntityId::Case { suite: self.suite_id, case: *case_id };
        self.reporter.new_entity(&entity_id, name).await?;
        Ok(CaseReporter {
            reporter: self.reporter,
            artifact_wrapper: self.artifact_wrapper,
            entity_id,
        })
    }

    /// Finalize and persist the test run.
    pub async fn finished(self) -> Result<(), Error> {
        self.reporter.entity_finished(&EntityId::Suite(self.suite_id)).await
    }

    /// Set the tags for this suite.
    pub async fn set_tags(&self, tags: Vec<TestTag>) {
        self.reporter
            .set_entity_info(&EntityId::Suite(self.suite_id), &EntityInfo { tags: Some(tags) })
            .await;
    }
}

impl<'a> CaseReporter<'a> {
    /// Create a new artifact scoped to the test case.
    pub async fn new_artifact(
        &self,
        artifact_type: &ArtifactType,
    ) -> Result<Box<DynArtifact>, Error> {
        self.reporter
            .new_artifact(&self.entity_id, artifact_type)
            .await
            .map(|artifact| (self.artifact_wrapper)(artifact_type, artifact))
    }

    /// Create a new directory artifact scoped to the test case
    pub async fn new_directory_artifact(
        &self,
        artifact_type: &DirectoryArtifactType,
        component_moniker: Option<String>,
    ) -> Result<Box<DynDirectoryArtifact>, Error> {
        self.reporter
            .new_directory_artifact(&self.entity_id, artifact_type, component_moniker)
            .await
    }

    /// Record that the case has started.
    pub async fn started(&self, timestamp: Timestamp) -> Result<(), Error> {
        self.reporter.entity_started(&self.entity_id, timestamp).await
    }

    /// Record the outcome of the test case.
    pub async fn stopped(
        &self,
        outcome: &ReportedOutcome,
        timestamp: Timestamp,
    ) -> Result<(), Error> {
        self.reporter.entity_stopped(&self.entity_id, outcome, timestamp).await
    }

    /// Record the outcome of the test case.
    pub async fn finished(self) -> Result<(), Error> {
        self.reporter.entity_finished(&self.entity_id).await
    }
}

/// An enumeration of different known artifact types.
#[derive(Clone, Copy, Debug, Hash, PartialEq, Eq)]
pub enum ArtifactType {
    Stdout,
    Stderr,
    Syslog,
    RestrictedLog,
}

/// An enumeration of different known artifact types consisting of multiple files.
#[derive(Clone, Copy, Debug)]
pub enum DirectoryArtifactType {
    /// An arbitrary set of custom files stored by the test in a directory.
    Custom,
    /// A collection of debug files. For example: coverage and profiling.
    Debug,
}

/// Common outcome type for test results, suites, and test cases.
#[derive(Clone, Copy, PartialEq, Debug)]
pub enum ReportedOutcome {
    Passed,
    Failed,
    Inconclusive,
    Timedout,
    Error,
    Skipped,
    Cancelled,
    DidNotFinish,
}

impl std::fmt::Display for ReportedOutcome {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let repr = match self {
            Self::Passed => "PASSED",
            Self::Failed => "FAILED",
            Self::Inconclusive => "INCONCLUSIVE",
            Self::Timedout => "TIMED_OUT",
            Self::Error => "ERROR",
            Self::Skipped => "SKIPPED",
            Self::Cancelled => "CANCELLED",
            Self::DidNotFinish => "DID_NOT_FINISH",
        };
        write!(f, "{}", repr)
    }
}

impl From<ftest_manager::CaseStatus> for ReportedOutcome {
    fn from(status: ftest_manager::CaseStatus) -> Self {
        match status {
            ftest_manager::CaseStatus::Passed => Self::Passed,
            ftest_manager::CaseStatus::Failed => Self::Failed,
            ftest_manager::CaseStatus::TimedOut => Self::Timedout,
            ftest_manager::CaseStatus::Skipped => Self::Skipped,
            // Test case 'Error' indicates the test failed to report a result, not internal error.
            ftest_manager::CaseStatus::Error => Self::DidNotFinish,
            ftest_manager::CaseStatusUnknown!() => {
                panic!("unrecognized case status");
            }
        }
    }
}

impl From<crate::Outcome> for ReportedOutcome {
    fn from(outcome: crate::Outcome) -> Self {
        match outcome {
            crate::Outcome::Passed => Self::Passed,
            crate::Outcome::Failed => Self::Failed,
            crate::Outcome::Inconclusive => Self::Inconclusive,
            crate::Outcome::Cancelled => Self::Cancelled,
            crate::Outcome::DidNotFinish => Self::DidNotFinish,
            crate::Outcome::Timedout => Self::Timedout,
            crate::Outcome::Error { .. } => Self::Error,
        }
    }
}

impl Into<test_output_directory::ArtifactType> for ArtifactType {
    fn into(self) -> test_output_directory::ArtifactType {
        match self {
            Self::Stdout => test_output_directory::ArtifactType::Stdout,
            Self::Stderr => test_output_directory::ArtifactType::Stderr,
            Self::Syslog => test_output_directory::ArtifactType::Syslog,
            Self::RestrictedLog => test_output_directory::ArtifactType::RestrictedLog,
        }
    }
}

impl Into<test_output_directory::ArtifactType> for DirectoryArtifactType {
    fn into(self) -> test_output_directory::ArtifactType {
        match self {
            Self::Custom => test_output_directory::ArtifactType::Custom,
            Self::Debug => test_output_directory::ArtifactType::Debug,
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
pub struct SuiteId(pub u32);
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
pub struct CaseId(pub u32);

#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
pub enum EntityId {
    TestRun,
    Suite(SuiteId),
    Case { suite: SuiteId, case: CaseId },
}

pub struct EntityInfo {
    pub tags: Option<Vec<TestTag>>,
}

/// A trait for structs that report test results.
/// Implementations of `Reporter` serve as the backend powering `RunReporter`.
///
/// As with `std::io::Write`, `Reporter` implementations are intended to be composable.
/// An implementation of `Reporter` may contain other `Reporter` implementors and delegate
/// calls to them.
#[async_trait]
pub trait Reporter: Send + Sync {
    /// Record a new test suite or test case. This should be called once per entity.
    async fn new_entity(&self, entity: &EntityId, name: &str) -> Result<(), Error>;

    /// Add additional info for an entity.
    async fn set_entity_info(&self, entity: &EntityId, info: &EntityInfo);

    /// Record that a test run, suite or case has started.
    async fn entity_started(&self, entity: &EntityId, timestamp: Timestamp) -> Result<(), Error>;

    /// Record that a test run, suite, or case has stopped.
    async fn entity_stopped(
        &self,
        entity: &EntityId,
        outcome: &ReportedOutcome,
        timestamp: Timestamp,
    ) -> Result<(), Error>;

    /// Record that a test run, suite, or case has stopped. After this method is called for
    /// an entity, no additional events or artifacts may be added to the entity.
    /// Implementations of `Reporter` may assume that `entity_finished` will be called no more
    /// than once for any entity.
    async fn entity_finished(&self, entity: &EntityId) -> Result<(), Error>;

    /// Create a new artifact scoped to the referenced entity.
    async fn new_artifact(
        &self,
        entity: &EntityId,
        artifact_type: &ArtifactType,
    ) -> Result<Box<DynArtifact>, Error>;

    /// Create a new artifact consisting of multiple files.
    async fn new_directory_artifact(
        &self,
        entity: &EntityId,
        artifact_type: &DirectoryArtifactType,
        component_moniker: Option<String>,
    ) -> Result<Box<DynDirectoryArtifact>, Error>;
}

/// A trait for writing artifacts that consist of multiple files organized in a directory.
pub trait DirectoryWrite {
    /// Create a new file within the directory. `path` must be a relative path with no parent
    /// segments.
    fn new_file(&self, path: &Path) -> Result<Box<DynArtifact>, Error>;
}

/// A wrapper around Fuchsia's representation of time.
/// This is added as fuchsia-zircon is not available on host.
#[derive(Clone, Copy)]
pub struct ZxTime(i64);

impl ZxTime {
    pub const fn from_nanos(nanos: i64) -> Self {
        ZxTime(nanos)
    }

    pub fn checked_sub(&self, rhs: Self) -> Option<std::time::Duration> {
        let nanos = self.0 - rhs.0;
        if nanos < 0 {
            None
        } else {
            Some(std::time::Duration::from_nanos(nanos as u64))
        }
    }
}

#[derive(Clone, Copy)]
pub enum Timestamp {
    Unknown,
    Given(ZxTime),
}

impl Timestamp {
    pub fn from_nanos(nanos: Option<i64>) -> Self {
        match nanos {
            None => Self::Unknown,
            Some(n) => Self::Given(ZxTime::from_nanos(n)),
        }
    }
}
