// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::inode_generation::*;
use super::node_holder::*;
use super::task_directory::*;
use crate::fd_impl_directory;
use crate::fs::*;
use crate::task::*;
use crate::types::*;

use maplit::hashmap;
use parking_lot::Mutex;
use std::collections::HashMap;
use std::sync::Arc;
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
    kernel: Weak<Kernel>,

    /// A map that stores lazy initializers for all the entries that are not "task-directories".
    ///
    /// Stored in an `Arc<Mutex<..>>` so that the nodes can be shared with the directory's
    /// `FileOps`.
    nodes: Arc<Mutex<HashMap<FsString, NodeHolder>>>,
}

impl ProcDirectory {
    /// Returns a new `ProcDirectory` exposing information about `kernel`.
    pub fn new(kernel: Weak<Kernel>) -> ProcDirectory {
        let nodes = Arc::new(Mutex::new(hashmap! {
            b"self".to_vec() => NodeHolder::new(SelfSymlink::new, SelfSymlink::file_mode()),
        }));

        ProcDirectory { kernel, nodes }
    }
}

impl FsNodeOps for ProcDirectory {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(DirectoryFileOps::new(self.nodes.clone())))
    }

    fn lookup(&self, node: &FsNode, name: &FsStr) -> Result<FsNodeHandle, Errno> {
        match self.nodes.lock().get_mut(name) {
            Some(node_holder) => node_holder.get_or_create_node(&node.fs()),
            None => {
                let pid_string = std::str::from_utf8(name).map_err(|_| ENOENT)?;
                let pid = pid_string.parse::<pid_t>().map_err(|_| ENOENT)?;
                if let Some(task) = self.kernel.upgrade().unwrap().pids.read().get_task(pid) {
                    node.fs().get_or_create_node(Some(dir_inode_num(pid)), |inode_num| {
                        Ok(FsNode::new(
                            Box::new(TaskDirectory::new(task)),
                            &node.fs(),
                            inode_num,
                            FileMode::IFDIR | FileMode::ALLOW_ALL,
                        ))
                    })
                } else {
                    Err(ENOENT)
                }
            }
        }
    }
}

/// `ProcDirectoryOps` implements `FileOps` for the `ProcDirectory`.
///
/// This involves responding to, for example, `readdir` requests by listing a directory for each
/// currently running task in `self.kernel`.
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

        // Subtract 2 from the offset, to account for `.` and `..`.
        let node_offset = (*offset - 2) as usize;
        let num_nodes = self.nodes.lock().len();
        if node_offset < num_nodes {
            // Get and sort the keys of the nodes, to keep the traversal order consistent.
            let mut nodes = self.nodes.lock();
            let mut names: Vec<FsString> = nodes.keys().cloned().collect();
            names.sort();

            // Iterate through all the named entries (i.e., non "task directories") and add them to
            // the sink.
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

        // Add 2 to the number of non-"task directories", to account for `.` and `..`.
        let pid_offset = (num_nodes + 2) as i32;

        // Adjust the offset to account for the other nodes in the directory.
        let adjusted_offset = (*offset - pid_offset as i64) as usize;
        // Sort the pids, to keep the traversal order consistent.
        let mut pids = task.thread_group.kernel.pids.read().task_ids();
        pids.sort();

        // The adjusted offset is used to figure out which task directories are to be listed.
        if let Some(start) = pids.iter().position(|pid| *pid as usize >= adjusted_offset) {
            for pid in &pids[start..] {
                // TODO: Figure out if this inode number is fine, given the content of the task
                // directories.
                let inode_num = dir_inode_num(*pid);
                let name = format!("{}", pid);

                // The + 1 is to set the offset to the next possible pid for subsequent reads.
                let next_offset = (*pid + pid_offset + 1) as i64;
                sink.add(inode_num, next_offset, DirectoryEntryType::DIR, name.as_bytes())?;
                *offset = next_offset;
            }
        }

        Ok(())
    }
}

/// A node that represents a symlink to `proc/<pid>` where <pid> is the pid of the task that
/// reads the `proc/self` symlink.
struct SelfSymlink {}

impl SelfSymlink {
    fn new() -> Box<dyn FsNodeOps> {
        Box::new(SelfSymlink {})
    }

    fn file_mode() -> FileMode {
        FileMode::IFLNK | FileMode::ALLOW_ALL
    }
}

impl FsNodeOps for SelfSymlink {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        unreachable!("Symlink nodes cannot be opened.");
    }

    fn readlink(&self, _node: &FsNode, task: &Task) -> Result<FsString, Errno> {
        Ok(format!("{}", task.id).as_bytes().to_vec())
    }
}
