// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;
use parking_lot::{RwLock, RwLockReadGuard, RwLockWriteGuard};
use std::sync::{Arc, Weak};

use crate::device::*;
use crate::fs::*;
use crate::types::*;

pub struct FsNode {
    /// The FsNodeOps for this FsNode.
    ///
    /// The FsNodeOps are implemented by the individual file systems to provide
    /// specific behaviors for this FsNode.
    ops: Option<Box<dyn FsNodeOps>>,

    /// The FileSystem that owns this FsNode's tree.
    fs: Weak<FileSystem>,

    /// The tasks waiting on signals (e.g., POLLIN, POLLOUT) from this FsNode.
    pub observers: ObserverList,

    /// Mutable informationa about this node.
    ///
    /// This data is used to populate the stat_t structure.
    info: RwLock<FsNodeInfo>,

    /// A RwLock to synchronize append operations for this node.
    ///
    /// FileObjects writing with O_APPEND should grab a write() lock on this
    /// field to ensure they operate sequentially. FileObjects writing without
    /// O_APPEND should grab read() lock so that they can operate in parallel.
    pub append_lock: RwLock<()>,
}

pub type FsNodeHandle = Arc<FsNode>;

#[derive(Default)]
pub struct FsNodeInfo {
    pub inode_num: ino_t,
    pub mode: FileMode,
    pub size: usize,
    pub storage_size: usize,
    pub blksize: usize,
    pub uid: uid_t,
    pub gid: gid_t,
    pub link_count: u64,
    pub time_create: zx::Time,
    pub time_access: zx::Time,
    pub time_modify: zx::Time,
    pub dev: DeviceType,
    pub rdev: DeviceType,
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum UnlinkKind {
    /// Unlink a directory.
    Directory,

    /// Unlink a non-directory.
    NonDirectory,
}

pub trait FsNodeOps: Send + Sync {
    /// Open a FileObject for this node.
    ///
    /// The returned FileOps will be used to create a FileObject, which might
    /// be assigned an FdNumber.
    fn open(&self, node: &FsNode, flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno>;

    /// Find an existing child node and populate the child parameter. Return the node.
    ///
    /// The child parameter is an empty node. Operations other than initialize may panic before
    /// initialize is called.
    fn lookup(&self, _node: &FsNode, _name: &FsStr, _child: &mut FsNode) -> Result<(), Errno> {
        Err(ENOTDIR)
    }

    /// Create and return the given child node.
    ///
    /// The mode field of the FsNodeInfo indicates what kind of child to
    /// create.
    ///
    /// This function is never called with FileMode::IFDIR. The mkdir function
    /// is used to create directories instead.
    fn mknod(&self, _node: &FsNode, _name: &FsStr, _child: &mut FsNode) -> Result<(), Errno> {
        Err(ENOTDIR)
    }

    /// Create and return the given child node as a subdirectory.
    fn mkdir(&self, _node: &FsNode, _name: &FsStr, _child: &mut FsNode) -> Result<(), Errno> {
        Err(ENOTDIR)
    }

    /// Creates a symlink with the given `target` path.
    fn create_symlink(
        &self,
        _node: &FsNode,
        _name: &FsStr,
        _target: &FsStr,
        _child: &mut FsNode,
    ) -> Result<(), Errno> {
        Err(ENOTDIR)
    }

    /// Reads the symlink from this node.
    fn readlink(&self, _node: &FsNode) -> Result<FsString, Errno> {
        Err(EINVAL)
    }

    /// Remove the child with the given name, if the child exists.
    ///
    /// The UnlinkKind parameter indicates whether the caller intends to unlink
    /// a directory or a non-directory child.
    fn unlink(&self, _node: &FsNode, _name: &FsStr, _child: &FsNodeHandle) -> Result<(), Errno> {
        Err(ENOTDIR)
    }

    /// Change the length of the file.
    fn truncate(&self, _node: &FsNode, _length: u64) -> Result<(), Errno> {
        Err(EINVAL)
    }

    /// Update node.info as needed.
    ///
    /// FsNode calls this method before converting the FsNodeInfo struct into
    /// the stat_t struct to give the file system a chance to update this data
    /// before it is used by clients.
    ///
    /// File systems that keep the FsNodeInfo up-to-date do not need to
    /// override this function.
    ///
    /// Return a reader lock on the updated information.
    fn update_info<'a>(&self, node: &'a FsNode) -> Result<RwLockReadGuard<'a, FsNodeInfo>, Errno> {
        Ok(node.info())
    }
}

