// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::pid_directory::*;
use crate::errno;
use crate::error;
use crate::fd_impl_directory;
use crate::fs::*;
use crate::fs_node_impl_symlink;
use crate::mode;
use crate::task::*;
use crate::types::*;

use maplit::hashmap;
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

    /// A map that stores all the nodes that aren't task directories.
    nodes: Arc<HashMap<FsString, FsNodeHandle>>,
}

impl ProcDirectory {
    /// Returns a new `ProcDirectory` exposing information about `kernel`.
    pub fn new(fs: &FileSystemHandle, kernel: Weak<Kernel>) -> ProcDirectory {
        let nodes = Arc::new(hashmap! {
            b"cmdline".to_vec() => {
                let cmdline = std::env::var("KERNEL_CMDLINE").unwrap_or("".into()).into_bytes();
                fs.create_node_with_ops(ByteVecFile::new(cmdline), mode!(IFREG, 0o444))
            },
            b"self".to_vec() => SelfSymlink::new(fs),
        });

        ProcDirectory { kernel, nodes }
    }
}

impl FsNodeOps for ProcDirectory {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(DirectoryFileOps::new(self.nodes.clone())))
    }

    fn lookup(&self, node: &FsNode, name: &FsStr) -> Result<FsNodeHandle, Errno> {
        match self.nodes.get(name) {
            Some(node) => Ok(Arc::clone(&node)),
            None => {
                let pid_string = std::str::from_utf8(name).map_err(|_| errno!(ENOENT))?;
                let pid = pid_string.parse::<pid_t>().map_err(|_| errno!(ENOENT))?;
                if let Some(task) = self.kernel.upgrade().unwrap().pids.read().get_task(pid) {
                    Ok(node.fs().create_node_with_ops(
                        PidDirectory::new(&node.fs(), task),
                        FileMode::IFDIR | FileMode::ALLOW_ALL,
                    ))
                } else {
                    error!(ENOENT)
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
    nodes: Arc<HashMap<FsString, FsNodeHandle>>,
}

impl DirectoryFileOps {
    fn new(nodes: Arc<HashMap<FsString, FsNodeHandle>>) -> DirectoryFileOps {
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
        file.unbounded_seek(offset, whence)
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
        let num_nodes = self.nodes.len();
        if node_offset < num_nodes {
            // Sort the keys of the nodes, to keep the traversal order consistent.
            let mut items: Vec<_> = self.nodes.iter().collect();
            items.sort_unstable_by(|(name1, _), (name2, _)| name1.cmp(name2));
            // Iterate through all the named entries (i.e., non "task directories") and add them to
            // the sink.
            for (name, node) in items {
                sink.add(
                    node.inode_num,
                    *offset,
                    DirectoryEntryType::from_mode(node.info().mode),
                    &name,
                )?;
                *offset = *offset + 1;
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
                let inode_num = file.fs.next_inode_num();
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
struct SelfSymlink;

impl SelfSymlink {
    fn new(fs: &FileSystemHandle) -> FsNodeHandle {
        fs.create_node_with_ops(Self, FileMode::IFLNK | FileMode::ALLOW_ALL)
    }
}

impl FsNodeOps for SelfSymlink {
    fs_node_impl_symlink!();

    fn readlink(&self, _node: &FsNode, task: &Task) -> Result<SymlinkTarget, Errno> {
        Ok(SymlinkTarget::Path(format!("{}", task.id).as_bytes().to_vec()))
    }
}
