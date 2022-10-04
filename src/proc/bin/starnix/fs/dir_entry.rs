// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::btree_map::Entry;
use std::collections::BTreeMap;
use std::fmt;
use std::sync::{Arc, Weak};

use crate::auth::FsCred;
use crate::fs::socket::*;
use crate::fs::*;
use crate::lock::{RwLock, RwLockWriteGuard};
use crate::task::CurrentTask;
use crate::types::*;

struct DirEntryState {
    /// The parent DirEntry.
    ///
    /// The DirEntry tree has strong references from child-to-parent and weak
    /// references from parent-to-child. This design ensures that the parent
    /// chain is always populated in the cache, but some children might be
    /// missing from the cache.
    parent: Option<DirEntryHandle>,

    /// The name that this parent calls this child.
    ///
    /// This name might not be reflected in the full path in the namespace that
    /// contains this DirEntry. For example, this DirEntry might be the root of
    /// a chroot.
    ///
    /// Most callers that want to work with names for DirEntries should use the
    /// NamespaceNodes.
    local_name: FsString,

    /// The number of filesystem mounted on the directory entry.
    mount_count: u32,
}

/// An entry in a directory.
///
/// This structure assigns a name to an FsNode in a given file system. An
/// FsNode might have multiple directory entries, for example if there are more
/// than one hard link to the same FsNode. In those cases, each hard link will
/// have a different parent and a different local_name because each hard link
/// has its own DirEntry object.
///
/// A directory cannot have more than one hard link, which means there is a
/// single DirEntry for each Directory FsNode. That invariant lets us store the
/// children for a directory in the DirEntry rather than in the FsNode.
pub struct DirEntry {
    /// The FsNode referenced by this DirEntry.
    ///
    /// A given FsNode can be referenced by multiple DirEntry objects, for
    /// example if there are multiple hard links to a given FsNode.
    pub node: FsNodeHandle,

    /// The mutable state for this DirEntry.
    ///
    /// Leaf lock - do not acquire other locks while holding this one.
    state: RwLock<DirEntryState>,

    /// A partial cache of the children of this DirEntry.
    ///
    /// DirEntries are added to this cache when they are looked up and removed
    /// when they are no longer referenced.
    ///
    /// This is separated from the DirEntryState for lock ordering. rename needs to lock the source
    /// parent, the target parent, the source, and the target - four (4) DirEntries in total.
    /// Getting the ordering right on these is nearly impossible. However, we only need to lock the
    /// children map on the two parents and we don't need to lock the children map on the two
    /// children. So splitting the children out into its own lock resolves this.
    children: RwLock<DirEntryChildren>,
}
type DirEntryChildren = BTreeMap<FsString, Weak<DirEntry>>;

pub type DirEntryHandle = Arc<DirEntry>;

impl DirEntry {
    pub fn new(
        node: FsNodeHandle,
        parent: Option<DirEntryHandle>,
        local_name: FsString,
    ) -> DirEntryHandle {
        let result = Arc::new(DirEntry {
            node,
            state: RwLock::new(DirEntryState { parent, local_name, mount_count: 0 }),
            children: Default::default(),
        });
        #[cfg(any(test, debug_assertions))]
        {
            let _l1 = result.children.read();
            let _l2 = result.state.read();
        }
        result
    }

    /// Returns a new DirEntry for the given `node` without parent. The entry has no local name.
    pub fn new_unrooted(node: FsNodeHandle) -> DirEntryHandle {
        Self::new(node, None, FsString::new())
    }

    /// Returns a file handle to this entry, associated with an anonymous namespace.
    pub fn open_anonymous(self: &DirEntryHandle, flags: OpenFlags) -> Result<FileHandle, Errno> {
        Ok(FileObject::new(
            self.node.create_file_ops(flags)?,
            NamespaceNode::new_anonymous(self.clone()),
            flags,
        ))
    }

