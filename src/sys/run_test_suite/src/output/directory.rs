// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::output::{
    ArtifactReporter, ArtifactType, CaseId, EntityId, ReportedOutcome, Reporter, SuiteId,
};
use std::fs::File;
use std::io::Error;
use std::path::PathBuf;

/// A reporter that saves results and artifacts to disk in the Fuchsia test output format.
// TODO(fxbug.dev/75133): implement.
pub(super) struct DirectoryReporter {
    /// Root directory in which to place results.
    #[allow(unused)]
    root: PathBuf,
}

impl DirectoryReporter {
    pub(super) fn new(root: PathBuf) -> Self {
        Self { root }
    }
}

impl ArtifactReporter for DirectoryReporter {
    type Writer = File;

    fn new_artifact(
        &self,
        _entity: &EntityId,
        _type: &ArtifactType,
    ) -> Result<Self::Writer, Error> {
        unimplemented!()
    }
}

impl Reporter for DirectoryReporter {
    fn outcome(&self, _entity: &EntityId, _outcome: &ReportedOutcome) -> Result<(), Error> {
        unimplemented!()
    }

    fn new_case(&self, _parent: &SuiteId, _name: &str) -> Result<CaseId, Error> {
        unimplemented!()
    }

    fn new_suite(&self, _url: &str) -> Result<SuiteId, Error> {
        unimplemented!()
    }

    fn record(&self, _entity: &EntityId) -> Result<(), Error> {
        unimplemented!()
    }
}
