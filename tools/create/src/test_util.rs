// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use lazy_static::lazy_static;
use std::env;
use std::ffi::OsString;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, MutexGuard};
use tempfile::{tempdir, TempDir};

lazy_static! {
    static ref ENV_MUTEX: Mutex<()> = Mutex::new(());
}

const FUCHSIA_DIR_KEY: &'static str = "FUCHSIA_DIR";

/// A test environment that has the current working directory
/// and the FUCHSIA_DIR environment variable set to a temporary
/// directory.
///
/// This test environment only permits serial execution of threads
/// to prevent other tests from overwriting environment variables.
///
/// Once dropped, the current working directory and FUCHSIA_DIR
/// environment variable are restored to their previous values.
pub struct LockedEnv<'a> {
    _guard: MutexGuard<'a, ()>,
    temp_dir: TempDir,
    fuchsia_dir_snapshot: Option<OsString>,
    current_dir_snapshot: PathBuf,
}

impl<'a> LockedEnv<'a> {
    pub fn path(&self) -> &Path {
        self.temp_dir.path()
    }
}

impl<'a> Drop for LockedEnv<'a> {
    fn drop(&mut self) {
        env::set_current_dir(&self.current_dir_snapshot)
            .expect("previous current_dir value should always be valid");
        if let Some(fuchsia_dir_snapshot) = &self.fuchsia_dir_snapshot {
            env::set_var(FUCHSIA_DIR_KEY, fuchsia_dir_snapshot);
        } else {
            env::remove_var(FUCHSIA_DIR_KEY);
        }
    }
}

/// Creates and locks a test environment with the current working directory and
/// FUCHSIA_DIR environment variable set to a temporary directory.
///
/// Using this environment prevents multiple tests from modifying environment
/// variables and interfering with each other.
pub fn lock_test_environment<'a>() -> LockedEnv<'a> {
    let guard = match ENV_MUTEX.lock() {
        Ok(guard) => guard,
        // If a test failed, it will have panicked while holding
        // this lock. We don't care, we just want to ensure serial
        // execution.
        Err(poisoned) => poisoned.into_inner(),
    };
    let current_dir_snapshot = env::current_dir().expect("failed to get current_dir");
    let fuchsia_dir_snapshot = env::var_os(FUCHSIA_DIR_KEY);
    let temp_dir = tempdir().expect("failed to create temp dir");
    env::set_current_dir(temp_dir.path()).expect("failed to set new current_dir");
    env::set_var(FUCHSIA_DIR_KEY, temp_dir.path());
    LockedEnv { _guard: guard, temp_dir, fuchsia_dir_snapshot, current_dir_snapshot }
}
