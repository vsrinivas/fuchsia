// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_io as fio;

pub struct FileSystem {
    // TODO: Replace with a real VFS. This can't last long.
    pub root: fio::DirectorySynchronousProxy,
}

impl FileSystem {
    pub fn new(root: fio::DirectorySynchronousProxy) -> FileSystem {
        FileSystem { root }
    }
}
