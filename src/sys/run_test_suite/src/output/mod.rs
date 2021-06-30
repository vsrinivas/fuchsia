// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::io::{Error, Write};
use std::path::PathBuf;
use std::sync::Arc;

mod directory;
mod line;
mod noop;
pub use line::{AnsiFilterWriter, MultiplexedWriter};

use directory::DirectoryReporter;
use fidl_fuchsia_test_manager as ftest_manager;
use noop::NoopReporter;

type DynReporter = dyn 'static + Reporter + Send + Sync;
type DynArtifact = dyn 'static + Write + Send + Sync;
type NewArtifactFn =
    dyn Fn(&EntityId, &ArtifactType) -> Result<Box<DynArtifact>, Error> + Send + Sync;

/// A reporter for structured results scoped to a test run.
/// To report results and artifacts for a test run, a user should create a `RunReporter`,
/// and create child `SuiteReporter`s and `CaseReporter`s beneath it to report results and
/// artifacts scoped to suites and cases. When the run is finished, the user should call
/// `record` to ensure the results are persisted.
pub struct RunReporter {
    /// Inner `Reporter` implementation used to record results.
    reporter: Arc<DynReporter>,
    /// Function used to produce a new artifact. This handles wrapping writers in any
    /// filters as necessary.
    artifact_fn: Box<NewArtifactFn>,
}

/// A reporter for structured results scoped to a test suite. Note this may not outlive
/// the `RunReporter` from which it is created.
pub struct SuiteReporter<'a> {
    reporter: &'a DynReporter,
    artifact_fn: &'a NewArtifactFn,
    suite_id: SuiteId,
}

/// A reporter for structured results scoped to a single test case. Note this may not outlive
/// the `SuiteReporter` from which it is created.
pub struct CaseReporter<'a> {
    reporter: &'a DynReporter,
    artifact_fn: &'a NewArtifactFn,
    entity_id: EntityId,
}

impl RunReporter {
    /// Create a `RunReporter` that simply discards any artifacts or results it is given.
    pub fn new_noop() -> Self {
        let reporter: Arc<NoopReporter> = Arc::new(NoopReporter);
        let reporter_dyn = reporter.clone() as Arc<DynReporter>;
        let artifact_fn = Box::new(move |entity: &EntityId, artifact_type: &ArtifactType| {
            let inner = reporter.new_artifact(entity, artifact_type)?;
            Ok(Box::new(inner) as Box<DynArtifact>)
        });
        Self { reporter: reporter_dyn, artifact_fn }
    }

    /// Create a `RunReporter` that saves artifacts and results to the given directory.
    /// Any stdout artifacts are filtered for ANSI escape sequences before saving.
    pub fn new_ansi_filtered(root: PathBuf) -> Result<Self, Error> {
        let reporter: Arc<DirectoryReporter> = Arc::new(DirectoryReporter::new(root)?);
        let reporter_dyn = reporter.clone() as Arc<DynReporter>;
        let artifact_fn = Box::new(move |entity: &EntityId, artifact_type: &ArtifactType| {
            let unfiltered = reporter.new_artifact(entity, artifact_type)?;
            Ok(match artifact_type {
                ArtifactType::Stdout | ArtifactType::Stderr => {
                    Box::new(AnsiFilterWriter::new(unfiltered)) as Box<DynArtifact>
                }
                ArtifactType::Syslog => Box::new(unfiltered) as Box<DynArtifact>,
            })
        });
        Ok(Self { reporter: reporter_dyn, artifact_fn })
    }

    /// Create a `RunReporter` that saves artifacts and results to the given directory.
    pub fn new(root: PathBuf) -> Result<Self, Error> {
        let reporter: Arc<DirectoryReporter> = Arc::new(DirectoryReporter::new(root)?);
        let reporter_dyn = reporter.clone() as Arc<DynReporter>;
        let artifact_fn = Box::new(move |entity: &EntityId, artifact_type: &ArtifactType| {
            let inner = reporter.new_artifact(entity, artifact_type)?;
            Ok(Box::new(inner) as Box<DynArtifact>)
        });
        Ok(Self { reporter: reporter_dyn, artifact_fn })
    }

    /// Create a new artifact scoped to the test run.
    pub fn new_artifact(&self, artifact_type: &ArtifactType) -> Result<Box<DynArtifact>, Error> {
        (self.artifact_fn)(&EntityId::TestRun, artifact_type)
    }

    /// Record that the test run has started.
    pub fn started(&self) -> Result<(), Error> {
        self.reporter.entity_started(&EntityId::TestRun)
    }

    /// Record the outcome of the test run.
    pub fn stopped(&self, outcome: &ReportedOutcome) -> Result<(), Error> {
        self.reporter.entity_stopped(&EntityId::TestRun, outcome)
    }

    /// Record a new suite under the test run.
    pub fn new_suite(&self, url: &str, suite_id: &SuiteId) -> Result<SuiteReporter<'_>, Error> {
        self.reporter.new_entity(&EntityId::Suite(*suite_id), url)?;
        Ok(SuiteReporter {
            reporter: self.reporter.as_ref(),
            artifact_fn: self.artifact_fn.as_ref(),
            suite_id: *suite_id,
        })
    }

    /// Finalize and persist the test run.
    pub fn finished(self) -> Result<(), Error> {
        self.reporter.entity_finished(&EntityId::TestRun)
    }
}

