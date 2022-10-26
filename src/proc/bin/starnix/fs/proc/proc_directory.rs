// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::pid_directory::*;
use crate::auth::FsCred;
use crate::fs::*;
use crate::task::*;
use crate::types::*;

use maplit::btreemap;
use std::collections::BTreeMap;
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
    nodes: BTreeMap<&'static FsStr, FsNodeHandle>,
}

impl ProcDirectory {
    /// Returns a new `ProcDirectory` exposing information about `kernel`.
    pub fn new(fs: &FileSystemHandle, kernel: Weak<Kernel>) -> Arc<ProcDirectory> {
        let nodes = btreemap! {
            &b"cmdline"[..] => {
                let cmdline = kernel.upgrade().unwrap().cmdline.clone();
                fs.create_node_with_ops(ByteVecFile::new_node(cmdline), mode!(IFREG, 0o444), FsCred::root())
            },
            &b"self"[..] => SelfSymlink::new_node(fs),
            &b"thread-self"[..] => ThreadSelfSymlink::new_node(fs),
            // TODO(tbodt): Put actual data in /proc/meminfo. Android is currently satistified by
            // an empty file though.
            &b"meminfo"[..] => fs.create_node_with_ops(ByteVecFile::new_node(vec![]), mode!(IFREG, 0o444), FsCred::root()),
            // Fake kmsg as being empty.
            &b"kmsg"[..] =>
                fs.create_node_with_ops(SimpleFileNode::new(|| Ok(ProcKmsgFile{})), mode!(IFREG, 0o100), FsCred::root()),
            // File must exist to pass the CgroupsAvailable check, which is a little bit optional
            // for init but not optional for a lot of the system!
            &b"cgroups"[..] => fs.create_node_with_ops(ByteVecFile::new_node(vec![]), mode!(IFREG, 0o444), FsCred::root()),
        };

        Arc::new(ProcDirectory { kernel, nodes })
    }
}

impl FsNodeOps for Arc<ProcDirectory> {
    fn create_file_ops(
        &self,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(self.clone()))
    }

    fn lookup(
        &self,
        node: &FsNode,
        _current_task: &CurrentTask,
        name: &FsStr,
    ) -> Result<FsNodeHandle, Errno> {
        match self.nodes.get(name) {
            Some(node) => Ok(Arc::clone(node)),
            None => {
                let pid_string = std::str::from_utf8(name).map_err(|_| errno!(ENOENT))?;
                let pid = pid_string.parse::<pid_t>().map_err(|_| errno!(ENOENT))?;
                if let Some(task) = self.kernel.upgrade().unwrap().pids.read().get_task(pid) {
                    Ok(pid_directory(&node.fs(), &task))
                } else {
                    error!(ENOENT)
                }
            }
        }
    }
}

impl FileOps for Arc<ProcDirectory> {
    fileops_impl_directory!();

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
        current_task: &CurrentTask,
        sink: &mut dyn DirentSink,
    ) -> Result<(), Errno> {
        emit_dotdot(file, sink)?;

        // Iterate through all the named entries (i.e., non "task directories") and add them to
        // the sink. Subtract 2 from the offset, to account for `.` and `..`.
        for (name, node) in self.nodes.iter().skip((sink.offset() - 2) as usize) {
            sink.add(
                node.inode_num,
                sink.offset() + 1,
                DirectoryEntryType::from_mode(node.info().mode),
                name,
            )?;
        }

        // Add 2 to the number of non-"task directories", to account for `.` and `..`.
        let pid_offset = (self.nodes.len() + 2) as i32;

        // Adjust the offset to account for the other nodes in the directory.
        let adjusted_offset = (sink.offset() - pid_offset as i64) as usize;
        // Sort the pids, to keep the traversal order consistent.
        let mut pids = current_task.thread_group.kernel.pids.read().process_ids();
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
            }
        }

        Ok(())
    }
}

struct ProcKmsgFile {}

impl FileOps for ProcKmsgFile {
    fileops_impl_seekless!();

    fn wait_async(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _waiter: &Waiter,
        _events: FdEvents,
        _handler: EventHandler,
        _options: WaitAsyncOptions,
    ) -> WaitKey {
        WaitKey::empty()
    }

    fn cancel_wait(&self, _current_task: &CurrentTask, _waiter: &Waiter, _key: WaitKey) {}

    fn query_events(&self, _current_task: &CurrentTask) -> FdEvents {
        FdEvents::empty()
    }

    fn read_at(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EAGAIN)
    }

    fn write_at(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EIO)
    }
}

/// A node that represents a symlink to `proc/<pid>` where <pid> is the pid of the task that
/// reads the `proc/self` symlink.
struct SelfSymlink;

impl SelfSymlink {
    fn new_node(fs: &FileSystemHandle) -> FsNodeHandle {
        fs.create_node_with_ops(Self, mode!(IFLNK, 0o777), FsCred::root())
    }
}

impl FsNodeOps for SelfSymlink {
    fs_node_impl_symlink!();

    fn readlink(&self, _node: &FsNode, current_task: &CurrentTask) -> Result<SymlinkTarget, Errno> {
        Ok(SymlinkTarget::Path(current_task.get_pid().to_string().into_bytes()))
    }
}

/// A node that represents a symlink to `proc/<pid>/task/<tid>` where <pid> and <tid> are derived
/// from the task reading the symlink.
struct ThreadSelfSymlink;

impl ThreadSelfSymlink {
    fn new_node(fs: &FileSystemHandle) -> FsNodeHandle {
        fs.create_node_with_ops(Self, mode!(IFLNK, 0o777), FsCred::root())
    }
}

impl FsNodeOps for ThreadSelfSymlink {
    fs_node_impl_symlink!();

    fn readlink(&self, _node: &FsNode, current_task: &CurrentTask) -> Result<SymlinkTarget, Errno> {
        Ok(SymlinkTarget::Path(
            format!("{}/task/{}", current_task.get_pid(), current_task.get_tid()).into_bytes(),
        ))
    }
}
