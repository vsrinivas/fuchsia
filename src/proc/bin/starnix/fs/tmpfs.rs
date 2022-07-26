// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use super::directory_file::MemoryDirectoryFile;
use super::*;
use crate::auth::FsCred;
use crate::lock::{Mutex, MutexGuard};
use crate::logging::not_implemented;
use crate::task::{CurrentTask, Kernel};
use crate::types::*;

pub struct TmpFs(());

impl FileSystemOps for Arc<TmpFs> {
    fn rename(
        &self,
        _fs: &FileSystem,
        old_parent: &FsNodeHandle,
        _old_name: &FsStr,
        new_parent: &FsNodeHandle,
        _new_name: &FsStr,
        renamed: &FsNodeHandle,
        replaced: Option<&FsNodeHandle>,
    ) -> Result<(), Errno> {
        fn child_count(node: &FsNodeHandle) -> MutexGuard<'_, u32> {
            // The following cast are safe, unless something is seriously wrong:
            // - The filesystem should not be asked to rename node that it doesn't handle.
            // - Parents in a rename operation need to be directories.
            // - TmpfsDirectory is the ops for directories in this filesystem.
            node.downcast_ops::<TmpfsDirectory>().unwrap().child_count.lock()
        }
        if let Some(replaced) = replaced {
            if replaced.is_dir() {
                if !renamed.is_dir() {
                    return error!(EISDIR);
                }
                // Ensures that replaces is empty.
                if *child_count(replaced) != 0 {
                    return error!(ENOTEMPTY);
                }
            }
        }
        *child_count(old_parent) -= 1;
        *child_count(new_parent) += 1;
        if renamed.is_dir() {
            old_parent.info_write().link_count -= 1;
            new_parent.info_write().link_count += 1;
        }
        // Fix the wrong changes to new_parent due to the fact that the target element has
        // been replaced instead of added.
        if let Some(replaced) = replaced {
            if replaced.is_dir() {
                new_parent.info_write().link_count -= 1;
            }
            *child_count(new_parent) -= 1;
        }
        Ok(())
    }
}

impl TmpFs {
    pub fn new(kernel: &Kernel) -> FileSystemHandle {
        Self::new_with_data(kernel, b"")
    }

    pub fn new_with_data(kernel: &Kernel, data: &FsStr) -> FileSystemHandle {
        let fs = FileSystem::new_with_permanent_entries(kernel, Arc::new(TmpFs(())));
        fs.set_root(TmpfsDirectory::new());
        let root_node = &fs.root().node;
        if data.len() > 0 {
            for option in data.split(|c| *c == b',') {
                let mut splitted_option = option.split(|c| *c == b'=');
                match splitted_option.next() {
                    Some(b"mode") => {
                        if let Some(mode) = splitted_option.next() {
                            if splitted_option.next().is_none() {
                                if let Ok(mode) = FileMode::from_string(mode) {
                                    root_node.chmod(mode);
                                    continue;
                                }
                            }
                        }
                    }
                    Some(b"uid") => {
                        if let Some(uid) = splitted_option.next() {
                            if splitted_option.next().is_none() {
                                if let Ok(uid) = std::str::from_utf8(uid) {
                                    if let Ok(uid) = uid.parse::<uid_t>() {
                                        root_node.chown(Some(uid), None);
                                        continue;
                                    }
                                }
                            }
                        }
                    }
                    Some(b"gid") => {
                        if let Some(gid) = splitted_option.next() {
                            if splitted_option.next().is_none() {
                                if let Ok(gid) = std::str::from_utf8(gid) {
                                    if let Ok(gid) = gid.parse::<gid_t>() {
                                        root_node.chown(None, Some(gid));
                                        continue;
                                    }
                                }
                            }
                        }
                    }
                    _ => {}
                }
                // Overall fallback. All matched option ended up in a continue.
                not_implemented!(
                    "[system]",
                    "Unknown tmpfs option: {}",
                    String::from_utf8_lossy(option)
                );
            }
        }
        fs
    }
}

pub struct TmpfsDirectory {
    xattrs: MemoryXattrStorage,
    child_count: Mutex<u32>,
}

impl TmpfsDirectory {
    pub fn new() -> Self {
        Self { xattrs: MemoryXattrStorage::default(), child_count: Mutex::new(0) }
    }
}

impl FsNodeOps for TmpfsDirectory {
    fs_node_impl_xattr_delegate!(self, self.xattrs);

