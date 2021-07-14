// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use once_cell::sync::OnceCell;
use parking_lot::{RwLock, RwLockReadGuard, RwLockUpgradableReadGuard, RwLockWriteGuard};
use std::collections::HashMap;
use std::sync::{Arc, Weak};

use fuchsia_zircon::Time;

use super::{FileOps, ObserverList};
use crate::devices::DeviceHandle;
use crate::fs::*;
use crate::syscalls::system::time_to_timespec;
use crate::types::*;

pub struct FsNode {
    /// The FsNodeOps for this FsNode.
    ///
    /// The FsNodeOps are implemented by the individual file systems to provide
    /// specific behaviors for this FsNode.
    ///
    /// The FsNode is created with this OnceCell empty in order to hold the
    /// slot in the children map during initialization. After initialization,
    /// this OnceCell is populated with the FsNodeOps returned by the parent.
    ops: OnceCell<Box<dyn FsNodeOps>>,

    /// The tasks waiting on signals (e.g., POLLIN, POLLOUT) from this FsNode.
    pub observers: ObserverList,

    // TODO: replace with superblock handle
    device: DeviceHandle,

    /// The parent FsNode.
    ///
    /// The FsNode tree has strong references from child-to-parent and weak
    /// references from parent-to-child. This design ensures that the parent
    /// chain is always populated in the cache, but some children might be
    /// missing from the cache.
    parent: Option<FsNodeHandle>,

    /// The name that this parent calls this child.
    ///
    /// This name might not be reflected in the full path in the namespace that
    /// contains this FsNode. For example, this FsNode might be the root of a
    /// chroot.
    ///
    /// Most callers that want to work with names for FsNodes should use the
    /// NamespaceNodes.
    local_name: FsString,

    /// Mutable informationa about this node.
    ///
    /// This data is used to populate the stat_t structure.
    info: RwLock<FsNodeInfo>,

    /// A partial cache of the children of this node.
    ///
    /// FsNodes are added to this cache when they are looked up and removed
    /// when they are no longer referenced.
    ///
    /// This cache can also include partially initialized nodes that are in
    /// the process of being looked up. If the lookup fails, the nodes will
    /// be dropped from the cache.
    children: RwLock<HashMap<FsString, Weak<FsNode>>>,
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
    pub time_create: Time,
    pub time_access: Time,
    pub time_modify: Time,
    pub dev: dev_t,
    pub rdev: dev_t,
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum UnlinkKind {
    /// Unlink a directory.
    Directory,

    /// Unlink a non-directory.
    NonDirectory,
}

pub trait FsNodeOps: Send + Sync {
    /// Open a session with this node.
    ///
    /// The returned FileOps will be used to create a FileObject, which might
    /// be assigned an FdNumber.
    fn open(&self, node: &FsNode, flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno>;

    /// Find an existing child node with the given name.
    ///
    /// If this function does not return an error, this function must populate
    /// the mode field of the given FsNodeInfo to reflect the type of node
    /// found.
    fn lookup(
        &self,
        _node: &FsNode,
        _name: &FsStr,
        _info: &mut FsNodeInfo,
    ) -> Result<Box<dyn FsNodeOps>, Errno> {
        Err(ENOTDIR)
    }

    /// Create a child node with the given name.
    ///
    /// The mode field of the FsNodeInfo indicates what kind of child to
    /// create.
    ///
    /// This function is never called with FileMode::IFDIR. The mkdir function
    /// is used to create directories instead.
    fn mknod(
        &self,
        _node: &FsNode,
        _name: &FsStr,
        _info: &mut FsNodeInfo,
    ) -> Result<Box<dyn FsNodeOps>, Errno> {
        Err(ENOTDIR)
    }

    /// Create a subdirectory with the given name.
    fn mkdir(
        &self,
        _node: &FsNode,
        _name: &FsStr,
        _info: &mut FsNodeInfo,
    ) -> Result<Box<dyn FsNodeOps>, Errno> {
        Err(ENOTDIR)
    }

    /// Remove the child with the given name, if the child exists.
    ///
    /// The UnlinkKind parameter indicates whether the caller intends to unlink
    /// a directory or a non-directory child.
    fn unlink(&self, _node: &FsNode, _name: &FsStr, _kind: UnlinkKind) -> Result<(), Errno> {
        Err(ENOTDIR)
    }

    /// This node was added to the FsNode cache.
    ///
    /// The callee should perform any initialization needed.
    fn initialize(&self, _node: &FsNodeHandle) {}

    /// This node was unlinked from its parent.
    ///
    /// The node might still be referenced by open FileObjects.
    fn unlinked(&self, _node: &FsNodeHandle) {}

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
    pub fn new_root<T: FsNodeOps + 'static>(ops: T, device: DeviceHandle) -> FsNodeHandle {
        // TODO: apply_umask
        Self::new_orphan(ops, FileMode::IFDIR | FileMode::ALLOW_ALL, device)
    }
    pub fn new_orphan<T: FsNodeOps + 'static>(
        ops: T,
        mode: FileMode,
        device: DeviceHandle,
    ) -> FsNodeHandle {
        let ops: Box<dyn FsNodeOps> = Box::new(ops);
        FsNode::new(OnceCell::from(ops), mode, device, None, FsString::new())
    }

