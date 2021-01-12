// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fs_management::{Filesystem, Minfs},
    futures::lock::Mutex,
    std::sync::Arc,
    stress_test_utils::{fvm::FvmInstance, instance::InstanceUnderTest, io::Directory},
};

/// Stores information about the running instance of Minfs.
/// This information is needed by the actors to function.
#[derive(Clone)]
pub struct MinfsInstance {
    // Directory to the root of the minfs filesystem
    pub root_dir: Directory,

    /// The minfs, fvm, isolated-devmgr and component manager processes
    processes: Arc<Mutex<Option<(Filesystem<Minfs>, FvmInstance)>>>,
}

impl MinfsInstance {
    pub fn new(minfs: Filesystem<Minfs>, fvm: FvmInstance, root_dir: Directory) -> Self {
        Self { root_dir, processes: Arc::new(Mutex::new(Some((minfs, fvm)))) }
    }

    pub async fn kill_component_manager(&self) {
        let mut lock = self.processes.lock().await;
        let (mut minfs, fvm) = lock.take().unwrap();
        fvm.kill_component_manager();
        let _ = minfs.kill();
    }
}

impl InstanceUnderTest for MinfsInstance {}