    fn lock_children<'a>(self: &'a DirEntryHandle) -> DirEntryLockedChildren<'a> {
        DirEntryLockedChildren { entry: self, children: self.children.write() }
    }

    /// Register that a filesystem is mounted on the directory.
    pub fn register_mount(&self) {
        self.state.write().mount_count += 1;
    }

    /// Unregister that a filesystem is mounted on the directory.
    pub fn unregister_mount(&self) {
        let mut state = self.state.write();
        assert!(state.mount_count > 0);
        state.mount_count -= 1;
    }

    /// The name that this node's parent calls this node.
    ///
    /// If this node is mounted in a namespace, the parent of this node in that
    /// namespace might have a different name for the point in the namespace at
    /// which this node is mounted.
    pub fn local_name(&self) -> FsString {
        self.state.read().local_name.clone()
    }

    /// The parent DirEntry object.
    ///
    /// Returns None if this DirEntry is the root of its file system.
    ///
    /// Be aware that the root of one file system might be mounted as a child
    /// in another file system. For that reason, consider walking the
    /// NamespaceNode tree (which understands mounts) rather than the DirEntry
    /// tree.
    pub fn parent(&self) -> Option<DirEntryHandle> {
        self.state.read().parent.clone()
    }

    /// The parent DirEntry object or this DirEntry if this entry is the root.
    ///
    /// Useful when traversing up the tree if you always want to find a parent
    /// (e.g., for "..").
    ///
    /// Be aware that the root of one file system might be mounted as a child
    /// in another file system. For that reason, consider walking the
    /// NamespaceNode tree (which understands mounts) rather than the DirEntry
    /// tree.
    pub fn parent_or_self(self: &DirEntryHandle) -> DirEntryHandle {
        self.state.read().parent.as_ref().unwrap_or(self).clone()
    }

    /// Whether the given name has special semantics as a directory entry.
    ///
    /// Specifically, whether the name is empty (which means "self"), dot
    /// (which also means "self"), or dot dot (which means "parent").
    pub fn is_reserved_name(name: &FsStr) -> bool {
        name.is_empty() || name == b"." || name == b".."
    }

    /// Look up a directory entry with the given name as direct child of this
    /// entry.
    pub fn component_lookup(
        self: &DirEntryHandle,
        current_task: &CurrentTask,
        name: &FsStr,
    ) -> Result<DirEntryHandle, Errno> {
        let (node, _) = self.get_or_create_child(name, || self.node.lookup(current_task, name))?;
        Ok(node)
    }

    /// Creates a new DirEntry
    ///
    /// The create_node_fn function is called to create the underlying FsNode
    /// for the DirEntry.
    ///
    /// If the entry already exists, create_node_fn is not called, and EEXIST is
    /// returned.
    pub fn create_entry<F>(
        self: &DirEntryHandle,
        name: &FsStr,
        create_node_fn: F,
    ) -> Result<DirEntryHandle, Errno>
    where
        F: FnOnce() -> Result<FsNodeHandle, Errno>,
    {
        if DirEntry::is_reserved_name(name) {
            return error!(EEXIST);
        }
        // TODO: Do we need to check name for embedded "/" or NUL characters?
        if name.len() > NAME_MAX as usize {
            return error!(ENAMETOOLONG);
        }
        let (entry, exists) = self.get_or_create_child(name, create_node_fn)?;
        if exists {
            return error!(EEXIST);
        }
        self.node.touch();
        entry.node.fs().did_create_dir_entry(&entry);
        Ok(entry)
    }

    /// Magically creates a node without asking the filesystem. All the other node creation
    /// functions go through the filesystem, such as create_node or create_symlink.
    pub fn add_node_ops(
        self: &DirEntryHandle,
        name: &FsStr,
        mode: FileMode,
        ops: impl FsNodeOps,
    ) -> Result<DirEntryHandle, Errno> {
        self.add_node_ops_dev(name, mode, DeviceType::NONE, ops)
    }

    pub fn add_node_ops_dev(
        self: &DirEntryHandle,
        name: &FsStr,
        mode: FileMode,
        dev: DeviceType,
        ops: impl FsNodeOps,
    ) -> Result<DirEntryHandle, Errno> {
        self.create_entry(name, || {
            let node = self.node.fs().create_node(Box::new(ops), mode, FsCred::root());
            {
                let mut info = node.info_write();
                info.rdev = dev;
            }
            Ok(node)
        })
    }

    // This is marked as test-only because it sets the owner/group to root instead of the current
    // user to save a bit of typing in tests, but this shouldn't happen silently in production.
    #[cfg(test)]
    pub fn create_dir(
        self: &DirEntryHandle,
        current_task: &CurrentTask,
        name: &FsStr,
    ) -> Result<DirEntryHandle, Errno> {
        // TODO: apply_umask
        self.create_entry(name, || {
            self.node.mkdir(current_task, name, mode!(IFDIR, 0o777), FsCred::root())
        })
    }

    /// Ask the filesystem to create a node. This is the equivalent of mknod. Works for any type of
    /// file other than a symlink.
    pub fn create_node(
        self: &DirEntryHandle,
        current_task: &CurrentTask,
        name: &FsStr,
        mode: FileMode,
        dev: DeviceType,
        owner: FsCred,
    ) -> Result<DirEntryHandle, Errno> {
        self.create_entry(name, || {
            if mode.is_dir() {
                self.node.mkdir(current_task, name, mode, owner)
            } else {
                let node = self.node.mknod(current_task, name, mode, dev, owner)?;
                if mode.is_sock() {
                    node.set_socket(Socket::new(
                        SocketDomain::Unix,
                        SocketType::Stream,
                        SocketProtocol::default(),
                    )?);
                }
                Ok(node)
            }
        })
    }

    pub fn bind_socket(
        self: &DirEntryHandle,
        current_task: &CurrentTask,
        name: &FsStr,
        socket: SocketHandle,
        socket_address: SocketAddress,
        mode: FileMode,
        owner: FsCred,
    ) -> Result<DirEntryHandle, Errno> {
        self.create_entry(name, || {
            let node = self.node.mknod(current_task, name, mode, DeviceType::NONE, owner)?;
            if let Some(unix_socket) = socket.downcast_socket::<UnixSocket>() {
                unix_socket.bind_socket_to_node(&socket, socket_address, &node)?;
            } else {
                return error!(ENOTSUP);
            }
            Ok(node)
        })
    }

    pub fn create_symlink(
        self: &DirEntryHandle,
        current_task: &CurrentTask,
        name: &FsStr,
        target: &FsStr,
        owner: FsCred,
    ) -> Result<DirEntryHandle, Errno> {
        self.create_entry(name, || self.node.create_symlink(current_task, name, target, owner))
    }

    pub fn link(
        self: &DirEntryHandle,
        current_task: &CurrentTask,
        name: &FsStr,
        child: &FsNodeHandle,
    ) -> Result<(), Errno> {
        if DirEntry::is_reserved_name(name) {
            return error!(EEXIST);
        }
        let (entry, exists) = self.get_or_create_child(name, || {
            self.node.link(current_task, name, child)?;
            Ok(child.clone())
        })?;
        if exists {
            return error!(EEXIST);
        }
        self.node.touch();
        entry.node.fs().did_create_dir_entry(&entry);
        Ok(())
    }

    pub fn unlink(
        self: &DirEntryHandle,
        current_task: &CurrentTask,
        name: &FsStr,
        kind: UnlinkKind,
    ) -> Result<(), Errno> {
        assert!(!DirEntry::is_reserved_name(name));
        let mut self_children = self.lock_children();

        let child = self_children.component_lookup(current_task, name)?;
        let child_children = child.children.read();

        if child.state.read().mount_count > 0 {
            return error!(EBUSY);
        }

        match kind {
            UnlinkKind::Directory => {
                if !child.node.is_dir() {
                    return error!(ENOTDIR);
                }
                // This check only covers whether the cache is non-empty.
                // We actually need to check whether the underlying directory is
                // empty by asking the node via remove below.
                if !child_children.is_empty() {
                    return error!(ENOTEMPTY);
                }
            }
            UnlinkKind::NonDirectory => {
                if child.node.is_dir() {
                    return error!(EISDIR);
                }
            }
        }

        self.node.unlink(current_task, name, &child.node)?;
        self_children.children.remove(name);

        std::mem::drop(child_children);
        // We drop our children lock before we drop the child so that we do
        // not trigger a deadlock in the Drop trait for FsNode, which attempts
        // to remove the FsNode from its parent's child list.
        std::mem::drop(self_children);

        self.node.fs().will_destroy_dir_entry(&child);

        std::mem::drop(child);
        Ok(())
    }

    /// Returns whether this entry is a descendant of |other|.
    pub fn is_descendant_of(self: &DirEntryHandle, other: &DirEntryHandle) -> bool {
        let mut current = self.clone();
        loop {
            if Arc::ptr_eq(&current, other) {
                // We found |other|.
                return true;
            }
            if let Some(next) = current.parent() {
                current = next;
            } else {
                // We reached the root of the file system.
                return false;
            }
        }
    }

    /// Rename the file with old_basename in old_parent to new_basename in
    /// new_parent.
    ///
    /// old_parent and new_parent must belong to the same file system.
    pub fn rename(
        current_task: &CurrentTask,
        old_parent_name: &NamespaceNode,
        old_basename: &FsStr,
        new_parent_name: &NamespaceNode,
        new_basename: &FsStr,
    ) -> Result<(), Errno> {
        // If either the old_basename or the new_basename is a reserved name
        // (e.g., "." or ".."), then we cannot do the rename.
        if DirEntry::is_reserved_name(old_basename) || DirEntry::is_reserved_name(new_basename) {
            return error!(EBUSY);
        }

        let old_parent = &old_parent_name.entry;
        let new_parent = &new_parent_name.entry;

        // If the names and parents are the same, then there's nothing to do
        // and we can report success.
        if Arc::ptr_eq(old_parent, new_parent) && old_basename == new_basename {
            return Ok(());
        }

        // We need to hold these DirEntryHandles until after we drop all the
        // locks so that we do not deadlock when we drop them.
        let (_renamed, maybe_replaced) = {
            // The mount_eq check in sys_renameat ensures that the nodes we're
            // touching are part of the same file system. It doesn't matter
            // where we grab the FileSystem reference from.
            let fs = old_parent.node.fs();

            // Before we take any locks, we need to take the rename mutex on
            // the file system. This lock ensures that no other rename
            // operations are happening in this file system while we're
            // analyzing this rename operation.
            //
            // For example, we grab writer locks on both old_parent and
            // new_parent. If there was another rename operation in flight with
            // old_parent and new_parent reversed, then we could deadlock while
            // trying to acquire these locks.
            let _lock = fs.rename_mutex.lock();

            // Compute the list of ancestors of new_parent to check whether new_parent is a
            // descendant of the renamed node. This must be computed before taking any lock to
            // prevent lock inversions.
            let mut new_parent_ancestor_list = Vec::<DirEntryHandle>::new();
            {
                let mut current = Some(new_parent.clone());
                while let Some(entry) = current {
                    current = entry.parent();
                    new_parent_ancestor_list.push(entry);
                }
            }

            // We cannot simply grab the locks on old_parent and new_parent
            // independently because old_parent and new_parent might be the
            // same directory entry. Instead, we use the RenameGuard helper to
            // grab the appropriate locks.
            let mut state = RenameGuard::lock(old_parent, new_parent);

            // Now that we know the old_parent child list cannot change, we
            // establish the DirEntry that we are going to try to rename.
            let renamed = state.old_parent().component_lookup(current_task, old_basename)?;

            // If new_parent is a descendant of renamed, the operation would
            // create a cycle. That's disallowed.
            if new_parent_ancestor_list.into_iter().any(|entry| Arc::ptr_eq(&entry, &renamed)) {
                return error!(EINVAL);
            }

            // Check whether the renamed entry is a mountpoint.
            // TODO: We should hold a read lock on the mount points for this
            //       namespace to prevent the child from becoming a mount point
            //       while this function is executing.
            if renamed.state.read().mount_count > 0 {
                return error!(EBUSY);
            }

            // We need to check if there is already a DirEntry with
            // new_basename in new_parent. If so, there are additional checks
            // we need to perform.
            let maybe_replaced =
                match state.new_parent().component_lookup(current_task, new_basename) {
                    Ok(replaced) => {
                        // Sayeth https://man7.org/linux/man-pages/man2/rename.2.html:
                        //
                        // "If oldpath and newpath are existing hard links referring to the
                        // same file, then rename() does nothing, and returns a success
                        // status."
                        if Arc::ptr_eq(&renamed.node, &replaced.node) {
                            return Ok(());
                        }

                        // Sayeth https://man7.org/linux/man-pages/man2/rename.2.html:
                        //
                        // "oldpath can specify a directory.  In this case, newpath must"
                        // either not exist, or it must specify an empty directory."
                        if replaced.node.is_dir() {
                            // Check whether the replaced entry is a mountpoint.
                            // TODO: We should hold a read lock on the mount points for this
                            //       namespace to prevent the child from becoming a mount point
                            //       while this function is executing.
                            if replaced.state.read().mount_count > 0 {
                                return error!(EBUSY);
                            }
                        } else if renamed.node.is_dir() {
                            return error!(ENOTDIR);
                        }
                        Some(replaced)
                    }
                    // It's fine for the lookup to fail to find a child.
                    Err(errno) if errno == ENOENT => None,
                    // However, other errors are fatal.
                    Err(e) => return Err(e),
                };

            // We've found all the errors that we know how to find. Ask the
            // file system to actually execute the rename operation. Once the
            // file system has executed the rename, we are no longer allowed to
            // fail because we will not be able to return the system to a
            // consistent state.
            fs.rename(
                &old_parent.node,
                old_basename,
                &new_parent.node,
                new_basename,
                &renamed.node,
                maybe_replaced.as_ref().map(|replaced| &replaced.node),
            )?;

            {
                // We need to update the parent and local name for the DirEntry
                // we are renaming to reflect its new parent and its new name.
                let mut renamed_state = renamed.state.write();
                renamed_state.parent = Some(new_parent.clone());
                renamed_state.local_name = new_basename.to_owned();
            }
            // Actually add the renamed child to the new_parent's child list.
            // This operation implicitly removes the replaced child (if any)
            // from the child list.
            state.new_parent().children.insert(new_basename.to_owned(), Arc::downgrade(&renamed));

            // Finally, remove the renamed child from the old_parent's child
            // list.
            state.old_parent().children.remove(old_basename);

            (renamed, maybe_replaced)
        };

        if let Some(replaced) = maybe_replaced {
            replaced.node.fs().will_destroy_dir_entry(&replaced);
        }

        Ok(())
    }

    pub fn get_children<F, T>(&self, callback: F) -> T
    where
        F: FnOnce(&DirEntryChildren) -> T,
    {
        let children = self.children.read();
        callback(&children)
    }

    /// Remove the child with the given name from the children cache.
    pub fn remove_child(&self, name: &FsStr) {
        let mut children = self.children.write();
        let child = children.get(name).and_then(Weak::upgrade);
        if let Some(child) = child {
            children.remove(name);
            std::mem::drop(children);
            self.node.fs().will_destroy_dir_entry(&child);
        }
    }

    fn get_or_create_child<F>(
        self: &DirEntryHandle,
        name: &FsStr,
        create_fn: F,
    ) -> Result<(DirEntryHandle, bool), Errno>
    where
        F: FnOnce() -> Result<FsNodeHandle, Errno>,
    {
        assert!(!DirEntry::is_reserved_name(name));
        // Only directories can have children.
        if !self.node.is_dir() {
            return error!(ENOTDIR);
        }
        // Check if the child is already in children. In that case, we can
        // simply return the child and we do not need to call init_fn.
        let mut children = self.lock_children();
        if let Some(child) = children.children.get(name).and_then(Weak::upgrade) {
            return Ok((child, true));
        }

        children.get_or_create_child(name, create_fn)
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
            .filter_map(|child| Weak::upgrade(child).map(|c| c.local_name()))
            .collect()
    }

    fn internal_remove_child(&self, child: &mut DirEntry) {
        let local_name = child.local_name();
        let mut children = self.children.write();
        if let Some(weak_child) = children.get(&local_name) {
            // If this entry is occupied, we need to check whether child is
            // the current occupant. If so, we should remove the entry
            // because the child no longer exists.
            if std::ptr::eq(weak_child.as_ptr(), child) {
                children.remove(&local_name);
            }
        }
    }
}

