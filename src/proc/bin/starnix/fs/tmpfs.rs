// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use parking_lot::Mutex;
use std::collections::HashMap;
use std::ops::Bound;
use std::sync::Arc;

use super::*;
use crate::fd_impl_directory;
use crate::fs::pipe::Pipe;
use crate::task::*;
use crate::types::*;

#[derive(Default)]
pub struct TmpFs {
    entries: Mutex<HashMap<usize, DirEntryHandle>>,
}
impl FileSystemOps for Arc<TmpFs> {
    fn did_create_dir_entry(&self, _fs: &FileSystem, entry: &DirEntryHandle) {
        let k = Arc::as_ptr(entry) as usize;
        self.entries.lock().insert(k, Arc::clone(entry));
    }

    fn will_destroy_dir_entry(&self, _fs: &FileSystem, entry: &DirEntryHandle) {
        let k = Arc::as_ptr(entry) as usize;
        self.entries.lock().remove(&k);
    }
}

impl TmpFs {
    pub fn new() -> FileSystemHandle {
        let ops = Arc::new(TmpFs::default());
        FileSystem::new(ops, FsNode::new_root(TmpfsDirectory), None)
    }
}

struct TmpfsDirectory;

impl FsNodeOps for TmpfsDirectory {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(DirectoryFileObject::new()))
    }

    fn lookup(&self, _node: &FsNode, _name: &FsStr) -> Result<FsNodeHandle, Errno> {
        Err(ENOENT)
    }

    fn mkdir(&self, node: &FsNode, _name: &FsStr) -> Result<FsNodeHandle, Errno> {
        Ok(node.fs().create_node(Box::new(TmpfsDirectory), FileMode::IFDIR))
    }

    fn mknod(&self, node: &FsNode, _name: &FsStr, mode: FileMode) -> Result<FsNodeHandle, Errno> {
        let ops: Box<dyn FsNodeOps> = match mode.fmt() {
            FileMode::IFREG => Box::new(VmoFileNode::new()?),
            FileMode::IFIFO => Box::new(FifoNode::new()),
            FileMode::IFBLK => Box::new(DeviceNode),
            FileMode::IFCHR => Box::new(DeviceNode),
            _ => return Err(EACCES),
        };
        Ok(node.fs().create_node(ops, mode))
    }

    fn create_symlink(
        &self,
        node: &FsNode,
        _name: &FsStr,
        target: &FsStr,
    ) -> Result<FsNodeHandle, Errno> {
        Ok(node.fs().create_node(Box::new(SymlinkNode::new(target)), FileMode::IFLNK))
    }

    fn link(&self, _node: &FsNode, _name: &FsStr, child: &FsNodeHandle) -> Result<(), Errno> {
        child.info_write().link_count += 1;
        Ok(())
    }

    fn unlink(&self, _node: &FsNode, _name: &FsStr, child: &FsNodeHandle) -> Result<(), Errno> {
        child.info_write().link_count -= 1;
        Ok(())
    }
}

struct DirectoryFileObject {
    /// The current position for readdir.
    ///
    /// When readdir is called multiple times, we need to return subsequent
    /// directory entries. This field records where the previous readdir
    /// stopped.
    ///
    /// The state is actually recorded twice: once in the offset for this
    /// FileObject and again here. Recovering the state from the offset is slow
    /// because we would need to iterate through the keys of the BTree. Having
    /// the FsString cached lets us search the keys of the BTree faster.
    ///
    /// The initial "." and ".." entries are not recorded here. They are
    /// represented only in the offset field in the FileObject.
    readdir_position: Mutex<Bound<FsString>>,
}

impl DirectoryFileObject {
    fn new() -> DirectoryFileObject {
        DirectoryFileObject { readdir_position: Mutex::new(Bound::Unbounded) }
    }
}

