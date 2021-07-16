// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, VmoOptions};
use parking_lot::Mutex;
use std::collections::HashMap;
use std::sync::{Arc, Weak};

use super::*;
use crate::devices::*;
use crate::fs::pipe::Pipe;
use crate::fs::SymlinkNode;
use crate::types::*;

#[derive(Default)]
pub struct TmpfsState {
    nodes: HashMap<ino_t, FsNodeHandle>,
}

impl TmpfsState {
    pub fn register(&mut self, node: &FsNodeHandle) {
        self.nodes.insert(node.info().inode_num, Arc::clone(node));
    }

    pub fn unregister(&mut self, node: &FsNodeHandle) {
        self.nodes.remove(&node.info().inode_num);
    }
}

pub struct Tmpfs {
    root: FsNodeHandle,
    _state: Arc<Mutex<TmpfsState>>,
}

impl Tmpfs {
    pub fn new() -> FileSystemHandle {
        let tmpfs_dev = AnonNodeDevice::new(0);
        let state = Arc::new(Mutex::new(TmpfsState::default()));
        let fs = Tmpfs {
            root: FsNode::new_root(TmpfsDirectory { fs: Arc::downgrade(&state) }, tmpfs_dev),
            _state: state,
        };
        Arc::new(fs)
    }
}

impl FileSystem for Tmpfs {
    fn root(&self) -> &FsNodeHandle {
        &self.root
    }
}

struct TmpfsDirectory {
    /// The file system to which this directory belongs.
    fs: Weak<Mutex<TmpfsState>>,
}

impl FsNodeOps for TmpfsDirectory {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(NullFile))
    }

    fn lookup(&self, _parent: &FsNode, _child: FsNode) -> Result<FsNodeHandle, Errno> {
        Err(ENOENT)
    }

    fn mkdir(&self, _parent: &FsNode, mut child: FsNode) -> Result<FsNodeHandle, Errno> {
        child.set_ops(TmpfsDirectory { fs: self.fs.clone() });
        let child = child.into_handle();
        self.fs.upgrade().map(|fs| fs.lock().register(&child));
        Ok(child)
    }

    fn mknod(&self, _parent: &FsNode, mut child: FsNode) -> Result<FsNodeHandle, Errno> {
        match child.info_mut().mode.fmt() {
            FileMode::IFREG => child.set_ops(TmpfsFileNode::new(self.fs.clone())?),
            FileMode::IFIFO => child.set_ops(TmpfsFifoNode::new(self.fs.clone())),
            _ => return Err(EACCES),
        }
        let child = child.into_handle();
        self.fs.upgrade().map(|fs| fs.lock().register(&child));
        Ok(child)
    }

    fn mksymlink(
        &self,
        _node: &FsNode,
        mut child: FsNode,
        target: &FsStr,
    ) -> Result<FsNodeHandle, Errno> {
        assert!(child.info_mut().mode.fmt() == FileMode::IFLNK);
        child.set_ops(SymlinkNode::new(self.fs.clone(), target));
        let child = child.into_handle();
        self.fs.upgrade().map(|fs| fs.lock().register(&child));
        Ok(child)
    }

    fn unlink(
        &self,
        _parent: &FsNode,
        _child: &FsNodeHandle,
        _kind: UnlinkKind,
    ) -> Result<(), Errno> {
        Ok(())
    }

    fn unlinked(&self, node: &FsNodeHandle) {
        self.fs.upgrade().map(|fs| fs.lock().unregister(node));
    }
}

struct TmpfsFileNode {
    /// The file system to which this file belongs.
    fs: Weak<Mutex<TmpfsState>>,

    /// The memory that backs this file.
    vmo: Arc<zx::Vmo>,
}

impl TmpfsFileNode {
    fn new(fs: Weak<Mutex<TmpfsState>>) -> Result<TmpfsFileNode, Errno> {
        let vmo = zx::Vmo::create_with_opts(VmoOptions::RESIZABLE, 0).map_err(|_| ENOMEM)?;
        Ok(TmpfsFileNode { fs, vmo: Arc::new(vmo) })
    }
}

impl FsNodeOps for TmpfsFileNode {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(VmoFileObject::new(self.vmo.clone())))
    }

    fn unlinked(&self, node: &FsNodeHandle) {
        self.fs.upgrade().map(|fs| fs.lock().unregister(node));
    }
}

struct TmpfsFifoNode {
    /// The file system to which this file belongs.
    fs: Weak<Mutex<TmpfsState>>,

    /// The pipe located at this node.
    pipe: Arc<Mutex<Pipe>>,
}

impl TmpfsFifoNode {
    fn new(fs: Weak<Mutex<TmpfsState>>) -> TmpfsFifoNode {
        TmpfsFifoNode { fs, pipe: Pipe::new() }
    }
}

