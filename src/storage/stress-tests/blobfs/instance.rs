// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fs_management::{Blobfs, Filesystem},
    futures::lock::Mutex,
    std::sync::Arc,
    stress_test_utils::{fvm::FvmInstance, instance::InstanceUnderTest, io::Directory},
};
/// Stores information about the running instance of Blobfs.
/// This information is needed by the BlobfsOperator to function.
#[derive(Clone)]
pub struct BlobfsInstance {
    // Directory to the root of the blobfs filesystem
    pub root_dir: Directory,

    /// The blobfs, fvm, isolated-devmgr and component manager processes
    processes: Arc<Mutex<Option<(Filesystem<Blobfs>, FvmInstance)>>>,
}

impl BlobfsInstance {
    pub fn new(blobfs: Filesystem<Blobfs>, fvm: FvmInstance, root_dir: Directory) -> Self {
        Self { root_dir, processes: Arc::new(Mutex::new(Some((blobfs, fvm)))) }
    }

    pub async fn kill_component_manager(&self) {
        let mut lock = self.processes.lock().await;
        let (mut blobfs, fvm) = lock.take().unwrap();
        fvm.kill_component_manager();
        let _ = blobfs.kill();
    }
}

impl InstanceUnderTest for BlobfsInstance {}