impl<'a> SuiteReporter<'a> {
    /// Create a new artifact scoped to the suite.
    pub fn new_artifact(&self, artifact_type: &ArtifactType) -> Result<Box<DynArtifact>, Error> {
        (self.artifact_fn)(&EntityId::Suite(self.suite_id), artifact_type)
    }

    /// Record that the suite has started.
    pub fn started(&self) -> Result<(), Error> {
        self.reporter.entity_started(&EntityId::Suite(self.suite_id))
    }

    /// Record the outcome of the suite.
    pub fn stopped(&self, outcome: &ReportedOutcome) -> Result<(), Error> {
        self.reporter.entity_stopped(&EntityId::Suite(self.suite_id), outcome)
    }

    /// Record a new suite under the suite.
    pub fn new_case(&self, name: &str, case_id: &CaseId) -> Result<CaseReporter<'_>, Error> {
        let entity_id = EntityId::Case { suite: self.suite_id, case: *case_id };
        self.reporter.new_entity(&entity_id, name)?;
        Ok(CaseReporter { reporter: self.reporter, artifact_fn: self.artifact_fn, entity_id })
    }

    /// Finalize and persist the test run.
    pub fn finished(self) -> Result<(), Error> {
        self.reporter.entity_finished(&EntityId::Suite(self.suite_id))
    }
}

impl<'a> CaseReporter<'a> {
    /// Create a new artifact scoped to the test case.
    pub fn new_artifact(&self, artifact_type: &ArtifactType) -> Result<Box<DynArtifact>, Error> {
        (self.artifact_fn)(&self.entity_id, artifact_type)
    }

    /// Record that the case has started.
    pub fn started(&self) -> Result<(), Error> {
        self.reporter.entity_started(&self.entity_id)
    }

    /// Record the outcome of the test case.
    pub fn stopped(&self, outcome: &ReportedOutcome) -> Result<(), Error> {
        self.reporter.entity_stopped(&self.entity_id, outcome)
    }

    /// Record the outcome of the test case.
    pub fn finished(self) -> Result<(), Error> {
        self.reporter.entity_finished(&self.entity_id)
    }
}

/// An enumeration of different known artifact types.
pub enum ArtifactType {
    Stdout,
    Stderr,
    Syslog,
}

/// Common outcome type for test results, suites, and test cases.
#[derive(Clone, Copy)]
pub enum ReportedOutcome {
    Passed,
    Failed,
    Inconclusive,
    Timedout,
    Error,
    Skipped,
}

impl From<ftest_manager::CaseStatus> for ReportedOutcome {
    fn from(status: ftest_manager::CaseStatus) -> Self {
        match status {
            ftest_manager::CaseStatus::Passed => Self::Passed,
            ftest_manager::CaseStatus::Failed => Self::Failed,
            ftest_manager::CaseStatus::TimedOut => Self::Timedout,
            ftest_manager::CaseStatus::Skipped => Self::Skipped,
            ftest_manager::CaseStatus::Error => Self::Error,
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
            crate::Outcome::Timedout => Self::Timedout,
            crate::Outcome::Error => Self::Error,
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Hash)]
pub struct SuiteId(pub u32);
#[derive(Clone, Copy, PartialEq, Eq, Hash)]
pub struct CaseId(pub u32);

#[derive(Clone, Copy, PartialEq, Eq, Hash)]
pub enum EntityId {
    TestRun,
    Suite(SuiteId),
    Case { suite: SuiteId, case: CaseId },
}

/// A trait for structs that report test results.
/// Implementations of `Reporter` serve as the backend powering `RunReporter`.
trait Reporter {
    /// Record a new test suite or test case. This should be called once per entity.
    fn new_entity(&self, entity: &EntityId, name: &str) -> Result<(), Error>;

    /// Record that a test run, suite or case has started.
    fn entity_started(&self, entity: &EntityId) -> Result<(), Error>;

    /// Record that a test run, suite, or case has stopped.
    fn entity_stopped(&self, entity: &EntityId, outcome: &ReportedOutcome) -> Result<(), Error>;

    /// Record that a test run, suite, or case has stopped. After this method is called for
    /// an entity, no additional events or artifacts may be added to the entity.
    fn entity_finished(&self, entity: &EntityId) -> Result<(), Error>;
}

/// A trait for structs that produce writers to which artifacts may be streamed.
/// Implementations of `ArtifactReporter` serve as the backend powering `RunReporter` alongside
/// `Reporter`. Note this trait is defined separately to avoid issues with trait objects needing to
/// concrete associated types.
trait ArtifactReporter {
    type Writer: 'static + Write + Send + Sync;

    /// Create a new artifact scoped to the referenced entity.
    fn new_artifact(
        &self,
        entity: &EntityId,
        artifact_type: &ArtifactType,
    ) -> Result<Self::Writer, Error>;
}