impl FsNodeOps for TmpfsFifoNode {
    fn open(&self, _node: &FsNode, flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Pipe::open(&self.pipe, flags))
    }

    fn unlinked(&self, node: &FsNodeHandle) {
        self.fs.upgrade().map(|fs| fs.lock().unregister(node));
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_async as fasync;
    use zerocopy::AsBytes;

    use crate::testing::*;

    #[test]
    fn test_tmpfs() {
        let fs = Tmpfs::new();
        let root = fs.root();
        let usr = root.mkdir(b"usr").unwrap();
        let _etc = root.mkdir(b"etc").unwrap();
        let _usr_bin = usr.mkdir(b"bin").unwrap();
        let mut names = root.copy_child_names();
        names.sort();
        assert!(names.iter().eq([b"etc", b"usr"].iter()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_read() {
        let fs = FsContext::new(Namespace::new(Tmpfs::new()));
        let (_kernel, task_owner) = create_kernel_and_task_with_fs(fs);
        let task = &task_owner.task;

        let test_mem_size = 0x10000;
        let test_vmo = zx::Vmo::create(test_mem_size).unwrap();

        let path = b"test.bin";
        let _file = task.fs.root.mknod(path, FileMode::IFREG | FileMode::ALLOW_ALL, 0).unwrap();

        let wr_file = task.open_file(path, OpenFlags::RDWR).unwrap();

        let flags = zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE;
        let test_addr = task
            .mm
            .map(UserAddress::default(), test_vmo, 0, test_mem_size as usize, flags)
            .unwrap();

        let seq_addr = UserAddress::from_ptr(test_addr.ptr() + path.len());
        let test_seq = 0..10000u16;
        let test_vec = test_seq.collect::<Vec<_>>();
        let test_bytes = test_vec.as_slice().as_bytes();
        task.mm.write_memory(seq_addr, test_bytes).unwrap();
        let buf = [UserBuffer { address: seq_addr, length: test_bytes.len() }];

        let written = wr_file.write(task, &buf).unwrap();
        assert_eq!(written, test_bytes.len());

        let mut read_vec = vec![0u8; test_bytes.len()];
        task.mm.read_memory(seq_addr, read_vec.as_bytes_mut()).unwrap();

        assert_eq!(test_bytes, &*read_vec);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_permissions() {
        let fs = FsContext::new(Namespace::new(Tmpfs::new()));
        let (_kernel, task_owner) = create_kernel_and_task_with_fs(fs);
        let task = &task_owner.task;

        let path = b"test.bin";
        let file = task
            .open_file_at(
                FdNumber::AT_FDCWD,
                path,
                OpenFlags::CREAT | OpenFlags::RDONLY,
                FileMode::ALLOW_ALL,
            )
            .expect("failed to create file");
        assert_eq!(0, file.read(task, &[]).expect("failed to read"));
        assert!(file.write(task, &[]).is_err());

        let file = task
            .open_file_at(FdNumber::AT_FDCWD, path, OpenFlags::WRONLY, FileMode::ALLOW_ALL)
            .expect("failed to open file WRONLY");
        assert!(file.read(task, &[]).is_err());
        assert_eq!(0, file.write(task, &[]).expect("failed to write"));

        let file = task
            .open_file_at(FdNumber::AT_FDCWD, path, OpenFlags::RDWR, FileMode::ALLOW_ALL)
            .expect("failed to open file RDWR");
        assert_eq!(0, file.read(task, &[]).expect("failed to read"));
        assert_eq!(0, file.write(task, &[]).expect("failed to write"));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_persistence() {
        let fs = Tmpfs::new();
        {
            let root = fs.root();
            let usr = root.mkdir(b"usr").expect("failed to create usr");
            root.mkdir(b"etc").expect("failed to create usr/etc");
            usr.mkdir(b"bin").expect("failed to create usr/bin");
        }

        // At this point, all the nodes are dropped.

        let (_kernel, task_owner) =
            create_kernel_and_task_with_fs(FsContext::new(Namespace::new(fs)));
        let task = &task_owner.task;

        task.open_file(b"/usr/bin", OpenFlags::RDONLY | OpenFlags::DIRECTORY)
            .expect("failed to open /usr/bin");
        assert_eq!(ENOENT, task.open_file(b"/usr/bin/test.txt", OpenFlags::RDWR).unwrap_err());
        task.open_file_at(
            FdNumber::AT_FDCWD,
            b"/usr/bin/test.txt",
            OpenFlags::RDWR | OpenFlags::CREAT,
            FileMode::ALLOW_ALL,
        )
        .expect("failed to create test.txt");
        let txt =
            task.open_file(b"/usr/bin/test.txt", OpenFlags::RDWR).expect("failed to open test.txt");

        let usr_bin =
            task.open_file(b"/usr/bin", OpenFlags::RDONLY).expect("failed to open /usr/bin");
        usr_bin
            .name()
            .unlink(b"test.txt", UnlinkKind::NonDirectory)
            .expect("failed to unlink test.text");
        assert_eq!(ENOENT, task.open_file(b"/usr/bin/test.txt", OpenFlags::RDWR).unwrap_err());
        assert_eq!(
            ENOENT,
            usr_bin.name().unlink(b"test.txt", UnlinkKind::NonDirectory).unwrap_err()
        );

        assert_eq!(0, txt.read(task, &[]).expect("failed to read"));
        std::mem::drop(txt);
        assert_eq!(ENOENT, task.open_file(b"/usr/bin/test.txt", OpenFlags::RDWR).unwrap_err());
        std::mem::drop(usr_bin);

        let usr = task.open_file(b"/usr", OpenFlags::RDONLY).expect("failed to open /usr");
        assert_eq!(ENOENT, task.open_file(b"/usr/foo", OpenFlags::RDONLY).unwrap_err());
        usr.name().unlink(b"bin", UnlinkKind::Directory).expect("failed to unlink /usr/bin");
        assert_eq!(ENOENT, task.open_file(b"/usr/bin", OpenFlags::RDONLY).unwrap_err());
    }
}
