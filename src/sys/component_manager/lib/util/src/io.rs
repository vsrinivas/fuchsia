// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_io as fio, io_util::clone_directory};

// TODO(https://fxbug.dev/94654): We should probably preserve the original error messages
// instead of dropping them.
pub fn clone_dir(dir: Option<&fio::DirectoryProxy>) -> Option<fio::DirectoryProxy> {
    dir.and_then(|d| clone_directory(d, fio::OpenFlags::CLONE_SAME_RIGHTS).ok())
}
