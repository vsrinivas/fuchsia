// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use super::node_holder::*;
use crate::fd_impl_directory;
use crate::fs::FsNodeOps;
use crate::fs::*;
use crate::task::Task;
use crate::types::*;

use maplit::hashmap;
use parking_lot::Mutex;
use std::collections::HashMap;

/// The `TaskDirectory` represents a `proc/<pid>` directory.
pub struct TaskDirectory {
    _task: Arc<Task>,

    /// A map that stores lazy initializers for all the entries in the directory.
    nodes: Arc<Mutex<HashMap<FsString, NodeHolder>>>,
}

impl TaskDirectory {
    pub fn new(task: Arc<Task>) -> TaskDirectory {
        let nodes = Arc::new(Mutex::new(hashmap! {
            b"exe".to_vec() => NodeHolder::new(ExeSymlink::new, ExeSymlink::file_mode()),
        }));

        TaskDirectory { _task: task, nodes }
    }
}

impl FsNodeOps for TaskDirectory {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(DirectoryFileOps::new(self.nodes.clone())))
    }

    fn lookup(&self, node: &FsNode, name: &FsStr) -> Result<FsNodeHandle, Errno> {
        match self.nodes.lock().get_mut(name) {
            Some(node_holder) => node_holder.get_or_create_node(&node.fs()),
            None => Err(ENOENT),
        }
    }
}

struct DirectoryFileOps {
    nodes: Arc<Mutex<HashMap<FsString, NodeHolder>>>,
}

impl DirectoryFileOps {
    fn new(nodes: Arc<Mutex<HashMap<FsString, NodeHolder>>>) -> DirectoryFileOps {
        DirectoryFileOps { nodes }
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
        _task: &Task,
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

        // Subtract 2 from the offset, to account for `.` and `..`.
        let node_offset = (*offset - 2) as usize;
        let num_nodes = self.nodes.lock().len();
        if node_offset < num_nodes {
            // Get and sort the keys of the nodes, to keep the traversal order consistent.
            let mut nodes = self.nodes.lock();
            let mut names: Vec<FsString> = nodes.keys().cloned().collect();
            names.sort();

            for name in names {
                // Unwrap is ok here since we are iterating through the keys while holding the
                // lock.
                let node = nodes.get_mut(&name).unwrap();
                let inode_num = node.get_inode_num(&file.fs)?;

                let new_offset = *offset + 1;
                sink.add(inode_num, *offset, DirectoryEntryType::from_mode(node.file_mode), &name)?;
                *offset = new_offset;
            }
        }
        Ok(())
    }
}

/// An `ExeSymlink` points to the `executable_node` (the node that contains the task's binary) of
/// the task that reads it.
pub struct ExeSymlink {}

impl ExeSymlink {
    fn new() -> Box<dyn FsNodeOps> {
        Box::new(ExeSymlink {})
    }

    fn file_mode() -> FileMode {
        FileMode::IFLNK | FileMode::ALLOW_ALL
    }
}

impl FsNodeOps for ExeSymlink {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        unreachable!("Symlink nodes cannot be opened.");
    }

    fn readlink(&self, _node: &FsNode, task: &Task) -> Result<FsString, Errno> {
        if let Some(node) = &*task.executable_node.read() {
            Ok(node.path())
        } else {
            Err(ENOENT)
        }
    }
}
