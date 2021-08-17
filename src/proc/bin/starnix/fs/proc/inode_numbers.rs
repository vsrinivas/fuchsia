// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::types::*;

/// Returns the inode number to use for the `/proc/<task.id>` node.
///
/// This `ino_t` can be used as an offset to calculate the inode numbers for the nodes within
/// the `proc/<pid>` directory. All the inode numbers between dir_inode_num(pid) and
/// dir_inode_num(pid + 1) are considered "reserved" for the content of that pid directory.
pub fn dir_inode_num(pid: pid_t) -> ino_t {
    pid as u64 * 2_u64.pow(16)
}

/// Returns the inode offset to use for the nodes in `/proc/<task.id>/fd/`.
///
/// This `ino_t` can be used as an offset to calculate the inode numbers for the file descriptors.
pub fn fd_inode_num(pid: pid_t) -> ino_t {
    // The offset to use for entries in the `fd` directory.
    const FD_INODE_DIR_OFFSET: u64 = 10;
    // All the inode numbers between dir_inode_num(pid) and dir_inode_num(pid + 1) are considered
    // "reserved" for the content of that pid directory. The offset makes room for existing
    // files in the pid directory, but will have to be kept in sync with the other directories
    // that share the pid's inode number range.
    dir_inode_num(pid) + FD_INODE_DIR_OFFSET
}
