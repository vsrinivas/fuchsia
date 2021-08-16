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
    _ops: Box<dyn FileSystemOps>,

    /// Whether DirEntries added to this filesystem should be considered permanent, instead of a
    /// cache of the backing storage. An example is tmpfs: the DirEntry tree *is* the backing
    /// storage, as opposed to ext4, which uses the DirEntry tree as a cache and removes unused
    /// nodes from it.
    permanent_entries: bool,

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

    /// DirEntryHandle cache for the filesystem. Currently only used by filesystems that set the
    /// permanent_entries flag, to store every node and make sure it doesn't get freed without
    /// being explicitly unlinked.
    entries: Mutex<HashMap<usize, DirEntryHandle>>,
}

impl FileSystem {
    /// Create a new file system.
    pub fn new(
        ops: impl FileSystemOps + 'static,
        mut root_node: FsNode,
        root_inode_num: Option<ino_t>,
        permanent_entries: bool,
    ) -> FileSystemHandle {
        // TODO(tbodt): I would like to use Arc::new_cyclic
        let fs = Self::new_internal(ops, permanent_entries);
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
        Self::new_internal(ops, false)
    }

    fn new_internal(
        ops: impl FileSystemOps + 'static,
        permanent_entries: bool,
    ) -> FileSystemHandle {
        Arc::new(FileSystem {
            root: OnceCell::new(),
            next_inode: AtomicU64::new(0),
            _ops: Box::new(ops),
            permanent_entries,
            nodes: Mutex::new(HashMap::new()),
            entries: Mutex::new(HashMap::new()),
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
        if self.permanent_entries {
            self.entries.lock().insert(Arc::as_ptr(&entry) as usize, entry.clone());
        }
    }

    pub fn will_destroy_dir_entry(&self, entry: &DirEntryHandle) {
        if self.permanent_entries {
            self.entries.lock().remove(&(Arc::as_ptr(entry) as usize));
        }
    }
}

/// The filesystem-implementation-specific data for FileSystem.
pub trait FileSystemOps: Send + Sync {}

pub type FileSystemHandle = Arc<FileSystem>;
