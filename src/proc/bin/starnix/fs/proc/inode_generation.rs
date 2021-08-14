// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::types::*;

/// Returns the inode number to use for the `/proc/<task.id>` node.
///
/// This `ino_t` can be used as an offset to calculate the inode numbers for the nodes within
/// the `proc/<pid>` directory.
pub fn dir_inode_num(pid: pid_t) -> ino_t {
    pid as u64 * 2_u64.pow(16)
}
