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
    pub root: fio::DirectoryProxy,
}

impl FileSystem {
    pub fn new(root: fio::DirectoryProxy) -> FileSystem {
        FileSystem { root }
    }
}
