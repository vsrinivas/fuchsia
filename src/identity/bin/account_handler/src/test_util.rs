// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module provides common constants and helpers to assist in the unit-testing of other
//! modules within the crate.

use {
    crate::common::AccountLifetime,
    account_common::{AccountId, PersonaId},
    lazy_static::lazy_static,
    std::path::PathBuf,
    tempfile::TempDir,
};

lazy_static! {
    pub static ref TEST_ACCOUNT_ID: AccountId = AccountId::new(111111);
    pub static ref TEST_PERSONA_ID: PersonaId = PersonaId::new(222222);
}

pub static TEST_ACCOUNT_ID_UINT: u64 = 111111;

pub struct TempLocation {
    /// A fresh temp directory that will be deleted when this object is dropped.
    _dir: TempDir,
    /// A path to an existing temp dir.
    pub path: PathBuf,
}

impl TempLocation {
    /// Return a writable, temporary location and optionally create it as a directory.
    pub fn new() -> TempLocation {
        let dir = TempDir::new().unwrap();
        let path = dir.path().to_path_buf();
        TempLocation { _dir: dir, path }
    }

    /// Returns a path to a static test path inside the temporary location which does not exist by
    /// default.
    #[allow(unused)]
    pub fn test_path(&self) -> PathBuf {
        self.path.join("test_path")
    }

    /// Returns a persistent AccountLifetime with the path set to this TempLocation's path.
    pub fn to_persistent_lifetime(&self) -> AccountLifetime {
        AccountLifetime::Persistent { account_dir: self.path.clone() }
    }
}
