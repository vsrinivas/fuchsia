// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::output::{ArtifactReporter, ArtifactType, EntityId, ReportedOutcome, Reporter};
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
    fn new_entity(&self, _: &EntityId, _: &str) -> Result<(), Error> {
        Ok(())
    }

    fn entity_started(&self, _: &EntityId) -> Result<(), Error> {
        Ok(())
    }

    fn entity_stopped(&self, _: &EntityId, _: &ReportedOutcome) -> Result<(), Error> {
        Ok(())
    }

    fn entity_finished(&self, _: &EntityId) -> Result<(), Error> {
        Ok(())
    }
}
