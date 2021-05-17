// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::io::{Error, Write};
use std::path::PathBuf;
use std::sync::Arc;

mod directory;
mod line;
mod noop;
pub use line::{AnsiFilterWriter, MultiplexedWriter, WriteLine};

use directory::DirectoryReporter;
use noop::NoopReporter;

type DynReporter = dyn 'static + Reporter + Send + Sync;
type DynArtifact = dyn 'static + WriteLine + Send + Sync;
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
    case_id: CaseId,
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
                ArtifactType::Stdout => {
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

    // TODO(fxbug.dev/75134): WriteLine isn't suitable for non-text formats, or non-line delimited
    // formats like json. Provide another method for dumping these data.

    /// Record the outcome of the test run.
    pub fn outcome(&self, outcome: &ReportedOutcome) -> Result<(), Error> {
        self.reporter.outcome(&EntityId::TestRun, outcome)
    }

    /// Record a new suite under the test run.
    pub fn new_suite(&self, url: &str) -> Result<SuiteReporter<'_>, Error> {
        let suite_id = self.reporter.new_suite(url)?;
        Ok(SuiteReporter {
            reporter: self.reporter.as_ref(),
            artifact_fn: self.artifact_fn.as_ref(),
            suite_id,
        })
    }

    /// Finalize and persist the test run.
    pub fn record(self) -> Result<(), Error> {
        self.reporter.record(&EntityId::TestRun)
    }
}

impl<'a> SuiteReporter<'a> {
    /// Create a new artifact scoped to the suite.
    pub fn new_artifact(&self, artifact_type: &ArtifactType) -> Result<Box<DynArtifact>, Error> {
        (self.artifact_fn)(&EntityId::Suite(self.suite_id), artifact_type)
    }

    /// Record the outcome of the suite.
    pub fn outcome(&self, outcome: &ReportedOutcome) -> Result<(), Error> {
        self.reporter.outcome(&EntityId::Suite(self.suite_id), outcome)
    }

    /// Record a new suite under the suite.
    pub fn new_case(&self, name: &str) -> Result<CaseReporter<'_>, Error> {
        let case_id = self.reporter.new_case(&self.suite_id, name)?;
        Ok(CaseReporter { reporter: self.reporter, artifact_fn: self.artifact_fn, case_id })
    }

    /// Finalize and persist the test run.
    pub fn record(self) -> Result<(), Error> {
        self.reporter.record(&EntityId::Suite(self.suite_id))
    }
}

impl<'a> CaseReporter<'a> {
    /// Create a new artifact scoped to the test case.
    pub fn new_artifact(&self, artifact_type: &ArtifactType) -> Result<Box<DynArtifact>, Error> {
        (self.artifact_fn)(&EntityId::Case(self.case_id), artifact_type)
    }

    /// Record the outcome of the test case.
    pub fn outcome(&self, outcome: &ReportedOutcome) -> Result<(), Error> {
        self.reporter.outcome(&EntityId::Case(self.case_id), outcome)
    }
}

/// An enumeration of different known artifact types.
pub enum ArtifactType {
    Stdout,
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

impl From<test_executor::TestResult> for ReportedOutcome {
    fn from(result: test_executor::TestResult) -> Self {
        match result {
            test_executor::TestResult::Error => Self::Error,
            test_executor::TestResult::Failed => Self::Failed,
            test_executor::TestResult::Passed => Self::Passed,
            test_executor::TestResult::Skipped => Self::Skipped,
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

#[derive(Clone, Copy)]
struct SuiteId(u64);
#[derive(Clone, Copy)]
struct CaseId(u64);

enum EntityId {
    TestRun,
    Suite(SuiteId),
    Case(CaseId),
}

/// A trait for structs that can save structured test results.
/// Implementations of `Reporter` serve as the backend powering `RunReporter`.
trait Reporter {
    /// Record the outcome of either a test run, test suite, or test case.
    fn outcome(&self, entity: &EntityId, outcome: &ReportedOutcome) -> Result<(), Error>;
    /// Record a new case under the referenced parent suite.
    fn new_case(&self, parent: &SuiteId, name: &str) -> Result<CaseId, Error>;
    /// Record a new suite under the test run.
    fn new_suite(&self, url: &str) -> Result<SuiteId, Error>;
    /// Finalize and persist the outcome and artifacts for an entity. This should only be
    /// called once per entity.
    fn record(&self, entity: &EntityId) -> Result<(), Error>;
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