impl FsNode {
    pub fn new_orphan<T: FsNodeOps + 'static>(
        ops: T,
        mode: FileMode,
        fs: &FileSystemHandle,
    ) -> FsNodeHandle {
        Arc::new(FsNode::new(Some(Box::new(ops)), mode, fs))
    }

    pub fn new(ops: Option<Box<dyn FsNodeOps>>, mode: FileMode, fs: &FileSystemHandle) -> FsNode {
        let now = fuchsia_runtime::utc_time();
        let info = FsNodeInfo {
            inode_num: fs.next_inode_num(),
            mode,
            time_create: now,
            time_access: now,
            time_modify: now,
            ..Default::default()
        };
        Self {
            ops,
            observers: ObserverList::default(),
            fs: Arc::downgrade(&fs),
            info: RwLock::new(info),
            append_lock: RwLock::new(()),
        }
    }

    pub fn file_system(&self) -> FileSystemHandle {
        self.fs.upgrade().expect("FileSystem did not live long enough")
    }

    fn ops(&self) -> &dyn FsNodeOps {
        // Empty nodes are never returned to users of the FsNode API, so most uses of the ops field
        // can assume it is non-empty.
        &**self.ops.as_ref().unwrap()
    }

    pub fn set_ops(&mut self, ops: impl FsNodeOps + 'static) {
        self.ops = Some(Box::new(ops));
    }

    pub fn has_ops(&self) -> bool {
        self.ops.is_some()
    }

    pub fn open(&self, flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        // If O_PATH is set, there is no need to create a real FileOps because
        // most file operations are disabled.
        if flags.contains(OpenFlags::PATH) {
            return Ok(Box::new(OPathOps::new()));
        }

        let (mode, rdev) = {
            // Don't hold the info lock while calling into open_device or self.ops().
            // TODO: The mode and rdev are immutable and shouldn't require a lock to read.
            let info = self.info();
            (info.mode, info.rdev)
        };
        if mode.is_chr() {
            open_character_device(rdev)
        } else if mode.is_blk() {
            open_block_device(rdev)
        } else {
            self.ops().open(&self, flags)
        }
    }

    pub fn lookup(&self, name: &FsStr, child: &mut FsNode) -> Result<(), Errno> {
        self.ops().lookup(self, name, child)
    }

    pub fn mknod(&self, name: &FsStr, child: &mut FsNode) -> Result<(), Errno> {
        self.ops().mknod(self, name, child)
    }

    pub fn mkdir(&self, name: &FsStr, child: &mut FsNode) -> Result<(), Errno> {
        self.ops().mkdir(self, name, child)
    }

    pub fn create_symlink(
        &self,
        name: &FsStr,
        target: &FsStr,
        child: &mut FsNode,
    ) -> Result<(), Errno> {
        self.ops().create_symlink(self, name, target, child)
    }

    pub fn readlink(&self) -> Result<FsString, Errno> {
        self.ops().readlink(self)
    }

    pub fn unlink(&self, name: &FsStr, child: &FsNodeHandle) -> Result<(), Errno> {
        self.ops().unlink(self, name, child)
    }

    pub fn truncate(&self, length: u64) -> Result<(), Errno> {
        self.ops().truncate(self, length)
    }

    pub fn stat(&self) -> Result<stat_t, Errno> {
        let info = self.ops().update_info(self)?;
        /// st_blksize is measured in units of 512 bytes.
        const BYTES_PER_BLOCK: i64 = 512;
        Ok(stat_t {
            st_ino: info.inode_num,
            st_mode: info.mode.bits(),
            st_size: info.size as off_t,
            st_blocks: info.storage_size as i64 / BYTES_PER_BLOCK,
            st_nlink: info.link_count,
            st_uid: info.uid,
            st_gid: info.gid,
            st_ctim: timespec_from_time(info.time_create),
            st_mtim: timespec_from_time(info.time_modify),
            st_atim: timespec_from_time(info.time_access),
            st_dev: info.dev.bits(),
            st_rdev: info.rdev.bits(),
            st_blksize: BYTES_PER_BLOCK,
            ..Default::default()
        })
    }

    pub fn info(&self) -> RwLockReadGuard<'_, FsNodeInfo> {
        self.info.read()
    }
    pub fn info_write(&self) -> RwLockWriteGuard<'_, FsNodeInfo> {
        self.info.write()
    }
    pub fn info_mut(&mut self) -> &mut FsNodeInfo {
        self.info.get_mut()
    }
}
