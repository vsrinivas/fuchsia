// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use once_cell::sync::OnceCell;
use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Weak};

use super::*;
use crate::auth::FsCred;
use crate::lock::Mutex;
use crate::task::Kernel;
use crate::types::*;

/// A file system that can be mounted in a namespace.
pub struct FileSystem {
    root: OnceCell<DirEntryHandle>,
    next_inode: AtomicU64,
    ops: Box<dyn FileSystemOps>,

    /// The device ID of this filesystem. Returned in the st_dev field when stating an inode in
    /// this filesystem.
    pub dev_id: DeviceType,

    /// Whether DirEntries added to this filesystem should be considered permanent, instead of a
    /// cache of the backing storage. An example is tmpfs: the DirEntry tree *is* the backing
    /// storage, as opposed to ext4, which uses the DirEntry tree as a cache and removes unused
    /// nodes from it.
    pub permanent_entries: bool,

    /// A file-system global mutex to serialize rename operations.
    ///
    /// This mutex is useful because the invariants enforced during a rename
    /// operation involve many DirEntry objects. In the future, we might be
    /// able to remove this mutex, but we will need to think carefully about
    /// how rename operations can interleave.
    ///
    /// See DirEntry::rename.
    pub rename_mutex: Mutex<()>,

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

    /// Hack meant to stand in for the fs_use_trans selinux feature. If set, this value will be set
    /// as the selinux label on any newly created inodes in the filesystem.
    pub selinux_context: OnceCell<FsString>,
}

impl FileSystem {
    /// Create a new filesystem.
    pub fn new(kernel: &Kernel, ops: impl FileSystemOps) -> FileSystemHandle {
        Self::new_internal(kernel, ops, false)
    }

    /// Create a new filesystem with the permanent_entries flag set.
    pub fn new_with_permanent_entries(
        kernel: &Kernel,
        ops: impl FileSystemOps,
    ) -> FileSystemHandle {
        Self::new_internal(kernel, ops, true)
    }

    /// Create a new filesystem and call set_root in one step.
    pub fn new_with_root(
        kernel: &Kernel,
        ops: impl FileSystemOps,
        root_node: FsNode,
    ) -> FileSystemHandle {
        let fs = Self::new_with_permanent_entries(kernel, ops);
        fs.set_root_node(root_node);
        fs
    }

    pub fn set_root(self: &FileSystemHandle, root: impl FsNodeOps) {
        self.set_root_node(FsNode::new_root(root));
    }

    /// Set up the root of the filesystem. Must not be called more than once.
    pub fn set_root_node(self: &FileSystemHandle, mut root: FsNode) {
        if root.inode_num == 0 {
            root.inode_num = self.next_inode_num();
        }
        root.set_fs(self);
        let root_node = Arc::new(root);
        self.nodes.lock().insert(root_node.inode_num, Arc::downgrade(&root_node));
        let root = DirEntry::new(root_node, None, FsString::new());
        assert!(self.root.set(root).is_ok(), "FileSystem::set_root can't be called more than once");
    }

