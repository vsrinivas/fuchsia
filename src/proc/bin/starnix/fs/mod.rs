// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_io as fio;

mod fd;
mod fuchsia;

pub use fd::*;
pub use fuchsia::*;

pub struct FileSystem {
    // TODO: Replace with a real VFS. This can't last long.
    pub root: fio::DirectorySynchronousProxy,
}

impl FileSystem {
    pub fn new(root: fio::DirectorySynchronousProxy) -> FileSystem {
        FileSystem { root }
    }
}

#[cfg(test)]
pub mod test {
    use super::*;
    use fidl::endpoints::Proxy;
    use fidl_fuchsia_io as fio;
    use io_util::directory;
    use std::sync::Arc;

    /// Create a FileSystem for use in testing.
    ///
    /// Open "/pkg" and returns a FileSystem rooted in that directory.
    pub fn create_test_file_system() -> Arc<FileSystem> {
        let root = directory::open_in_namespace(
            "/pkg",
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
        )
        .expect("failed to open /pkg");
        return Arc::new(FileSystem::new(fio::DirectorySynchronousProxy::new(
            root.into_channel().unwrap().into_zx_channel(),
        )));
    }
}
