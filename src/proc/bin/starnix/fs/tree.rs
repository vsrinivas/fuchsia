// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use once_cell::sync::OnceCell;
use parking_lot::{RwLock, RwLockUpgradableReadGuard, RwLockWriteGuard};
use std::collections::HashMap;
use std::sync::{Arc, Weak};

use super::{FileHandle, FileObject, FileOps, ObserverList};
use crate::devices::DeviceHandle;
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
    stat: RwLock<stat_t>,
    state: RwLock<FsNodeState>,
}

pub type FsNodeHandle = Arc<FsNode>;

#[derive(Default)]
struct FsNodeState {
    children: HashMap<FsString, Weak<FsNode>>,
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
        Arc::new(Self {
            ops: OnceCell::from(ops),
            observers: ObserverList::default(),
            device,
            parent: None,
            name: FsString::new(),
            stat: RwLock::new(stat_t {
                st_ino: inode_number,
                st_dev: device_id,
                st_mode: mode,
                ..Default::default()
            }),
            state: Default::default(),
        })
    }
    fn name(&self) -> &FsStr {
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

    pub fn open(self: &FsNodeHandle) -> Result<FileHandle, Errno> {
        Ok(FileObject::new_from_box(self.ops().open(&self)?, Arc::clone(self)))
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

    pub fn update_stat(&self) -> Result<stat_t, Errno> {
        self.ops().update_stat(self)?;
        Ok(self.stat())
    }
    pub fn stat(&self) -> stat_t {
        self.stat.read().clone()
    }
    pub fn stat_mut(&self) -> RwLockWriteGuard<'_, stat_t> {
        self.stat.write()
    }

    fn get_or_create_empty_child(self: &FsNodeHandle, name: FsString) -> FsNodeHandle {
        let state = self.state.upgradable_read();
        let child = state.children.get(&name).and_then(|child| child.upgrade());
        if let Some(child) = child {
            return child;
        }
        let mut state = RwLockUpgradableReadGuard::upgrade(state);
        let child = Arc::new(Self {
            ops: OnceCell::new(),
            observers: ObserverList::default(),
            device: Arc::clone(&self.device),
            parent: Some(Arc::clone(self)),
            name: name.clone(),
            stat: RwLock::new(stat_t {
                st_ino: self.device.allocate_inode_number(),
                ..Default::default()
            }),
            state: Default::default(),
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
            .map(|child| child.upgrade().unwrap().name().to_owned())
            .collect()
    }

    fn internal_remove_child(&self, child: &mut FsNode) {
        // possible deadlock? this is called from Drop, so we need to be careful about dropping any
        // FsNodeHandle while locking the state.
        let removed = self.state.write().children.remove(child.name());
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
