// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use lazy_static::lazy_static;
use regex::Regex;
use std::sync::Arc;

use crate::fs::*;
use crate::lock::Mutex;
use crate::mm::{ProcMapsFile, ProcStatFile};
use crate::task::{CurrentTask, Task, ThreadGroup};
use crate::types::*;

/// Creates an [`FsNode`] that represents the `/proc/<pid>` directory for `task`.
pub fn pid_directory(fs: &FileSystemHandle, task: &Arc<Task>) -> Arc<FsNode> {
    static_directory_builder_with_common_task_entries(fs, task)
        .entry_creds(task.as_fscred())
        .entry(
            b"task",
            TaskListDirectory { thread_group: task.thread_group.clone() },
            mode!(IFDIR, 0o777),
        )
        .build()
}

/// Creates an [`FsNode`] that represents the `/proc/<pid>/task/<tid>` directory for `task`.
fn tid_directory(fs: &FileSystemHandle, task: &Arc<Task>) -> Arc<FsNode> {
    static_directory_builder_with_common_task_entries(fs, task).build()
}

/// Creates a [`StaticDirectoryBuilder`] and pre-populates it with files that are present in both
/// `/pid/<pid>` and `/pid/<pid>/task/<tid>`.
fn static_directory_builder_with_common_task_entries<'a>(
    fs: &'a FileSystemHandle,
    task: &Arc<Task>,
) -> StaticDirectoryBuilder<'a> {
    StaticDirectoryBuilder::new(fs)
        .entry_creds(task.as_fscred())
        .entry(b"exe", ExeSymlink::new(task), mode!(IFLNK, 0o777))
        .entry(b"fd", FdDirectory::new(task), mode!(IFDIR, 0o777))
        .entry(b"fdinfo", FdInfoDirectory::new(task), mode!(IFDIR, 0o777))
        .entry(b"maps", ProcMapsFile::new_node(task), mode!(IFREG, 0o444))
        .entry(b"stat", ProcStatFile::new_node(task), mode!(IFREG, 0o444))
        .entry(b"cmdline", CmdlineFile::new_node(task), mode!(IFREG, 0o444))
        .entry(b"comm", CommFile::new_node(task), mode!(IFREG, 0o444))
        .node(b"attr", attr_directory(task, fs))
        .entry(b"ns", NsDirectory { task: task.clone() }, mode!(IFDIR, 0o777))
        .dir_creds(task.as_fscred())
}

/// Creates an [`FsNode`] that represents the `/proc/<pid>/attr` directory.
fn attr_directory(task: &Arc<Task>, fs: &FileSystemHandle) -> Arc<FsNode> {
    StaticDirectoryBuilder::new(fs)
        // The `current` security context is, with selinux disabled, unconfined.
        .entry_creds(task.as_fscred())
        .entry(b"current", ByteVecFile::new_node(b"unconfined\n".to_vec()), mode!(IFREG, 0o666))
        .entry(b"fscreate", SimpleFileNode::new(|| Ok(SeLinuxAttribute)), mode!(IFREG, 0o666))
        .dir_creds(task.as_fscred())
        .build()
}

// TODO(tbodt): Make this more than a stub, use for all selinux attributes
struct SeLinuxAttribute;
impl FileOps for SeLinuxAttribute {
    fileops_impl_nonseekable!();
    fileops_impl_nonblocking!();

    fn read(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        Ok(0)
    }

    fn write(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        UserBuffer::get_total_length(data)
    }
}

/// `FdDirectory` implements the directory listing operations for a `proc/<pid>/fd` directory.
///
/// Reading the directory returns a list of all the currently open file descriptors for the
/// associated task.
struct FdDirectory {
    task: Arc<Task>,
}

impl FdDirectory {
    fn new(task: &Arc<Task>) -> Self {
        Self { task: Arc::clone(task) }
    }
}

impl FsNodeOps for FdDirectory {
    fs_node_impl_dir_readonly!();

    fn create_file_ops(
        &self,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        Ok(VecDirectory::new_file(fds_to_directory_entries(self.task.files.get_all_fds())))
    }

