// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use once_cell::sync::OnceCell;
use parking_lot::{RwLock, RwLockUpgradableReadGuard};
use std::collections::HashMap;
use std::sync::{Arc, Weak};

use crate::types::*;

pub type FsString = Vec<u8>;
pub type FsStr = [u8];

pub struct FsNode {
    ops: OnceCell<Box<dyn FsNodeOps>>,
    parent: Option<FsNodeHandle>,
    name: FsString,
    state: RwLock<FsNodeState>,
}
pub type FsNodeHandle = Arc<FsNode>;

#[derive(Default)]
struct FsNodeState {
    children: HashMap<FsString, Weak<FsNode>>,
}

pub trait FsNodeOps {
    // The various ways of creating sub-nodes
    fn lookup(&self, _name: &FsStr) -> Result<Box<dyn FsNodeOps>, Errno> {
        Err(ENOTDIR)
    }
    fn mkdir(&self, _name: &FsStr) -> Result<Box<dyn FsNodeOps>, Errno> {
        Err(ENOTDIR)
    }
}

impl FsNode {
    pub fn new_root<T: FsNodeOps + 'static>(ops: T) -> FsNodeHandle {
        let ops: Box<dyn FsNodeOps> = Box::new(ops);
        Arc::new(FsNode {
            ops: OnceCell::from(ops),
            parent: None,
            name: FsString::new(),
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

    fn mkdir(self: &FsNodeHandle, name: FsString) -> Result<FsNodeHandle, Errno> {
        let node = self.get_or_create_empty_child(name);
        let exists = node.initialize(|name| self.ops().mkdir(name))?;
        if exists {
            return Err(EEXIST);
        }
        Ok(node)
    }

    fn lookup(self: &FsNodeHandle, name: &FsStr) -> Result<FsNodeHandle, Errno> {
        let node = self.get_or_create_empty_child(name.to_vec());
        node.initialize(|name| self.ops().lookup(name))?;
        Ok(node)
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
            parent: Some(Arc::clone(self)),
            name: name.clone(),
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

pub fn path_lookup(node: &FsNodeHandle, path: &FsStr) -> Result<FsNodeHandle, Errno> {
    // I'm a little disappointed in the number of refcount increments and decrements that happen
    // here.
    let mut node = Arc::clone(node);
    for component in path.split(|c| *c == b'/') {
        if component == b"." || component == b"" {
            // ignore
        } else if component == b".." {
            node = Arc::clone(node.parent());
        } else {
            node = node.lookup(path)?;
        }
    }
    Ok(node)
}

struct TmpfsDirectory;

impl FsNodeOps for TmpfsDirectory {
    fn mkdir(&self, _name: &FsStr) -> Result<Box<dyn FsNodeOps>, Errno> {
        Ok(Box::new(Self))
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_tmpfs() {
        let root = FsNode::new_root(TmpfsDirectory {});
        let usr = root.mkdir(b"usr".to_vec()).unwrap();
        let _etc = root.mkdir(b"etc".to_vec()).unwrap();
        let _usr_bin = usr.mkdir(b"bin".to_vec()).unwrap();
        let mut names = root.copy_child_names();
        names.sort();
        assert!(names.iter().eq([b"etc", b"usr"].iter()));
    }
}
