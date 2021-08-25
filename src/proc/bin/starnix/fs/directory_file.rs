// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use parking_lot::Mutex;
use std::ops::Bound;

use super::*;
use crate::errno;
use crate::error;
use crate::fd_impl_directory;
use crate::task::*;
use crate::types::*;

// TODO: It should be possible to replace all uses of ROMemoryDirectory with TmpfsDirectory +
// MS_RDONLY (at which point MemoryDirectory would be a better name for it).
pub struct ROMemoryDirectory;
impl FsNodeOps for ROMemoryDirectory {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(MemoryDirectoryFile::new()))
    }

    fn lookup(&self, _node: &FsNode, _name: &FsStr) -> Result<FsNodeHandle, Errno> {
        error!(ENOENT)
    }

    fn unlink(&self, _parent: &FsNode, _name: &FsStr, _child: &FsNodeHandle) -> Result<(), Errno> {
        // TODO: use MS_RDONLY instead
        error!(ENOSYS)
    }
}

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
pub fn emit_dotdot(
    file: &FileObject,
    sink: &mut dyn DirentSink,
    offset: &mut i64,
) -> Result<(), Errno> {
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
    Ok(())
}

impl FileOps for MemoryDirectoryFile {
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
            SeekOrigin::END => None,
        }
        .ok_or(errno!(EINVAL))?;

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
        _task: &Task,
        sink: &mut dyn DirentSink,
    ) -> Result<(), Errno> {
        let mut offset = file.offset.lock();
        emit_dotdot(file, sink, &mut offset)?;

        let mut readdir_position = self.readdir_position.lock();
        file.name.entry.get_children(|children| {
            for (name, maybe_entry) in children.range((readdir_position.clone(), Bound::Unbounded))
            {
                if let Some(entry) = maybe_entry.upgrade() {
                    let next_offset = *offset + 1;
                    let info = entry.node.info();
                    sink.add(
                        entry.node.inode_num,
                        next_offset,
                        DirectoryEntryType::from_mode(info.mode),
                        &name,
                    )?;
                    *offset = next_offset;
                    *readdir_position = Bound::Excluded(name.to_vec());
                }
            }
            Ok(())
        })
    }
}
