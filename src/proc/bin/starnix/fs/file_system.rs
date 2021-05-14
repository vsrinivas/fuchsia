// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_io as fio;
use std::sync::Arc;

pub struct FileSystem {
    // TODO: Replace with a real VFS. This can't last long.
    pub root: Arc<fio::DirectorySynchronousProxy>,
    // TODO: Add cwd and other state here. Some of this state should
    // be copied in FileSystem::fork below.
}

impl FileSystem {
    pub fn new(root: fio::DirectorySynchronousProxy) -> Arc<FileSystem> {
        Arc::new(FileSystem { root: Arc::new(root) })
    }

    pub fn fork(&self) -> Arc<FileSystem> {
        Arc::new(FileSystem { root: self.root.clone() })
    }
}
