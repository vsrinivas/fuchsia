// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::output::{
    ArtifactType, DirectoryArtifactType, DirectoryWrite, DynArtifact, DynDirectoryArtifact,
    EntityId, ReportedOutcome, Reporter, Timestamp,
};
use std::{io::Error, path::Path};

/// A reporter that acts as a data sink and does not save results or artifacts.
pub(super) struct NoopReporter;
struct NoopDirectoryWriter;

impl Reporter for NoopReporter {
    fn new_entity(&self, _: &EntityId, _: &str) -> Result<(), Error> {
        Ok(())
    }

    fn entity_started(&self, _: &EntityId, _: Timestamp) -> Result<(), Error> {
        Ok(())
    }

    fn entity_stopped(&self, _: &EntityId, _: &ReportedOutcome, _: Timestamp) -> Result<(), Error> {
        Ok(())
    }

    fn entity_finished(&self, _: &EntityId) -> Result<(), Error> {
        Ok(())
    }

    fn new_artifact(
        &self,
        _entity: &EntityId,
        _type: &ArtifactType,
    ) -> Result<Box<DynArtifact>, Error> {
        Ok(Box::new(std::io::sink()))
    }

    fn new_directory_artifact(
        &self,
        _entity: &EntityId,
        _artifact_type: &DirectoryArtifactType,
        _component_moniker: Option<String>,
    ) -> Result<Box<DynDirectoryArtifact>, Error> {
        Ok(Box::new(NoopDirectoryWriter))
    }
}

impl DirectoryWrite for NoopDirectoryWriter {
    fn new_file(&self, _path: &Path) -> Result<Box<DynArtifact>, Error> {
        Ok(Box::new(std::io::sink()))
    }
}
