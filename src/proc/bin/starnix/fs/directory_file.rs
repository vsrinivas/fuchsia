// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ops::Bound;

use super::*;
use crate::fs::fileops_impl_directory;
use crate::lock::Mutex;
use crate::task::*;
use crate::types::*;

pub struct MemoryDirectoryFile {
    /// The current position for readdir.
    ///
    /// When readdir is called multiple times, we need to return subsequent
    /// directory entries. This field records where the previous readdir
    /// stopped.
    ///
    /// The state is actually recorded twice: once in the offset for this
    /// FileObject and again here. Recovering the state from the offset is slow
    /// because we would need to iterate through the keys of the BTree. Having
    /// the FsString cached lets us search the keys of the BTree faster.
    ///
    /// The initial "." and ".." entries are not recorded here. They are
    /// represented only in the offset field in the FileObject.
    readdir_position: Mutex<Bound<FsString>>,
}

impl MemoryDirectoryFile {
    pub fn new() -> MemoryDirectoryFile {
        MemoryDirectoryFile { readdir_position: Mutex::new(Bound::Unbounded) }
    }
}

/// If the offset is less than 2, emits . and .. entries for the specified file.
///
/// The offset will always be at least 2 after this function returns successfully. It's often
/// necessary to subtract 2 from the offset in subsequent logic.
pub fn emit_dotdot(file: &FileObject, sink: &mut dyn DirentSink) -> Result<(), Errno> {
    if sink.offset() == 0 {
        sink.add(file.node().inode_num, 1, DirectoryEntryType::DIR, b".")?;
    }
    if sink.offset() == 1 {
        sink.add(
            file.name.entry.parent_or_self().node.inode_num,
            2,
            DirectoryEntryType::DIR,
            b"..",
        )?;
    }
    Ok(())
}

impl FileOps for MemoryDirectoryFile {
    fileops_impl_directory!();

    fn seek(
        &self,
        file: &FileObject,
        _current_task: &CurrentTask,
        offset: off_t,
        whence: SeekOrigin,
    ) -> Result<off_t, Errno> {
        let mut current_offset = file.offset.lock();
        let new_offset = match whence {
            SeekOrigin::Set => Some(offset),
            SeekOrigin::Cur => (*current_offset).checked_add(offset),
            SeekOrigin::End => None,
        }
        .ok_or_else(|| errno!(EINVAL))?;

        if new_offset < 0 {
            return error!(EINVAL);
        }

        // Nothing to do.
        if *current_offset == new_offset {
            return Ok(new_offset);
        }

        let mut readdir_position = self.readdir_position.lock();
        *current_offset = new_offset;

        // We use 0 and 1 for "." and ".."
        if new_offset <= 2 {
            *readdir_position = Bound::Unbounded;
        } else {
            file.name.entry.get_children(|children| {
                let count = (new_offset - 2) as usize;
                *readdir_position = children
                    .iter()
                    .take(count)
                    .last()
                    .map_or(Bound::Unbounded, |(name, _)| Bound::Excluded(name.clone()));
            });
        }

        Ok(*current_offset)
    }

    fn readdir(
        &self,
        file: &FileObject,
        _current_task: &CurrentTask,
        sink: &mut dyn DirentSink,
    ) -> Result<(), Errno> {
        emit_dotdot(file, sink)?;

        let mut readdir_position = self.readdir_position.lock();
        file.name.entry.get_children(|children| {
            for (name, maybe_entry) in children.range((readdir_position.clone(), Bound::Unbounded))
            {
                if let Some(entry) = maybe_entry.upgrade() {
                    let mode = entry.node.info().mode;
                    sink.add(
                        entry.node.inode_num,
                        sink.offset() + 1,
                        DirectoryEntryType::from_mode(mode),
                        name,
                    )?;
                    *readdir_position = Bound::Excluded(name.to_vec());
                }
            }
            Ok(())
        })
    }
}
