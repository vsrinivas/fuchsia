// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, VmoOptions};
use parking_lot::Mutex;
use std::collections::HashMap;
use std::sync::{Arc, Weak};

use super::*;
use crate::devices::*;
use crate::fd_impl_seekable;
use crate::mm::PAGE_SIZE;
use crate::task::*;
use crate::types::*;

#[derive(Default)]
struct TmpfsState {
    last_inode_num: ino_t,
    nodes: HashMap<ino_t, FsNodeHandle>,
}

impl TmpfsState {
    fn register(&mut self, node: &FsNodeHandle) {
        self.last_inode_num += 1;
        let inode_num = self.last_inode_num;
        node.state_mut().inode_num = inode_num;
        self.nodes.insert(inode_num, Arc::clone(node));
    }

    fn unregister(&mut self, node: &FsNodeHandle) {
        self.nodes.remove(&node.state_mut().inode_num);
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
    fn open(&self, _node: &FsNode) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(NullFile))
    }

    fn lookup(&self, _node: &FsNode, _name: &FsStr) -> Result<Box<dyn FsNodeOps>, Errno> {
        Err(ENOENT)
    }

    fn mkdir(&self, _node: &FsNode, _name: &FsStr) -> Result<Box<dyn FsNodeOps>, Errno> {
        Ok(Box::new(TmpfsDirectory { fs: self.fs.clone() }))
    }

    fn create(&self, _node: &FsNode, _name: &FsStr) -> Result<Box<dyn FsNodeOps>, Errno> {
        Ok(Box::new(TmpfsFileNode::new(self.fs.clone())?))
    }

    fn unlink(&self, _node: &FsNode, _name: &FsStr, _kind: UnlinkKind) -> Result<(), Errno> {
        Ok(())
    }

    fn initialize(&self, node: &FsNodeHandle) {
        self.fs.upgrade().map(|fs| fs.lock().register(node));
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
    fn open(&self, _node: &FsNode) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(TmpfsFileObject { vmo: self.vmo.clone() }))
    }

    fn initialize(&self, node: &FsNodeHandle) {
        self.fs.upgrade().map(|fs| fs.lock().register(node));
    }

    fn unlinked(&self, node: &FsNodeHandle) {
        self.fs.upgrade().map(|fs| fs.lock().unregister(node));
    }
}

struct TmpfsFileObject {
    vmo: Arc<zx::Vmo>,
}

impl FileOps for TmpfsFileObject {
    fd_impl_seekable!();

    fn read_at(
        &self,
        file: &FileObject,
        task: &Task,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let mut state = file.node().state_mut();
        let file_length = state.size;
        let want_read = UserBuffer::get_total_length(data);
        let to_read =
            if file_length < offset + want_read { file_length - offset } else { want_read };
        let mut buf = vec![0u8; to_read];
        self.vmo.read(&mut buf[..], offset as u64).map_err(|_| EIO)?;
        // TODO(steveaustin) - write_each might might be more efficient
        task.mm.write_all(data, &mut buf[..])?;
        state.time_access = fuchsia_runtime::utc_time();
        Ok(to_read)
    }

    fn write_at(
        &self,
        file: &FileObject,
        task: &Task,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let mut state = file.node().state_mut();
        let want_write = UserBuffer::get_total_length(data);
        let write_end = offset + want_write;
        let mut update_content_size = false;
        if write_end > state.size {
            if write_end > state.storage_size {
                let mut new_size = write_end as u64;
                // TODO(steveaustin) move the padding logic
                // to a library where it can be shared with
                // similar code in pipe
                let padding = new_size as u64 % *PAGE_SIZE;
                if padding > 0 {
                    new_size += padding;
                }
                self.vmo.set_size(new_size).map_err(|_| ENOMEM)?;
                state.storage_size = new_size as usize;
            }
            update_content_size = true;
        }

        let mut buf = vec![0u8; want_write];
        task.mm.read_all(data, &mut buf[..])?;
        self.vmo.write(&mut buf[..], offset as u64).map_err(|_| EIO)?;
        if update_content_size {
            state.size = write_end;
        }
        let now = fuchsia_runtime::utc_time();
        state.time_access = now;
        state.time_modify = now;
        Ok(want_write)
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
        let _file = task.fs.root.mknod(path, FileMode::IFREG | FileMode::ALLOW_ALL).unwrap();

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
