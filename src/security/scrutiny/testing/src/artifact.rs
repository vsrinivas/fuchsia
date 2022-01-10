// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    scrutiny_utils::artifact::ArtifactReader,
    std::{collections::HashSet, sync::RwLock},
};

pub struct MockArtifactReader {
    bytes: RwLock<Vec<Vec<u8>>>,
    deps: HashSet<String>,
}

impl MockArtifactReader {
    pub fn new() -> Self {
        Self { bytes: RwLock::new(Vec::new()), deps: HashSet::new() }
    }

    pub fn append_bytes(&self, byte_vec: Vec<u8>) {
        self.bytes.write().unwrap().push(byte_vec);
    }
}

impl ArtifactReader for MockArtifactReader {
    fn read_raw(&mut self, path: &str) -> Result<Vec<u8>> {
        let mut borrow = self.bytes.write().unwrap();
        {
            if borrow.len() == 0 {
                return Err(anyhow!("No more byte vectors left to return. Maybe append more?"));
            }
            self.deps.insert(path.to_string());
            Ok(borrow.remove(0))
        }
    }

    fn get_deps(&self) -> HashSet<String> {
        self.deps.clone()
    }
}