    fn create_file_ops(
        &self,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(MemoryDirectoryFile::new()))
    }

    fn lookup(
        &self,
        _node: &FsNode,
        _current_task: &CurrentTask,
        name: &FsStr,
    ) -> Result<FsNodeHandle, Errno> {
        error!(ENOENT, format!("looking for {:?}", String::from_utf8_lossy(name)))
    }

    fn mkdir(
        &self,
        node: &FsNode,
        _name: &FsStr,
        mode: FileMode,
        owner: FsCred,
    ) -> Result<FsNodeHandle, Errno> {
        node.info_write().link_count += 1;
        *self.child_count.lock() += 1;
        Ok(node.fs().create_node(Box::new(TmpfsDirectory::new()), mode, owner))
    }

    fn mknod(
        &self,
        node: &FsNode,
        _name: &FsStr,
        mode: FileMode,
        dev: DeviceType,
        owner: FsCred,
    ) -> Result<FsNodeHandle, Errno> {
        let ops: Box<dyn FsNodeOps> = match mode.fmt() {
            FileMode::IFREG => Box::new(VmoFileNode::new()?),
            FileMode::IFIFO | FileMode::IFBLK | FileMode::IFCHR | FileMode::IFSOCK => {
                Box::new(TmpfsSpecialNode::new())
            }
            _ => return error!(EACCES),
        };
        *self.child_count.lock() += 1;
        let node = node.fs().create_node(ops, mode, owner);
        node.info_write().rdev = dev;
        Ok(node)
    }

    fn create_symlink(
        &self,
        node: &FsNode,
        _name: &FsStr,
        target: &FsStr,
        owner: FsCred,
    ) -> Result<FsNodeHandle, Errno> {
        *self.child_count.lock() += 1;
        Ok(node.fs().create_node(Box::new(SymlinkNode::new(target)), mode!(IFLNK, 0o777), owner))
    }

    fn link(&self, _node: &FsNode, _name: &FsStr, child: &FsNodeHandle) -> Result<(), Errno> {
        child.info_write().link_count += 1;
        *self.child_count.lock() += 1;
        Ok(())
    }

    fn unlink(&self, node: &FsNode, _name: &FsStr, child: &FsNodeHandle) -> Result<(), Errno> {
        if child.is_dir() {
            node.info_write().link_count -= 1;
        }
        child.info_write().link_count -= 1;
        *self.child_count.lock() -= 1;
        Ok(())
    }
}

struct TmpfsSpecialNode {
    xattrs: MemoryXattrStorage,
}

impl TmpfsSpecialNode {
    pub fn new() -> Self {
        Self { xattrs: MemoryXattrStorage::default() }
    }
}

impl FsNodeOps for TmpfsSpecialNode {
    fs_node_impl_xattr_delegate!(self, self.xattrs);

