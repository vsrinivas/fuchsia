// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use parking_lot::{RwLock, RwLockReadGuard};
use std::sync::{Arc, Weak};

pub type FsString = Vec<u8>;
pub type FsStr = [u8];

pub struct FsNode {
    ops: Box<dyn FilesystemNodeOps>,
    parent: Option<Arc<FsNode>>,
    name: FsString,
    state: RwLock<FsNodeState>,
}
struct FsNodeState {
    // TODO(tbodt): replace with something with O(1) deletion
    children: Vec<Weak<FsNode>>,
}

pub trait FilesystemNodeOps {
    fn add_child(&self, name: &FsStr) -> Box<dyn FilesystemNodeOps>;
}

impl FsNode {
    fn new_root<T: FilesystemNodeOps + 'static>(ops: T) -> Arc<FsNode> {
        Arc::new(FsNode {
            ops: Box::new(ops),
            parent: None,
            name: FsString::new(),
            state: RwLock::new(FsNodeState { children: Vec::new() }),
        })
    }
    fn name(&self) -> &FsStr {
        &self.name
    }
    fn read_lock(&self) -> RwLockReadGuard<'_, FsNodeState> {
        self.state.read()
    }

    fn mkdir(self: &Arc<Self>, name: FsString) -> Arc<FsNode> {
        let node = Arc::new(FsNode {
            ops: self.ops.add_child(&name),
            parent: Some(Arc::clone(self)),
            name,
            state: RwLock::new(FsNodeState { children: Vec::new() }),
        });
        self.state.write().children.push(Arc::downgrade(&node));
        node
    }
}

impl FsNodeState {
    fn iter_children<'a>(&'a self) -> impl Iterator<Item = Arc<FsNode>> + 'a {
        // The Drop impl guarantees the child pointer will never be invalid.
        self.children.iter().map(|child| child.upgrade().expect("invalid child pointer"))
    }
}

impl Drop for FsNode {
    fn drop(&mut self) {
        // Remove ourself from the parent's list of children
        // possible deadlock?!
        if let Some(ref parent) = self.parent {
            let parent_children = &mut parent.state.write().children;
            let pos = parent_children
                .iter()
                .position(|c| c.as_ptr() == self)
                .expect("a node should always be in its parent's list of children!");
            parent_children.remove(pos);
        }
    }
}

struct TmpfsDirectory;

impl FilesystemNodeOps for TmpfsDirectory {
    fn add_child(&self, _name: &FsStr) -> Box<dyn FilesystemNodeOps> {
        Box::new(Self)
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_tmpfs() {
        let root = FsNode::new_root(TmpfsDirectory {});
        let usr = root.mkdir(b"usr".to_vec());
        let _etc = root.mkdir(b"etc".to_vec());
        let _usr_bin = usr.mkdir(b"bin".to_vec());
        for (a, b) in root.read_lock().iter_children().zip([b"usr", b"etc"].iter()) {
            assert!(a.name() == *b);
        }
    }
}
