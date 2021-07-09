// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use once_cell::sync::OnceCell;
use parking_lot::{RwLock, RwLockUpgradableReadGuard, RwLockWriteGuard};
use std::collections::HashMap;
use std::sync::{Arc, Weak};

use fuchsia_zircon::Time;

use super::{FileOps, ObserverList};
use crate::devices::DeviceHandle;
use crate::syscalls::system::time_to_timespec;
use crate::types::*;

pub type FsString = Vec<u8>;
pub type FsStr = [u8];

pub struct FsNode {
    ops: OnceCell<Box<dyn FsNodeOps>>,
    pub observers: ObserverList,
    // TODO: replace with superblock handle
    device: DeviceHandle,
    parent: Option<FsNodeHandle>,
    name: FsString,
    state: RwLock<FsNodeState>,
}

pub type FsNodeHandle = Arc<FsNode>;

#[derive(Default)]
pub struct FsNodeState {
    children: HashMap<FsString, Weak<FsNode>>,
    pub node_id: u64,
    pub device_id: u64,
    pub content_size: usize,
    pub storage_size: usize,
    pub no_blocks: usize,
    pub block_size: usize,
    pub mode: u32,
    pub link_count: u64,
    pub time_create: Time,
    pub time_access: Time,
    pub time_modify: Time,
}

pub trait FsNodeOps: Send + Sync {
    fn open(&self, node: &FsNode) -> Result<Box<dyn FileOps>, Errno>;

    // The various ways of creating sub-nodes
    fn lookup(&self, _node: &FsNode, _name: &FsStr) -> Result<Box<dyn FsNodeOps>, Errno> {
        Err(ENOTDIR)
    }
    fn mkdir(&self, _node: &FsNode, _name: &FsStr) -> Result<Box<dyn FsNodeOps>, Errno> {
        Err(ENOTDIR)
    }
    /// Change the length of the file.
    fn truncate(&self, _node: &FsNode, _length: u64) -> Result<(), Errno> {
        Err(EINVAL)
    }

    fn create(&self, _node: &FsNode, _name: &FsStr) -> Result<Box<dyn FsNodeOps>, Errno> {
        Err(ENOSYS)
    }

    /// Update node.stat if needed.
    fn update_stat(&self, _node: &FsNode) -> Result<(), Errno> {
        Ok(())
    }
}

