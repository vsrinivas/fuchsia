// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use crate::errno;
use crate::error;
use crate::fd_impl_directory;
use crate::fs::FsNodeOps;
use crate::fs::*;
use crate::fs_node_impl_symlink;
use crate::task::Task;
use crate::types::*;

use maplit::hashmap;
use std::collections::HashMap;

/// The `PidDirectory` implements the `FsNodeOps` for the `proc/<pid>` directory.
pub struct PidDirectory {
    /// A map that stores lazy initializers for all the entries in the directory.
    nodes: Arc<HashMap<FsString, FsNodeHandle>>,
}

impl PidDirectory {
    pub fn new(fs: &FileSystemHandle, task: Arc<Task>) -> PidDirectory {
        let nodes = Arc::new(hashmap! {
            b"exe".to_vec() => ExeSymlink::new(fs, task.clone()),
            b"fd".to_vec() => FdDirectory::new(fs, task.clone()),
        });

        PidDirectory { nodes }
    }
}

impl FsNodeOps for PidDirectory {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(PidDirectoryFileOps::new(self.nodes.clone())))
    }

    fn lookup(&self, _node: &FsNode, name: &FsStr) -> Result<FsNodeHandle, Errno> {
        let node = self.nodes.get(name).ok_or(errno!(ENOENT))?;
        Ok(node.clone())
    }
}

/// `PidDirectoryFileOps` implements file operations for the `/proc/<pid>` directory.
struct PidDirectoryFileOps {
    /// The nodes that exist in the associated `PidDirectory`.
    nodes: Arc<HashMap<FsString, FsNodeHandle>>,
}

impl PidDirectoryFileOps {
    fn new(nodes: Arc<HashMap<FsString, FsNodeHandle>>) -> PidDirectoryFileOps {
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
        Ok(())
    }
}

/// `FdDirectory` implements the `FsNodeOps` for a `proc/<pid>/fd` directory.
pub struct FdDirectory {
    /// The task from which the `FdDirectory` fetches file descriptors.
    task: Arc<Task>,
}

impl FdDirectory {
    fn new(fs: &FileSystemHandle, task: Arc<Task>) -> FsNodeHandle {
        fs.create_node_with_ops(FdDirectory { task }, FileMode::IFDIR | FileMode::ALLOW_ALL)
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
            Ok(node.fs().create_node(FdSymlink::new(self.task.clone(), fd), FdSymlink::file_mode()))
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
        emit_dotdot(file, sink, &mut offset)?;

        let mut fds = self.task.files.get_all_fds();
        fds.sort();

        for fd in fds {
            if (fd.raw() as i64) >= *offset - 2 {
                let new_offset = *offset + 1;
                sink.add(
                    file.fs.next_inode_num(),
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
    fn new(fs: &FileSystemHandle, task: Arc<Task>) -> FsNodeHandle {
        fs.create_node_with_ops(ExeSymlink { task }, FileMode::IFLNK | FileMode::ALLOW_ALL)
    }
}

impl FsNodeOps for ExeSymlink {
    fs_node_impl_symlink!();

    fn readlink(&self, _node: &FsNode, _task: &Task) -> Result<SymlinkTarget, Errno> {
        if let Some(node) = &*self.task.executable_node.read() {
            Ok(SymlinkTarget::Node(node.clone()))
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
    fs_node_impl_symlink!();

    fn readlink(&self, _node: &FsNode, _task: &Task) -> Result<SymlinkTarget, Errno> {
        let file = self.task.files.get(self.fd).map_err(|_| errno!(ENOENT))?;
        Ok(SymlinkTarget::Node(file.name.clone()))
    }
}
