// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::output::{
    ArtifactType, DirectoryArtifactType, DirectoryWrite, DynArtifact, DynDirectoryArtifact,
    EntityId, EntityInfo, ReportedOutcome, Reporter, Timestamp,
};
use async_trait::async_trait;
use futures::future::try_join;
use std::{
    io::{Error, Write},
    path::Path,
};

/// A writer that writes to two writers.
pub struct MultiplexedWriter<A: Write, B: Write> {
    a: A,
    b: B,
}

impl<A: Write, B: Write> Write for MultiplexedWriter<A, B> {
    fn write(&mut self, bytes: &[u8]) -> Result<usize, Error> {
        let bytes_written = self.a.write(bytes)?;
        // Since write is allowed to only write a portion of the data,
        // we force a and b to write the same number of bytes.
        self.b.write_all(&bytes[..bytes_written])?;
        Ok(bytes_written)
    }

    fn flush(&mut self) -> Result<(), Error> {
        self.a.flush()?;
        self.b.flush()
    }
}

impl<A: Write, B: Write> MultiplexedWriter<A, B> {
    pub fn new(a: A, b: B) -> Self {
        Self { a, b }
    }
}

/// A reporter that reports results to two contained reporters.
pub struct MultiplexedReporter<A: Reporter, B: Reporter> {
    a: A,
    b: B,
}

impl<A: Reporter, B: Reporter> MultiplexedReporter<A, B> {
    pub fn new(a: A, b: B) -> Self {
        Self { a, b }
    }
}

/// A stub that maps ((), ()) to ().
fn map_void(_: ((), ())) {
    ()
}

#[async_trait]
impl<A: Reporter, B: Reporter> Reporter for MultiplexedReporter<A, B> {
    async fn new_entity(&self, entity: &EntityId, name: &str) -> Result<(), Error> {
        try_join(self.a.new_entity(entity, name), self.b.new_entity(entity, name))
            .await
            .map(map_void)
    }

    async fn set_entity_info(&self, entity: &EntityId, info: &EntityInfo) {
        futures::future::join(
            self.a.set_entity_info(entity, info),
            self.b.set_entity_info(entity, info),
        )
        .await;
    }

    async fn entity_started(&self, entity: &EntityId, timestamp: Timestamp) -> Result<(), Error> {
        try_join(self.a.entity_started(entity, timestamp), self.b.entity_started(entity, timestamp))
            .await
            .map(map_void)
    }

    async fn entity_stopped(
        &self,
        entity: &EntityId,
        outcome: &ReportedOutcome,
        timestamp: Timestamp,
    ) -> Result<(), Error> {
        try_join(
            self.a.entity_stopped(entity, outcome, timestamp),
            self.b.entity_stopped(entity, outcome, timestamp),
        )
        .await
        .map(map_void)
    }

    async fn entity_finished(&self, entity: &EntityId) -> Result<(), Error> {
        try_join(self.a.entity_finished(entity), self.b.entity_finished(entity)).await.map(map_void)
    }

    async fn new_artifact(
        &self,
        entity: &EntityId,
        artifact_type: &ArtifactType,
    ) -> Result<Box<DynArtifact>, Error> {
        let (a, b) = try_join(
            self.a.new_artifact(entity, artifact_type),
            self.b.new_artifact(entity, artifact_type),
        )
        .await?;
        Ok(Box::new(MultiplexedWriter::new(a, b)))
    }

    async fn new_directory_artifact(
        &self,
        entity: &EntityId,
        artifact_type: &DirectoryArtifactType,
        component_moniker: Option<String>,
    ) -> Result<Box<DynDirectoryArtifact>, Error> {
        let (a, b) = try_join(
            self.a.new_directory_artifact(entity, artifact_type, component_moniker.clone()),
            self.b.new_directory_artifact(entity, artifact_type, component_moniker),
        )
        .await?;

        Ok(Box::new(MultiplexedDirectoryWriter { a, b }))
    }
}

/// A directory artifact writer that writes to two contained directory artifact writers.
pub(super) struct MultiplexedDirectoryWriter {
    a: Box<DynDirectoryArtifact>,
    b: Box<DynDirectoryArtifact>,
}

