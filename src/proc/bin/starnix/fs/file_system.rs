// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use once_cell::sync::OnceCell;
use parking_lot::Mutex;
use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Weak};

use super::*;
use crate::types::*;

/// A file system that can be mounted in a namespace.
pub struct FileSystem {
    root: OnceCell<DirEntryHandle>,
    next_inode: AtomicU64,
    ops: Box<dyn FileSystemOps>,

    /// The FsNode cache for this file system.
    ///
    /// When two directory entries are hard links to the same underlying inode,
    /// this cache lets us re-use the same FsNode object for both directory
    /// entries.
    ///
    /// Rather than calling FsNode::new directly, file systems should call
    /// FileSystem::get_or_create_node to see if the FsNode already exists in
    /// the cache.
    nodes: Mutex<HashMap<ino_t, Weak<FsNode>>>,
}

impl FileSystem {
    /// Create a new file system.
    pub fn new(
        ops: impl FileSystemOps + 'static,
        mut root_node: FsNode,
        root_inode_num: Option<ino_t>,
    ) -> FileSystemHandle {
        // TODO(tbodt): I would like to use Arc::new_cyclic
        let fs = Self::new_no_root(ops);
        root_node.set_fs(&fs);
        root_node.inode_num = root_inode_num.unwrap_or(fs.next_inode_num());
        let root_node = Arc::new(root_node);
        fs.nodes.lock().insert(root_node.inode_num, Arc::downgrade(&root_node));
        let root = DirEntry::new(root_node, None, FsString::new());
        if fs.root.set(root).is_err() {
            panic!("there's no way fs.root could have been set");
        }
        fs
    }

    /// Create a file system with no root directory.
    pub fn new_no_root(ops: impl FileSystemOps + 'static) -> FileSystemHandle {
        Arc::new(FileSystem {
            root: OnceCell::new(),
            next_inode: AtomicU64::new(0),
            ops: Box::new(ops),
            nodes: Mutex::new(HashMap::new()),
        })
    }

    /// The root directory entry of this file system.
    ///
    /// Panics if this file system does not have a root directory.
    pub fn root(&self) -> &DirEntryHandle {
        self.root.get().unwrap()
    }

    /// Get or create an FsNode for this file system.
    ///
    /// If inode_num is Some, then this function checks the node cache to
    /// determine whether this node is already open. If so, the function
    /// returns the existing FsNode. If not, the function calls the given
    /// create_fn function to create the FsNode.
    ///
    /// If inode_num is None, then this function assigns a new inode number
    /// and calls the given create_fn function to create the FsNode with the
    /// assigned number.
    ///
    /// Returns Err only if create_fn returns Err.
    pub fn get_or_create_node<F>(
        &self,
        inode_num: Option<ino_t>,
        create_fn: F,
    ) -> Result<FsNodeHandle, Errno>
    where
        F: FnOnce(ino_t) -> Result<FsNodeHandle, Errno>,
    {
        let inode_num = inode_num.unwrap_or_else(|| self.next_inode_num());
        let mut nodes = self.nodes.lock();
        match nodes.entry(inode_num) {
            Entry::Vacant(entry) => {
                let node = create_fn(inode_num)?;
                entry.insert(Arc::downgrade(&node));
                Ok(node)
            }
            Entry::Occupied(mut entry) => {
                if let Some(node) = entry.get().upgrade() {
                    return Ok(node);
                }
                let node = create_fn(inode_num)?;
                entry.insert(Arc::downgrade(&node));
                Ok(node)
            }
        }
    }

    pub fn create_node(self: &Arc<Self>, ops: Box<dyn FsNodeOps>, mode: FileMode) -> FsNodeHandle {
        let inode_num = self.next_inode_num();
        let node = FsNode::new(ops, self, inode_num, mode);
        self.nodes.lock().insert(node.inode_num, Arc::downgrade(&node));
        node
    }

    /// Remove the given FsNode from the node cache.
    ///
    /// Called from the Drop trait of FsNode.
    pub fn remove_node(&self, node: &mut FsNode) {
        let mut nodes = self.nodes.lock();
        if let Some(weak_node) = nodes.get(&node.inode_num) {
            if std::ptr::eq(weak_node.as_ptr(), node) {
                nodes.remove(&node.inode_num);
            }
        }
    }

    fn next_inode_num(&self) -> ino_t {
        self.next_inode.fetch_add(1, Ordering::Relaxed)
    }

    pub fn did_create_dir_entry(&self, entry: &DirEntryHandle) {
        self.ops.did_create_dir_entry(self, entry);
    }

    pub fn will_destroy_dir_entry(&self, entry: &DirEntryHandle) {
        self.ops.will_destroy_dir_entry(self, entry);
    }
}

/// The filesystem-implementation-specific data for FileSystem.
pub trait FileSystemOps: Send + Sync {
    fn did_create_dir_entry(&self, _fs: &FileSystem, _entry: &DirEntryHandle) {}
    fn will_destroy_dir_entry(&self, _fs: &FileSystem, _entry: &DirEntryHandle) {}
}

pub type FileSystemHandle = Arc<FileSystem>;
