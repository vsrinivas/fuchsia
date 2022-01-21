// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::output::{
    ArtifactType, DirectoryArtifactType, DirectoryWrite, DynArtifact, DynDirectoryArtifact,
    EntityId, ReportedOutcome, Reporter, Timestamp,
};
use async_trait::async_trait;
use parking_lot::Mutex;
use std::{collections::HashMap, io::Error, io::Write, path::Path, path::PathBuf, sync::Arc};

/// A reporter that acts as a data sink and stores results for inspection in memory.
///
/// Primarily used for testing.
#[derive(Clone)]
pub struct InMemoryReporter {
    pub entities: Arc<Mutex<HashMap<EntityId, EntityReport>>>,
}

#[derive(Clone)]
pub struct InMemoryReport {
    pub id: EntityId,
    pub report: EntityReport,
}

impl InMemoryReporter {
    pub fn new() -> Self {
        Self { entities: Arc::new(Mutex::new(HashMap::new())) }
    }

    pub fn get_reports(&self) -> Vec<InMemoryReport> {
        self.entities
            .lock()
            .iter()
            .map(|(key, value)| InMemoryReport { id: key.clone(), report: value.clone() })
            .collect::<Vec<_>>()
    }
}

#[derive(Clone)]
pub struct InMemoryDirectoryWriter {
    pub moniker: Option<String>,
    pub files: Arc<Mutex<Vec<(PathBuf, InMemoryArtifact)>>>,
}

impl Default for InMemoryDirectoryWriter {
    fn default() -> Self {
        Self { moniker: None, files: Arc::new(Mutex::new(vec![])) }
    }
}

#[derive(Default, Clone)]
pub struct EntityReport {
    pub name: String,
    pub started_time: Option<Timestamp>,
    pub stopped_time: Option<Timestamp>,
    pub outcome: Option<ReportedOutcome>,
    pub is_finished: bool,
    pub artifacts: Vec<(ArtifactType, InMemoryArtifact)>,
    pub directories: Vec<(DirectoryArtifactType, InMemoryDirectoryWriter)>,
}

#[derive(Clone)]
pub struct InMemoryArtifact {
    contents: Arc<Mutex<Vec<u8>>>,
}

impl InMemoryArtifact {
    pub fn new() -> Self {
        Self { contents: Arc::new(Mutex::new(Vec::new())) }
    }

    /// Obtain a copy of the contained buffer.
    pub fn get_contents(&self) -> Vec<u8> {
        self.contents.lock().clone()
    }
}

impl Write for InMemoryArtifact {
    fn write(&mut self, val: &[u8]) -> Result<usize, std::io::Error> {
        self.contents.lock().write(val)
    }
    fn flush(&mut self) -> Result<(), std::io::Error> {
        self.contents.lock().flush()
    }
}

#[async_trait]
impl Reporter for InMemoryReporter {
    async fn new_entity(&self, id: &EntityId, name: &str) -> Result<(), Error> {
        self.entities.lock().entry(*id).or_default().name = name.to_string();
        Ok(())
    }

    async fn entity_started(&self, id: &EntityId, timestamp: Timestamp) -> Result<(), Error> {
        self.entities.lock().entry(*id).or_default().started_time = Some(timestamp);
        Ok(())
    }

    async fn entity_stopped(
        &self,
        id: &EntityId,
        outcome: &ReportedOutcome,
        timestamp: Timestamp,
    ) -> Result<(), Error> {
        let mut entities = self.entities.lock();
        let e = entities.entry(*id).or_default();
        e.stopped_time = Some(timestamp);
        e.outcome = Some(*outcome);
        Ok(())
    }

    async fn entity_finished(&self, id: &EntityId) -> Result<(), Error> {
        let mut entities = self.entities.lock();
        let e = entities.entry(*id).or_default();
        e.is_finished = true;
        Ok(())
    }

    async fn new_artifact(
        &self,
        id: &EntityId,
        artifact_type: &ArtifactType,
    ) -> Result<Box<DynArtifact>, Error> {
        let mut entities = self.entities.lock();
        let e = entities.entry(*id).or_default();
        let artifact = InMemoryArtifact::new();
        e.artifacts.push((*artifact_type, artifact.clone()));

        Ok(Box::new(artifact))
    }

    async fn new_directory_artifact(
        &self,
        id: &EntityId,
        artifact_type: &DirectoryArtifactType,
        moniker: Option<String>,
    ) -> Result<Box<DynDirectoryArtifact>, Error> {
        let mut entities = self.entities.lock();
        let e = entities.entry(*id).or_default();
        let mut dir = InMemoryDirectoryWriter::default();
        dir.moniker = moniker;

        e.directories.push((*artifact_type, dir.clone()));

        Ok(Box::new(dir))
    }
}

impl DirectoryWrite for InMemoryDirectoryWriter {
    fn new_file(&self, path: &Path) -> Result<Box<DynArtifact>, Error> {
        let artifact = InMemoryArtifact::new();
        self.files.lock().push((path.to_owned(), artifact.clone()));

        Ok(Box::new(artifact))
    }
}