    fn create_file_ops(
        &self,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        unreachable!("Special nodes cannot be opened.");
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::mm::*;
    use crate::testing::*;
    use fuchsia_zircon as zx;
    use std::sync::Arc;
    use zerocopy::AsBytes;

    #[::fuchsia::test]
    fn test_tmpfs() {
        let (kernel, current_task) = create_kernel_and_task();
        let fs = TmpFs::new(&kernel);
        let root = fs.root();
        let usr = root.create_dir(&current_task, b"usr").unwrap();
        let _etc = root.create_dir(&current_task, b"etc").unwrap();
        let _usr_bin = usr.create_dir(&current_task, b"bin").unwrap();
        let mut names = root.copy_child_names();
        names.sort();
        assert!(names.iter().eq([b"etc", b"usr"].iter()));
    }

    #[::fuchsia::test]
    fn test_write_read() {
        let (_kernel, current_task) = create_kernel_and_task();

        let test_mem_size = 0x10000;
        let test_vmo = Arc::new(zx::Vmo::create(test_mem_size).unwrap());

        let path = b"test.bin";
        let _file = current_task
            .fs()
            .root()
            .create_node(&current_task, path, mode!(IFREG, 0o777), DeviceType::NONE)
            .unwrap();

        let wr_file = current_task.open_file(path, OpenFlags::RDWR).unwrap();

        let flags = zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE;
        let test_addr = current_task
            .mm
            .map(
                DesiredAddress::Hint(UserAddress::default()),
                test_vmo,
                0,
                test_mem_size as usize,
                flags,
                MappingOptions::empty(),
                None,
            )
            .unwrap();

        let seq_addr = UserAddress::from_ptr(test_addr.ptr() + path.len());
        let test_seq = 0..10000u16;
        let test_vec = test_seq.collect::<Vec<_>>();
        let test_bytes = test_vec.as_slice().as_bytes();
        current_task.mm.write_memory(seq_addr, test_bytes).unwrap();
        let buf = [UserBuffer { address: seq_addr, length: test_bytes.len() }];

        let written = wr_file.write(&current_task, &buf).unwrap();
        assert_eq!(written, test_bytes.len());

        let mut read_vec = vec![0u8; test_bytes.len()];
        current_task.mm.read_memory(seq_addr, read_vec.as_bytes_mut()).unwrap();

        assert_eq!(test_bytes, &*read_vec);
    }

    #[::fuchsia::test]
    fn test_read_past_eof() {
        let (_kernel, current_task) = create_kernel_and_task();

        // Open an empty file
        let path = b"test.bin";
        let _file = current_task
            .fs()
            .root()
            .create_node(&current_task, path, mode!(IFREG, 0o777), DeviceType::NONE)
            .unwrap();
        let rd_file = current_task.open_file(path, OpenFlags::RDONLY).unwrap();

        // Verify that attempting to read past the EOF (i.e. at a non-zero offset) returns 0
        let test_mem_size = 0x10000;
        let test_vmo = Arc::new(zx::Vmo::create(test_mem_size).unwrap());
        let flags = zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE;
        let test_addr = current_task
            .mm
            .map(
                DesiredAddress::Hint(UserAddress::default()),
                test_vmo,
                0,
                test_mem_size as usize,
                flags,
                MappingOptions::empty(),
                None,
            )
            .unwrap();
        let buf = [UserBuffer { address: test_addr, length: test_mem_size as usize }];
        let test_offset = 100;
        let result = rd_file.read_at(&current_task, test_offset, &buf).unwrap();
        assert_eq!(result, 0);
    }

    #[::fuchsia::test]
    fn test_permissions() {
        let (_kernel, current_task) = create_kernel_and_task();

        let path = b"test.bin";
        let file = current_task
            .open_file_at(
                FdNumber::AT_FDCWD,
                path,
                OpenFlags::CREAT | OpenFlags::RDONLY,
                FileMode::from_bits(0o777),
            )
            .expect("failed to create file");
        assert_eq!(0, file.read(&current_task, &[]).expect("failed to read"));
        assert!(file.write(&current_task, &[]).is_err());

        let file = current_task
            .open_file_at(FdNumber::AT_FDCWD, path, OpenFlags::WRONLY, FileMode::EMPTY)
            .expect("failed to open file WRONLY");
        assert!(file.read(&current_task, &[]).is_err());
        assert_eq!(0, file.write(&current_task, &[]).expect("failed to write"));

        let file = current_task
            .open_file_at(FdNumber::AT_FDCWD, path, OpenFlags::RDWR, FileMode::EMPTY)
            .expect("failed to open file RDWR");
        assert_eq!(0, file.read(&current_task, &[]).expect("failed to read"));
        assert_eq!(0, file.write(&current_task, &[]).expect("failed to write"));
    }

    #[::fuchsia::test]
    fn test_persistence() {
        let (_kernel, current_task) = create_kernel_and_task();

        {
            let root = &current_task.fs().root().entry;
            let usr = root.create_dir(&current_task, b"usr").expect("failed to create usr");
            root.create_dir(&current_task, b"etc").expect("failed to create usr/etc");
            usr.create_dir(&current_task, b"bin").expect("failed to create usr/bin");
        }

        // At this point, all the nodes are dropped.

        current_task
            .open_file(b"/usr/bin", OpenFlags::RDONLY | OpenFlags::DIRECTORY)
            .expect("failed to open /usr/bin");
        assert_eq!(
            errno!(ENOENT),
            current_task.open_file(b"/usr/bin/test.txt", OpenFlags::RDWR).unwrap_err()
        );
        current_task
            .open_file_at(
                FdNumber::AT_FDCWD,
                b"/usr/bin/test.txt",
                OpenFlags::RDWR | OpenFlags::CREAT,
                FileMode::from_bits(0o777),
            )
            .expect("failed to create test.txt");
        let txt = current_task
            .open_file(b"/usr/bin/test.txt", OpenFlags::RDWR)
            .expect("failed to open test.txt");

        let usr_bin = current_task
            .open_file(b"/usr/bin", OpenFlags::RDONLY)
            .expect("failed to open /usr/bin");
        usr_bin
            .name
            .unlink(&current_task, b"test.txt", UnlinkKind::NonDirectory)
            .expect("failed to unlink test.text");
        assert_eq!(
            errno!(ENOENT),
            current_task.open_file(b"/usr/bin/test.txt", OpenFlags::RDWR).unwrap_err()
        );
        assert_eq!(
            errno!(ENOENT),
            usr_bin.name.unlink(&current_task, b"test.txt", UnlinkKind::NonDirectory).unwrap_err()
        );

        assert_eq!(0, txt.read(&current_task, &[]).expect("failed to read"));
        std::mem::drop(txt);
        assert_eq!(
            errno!(ENOENT),
            current_task.open_file(b"/usr/bin/test.txt", OpenFlags::RDWR).unwrap_err()
        );
        std::mem::drop(usr_bin);

        let usr = current_task.open_file(b"/usr", OpenFlags::RDONLY).expect("failed to open /usr");
        assert_eq!(
            errno!(ENOENT),
            current_task.open_file(b"/usr/foo", OpenFlags::RDONLY).unwrap_err()
        );
        usr.name
            .unlink(&current_task, b"bin", UnlinkKind::Directory)
            .expect("failed to unlink /usr/bin");
    }

    #[::fuchsia::test]
    fn test_data() {
        let (kernel, _current_task) = create_kernel_and_task();
        let fs = TmpFs::new_with_data(&kernel, b"mode=0123,uid=42,gid=84");
        let info = fs.root().node.info();
        assert_eq!(info.mode, mode!(IFDIR, 0o123));
        assert_eq!(info.uid, 42);
        assert_eq!(info.gid, 84);
    }
}