    fn lookup(
        &self,
        node: &FsNode,
        _current_task: &CurrentTask,
        name: &FsStr,
    ) -> Result<Arc<FsNode>, Errno> {
        let fd = FdNumber::from_fs_str(name).map_err(|_| errno!(ENOENT))?;
        // Make sure that the file descriptor exists before creating the node.
        let _ = self.task.files.get(fd).map_err(|_| errno!(ENOENT))?;
        Ok(node.fs().create_node(
            FdSymlink::new_node(self.task.clone(), fd),
            FdSymlink::file_mode(),
            self.task.as_fscred(),
        ))
    }
}

const NS_ENTRIES: &[&str] = &[
    "cgroup",
    "ipc",
    "mnt",
    "net",
    "pid",
    "pid_for_children",
    "time",
    "time_for_children",
    "user",
    "uts",
];

/// /proc/[pid]/ns directory
struct NsDirectory {
    task: Arc<Task>,
}

impl FsNodeOps for NsDirectory {
    fs_node_impl_dir_readonly!();

    fn create_file_ops(
        &self,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        // For each namespace, this contains a link to the current identifier of the given namespace
        // for the current task.
        Ok(VecDirectory::new_file(
            NS_ENTRIES
                .iter()
                .map(|name| VecDirectoryEntry {
                    entry_type: DirectoryEntryType::LNK,
                    name: name.as_bytes().to_vec(),
                    inode: None,
                })
                .collect(),
        ))
    }

    fn lookup(
        &self,
        node: &FsNode,
        _current_task: &CurrentTask,
        name: &FsStr,
    ) -> Result<Arc<FsNode>, Errno> {
        // If name is a given namespace, link to the current identifier of the that namespace for
        // the current task.
        // If name is {namespace}:[id], get a file descriptor for the given namespace.

        let name = String::from_utf8(name.to_vec()).map_err(|_| errno!(ENOENT))?;
        let mut elements = name.split(':');
        let ns = elements.next().expect("name must not be empty");
        // The name doesn't starts with a known namespace.
        if !NS_ENTRIES.contains(&ns) {
            return error!(ENOENT);
        }
        if let Some(id) = elements.next() {
            // The name starts with {namespace}:, check that it matches {namespace}:[id]
            lazy_static! {
                static ref NS_IDENTIFIER_RE: Regex = Regex::new("^\\[[0-9]+\\]$").unwrap();
            }
            if NS_IDENTIFIER_RE.is_match(id) {
                // TODO(qsr): For now, returns an empty file. In the future, this should create a
                // reference to to correct namespace, and ensures it keeps it alive.
                Ok(node.fs().create_node_with_ops(
                    ByteVecFile::new_node(vec![]),
                    mode!(IFREG, 0o444),
                    self.task.as_fscred(),
                ))
            } else {
                error!(ENOENT)
            }
        } else {
            // The name is {namespace}, link to the correct one of the current task.
            Ok(node.fs().create_node(
                Box::new(NsDirectoryEntry { name }),
                mode!(IFLNK, 0o7777),
                self.task.as_fscred(),
            ))
        }
    }
}

/// An entry in the ns pseudo directory. For now, all namespace have the identifier 1.
struct NsDirectoryEntry {
    name: String,
}

impl FsNodeOps for NsDirectoryEntry {
    fs_node_impl_symlink!();

    fn readlink(
        &self,
        _node: &FsNode,
        _current_task: &CurrentTask,
    ) -> Result<SymlinkTarget, Errno> {
        Ok(SymlinkTarget::Path(format!("{}:[1]", self.name).as_bytes().to_vec()))
    }
}

/// `FdInfoDirectory` implements the directory listing operations for a `proc/<pid>/fdinfo`
/// directory.
///
/// Reading the directory returns a list of all the currently open file descriptors for the
/// associated task.
struct FdInfoDirectory {
    task: Arc<Task>,
}

impl FdInfoDirectory {
    fn new(task: &Arc<Task>) -> Self {
        Self { task: Arc::clone(task) }
    }
}

impl FsNodeOps for FdInfoDirectory {
    fn create_file_ops(
        &self,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        Ok(VecDirectory::new_file(fds_to_directory_entries(self.task.files.get_all_fds())))
    }

