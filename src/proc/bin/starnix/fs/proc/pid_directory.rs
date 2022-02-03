// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use crate::fs::*;
use crate::mm::{ProcMapsFile, ProcStatFile};
use crate::mode;
use crate::task::{CurrentTask, Task};
use crate::types::*;
use crate::{
    errno, error, fd_impl_directory, fd_impl_nonblocking, fd_impl_seekable, fs_node_impl_symlink,
};

use maplit::hashmap;
use parking_lot::Mutex;
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
            b"fdinfo".to_vec() => FdInfoDirectory::new(fs, task.clone()),
            b"maps".to_vec() => ProcMapsFile::new(fs, task.clone()),
            b"stat".to_vec() => ProcStatFile::new(fs, task.clone()),
            b"cmdline".to_vec() => CmdlineFile::new(fs, task.clone()),
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
    fd_impl_nonblocking!();

    fn seek(
        &self,
        file: &FileObject,
        _current_task: &CurrentTask,
        offset: off_t,
        whence: SeekOrigin,
    ) -> Result<off_t, Errno> {
        file.unbounded_seek(offset, whence)
    }

    fn readdir(
        &self,
        file: &FileObject,
        _current_task: &CurrentTask,
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

fn parse_fd(name: &FsStr) -> Result<FdNumber, Errno> {
    let name = std::str::from_utf8(name).map_err(|_| errno!(ENOENT))?;
    let num = name.parse::<i32>().map_err(|_| errno!(ENOENT))?;
    Ok(FdNumber::from_raw(num))
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
        let fd = parse_fd(name)?;
        // Make sure that the file descriptor exists before creating the node.
        if let Ok(_) = self.task.files.get(fd) {
            Ok(node.fs().create_node(FdSymlink::new(self.task.clone(), fd), FdSymlink::file_mode()))
        } else {
            error!(ENOENT)
        }
    }
}

/// `FdInfoDirectory` implements the `FsNodeOps` for a `proc/<pid>/fdinfo` directory.
pub struct FdInfoDirectory {
    /// The task from which the `FdDirectory` fetches file descriptors.
    task: Arc<Task>,
}

impl FdInfoDirectory {
    fn new(fs: &FileSystemHandle, task: Arc<Task>) -> FsNodeHandle {
        fs.create_node_with_ops(FdInfoDirectory { task }, FileMode::IFDIR | FileMode::ALLOW_ALL)
    }
}

impl FsNodeOps for FdInfoDirectory {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(FdDirectoryFileOps::new(self.task.clone())))
    }

    fn lookup(&self, node: &FsNode, name: &FsStr) -> Result<FsNodeHandle, Errno> {
        let fd = parse_fd(name)?;

        if let Ok(file) = self.task.files.get(fd) {
            let pos = *file.offset.lock();
            let flags = file.flags();
            let data = format!("pos:\t{}flags:\t0{:o}\n", pos, flags.bits()).into_bytes();
            Ok(node.fs().create_node(Box::new(ByteVecFile::new(data)), mode!(IFREG, 0o444)))
        } else {
            error!(ENOENT)
        }
    }
}

/// `FdDirectoryFileOps` implements `FileOps` for the `proc/<pid>/fd` and
/// `proc/<pid>/fdinfo` directories.
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
    fd_impl_nonblocking!();

    fn seek(
        &self,
        file: &FileObject,
        _current_task: &CurrentTask,
        offset: off_t,
        whence: SeekOrigin,
    ) -> Result<off_t, Errno> {
        file.unbounded_seek(offset, whence)
    }

    fn readdir(
        &self,
        file: &FileObject,
        _current_task: &CurrentTask,
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

    fn readlink(
        &self,
        _node: &FsNode,
        _current_task: &CurrentTask,
    ) -> Result<SymlinkTarget, Errno> {
        if let Some(node) = self.task.mm.executable_node() {
            Ok(SymlinkTarget::Node(node))
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

    fn readlink(
        &self,
        _node: &FsNode,
        _current_task: &CurrentTask,
    ) -> Result<SymlinkTarget, Errno> {
        let file = self.task.files.get(self.fd).map_err(|_| errno!(ENOENT))?;
        Ok(SymlinkTarget::Node(file.name.clone()))
    }
}

/// `CmdlineFile` implements the `FsNodeOps` for a `proc/<pid>/cmdline` file.
pub struct CmdlineFile {
    /// The task from which the `CmdlineFile` fetches the command line parameters.
    task: Arc<Task>,
    seq: Mutex<SeqFileState<()>>,
}

impl CmdlineFile {
    fn new(fs: &FileSystemHandle, task: Arc<Task>) -> FsNodeHandle {
        fs.create_node_with_ops(
            SimpleFileNode::new(move || {
                Ok(CmdlineFile { task: Arc::clone(&task), seq: Mutex::new(SeqFileState::new()) })
            }),
            mode!(IFREG, 0o444),
        )
    }
}

impl FileOps for CmdlineFile {
    fd_impl_seekable!();
    fd_impl_nonblocking!();

    fn read_at(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let mut seq = self.seq.lock();
        let iter = |_cursor, sink: &mut SeqFileBuf| {
            let argv = self.task.argv.read();
            for arg in &*argv {
                sink.write(arg.as_bytes_with_nul());
            }
            Ok(None)
        };
        seq.read_at(current_task, iter, offset, data)
    }

    fn write_at(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        Err(ENOSYS)
    }
}