struct DirEntryLockedChildren<'a> {
    entry: &'a DirEntryHandle,
    children: RwLockWriteGuard<'a, DirEntryChildren>,
}

impl<'a> DirEntryLockedChildren<'a> {
    fn component_lookup(
        &mut self,
        current_task: &CurrentTask,
        name: &FsStr,
    ) -> Result<DirEntryHandle, Errno> {
        assert!(!DirEntry::is_reserved_name(name));
        let (node, _) =
            self.get_or_create_child(name, || self.entry.node.lookup(current_task, name))?;
        Ok(node)
    }

    fn get_or_create_child<F>(
        &mut self,
        name: &FsStr,
        create_fn: F,
    ) -> Result<(DirEntryHandle, bool), Errno>
    where
        F: FnOnce() -> Result<FsNodeHandle, Errno>,
    {
        let create_child = || {
            let node = create_fn()?;
            assert!(
                node.info().mode & FileMode::IFMT != FileMode::EMPTY,
                "FsNode initialization did not populate the FileMode in FsNodeInfo."
            );
            let entry = DirEntry::new(node, Some(self.entry.clone()), name.to_vec());
            #[cfg(any(test, debug_assertions))]
            {
                // Take the lock on child while holding the one on the parent to ensure any wrong ordering
                // will trigger the tracing-mutex at the right call site.
                let _l1 = entry.state.read();
            }
            Ok(entry)
        };

        match self.children.entry(name.to_vec()) {
            Entry::Vacant(entry) => {
                let child = create_child()?;
                entry.insert(Arc::downgrade(&child));
                Ok((child, false))
            }
            Entry::Occupied(mut entry) => {
                // It's possible that the upgrade will succeed this time around
                // because we dropped the read lock before acquiring the write
                // lock. Another thread might have populated this entry while
                // we were not holding any locks.
                if let Some(child) = Weak::upgrade(entry.get()) {
                    return Ok((child, true));
                }
                let child = create_child()?;
                entry.insert(Arc::downgrade(&child));
                Ok((child, false))
            }
        }
    }
}

