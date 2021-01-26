// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_io::{OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    fs_management::{Filesystem, Minfs},
    fuchsia_zircon::Status,
    futures::lock::Mutex,
    std::collections::HashMap,
    std::sync::Arc,
    stress_test_utils::{fvm::FvmInstance, instance::InstanceUnderTest, io::Directory},
};

/// Stores information about the running instance of Minfs.
/// This information is needed by the actors to function.
#[derive(Clone)]
pub struct MinfsInstance {
    // Directory to the root of the minfs filesystem
    root_dir: Directory,

    // A map of filename to directory connections. See |open_dir|.
    dir_connections: HashMap<String, Directory>,

    /// The minfs, fvm, isolated-devmgr and component manager processes
    processes: Arc<Mutex<Option<(Filesystem<Minfs>, FvmInstance)>>>,
}

impl MinfsInstance {
    pub fn new(minfs: Filesystem<Minfs>, fvm: FvmInstance, root_dir: Directory) -> Self {
        Self {
            root_dir,
            dir_connections: HashMap::new(),
            processes: Arc::new(Mutex::new(Some((minfs, fvm)))),
        }
    }

    // Opens |path| (which should have no slashes) as a dir in the instance's root directory.
    // If |path| doesn't exist, creates it as a new directory.
    // The connection may be cached, in which case this returns immediately.
    pub async fn open_dir(&mut self, path: &str) -> Result<&Directory, Status> {
        if !self.dir_connections.contains_key(path) {
            let conn = match self
                .root_dir
                .create_directory(path, OPEN_RIGHT_WRITABLE | OPEN_RIGHT_READABLE)
                .await
            {
                Ok(d) => Ok(d),
                Err(Status::PEER_CLOSED) => Err(Status::PEER_CLOSED),
                Err(s) => panic!("Unexpected error {} in open_dir({})", s, path),
            }?;
            self.dir_connections.insert(path.to_string(), conn);
        }
        Ok(&self.dir_connections[path])
    }

    pub async fn kill_component_manager(&self) {
        let mut lock = self.processes.lock().await;
        let (mut minfs, fvm) = lock.take().unwrap();
        fvm.kill_component_manager();
        let _ = minfs.kill();
    }
}

impl InstanceUnderTest for MinfsInstance {}