    fn new(
        ops: OnceCell<Box<dyn FsNodeOps>>,
        mode: FileMode,
        device: DeviceHandle,
        parent: Option<FsNodeHandle>,
        local_name: FsString,
    ) -> FsNodeHandle {
        let now = fuchsia_runtime::utc_time();
        let info = FsNodeInfo {
            inode_num: device.allocate_inode_number(),
            dev: device.get_device_id(),
            mode,
            time_create: now,
            time_access: now,
            time_modify: now,
            ..Default::default()
        };
        Arc::new(Self {
            ops,
            observers: ObserverList::default(),
            device,
            parent,
            local_name,
            children: RwLock::new(HashMap::new()),
            info: RwLock::new(info),
        })
    }

    /// The name that this node's parent calls this node.
    ///
    /// If this node is mounted in a namespace, the parent of this node in that
    /// namespace might have a different name for the point in the namespace at
    /// which this node is mounted.
    pub fn local_name(&self) -> &FsStr {
        &self.local_name
    }
    pub fn parent<'a>(self: &'a FsNodeHandle) -> Option<&'a FsNodeHandle> {
        self.parent.as_ref()
    }

    fn ops(&self) -> &dyn FsNodeOps {
        // Empty nodes are never returned to users of the FsNode API, so most uses of the ops field
        // can assume it is non-empty.
        &**self.ops.get().unwrap()
    }

    pub fn open(self: &FsNodeHandle, flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        self.ops().open(&self, flags)
    }

    pub fn component_lookup(self: &FsNodeHandle, name: &FsStr) -> Result<FsNodeHandle, Errno> {
        let node = self.get_or_create_empty_child(name);
        node.initialize(|name| {
            let mut info = node.info_mut();
            let ops = self.ops().lookup(&self, name, &mut info)?;
            assert!(
                info.mode & FileMode::IFMT != FileMode::EMPTY,
                "FsNodeOps::lookup did not populate the FileMode in FsNodeInfo."
            );
            Ok(ops)
        })?;
        Ok(node)
    }

    #[cfg(test)]
    pub fn mkdir(self: &FsNodeHandle, name: &FsStr) -> Result<FsNodeHandle, Errno> {
        // TODO: apply_umask
        self.mknod(name, FileMode::IFDIR | FileMode::ALLOW_ALL, 0)
    }

    pub fn mknod(
        self: &FsNodeHandle,
        name: &FsStr,
        mode: FileMode,
        dev: dev_t,
    ) -> Result<FsNodeHandle, Errno> {
        assert!(mode & FileMode::IFMT != FileMode::EMPTY, "mknod called without node type.");
        let node = self.get_or_create_empty_child(name);
        let exists = node.initialize(|name| {
            let mut info = node.info_mut();
            info.mode = mode;
            if mode.is_blk() || mode.is_chr() {
                info.rdev = dev;
            }
            if mode.is_dir() {
                self.ops().mkdir(&self, name, &mut info)
            } else {
                self.ops().mknod(&self, name, &mut info)
            }
        })?;
        if exists {
            return Err(EEXIST);
        }
        node.ops().initialize(&node);
        let now = fuchsia_runtime::utc_time();
        let mut info = self.info_mut();
        info.time_access = now;
        info.time_modify = now;
        Ok(node)
    }

    pub fn unlink(self: &FsNodeHandle, name: &FsStr, kind: UnlinkKind) -> Result<(), Errno> {
        let child = {
            let mut children = self.children.write();
            let child = children.remove(name).ok_or(ENOENT)?.upgrade().unwrap();
            // TODO: Check _kind against the child's mode.
            self.ops().unlink(self, name, kind).map_err(|e| {
                children.insert(name.to_owned(), Arc::downgrade(&child));
                e
            })?;
            child
        };
        // We hold a reference to child until we have released the children
        // lock so that we do not trigger a deadlock in the Drop trait for
        // Fsnode, which attempts to remove the FsNode from its parent's child
        // list.
        child.ops().unlinked(&child);
        Ok(())
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
            st_ctim: time_to_timespec(&info.time_create),
            st_mtim: time_to_timespec(&info.time_modify),
            st_atim: time_to_timespec(&info.time_access),
            st_dev: info.dev,
            st_rdev: info.rdev,
            st_blksize: BYTES_PER_BLOCK,
            ..Default::default()
        })
    }

    pub fn info(&self) -> RwLockReadGuard<'_, FsNodeInfo> {
        self.info.read()
    }

    pub fn info_mut(&self) -> RwLockWriteGuard<'_, FsNodeInfo> {
        self.info.write()
    }

    fn get_or_create_empty_child(self: &FsNodeHandle, name: &FsStr) -> FsNodeHandle {
        let children = self.children.upgradable_read();
        let child = children.get(name).and_then(|child| child.upgrade());
        if let Some(child) = child {
            return child;
        }
        let child = FsNode::new(
            OnceCell::new(),
            FileMode::EMPTY,
            Arc::clone(&self.device),
            Some(Arc::clone(self)),
            name.to_vec(),
        );
        let mut children = RwLockUpgradableReadGuard::upgrade(children);
        children.insert(child.local_name.clone(), Arc::downgrade(&child));
        child
    }

    fn initialize<F>(&self, init_fn: F) -> Result<bool, Errno>
    where
        F: FnOnce(&FsStr) -> Result<Box<dyn FsNodeOps>, Errno>,
    {
        let mut exists = true;
        self.ops.get_or_try_init(|| {
            exists = false;
            init_fn(&self.local_name)
        })?;
        Ok(exists)
    }

    // This function is only useful for tests and has some oddities.
    //
    // For example, not all the children might have been looked up yet, which
    // means the returned vector could be missing some names.
    //
    // Also, the vector might have "extra" names that are in the process of
    // being looked up. If the lookup fails, they'll be removed.
    #[cfg(test)]
    pub fn copy_child_names(&self) -> Vec<FsString> {
        self.children
            .read()
            .values()
            .map(|child| child.upgrade().unwrap().local_name().to_owned())
            .collect()
    }

    fn internal_remove_child(&self, child: &mut FsNode) {
        // possible deadlock? this is called from Drop, so we need to be careful about dropping any
        // FsNodeHandle while locking the children.
        self.children.write().remove(child.local_name());
    }
}

impl Drop for FsNode {
    fn drop(&mut self) {
        if let Some(parent) = self.parent.take() {
            parent.internal_remove_child(self);
        }
    }
}
