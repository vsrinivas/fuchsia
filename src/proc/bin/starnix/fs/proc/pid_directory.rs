// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use super::inode_numbers::fd_inode_num;
use super::node_holder::*;
use crate::errno;
use crate::error;
use crate::fd_impl_directory;
use crate::fs::FsNodeOps;
use crate::fs::*;
use crate::task::Task;
use crate::types::*;

use maplit::hashmap;
use parking_lot::Mutex;
use std::collections::HashMap;

/// The `PidDirectory` implements the `FsNodeOps` for the `proc/<pid>` directory.
pub struct PidDirectory {
    /// A map that stores lazy initializers for all the entries in the directory.
    nodes: Arc<Mutex<HashMap<FsString, NodeHolder>>>,
}

impl PidDirectory {
    pub fn new(task: Arc<Task>) -> PidDirectory {
        let task_clone = task.clone();
        let nodes = Arc::new(Mutex::new(hashmap! {
            b"exe".to_vec() => NodeHolder::new(move || { Ok(ExeSymlink::new(task_clone.clone())) }, ExeSymlink::file_mode()),
            b"fd".to_vec() => NodeHolder::new(move || { Ok(FdDirectory::new(task.clone())) }, FdDirectory::file_mode()),
        }));

        PidDirectory { nodes }
    }
}

impl FsNodeOps for PidDirectory {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(PidDirectoryFileOps::new(self.nodes.clone())))
    }

    fn lookup(&self, node: &FsNode, name: &FsStr) -> Result<FsNodeHandle, Errno> {
        match self.nodes.lock().get_mut(name) {
            Some(node_holder) => node_holder.get_or_create_node(&node.fs()),
            None => error!(ENOENT),
        }
    }
}

/// `PidDirectoryFileOps` implements file operations for the `/proc/<pid>` directory.
struct PidDirectoryFileOps {
    /// The nodes that exist in the associated `PidDirectory`.
    nodes: Arc<Mutex<HashMap<FsString, NodeHolder>>>,
}

impl PidDirectoryFileOps {
    fn new(nodes: Arc<Mutex<HashMap<FsString, NodeHolder>>>) -> PidDirectoryFileOps {
        PidDirectoryFileOps { nodes }
    }
}

impl FileOps for PidDirectoryFileOps {
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

/// `FdDirectory` implements the `FsNodeOps` for a `proc/<pid>/fd` directory.
pub struct FdDirectory {
    /// The task from which the `FdDirectory` fetches file descriptors.
    task: Arc<Task>,
}

impl FdDirectory {
    fn new(task: Arc<Task>) -> Box<dyn FsNodeOps> {
        Box::new(FdDirectory { task })
    }

    fn file_mode() -> FileMode {
        // TODO(security): Return a proper file mode.
        FileMode::IFDIR | FileMode::ALLOW_ALL
    }
}

impl FsNodeOps for FdDirectory {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(FdDirectoryFileOps::new(self.task.clone())))
    }

    fn lookup(&self, node: &FsNode, name: &FsStr) -> Result<FsNodeHandle, Errno> {
        let name = std::str::from_utf8(name).map_err(|_| errno!(ENOENT))?;
        let num = name.parse::<i32>().map_err(|_| errno!(ENOENT))?;
        let fd = FdNumber::from_raw(num);

        // Make sure that the file descriptor exists before fetching the node.
        if let Ok(_) = self.task.files.get(fd) {
            node.fs().get_or_create_node(
                Some(fd_inode_num(self.task.id) + fd.raw() as u64),
                |inode_num| {
                    Ok(FsNode::new(
                        FdSymlink::new(self.task.clone(), fd),
                        &node.fs(),
                        inode_num,
                        FdSymlink::file_mode(),
                    ))
                },
            )
        } else {
            error!(ENOENT)
        }
    }
}

/// `FdDirectoryFileOps` implements `FileOps` for a `proc/<pid>/fd` directory.
///
/// Reading the directory returns a list of all the currently open file descriptors for the
/// associated task.
struct FdDirectoryFileOps {
    /// The task that is used to populate the list of open file descriptors.
    task: Arc<Task>,
}

impl FdDirectoryFileOps {
    fn new(task: Arc<Task>) -> FdDirectoryFileOps {
        FdDirectoryFileOps { task }
    }
}

impl FileOps for FdDirectoryFileOps {
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

        let mut fds = self.task.files.get_all_fds();
        fds.sort();

        for fd in fds {
            if (fd.raw() as i64) >= *offset - 2 {
                let new_offset = *offset + 1;
                sink.add(
                    fd_inode_num(self.task.id) + fd.raw() as u64,
                    new_offset,
                    DirectoryEntryType::DIR,
                    format!("{}", fd.raw()).as_bytes(),
                )?;
                *offset = new_offset;
            }
        }
        Ok(())
    }
}

/// An `ExeSymlink` points to the `executable_node` (the node that contains the task's binary) of
/// the task that reads it.
pub struct ExeSymlink {
    task: Arc<Task>,
}

impl ExeSymlink {
    fn new(task: Arc<Task>) -> Box<dyn FsNodeOps> {
        Box::new(ExeSymlink { task })
    }

    fn file_mode() -> FileMode {
        FileMode::IFLNK | FileMode::ALLOW_ALL
    }
}

impl FsNodeOps for ExeSymlink {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        unreachable!("Symlink nodes cannot be opened.");
    }

    fn readlink(&self, _node: &FsNode, _task: &Task) -> Result<FsString, Errno> {
        if let Some(node) = &*self.task.executable_node.read() {
            Ok(node.path())
        } else {
            error!(ENOENT)
        }
    }
}

/// `FdSymlink` is a symlink that points to a specific file descriptor in a task.
pub struct FdSymlink {
    /// The file descriptor that this symlink targets.
    fd: FdNumber,

    /// The task that `fd` is to be read from.
    task: Arc<Task>,
}

impl FdSymlink {
    fn new(task: Arc<Task>, fd: FdNumber) -> Box<dyn FsNodeOps> {
        Box::new(FdSymlink { fd, task })
    }

    fn file_mode() -> FileMode {
        FileMode::IFLNK | FileMode::ALLOW_ALL
    }
}

impl FsNodeOps for FdSymlink {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        unreachable!("Symlink nodes cannot be opened.");
    }

    fn readlink(&self, _node: &FsNode, _task: &Task) -> Result<FsString, Errno> {
        let file = self.task.files.get(self.fd).map_err(|_| errno!(ENOENT))?;
        let name = file.name.path();
        if name.is_empty() {
            // TODO: this should be an error once we set up stdio nodes correctly.
            Ok(b"<broken fd symlink>".to_vec())
        } else {
            Ok(name)
        }
    }
}