impl MultiplexedDirectoryWriter {
    pub(super) fn new(a: Box<DynDirectoryArtifact>, b: Box<DynDirectoryArtifact>) -> Self {
        Self { a, b }
    }
}

impl DirectoryWrite for MultiplexedDirectoryWriter {
    fn new_file(&self, path: &Path) -> Result<Box<DynArtifact>, Error> {
        Ok(Box::new(MultiplexedWriter::new(self.a.new_file(path)?, self.b.new_file(path)?)))
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::output::{
        directory::{DirectoryReporter, SchemaVersion},
        ArtifactType, RunReporter, SuiteId,
    };
    use tempfile::tempdir;
    use test_output_directory as directory;
    use test_output_directory::testing::{
        assert_run_result, ExpectedDirectory, ExpectedSuite, ExpectedTestRun,
    };

    #[fuchsia::test]
    fn multiplexed_writer() {
        const WRITTEN: &str = "test output";

        let mut buf_1: Vec<u8> = vec![];
        let mut buf_2: Vec<u8> = vec![];
        let mut multiplexed_writer = MultiplexedWriter::new(&mut buf_1, &mut buf_2);

        multiplexed_writer.write_all(WRITTEN.as_bytes()).expect("write_all failed");
        assert_eq!(std::str::from_utf8(&buf_1).unwrap(), WRITTEN);
        assert_eq!(std::str::from_utf8(&buf_2).unwrap(), WRITTEN);
    }

    #[fuchsia::test]
    async fn multiplexed_reporter() {
        let tempdir_1 = tempdir().expect("create temp directory");
        let reporter_1 = DirectoryReporter::new(tempdir_1.path().to_path_buf(), SchemaVersion::V1)
            .expect("Create reporter");
        let tempdir_2 = tempdir().expect("create temp directory");
        let reporter_2 = DirectoryReporter::new(tempdir_2.path().to_path_buf(), SchemaVersion::V1)
            .expect("Create reporter");
        let multiplexed_reporter = MultiplexedReporter::new(reporter_1, reporter_2);

        let run_reporter = RunReporter::new(multiplexed_reporter);
        run_reporter.started(Timestamp::Unknown).await.expect("start run");
        let mut run_artifact =
            run_reporter.new_artifact(&ArtifactType::Stdout).await.expect("create artifact");
        writeln!(run_artifact, "run artifact contents").expect("write to run artifact");
        run_artifact.flush().expect("flush run artifact");

        let suite_reporter =
            run_reporter.new_suite("suite", &SuiteId(0)).await.expect("create suite");
        suite_reporter.started(Timestamp::Unknown).await.expect("start suite");
        suite_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("start suite");
        let suite_dir_artifact = suite_reporter
            .new_directory_artifact(&DirectoryArtifactType::Custom, None)
            .await
            .expect("new artifact");
        let mut suite_artifact =
            suite_dir_artifact.new_file("test.txt".as_ref()).expect("create suite artifact file");
        writeln!(suite_artifact, "suite artifact contents").expect("write to suite artifact");
        suite_artifact.flush().expect("flush suite artifact");
        suite_reporter.finished().await.expect("finish suite");

        run_reporter.stopped(&ReportedOutcome::Passed, Timestamp::Unknown).await.expect("stop run");
        run_reporter.finished().await.expect("finish run");

        let expected_run = ExpectedTestRun::new(directory::Outcome::Passed)
            .with_artifact(
                directory::ArtifactType::Stdout,
                Option::<&str>::None,
                "run artifact contents\n",
            )
            .with_suite(
                ExpectedSuite::new("suite", directory::Outcome::Passed).with_directory_artifact(
                    directory::ArtifactType::Custom,
                    Option::<&str>::None,
                    ExpectedDirectory::new().with_file("test.txt", "suite artifact contents\n"),
                ),
            );

        // directories shuold contain identical contents.
        assert_run_result(tempdir_1.path(), &expected_run);
        assert_run_result(tempdir_2.path(), &expected_run);
    }
}