    fn lookup(
        &self,
        node: &FsNode,
        _current_task: &CurrentTask,
        name: &FsStr,
    ) -> Result<FsNodeHandle, Errno> {
        let fd = FdNumber::from_fs_str(name).map_err(|_| errno!(ENOENT))?;
        let file = self.task.files.get(fd).map_err(|_| errno!(ENOENT))?;
        let pos = *file.offset.lock();
        let flags = file.flags();
        let data = format!("pos:\t{}flags:\t0{:o}\n", pos, flags.bits()).into_bytes();
        Ok(node.fs().create_node_with_ops(
            ByteVecFile::new_node(data),
            mode!(IFREG, 0o444),
            self.task.as_fscred(),
        ))
    }
}

fn fds_to_directory_entries(fds: Vec<FdNumber>) -> Vec<VecDirectoryEntry> {
    fds.into_iter()
        .map(|fd| VecDirectoryEntry {
            entry_type: DirectoryEntryType::DIR,
            name: fd.raw().to_string().into_bytes(),
            inode: None,
        })
        .collect()
}

/// Directory that lists the task IDs (tid) in a process. Located at `/proc/<pid>/task/`.
struct TaskListDirectory {
    thread_group: Arc<ThreadGroup>,
}

impl FsNodeOps for TaskListDirectory {
    fn create_file_ops(
        &self,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        Ok(VecDirectory::new_file(
            self.thread_group
                .read()
                .tasks
                .keys()
                .map(|tid| VecDirectoryEntry {
                    entry_type: DirectoryEntryType::DIR,
                    name: tid.to_string().into_bytes(),
                    inode: None,
                })
                .collect(),
        ))
    }

    fn lookup(
        &self,
        node: &FsNode,
        _current_task: &CurrentTask,
        name: &FsStr,
    ) -> Result<Arc<FsNode>, Errno> {
        let tid = std::str::from_utf8(name)
            .map_err(|_| errno!(ENOENT))?
            .parse::<pid_t>()
            .map_err(|_| errno!(ENOENT))?;
        // Make sure the tid belongs to this process.
        if !self.thread_group.read().tasks.contains_key(&tid) {
            return error!(ENOENT);
        }
        let task =
            self.thread_group.kernel.pids.read().get_task(tid).ok_or_else(|| errno!(ENOENT))?;
        Ok(tid_directory(&node.fs(), &task))
    }
}

/// An `ExeSymlink` points to the `executable_node` (the node that contains the task's binary) of
/// the task that reads it.
pub struct ExeSymlink {
    task: Arc<Task>,
}

impl ExeSymlink {
    fn new(task: &Arc<Task>) -> Self {
        Self { task: Arc::clone(task) }
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
    fn new_node(task: Arc<Task>, fd: FdNumber) -> Box<dyn FsNodeOps> {
        Box::new(FdSymlink { fd, task })
    }

    fn file_mode() -> FileMode {
        mode!(IFLNK, 0o777)
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
    fn new_node(task: &Arc<Task>) -> impl FsNodeOps {
        let task = Arc::clone(task);
        SimpleFileNode::new(move || {
            Ok(CmdlineFile { task: Arc::clone(&task), seq: Mutex::new(SeqFileState::new()) })
        })
    }
}

impl FileOps for CmdlineFile {
    fileops_impl_seekable!();
    fileops_impl_nonblocking!();

    fn read_at(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let argv = self.task.read().argv.clone();
        let mut seq = self.seq.lock();
        let iter = move |_cursor, sink: &mut SeqFileBuf| {
            for arg in &argv {
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
        error!(ENOSYS)
    }
}

/// `CommFile` implements the `FsNodeOps` for a `proc/<pid>/comm` file.
pub struct CommFile {
    /// The task from which the `CommFile` fetches the command line parameters.
    task: Arc<Task>,
    seq: Mutex<SeqFileState<()>>,
}

impl CommFile {
    fn new_node(task: &Arc<Task>) -> impl FsNodeOps {
        let task = Arc::clone(task);
        SimpleFileNode::new(move || {
            Ok(CommFile { task: Arc::clone(&task), seq: Mutex::new(SeqFileState::new()) })
        })
    }
}

impl FileOps for CommFile {
    fileops_impl_seekable!();
    fileops_impl_nonblocking!();

    fn read_at(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let comm = self.task.command();
        let mut seq = self.seq.lock();
        let iter = move |_cursor, sink: &mut SeqFileBuf| {
            sink.write(comm.as_bytes());
            sink.write(b"\n");
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
        error!(ENOSYS)
    }
}
