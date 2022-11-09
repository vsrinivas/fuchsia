// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    scrutiny_utils::{artifact::ArtifactReader, io::ReadSeek},
    std::{
        collections::{HashMap, HashSet},
        io::Cursor,
        path::{Path, PathBuf},
    },
};

/// The result of appending data to a mock implementation.
pub enum AppendResult {
    /// All data added as new data (no duplicates).
    Appended,
    /// Some data merged into existing data (squashing duplicates).
    Merged,
}

pub struct MockArtifactReader {
    artifacts: HashMap<PathBuf, Vec<u8>>,
    deps: HashSet<PathBuf>,
}

impl MockArtifactReader {
    pub fn new() -> Self {
        Self { artifacts: HashMap::new(), deps: HashSet::new() }
    }

    pub fn append_artifact<P: AsRef<Path>>(&mut self, path: P, contents: Vec<u8>) -> AppendResult {
        let path_buf = path.as_ref().to_path_buf();
        if let Some(artifact) = self.artifacts.get_mut(&path_buf) {
            *artifact = contents;
            AppendResult::Merged
        } else {
            self.artifacts.insert(path_buf, contents);
            AppendResult::Appended
        }
    }

    pub fn append_dep(&mut self, path_buf: PathBuf) -> AppendResult {
        if self.deps.insert(path_buf) {
            AppendResult::Appended
        } else {
            AppendResult::Merged
        }
    }
}

impl ArtifactReader for MockArtifactReader {
    fn open(&mut self, path: &Path) -> Result<Box<dyn ReadSeek>> {
        let path_buf = path.to_path_buf();
        let artifact = self.artifacts.get(&path_buf).ok_or_else(|| {
            anyhow!("Mock artifact reader contains no artifact definition for {:?}", path)
        })?;
        Ok(Box::new(Cursor::new(artifact.clone())))
    }

    fn read_bytes(&mut self, path: &Path) -> Result<Vec<u8>> {
        let path_buf = path.to_path_buf();
        self.artifacts.get(&path_buf).map(|artifact| artifact.clone()).ok_or_else(|| {
            anyhow!("Mock artifact reader contains no artifact definition for {:?}", path)
        })
    }

    fn get_deps(&self) -> HashSet<PathBuf> {
        self.deps.clone()
    }
}
