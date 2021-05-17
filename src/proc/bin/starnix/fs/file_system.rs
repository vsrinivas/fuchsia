// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_io as fio;
use parking_lot::RwLock;
use std::sync::Arc;

use crate::types::*;

#[derive(Debug, Clone)]
struct FileSystemState {
    // See <https://man7.org/linux/man-pages/man2/umask.2.html>
    umask: mode_t,
}

impl Default for FileSystemState {
    fn default() -> FileSystemState {
        FileSystemState { umask: 0o022 }
    }
}

pub struct FileSystem {
    // TODO: Replace with a real VFS. This can't last long.
    pub root: Arc<fio::DirectorySynchronousProxy>,

    // TODO: Add cwd and other state here. Some of this state should
    // be copied in FileSystem::fork below.
    state: RwLock<FileSystemState>,
}

impl FileSystem {
    pub fn new(root: fio::DirectorySynchronousProxy) -> Arc<FileSystem> {
        Arc::new(FileSystem {
            root: Arc::new(root),
            state: RwLock::new(FileSystemState::default()),
        })
    }

    pub fn fork(&self) -> Arc<FileSystem> {
        // A child process created via fork(2) inherits its parent's umask.
        // The umask is left unchanged by execve(2).
        //
        // See <https://man7.org/linux/man-pages/man2/umask.2.html>

        Arc::new(FileSystem {
            root: self.root.clone(),
            state: RwLock::new(self.state.read().clone()),
        })
    }

    #[cfg(test)]
    pub fn apply_umask(&self, mode: mode_t) -> mode_t {
        let umask = self.state.read().umask;
        mode & !umask
    }

    pub fn set_umask(&self, umask: mode_t) -> mode_t {
        let mut state = self.state.write();
        let old_umask = state.umask;

        // umask() sets the calling process's file mode creation mask
        // (umask) to mask & 0777 (i.e., only the file permission bits of
        // mask are used), and returns the previous value of the mask.
        //
        // See <https://man7.org/linux/man-pages/man2/umask.2.html>
        state.umask = umask & 0o777;

        old_umask
    }
}

#[cfg(test)]
mod test {
    use fuchsia_async as fasync;

    use crate::testing::*;

    #[fasync::run_singlethreaded(test)]
    async fn test_umask() {
        let fs = create_test_file_system();

        assert_eq!(0o22, fs.set_umask(0o3020));
        assert_eq!(0o646, fs.apply_umask(0o666));
        assert_eq!(0o3646, fs.apply_umask(0o3666));
        assert_eq!(0o20, fs.set_umask(0o11));
    }
}
