// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::output::{
    ArtifactReporter, ArtifactType, CaseId, EntityId, ReportedOutcome, Reporter, SuiteId,
};
use std::io::{Error, Sink};

/// A reporter that acts as a data sink and does not save results or artifacts.
pub(super) struct NoopReporter;

impl ArtifactReporter for NoopReporter {
    type Writer = Sink;

    fn new_artifact(
        &self,
        _entity: &EntityId,
        _type: &ArtifactType,
    ) -> Result<Self::Writer, Error> {
        Ok(std::io::sink())
    }
}

impl Reporter for NoopReporter {
    fn outcome(&self, _entity: &EntityId, _outcome: &ReportedOutcome) -> Result<(), Error> {
        Ok(())
    }

    fn new_case(&self, _parent: &SuiteId, _name: &str) -> Result<CaseId, Error> {
        Ok(CaseId(0))
    }

    fn new_suite(&self, _url: &str) -> Result<SuiteId, Error> {
        Ok(SuiteId(0))
    }

    fn record(&self, _entity: &EntityId) -> Result<(), Error> {
        Ok(())
    }
}