impl fmt::Debug for DirEntry {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let mut parents = vec![];
        let mut maybe_parent = self.parent();
        while let Some(parent) = maybe_parent {
            parents.push(String::from_utf8_lossy(&parent.local_name()).into_owned());
            maybe_parent = parent.parent();
        }
        let mut builder = f.debug_struct("DirEntry");
        builder.field("id", &(self as *const DirEntry));
        builder.field("local_name", &String::from_utf8_lossy(&self.local_name()));
        if !parents.is_empty() {
            builder.field("parents", &parents);
        }
        builder.finish()
    }
}

struct RenameGuard<'a> {
    old_parent_guard: DirEntryLockedChildren<'a>,
    new_parent_guard: Option<DirEntryLockedChildren<'a>>,
}

impl<'a> RenameGuard<'a> {
    fn lock(old_parent: &'a DirEntryHandle, new_parent: &'a DirEntryHandle) -> Self {
        if Arc::ptr_eq(old_parent, new_parent) {
            Self { old_parent_guard: old_parent.lock_children(), new_parent_guard: None }
        } else {
            // Following gVisor, these locks are taken in ancestor-to-descendant order.
            // Moreover, if the node are not comparable, they are taken from smallest inode to
            // biggest.
            if new_parent.is_descendant_of(old_parent)
                || (!old_parent.is_descendant_of(new_parent)
                    && old_parent.node.inode_num < new_parent.node.inode_num)
            {
                let old_parent_guard = old_parent.lock_children();
                let new_parent_guard = new_parent.lock_children();
                Self { old_parent_guard, new_parent_guard: Some(new_parent_guard) }
            } else {
                let new_parent_guard = new_parent.lock_children();
                let old_parent_guard = old_parent.lock_children();
                Self { old_parent_guard, new_parent_guard: Some(new_parent_guard) }
            }
        }
    }

    fn old_parent(&mut self) -> &mut DirEntryLockedChildren<'a> {
        &mut self.old_parent_guard
    }

    fn new_parent(&mut self) -> &mut DirEntryLockedChildren<'a> {
        if let Some(new_guard) = self.new_parent_guard.as_mut() {
            new_guard
        } else {
            &mut self.old_parent_guard
        }
    }
}

/// The Drop trait for DirEntry removes the entry from the child list of the
/// parent entry, which means we cannot drop DirEntry objects while holding a
/// lock on the parent's child list.
impl Drop for DirEntry {
    fn drop(&mut self) {
        let maybe_parent = self.state.write().parent.take();
        if let Some(parent) = maybe_parent {
            parent.internal_remove_child(self);
        }
    }
}