impl FileOps for DirectoryFileObject {
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
            SeekOrigin::END => None,
        }
        .ok_or(EINVAL)?;

        if new_offset < 0 {
            return Err(EINVAL);
        }

        // Nothing to do.
        if *current_offset == new_offset {
            return Ok(new_offset);
        }

        let mut readdir_position = self.readdir_position.lock();
        *current_offset = new_offset;

        // We use 0 and 1 for "." and ".."
        if new_offset <= 2 {
            *readdir_position = Bound::Unbounded;
        } else {
            file.name.entry.get_children(|children| {
                let count = (new_offset - 2) as usize;
                *readdir_position = children
                    .iter()
                    .take(count)
                    .last()
                    .map_or(Bound::Unbounded, |(name, _)| Bound::Excluded(name.clone()));
            });
        }

        Ok(*current_offset)
    }

    fn readdir(
        &self,
        file: &FileObject,
        _task: &Task,
        sink: &mut dyn DirentSink,
    ) -> Result<(), Errno> {
        let mut offset = file.offset.lock();
        let mut readdir_position = self.readdir_position.lock();
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
        file.name.entry.get_children(|children| {
            for (name, maybe_entry) in children.range((readdir_position.clone(), Bound::Unbounded))
            {
                if let Some(entry) = maybe_entry.upgrade() {
                    let next_offset = *offset + 1;
                    let info = entry.node.info();
                    sink.add(
                        entry.node.inode_num,
                        next_offset,
                        DirectoryEntryType::from_mode(info.mode),
                        &name,
                    )?;
                    *offset = next_offset;
                    *readdir_position = Bound::Excluded(name.to_vec());
                }
            }
            Ok(())
        })
    }
}

struct FifoNode {
    /// The pipe located at this node.
    pipe: Arc<Mutex<Pipe>>,
}

impl FifoNode {
    fn new() -> FifoNode {
        FifoNode { pipe: Pipe::new() }
    }
}

impl FsNodeOps for FifoNode {
    fn open(&self, _node: &FsNode, flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Pipe::open(&self.pipe, flags))
    }
}

pub struct DeviceNode;

impl FsNodeOps for DeviceNode {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Err(ENOSYS)
    }
}

#[cfg(test)]
mod test {
    use super::*;

    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use std::sync::Arc;
    use zerocopy::AsBytes;

    use crate::mm::*;
    use crate::testing::*;

    #[test]
    fn test_tmpfs() {
        let fs = TmpFs::new();
        let root = fs.root();
        let usr = root.create_dir(b"usr").unwrap();
        let _etc = root.create_dir(b"etc").unwrap();
        let _usr_bin = usr.create_dir(b"bin").unwrap();
        let mut names = root.copy_child_names();
        names.sort();
        assert!(names.iter().eq([b"etc", b"usr"].iter()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_read() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let task = &task_owner.task;

        let test_mem_size = 0x10000;
        let test_vmo = Arc::new(zx::Vmo::create(test_mem_size).unwrap());

        let path = b"test.bin";
        let _file = task
            .fs
            .root
            .create_node(path, FileMode::IFREG | FileMode::ALLOW_ALL, DeviceType::NONE)
            .unwrap();

        let wr_file = task.open_file(path, OpenFlags::RDWR).unwrap();

        let flags = zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE;
        let test_addr = task
            .mm
            .map(
                UserAddress::default(),
                test_vmo,
                0,
                test_mem_size as usize,
                flags,
                MappingOptions::empty(),
            )
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
        let (_kernel, task_owner) = create_kernel_and_task();
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
        let fs = TmpFs::new();
        {
            let root = fs.root();
            let usr = root.create_dir(b"usr").expect("failed to create usr");
            root.create_dir(b"etc").expect("failed to create usr/etc");
            usr.create_dir(b"bin").expect("failed to create usr/bin");
        }

        // At this point, all the nodes are dropped.

        let (_kernel, task_owner) = create_kernel_and_task_with_fs(FsContext::new(fs));
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
            .name
            .unlink(task, b"test.txt", UnlinkKind::NonDirectory)
            .expect("failed to unlink test.text");
        assert_eq!(ENOENT, task.open_file(b"/usr/bin/test.txt", OpenFlags::RDWR).unwrap_err());
        assert_eq!(
            ENOENT,
            usr_bin.name.unlink(task, b"test.txt", UnlinkKind::NonDirectory).unwrap_err()
        );

        assert_eq!(0, txt.read(task, &[]).expect("failed to read"));
        std::mem::drop(txt);
        assert_eq!(ENOENT, task.open_file(b"/usr/bin/test.txt", OpenFlags::RDWR).unwrap_err());
        std::mem::drop(usr_bin);

        let usr = task.open_file(b"/usr", OpenFlags::RDONLY).expect("failed to open /usr");
        assert_eq!(ENOENT, task.open_file(b"/usr/foo", OpenFlags::RDONLY).unwrap_err());
        usr.name.unlink(task, b"bin", UnlinkKind::Directory).expect("failed to unlink /usr/bin");
    }
}
