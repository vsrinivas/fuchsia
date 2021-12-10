// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::output::{
    ArtifactType, DirectoryArtifactType, DirectoryWrite, DynArtifact, DynDirectoryArtifact,
    EntityId, ReportedOutcome, Reporter, Timestamp,
};
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
pub(super) struct MultiplexedReporter<A: Reporter, B: Reporter> {
    a: A,
    b: B,
}

impl<A: Reporter, B: Reporter> MultiplexedReporter<A, B> {
    pub fn new(a: A, b: B) -> Self {
        Self { a, b }
    }
}

impl<A: Reporter, B: Reporter> Reporter for MultiplexedReporter<A, B> {
    fn new_entity(&self, entity: &EntityId, name: &str) -> Result<(), Error> {
        self.a.new_entity(entity, name)?;
        self.b.new_entity(entity, name)
    }

    fn entity_started(&self, entity: &EntityId, timestamp: Timestamp) -> Result<(), Error> {
        self.a.entity_started(entity, timestamp)?;
        self.b.entity_started(entity, timestamp)
    }

    fn entity_stopped(
        &self,
        entity: &EntityId,
        outcome: &ReportedOutcome,
        timestamp: Timestamp,
    ) -> Result<(), Error> {
        self.a.entity_stopped(entity, outcome, timestamp)?;
        self.b.entity_stopped(entity, outcome, timestamp)
    }

    fn entity_finished(&self, entity: &EntityId) -> Result<(), Error> {
        self.a.entity_finished(entity)?;
        self.b.entity_finished(entity)
    }

    fn new_artifact(
        &self,
        entity: &EntityId,
        artifact_type: &ArtifactType,
    ) -> Result<Box<DynArtifact>, Error> {
        Ok(Box::new(MultiplexedWriter::new(
            self.a.new_artifact(entity, artifact_type)?,
            self.b.new_artifact(entity, artifact_type)?,
        )))
    }

    fn new_directory_artifact(
        &self,
        entity: &EntityId,
        artifact_type: &DirectoryArtifactType,
        component_moniker: Option<String>,
    ) -> Result<Box<DynDirectoryArtifact>, Error> {
        Ok(Box::new(MultiplexedDirectoryWriter {
            a: self.a.new_directory_artifact(entity, artifact_type, component_moniker.clone())?,
            b: self.b.new_directory_artifact(entity, artifact_type, component_moniker)?,
        }))
    }
}

/// A directory artifact writer that writes to two contained directory artifact writers.
struct MultiplexedDirectoryWriter {
    a: Box<DynDirectoryArtifact>,
    b: Box<DynDirectoryArtifact>,
}

impl DirectoryWrite for MultiplexedDirectoryWriter {
    fn new_file(&self, path: &Path) -> Result<Box<DynArtifact>, Error> {
        Ok(Box::new(MultiplexedWriter::new(self.a.new_file(path)?, self.b.new_file(path)?)))
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::output::{directory::DirectoryReporter, ArtifactType, RunReporter, SuiteId};
    use tempfile::tempdir;
    use test_output_directory as directory;
    use test_output_directory::testing::{
        assert_run_result, assert_suite_results, parse_json_in_output, ExpectedDirectory,
        ExpectedSuite, ExpectedTestRun,
    };

    #[test]
    fn multiplexed_writer() {
        const WRITTEN: &str = "test output";

        let mut buf_1: Vec<u8> = vec![];
        let mut buf_2: Vec<u8> = vec![];
        let mut multiplexed_writer = MultiplexedWriter::new(&mut buf_1, &mut buf_2);

        multiplexed_writer.write_all(WRITTEN.as_bytes()).expect("write_all failed");
        assert_eq!(std::str::from_utf8(&buf_1).unwrap(), WRITTEN);
        assert_eq!(std::str::from_utf8(&buf_2).unwrap(), WRITTEN);
    }

    #[test]
    fn multiplexed_reporter() {
        let tempdir_1 = tempdir().expect("create temp directory");
        let reporter_1 =
            DirectoryReporter::new(tempdir_1.path().to_path_buf()).expect("Create reporter");
        let tempdir_2 = tempdir().expect("create temp directory");
        let reporter_2 =
            DirectoryReporter::new(tempdir_2.path().to_path_buf()).expect("Create reporter");
        let multiplexed_reporter = MultiplexedReporter::new(reporter_1, reporter_2);

        let run_reporter = RunReporter::new_for_test(multiplexed_reporter);
        run_reporter.started(Timestamp::Unknown).expect("start run");
        let mut run_artifact =
            run_reporter.new_artifact(&ArtifactType::Stdout).expect("create artifact");
        writeln!(run_artifact, "run artifact contents").expect("write to run artifact");
        run_artifact.flush().expect("flush run artifact");

        let suite_reporter = run_reporter.new_suite("suite", &SuiteId(0)).expect("create suite");
        suite_reporter.started(Timestamp::Unknown).expect("start suite");
        suite_reporter.stopped(&ReportedOutcome::Passed, Timestamp::Unknown).expect("start suite");
        let suite_dir_artifact = suite_reporter
            .new_directory_artifact(&DirectoryArtifactType::Custom, None)
            .expect("new artifact");
        let mut suite_artifact =
            suite_dir_artifact.new_file("test.txt".as_ref()).expect("create suite artifact file");
        writeln!(suite_artifact, "suite artifact contents").expect("write to suite artifact");
        suite_artifact.flush().expect("flush suite artifact");
        suite_reporter.finished().expect("finish suite");

        run_reporter.stopped(&ReportedOutcome::Passed, Timestamp::Unknown).expect("stop run");
        run_reporter.finished().expect("finish run");

        let expected_run = ExpectedTestRun::new(directory::Outcome::Passed).with_artifact(
            directory::ArtifactType::Stdout,
            Option::<&str>::None,
            "run artifact contents\n",
        );

        let expected_suites = vec![ExpectedSuite::new("suite", directory::Outcome::Passed)
            .with_directory_artifact(
                directory::ArtifactType::Custom,
                Option::<&str>::None,
                ExpectedDirectory::new().with_file("test.txt", "suite artifact contents\n"),
            )];

        // directories shuold contain identical contents.
        let (run_result_1, suite_results_1) = parse_json_in_output(tempdir_1.path());
        assert_run_result(tempdir_1.path(), &run_result_1, &expected_run);
        assert_suite_results(tempdir_1.path(), &suite_results_1, &expected_suites);

        let (run_result_2, suite_results_2) = parse_json_in_output(tempdir_2.path());
        assert_run_result(tempdir_2.path(), &run_result_2, &expected_run);
        assert_suite_results(tempdir_2.path(), &suite_results_2, &expected_suites);
    }
}