    fn new_internal(
        kernel: &Kernel,
        ops: impl FileSystemOps,
        permanent_entries: bool,
    ) -> FileSystemHandle {
        Arc::new(FileSystem {
            root: OnceCell::new(),
            next_inode: AtomicU64::new(1),
            ops: Box::new(ops),
            dev_id: kernel.device_registry.write().next_anonymous_dev_id(),
            permanent_entries,
            rename_mutex: Mutex::new(()),
            nodes: Mutex::new(HashMap::new()),
            entries: Mutex::new(HashMap::new()),
            selinux_context: OnceCell::new(),
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

    /// File systems that produce their own IDs for nodes should invoke this
    /// function. The ones who leave to this object to assign the IDs should
    /// call |create_node|.
    pub fn create_node_with_id(
        self: &Arc<Self>,
        ops: Box<dyn FsNodeOps>,
        id: ino_t,
        mode: FileMode,
        owner: FsCred,
    ) -> FsNodeHandle {
        if let Some(label) = self.selinux_context.get() {
            let _ = ops.set_xattr(b"security.selinux", label, XattrOp::Create);
        }
        let node = FsNode::new_uncached(ops, self, id, mode, owner);
        self.nodes.lock().insert(node.inode_num, Arc::downgrade(&node));
        node
    }

    pub fn create_node(
        self: &Arc<Self>,
        ops: Box<dyn FsNodeOps>,
        mode: FileMode,
        owner: FsCred,
    ) -> FsNodeHandle {
        let inode_num = self.next_inode_num();
        self.create_node_with_id(ops, inode_num, mode, owner)
    }

    pub fn create_node_with_ops(
        self: &Arc<Self>,
        ops: impl FsNodeOps,
        mode: FileMode,
        owner: FsCred,
    ) -> FsNodeHandle {
        self.create_node(Box::new(ops), mode, owner)
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

    pub fn next_inode_num(&self) -> ino_t {
        assert!(!self.ops.generate_node_ids());
        self.next_inode.fetch_add(1, Ordering::Relaxed)
    }

    /// Move |renamed| that is at |old_name| in |old_parent| to |new_name| in |new_parent|
    /// replacing |replaced|.
    /// If |replaced| exists and is a directory, this function must check that |renamed| is n
    /// directory and that |replaced| is empty.
    pub fn rename(
        &self,
        old_parent: &FsNodeHandle,
        old_name: &FsStr,
        new_parent: &FsNodeHandle,
        new_name: &FsStr,
        renamed: &FsNodeHandle,
        replaced: Option<&FsNodeHandle>,
    ) -> Result<(), Errno> {
        self.ops.rename(self, old_parent, old_name, new_parent, new_name, renamed, replaced)
    }

    /// Returns the `statfs` for this filesystem.
    ///
    /// Each `FileSystemOps` impl is expected to override this to return the specific statfs for
    /// the filesystem.
    ///
    /// Returns `ENOSYS` if the `FileSystemOps` don't implement `stat`.
    pub fn statfs(&self) -> Result<statfs, Errno> {
        self.ops.statfs(self)
    }

    pub fn did_create_dir_entry(&self, entry: &DirEntryHandle) {
        if self.permanent_entries {
            self.entries.lock().insert(Arc::as_ptr(entry) as usize, entry.clone());
        }
    }

    pub fn will_destroy_dir_entry(&self, entry: &DirEntryHandle) {
        if self.permanent_entries {
            self.entries.lock().remove(&(Arc::as_ptr(entry) as usize));
        }
    }
}

/// The filesystem-implementation-specific data for FileSystem.
pub trait FileSystemOps: Send + Sync + 'static {
    /// Return information about this filesystem.
    ///
    /// A typical implementation looks like this:
    /// ```
    /// Ok(statfs::default(FILE_SYSTEM_MAGIC))
    /// ```
    /// or, if the filesystem wants to customize fields:
    /// ```
    /// Ok(statfs {
    ///     f_blocks: self.blocks,
    ///     ..statfs::default(FILE_SYSTEM_MAGIC)
    /// })
    /// ```
    fn statfs(&self, _fs: &FileSystem) -> Result<statfs, Errno>;

    /// Whether this file system generates its own node IDs.
    fn generate_node_ids(&self) -> bool {
        false
    }

    /// Rename the given node.
    ///
    /// The node to be renamed is passed as "renamed". It currently has
    /// old_name in old_parent. After the rename operation, it should have
    /// new_name in new_parent.
    ///
    /// If new_parent already has a child named new_name, that node is passed as
    /// "replaced". In that case, both "renamed" and "replaced" will be
    /// directories and the rename operation should succeed only if "replaced"
    /// is empty. The VFS will check that there are no children of "replaced" in
    /// the DirEntry cache, but the implementation of this function is
    /// responsible for checking that there are no children of replaced that are
    /// known only to the file system implementation (e.g., present on-disk but
    /// not in the DirEntry cache).
    fn rename(
        &self,
        _fs: &FileSystem,
        _old_parent: &FsNodeHandle,
        _old_name: &FsStr,
        _new_parent: &FsNodeHandle,
        _new_name: &FsStr,
        _renamed: &FsNodeHandle,
        _replaced: Option<&FsNodeHandle>,
    ) -> Result<(), Errno> {
        error!(EROFS)
    }
}

pub type FileSystemHandle = Arc<FileSystem>;
