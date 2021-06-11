// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use once_cell::sync::OnceCell;
use parking_lot::{RwLock, RwLockUpgradableReadGuard};
use std::collections::HashMap;
use std::sync::{Arc, Weak};

use super::{FileHandle, FileObject, FileOps};
use crate::devices::DeviceHandle;
use crate::types::*;

pub type FsString = Vec<u8>;
pub type FsStr = [u8];

pub struct FsNode {
    ops: OnceCell<Box<dyn FsNodeOps>>,
    device: DeviceHandle,
    parent: Option<FsNodeHandle>,
    name: FsString,
    inode_number: ino_t,
    state: RwLock<FsNodeState>,
}

pub type FsNodeHandle = Arc<FsNode>;

#[derive(Default)]
struct FsNodeState {
    children: HashMap<FsString, Weak<FsNode>>,
}

pub trait FsNodeOps: Send + Sync {
    fn open(&self) -> Result<Box<dyn FileOps>, Errno>;

    // The various ways of creating sub-nodes
    fn lookup(&self, _name: &FsStr) -> Result<Box<dyn FsNodeOps>, Errno> {
        Err(ENOTDIR)
    }
    fn mkdir(&self, _name: &FsStr) -> Result<Box<dyn FsNodeOps>, Errno> {
        Err(ENOTDIR)
    }
}

impl FsNode {
    pub fn new_root<T: FsNodeOps + 'static>(ops: T, device: DeviceHandle) -> FsNodeHandle {
        let ops: Box<dyn FsNodeOps> = Box::new(ops);
        let inode_number = device.allocate_inode_number();
        Arc::new(FsNode {
            ops: OnceCell::from(ops),
            device,
            parent: None,
            name: FsString::new(),
            inode_number,
            state: Default::default(),
        })
    }
    fn name(&self) -> &FsStr {
        &self.name
    }
    fn parent<'a>(self: &'a FsNodeHandle) -> &'a FsNodeHandle {
        self.parent.as_ref().unwrap_or(self)
    }

    fn ops(&self) -> &dyn FsNodeOps {
        // Empty nodes are never returned to users of the FsNode API, so most uses of the ops field
        // can assume it is non-empty.
        &**self.ops.get().unwrap()
    }

    pub fn open(self: &FsNodeHandle) -> Result<FileHandle, Errno> {
        Ok(FileObject::new_from_box(self.ops().open()?, Arc::clone(self)))
    }

    pub fn traverse(self: &FsNodeHandle, path: &FsStr) -> Result<FsNodeHandle, Errno> {
        // I'm a little disappointed in the number of refcount increments and decrements that happen
        // here.
        let mut node = Arc::clone(self);
        for component in path.split(|c| *c == b'/') {
            if component == b"." || component == b"" {
                // ignore
            } else if component == b".." {
                node = Arc::clone(node.parent());
            } else {
                node = node.component_lookup(component)?;
            }
        }
        Ok(node)
    }

    fn component_lookup(self: &FsNodeHandle, name: &FsStr) -> Result<FsNodeHandle, Errno> {
        let node = self.get_or_create_empty_child(name.to_vec());
        node.initialize(|name| self.ops().lookup(name))?;
        Ok(node)
    }

    fn mkdir(self: &FsNodeHandle, name: FsString) -> Result<FsNodeHandle, Errno> {
        let node = self.get_or_create_empty_child(name);
        let exists = node.initialize(|name| self.ops().mkdir(name))?;
        if exists {
            return Err(EEXIST);
        }
        Ok(node)
    }

    pub fn fstat(&self) -> stat_t {
        stat_t {
            st_dev: self.device.get_device_id(),
            st_ino: self.inode_number,
            ..stat_t::default()
        }
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
            device: Arc::clone(&self.device),
            parent: Some(Arc::clone(self)),
            name: name.clone(),
            inode_number: self.device.allocate_inode_number(),
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

struct TmpfsDirectory;

impl FsNodeOps for TmpfsDirectory {
    fn mkdir(&self, _name: &FsStr) -> Result<Box<dyn FsNodeOps>, Errno> {
        Ok(Box::new(Self))
    }
    fn open(&self) -> Result<Box<dyn FileOps>, Errno> {
        Err(ENOSYS)
    }
}

#[cfg(test)]
mod test {
    use super::*;

    use crate::devices::*;

    #[test]
    fn test_tmpfs() {
        let tmpfs = AnonNodeDevice::new(0);
        let root = FsNode::new_root(TmpfsDirectory {}, tmpfs);
        let usr = root.mkdir(b"usr".to_vec()).unwrap();
        let _etc = root.mkdir(b"etc".to_vec()).unwrap();
        let _usr_bin = usr.mkdir(b"bin".to_vec()).unwrap();
        let mut names = root.copy_child_names();
        names.sort();
        assert!(names.iter().eq([b"etc", b"usr"].iter()));
    }
}