impl FsNode {
    pub fn new_root<T: FsNodeOps + 'static>(ops: T, device: DeviceHandle) -> FsNodeHandle {
        Self::new_orphan(ops, S_IFDIR | 0777, device)
    }
    pub fn new_orphan<T: FsNodeOps + 'static>(
        ops: T,
        mode: mode_t,
        device: DeviceHandle,
    ) -> FsNodeHandle {
        let ops: Box<dyn FsNodeOps> = Box::new(ops);
        let inode_number = device.allocate_inode_number();
        let device_id = device.get_device_id();
        let now = fuchsia_runtime::utc_time();
        let node_state = FsNodeState {
            node_id: inode_number,
            device_id: device_id,
            mode: mode,
            time_create: now,
            time_access: now,
            time_modify: now,
            ..Default::default()
        };
        Arc::new(Self {
            ops: OnceCell::from(ops),
            observers: ObserverList::default(),
            device,
            parent: None,
            name: FsString::new(),
            state: RwLock::new(node_state),
        })
    }

    /// The name that this node's parent calls this node.
    ///
    /// If this node is mounted in a namespace, the parent of this node in that
    /// namespace might have a different name for the point in the namespace at
    /// which this node is mounted.
    pub fn local_name(&self) -> &FsStr {
        &self.name
    }
    pub fn parent<'a>(self: &'a FsNodeHandle) -> Option<&'a FsNodeHandle> {
        self.parent.as_ref()
    }

    fn ops(&self) -> &dyn FsNodeOps {
        // Empty nodes are never returned to users of the FsNode API, so most uses of the ops field
        // can assume it is non-empty.
        &**self.ops.get().unwrap()
    }

    pub fn open(self: &FsNodeHandle) -> Result<Box<dyn FileOps>, Errno> {
        self.ops().open(&self)
    }

    pub fn create(self: &FsNodeHandle, name: &FsStr) -> Result<FsNodeHandle, Errno> {
        let node = self.get_or_create_empty_child(name.to_vec());
        let exists = node.initialize(|name| self.ops().create(&node, name))?;
        if exists {
            return Err(EEXIST);
        }
        let now = fuchsia_runtime::utc_time();
        let mut st = self.state.write();
        st.time_access = now;
        st.time_modify = now;
        Ok(node)
    }

    pub fn component_lookup(self: &FsNodeHandle, name: &FsStr) -> Result<FsNodeHandle, Errno> {
        let node = self.get_or_create_empty_child(name.to_vec());
        node.initialize(|name| self.ops().lookup(&self, name))?;
        Ok(node)
    }

    #[cfg(test)]
    pub fn mkdir(self: &FsNodeHandle, name: FsString) -> Result<FsNodeHandle, Errno> {
        let node = self.get_or_create_empty_child(name);
        let exists = node.initialize(|name| self.ops().mkdir(&self, name))?;
        if exists {
            return Err(EEXIST);
        }
        Ok(node)
    }

    pub fn truncate(&self, length: u64) -> Result<(), Errno> {
        self.ops().truncate(self, length)
    }

    pub fn update_stat(&self) -> Result<stat_t, Errno> {
        self.ops().update_stat(self)?;
        Ok(self.stat())
    }

    pub fn stat(&self) -> stat_t {
        let state = self.state.read();
        /// st_blksize is measured in units of 512 bytes.
        const BYTES_PER_BLOCK: i64 = 512;
        stat_t {
            st_ino: state.node_id,
            st_mode: state.mode,
            st_size: state.content_size as off_t,
            st_blocks: state.storage_size as i64 / BYTES_PER_BLOCK,
            st_nlink: state.link_count,
            st_uid: 0,
            st_gid: 0,
            st_ctim: time_to_timespec(&state.time_create),
            st_mtim: time_to_timespec(&state.time_modify),
            st_atim: time_to_timespec(&state.time_access),
            st_dev: state.device_id,
            st_rdev: 0,
            st_blksize: BYTES_PER_BLOCK,
            ..Default::default()
        }
    }

    pub fn state_mut(&self) -> RwLockWriteGuard<'_, FsNodeState> {
        self.state.write()
    }

    pub fn get_or_create_empty_child(self: &FsNodeHandle, name: FsString) -> FsNodeHandle {
        let state = self.state.upgradable_read();
        let child = state.children.get(&name).and_then(|child| child.upgrade());
        if let Some(child) = child {
            return child;
        }
        let mut state = RwLockUpgradableReadGuard::upgrade(state);
        let now = fuchsia_runtime::utc_time();
        let node_state = FsNodeState {
            node_id: self.device.allocate_inode_number(),
            device_id: self.device.get_device_id(),
            time_create: now,
            time_access: now,
            time_modify: now,
            ..Default::default()
        };
        let child = Arc::new(Self {
            ops: OnceCell::new(),
            observers: ObserverList::default(),
            device: Arc::clone(&self.device),
            parent: Some(Arc::clone(self)),
            name: name.clone(),

            state: RwLock::new(node_state),
        });
        state.children.insert(name, Arc::downgrade(&child));
        child
    }

    fn initialize<F>(&self, init_fn: F) -> Result<bool, Errno>
    where
        F: FnOnce(&FsStr) -> Result<Box<dyn FsNodeOps>, Errno>,
    {
        let mut exists = true;
        self.ops.get_or_try_init(|| {
            exists = false;
            init_fn(&self.name)
        })?;
        Ok(exists)
    }

    #[cfg(test)]
    fn copy_child_names(&self) -> Vec<FsString> {
        self.state
            .read()
            .children
            .values()
            .map(|child| child.upgrade().unwrap().local_name().to_owned())
            .collect()
    }

    fn internal_remove_child(&self, child: &mut FsNode) {
        // possible deadlock? this is called from Drop, so we need to be careful about dropping any
        // FsNodeHandle while locking the state.
        let removed = self.state.write().children.remove(child.local_name());
        assert!(removed.is_some(), "a node should always be in its parent's set of children!");
    }
}

impl Drop for FsNode {
    fn drop(&mut self) {
        if let Some(parent) = self.parent.take() {
            parent.internal_remove_child(self);
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;

    use crate::devices::*;
    use crate::fs::tmp::TmpfsDirectory;

    #[test]
    fn test_tmpfs() {
        let tmpfs = AnonNodeDevice::new(0);
        let root = FsNode::new_root(TmpfsDirectory, tmpfs);
        let usr = root.mkdir(b"usr".to_vec()).unwrap();
        let _etc = root.mkdir(b"etc".to_vec()).unwrap();
        let _usr_bin = usr.mkdir(b"bin".to_vec()).unwrap();
        let mut names = root.copy_child_names();
        names.sort();
        assert!(names.iter().eq([b"etc", b"usr"].iter()));
    }
}
