// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fd_impl_directory;
use crate::fs::*;
use crate::task::*;
use crate::types::*;

use std::sync::Weak;

/// `ProcDirectory` represents the top-level directory in `procfs`.
///
/// It contains, for example, a directory for each running task, named after the task's pid.
///
/// It also contains a special symlink, `self`, which targets the task directory for the task
/// that reads the symlink.
pub struct ProcDirectory {
    /// The kernel that this directory is associated with. This is used to populate the
    /// directory contents on-demand.
    _kernel: Weak<Kernel>,
}

impl ProcDirectory {
    /// Returns a new `ProcDirectory` exposing information about `kernel`.
    pub fn new(kernel: Weak<Kernel>) -> ProcDirectory {
        ProcDirectory { _kernel: kernel }
    }
}

impl FsNodeOps for ProcDirectory {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(DirectoryFileOps::new()))
    }

    fn lookup(&self, _node: &FsNode, _name: &FsStr) -> Result<FsNodeHandle, Errno> {
        Err(ENOENT)
    }
}

/// `ProcDirectoryOps` implements `FileOps` for the `ProcDirectory`.
///
/// This involves responding to, for example, `readdir` requests by listing a directory for each
/// currently running task in `self.kernel`.
struct DirectoryFileOps {}

impl DirectoryFileOps {
    fn new() -> DirectoryFileOps {
        DirectoryFileOps {}
    }
}

impl FileOps for DirectoryFileOps {
    fd_impl_directory!();

    fn seek(
        &self,
        file: &FileObject,
        _task: &Task,
        offset: off_t,
        whence: SeekOrigin,
    ) -> Result<off_t, Errno> {
        let mut current_offset = file.offset.lock();
        let new_offset = match whence {
            SeekOrigin::SET => Some(offset),
            SeekOrigin::CUR => (*current_offset).checked_add(offset),
            SeekOrigin::END => Some(MAX_LFS_FILESIZE as i64),
        }
        .ok_or(EINVAL)?;

        if new_offset < 0 {
            return Err(EINVAL);
        }

        *current_offset = new_offset;
        Ok(*current_offset)
    }

    fn readdir(
        &self,
        file: &FileObject,
        task: &Task,
        sink: &mut dyn DirentSink,
    ) -> Result<(), Errno> {
        let mut offset = file.offset.lock();
        if *offset == 0 {
            sink.add(file.node().inode_num, 1, DirectoryEntryType::DIR, b".")?;
            *offset += 1;
        }
        if *offset == 1 {
            sink.add(
                file.name.entry.parent_or_self().node.inode_num,
                2,
                DirectoryEntryType::DIR,
                b"..",
            )?;
            *offset += 1;
        }

        // The amount to offset pids by, accounting for the non-pid entries.
        const PID_OFFSET: i32 = 2;

        // Adjust the offset to take the `.` and `..` entries into account. The offset is
        // guaranteed to be >= 2 at this point.
        let adjusted_offset = (*offset - PID_OFFSET as i64) as usize;
        let pids = task.thread_group.kernel.pids.read().task_ids();

        if let Some(start) = pids.iter().position(|pid| *pid as usize >= adjusted_offset) {
            // TODO: What are the requirements (if any) for these inode numbers?
            for pid in &pids[start..] {
                // The + 1 is to set the offset to the next possible pid for subsequent reads.
                let next_offset = (*pid + PID_OFFSET + 1) as i64;
                sink.add(
                    *pid as u64,
                    next_offset,
                    DirectoryEntryType::DIR,
                    format!("{}", pid).as_bytes(),
                )?;
                *offset = next_offset;
            }
        }
        Ok(())
    }
}
