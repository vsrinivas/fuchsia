// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use parking_lot::{RwLock, RwLockWriteGuard};
use std::collections::btree_map::Entry;
use std::collections::BTreeMap;
use std::sync::{Arc, Weak};

use crate::errno;
use crate::error;
use crate::fs::socket::*;
use crate::fs::*;
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

    /// A partial cache of the children of this DirEntry.
    ///
    /// DirEntries are added to this cache when they are looked up and removed
    /// when they are no longer referenced.
    children: BTreeMap<FsString, Weak<DirEntry>>,
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
    state: RwLock<DirEntryState>,
}

pub type DirEntryHandle = Arc<DirEntry>;

impl DirEntry {
    pub fn new(
        node: FsNodeHandle,
        parent: Option<DirEntryHandle>,
        local_name: FsString,
    ) -> DirEntryHandle {
        Arc::new(DirEntry {
            node,
            state: RwLock::new(DirEntryState { parent, local_name, children: BTreeMap::new() }),
        })
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
    pub fn parent(self: &DirEntryHandle) -> Option<DirEntryHandle> {
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
    pub fn component_lookup(self: &DirEntryHandle, name: &FsStr) -> Result<DirEntryHandle, Errno> {
        let (node, _) = self.get_or_create_child(name, || self.node.lookup(name))?;
        Ok(node)
    }

    /// Creates a new DirEntry
    ///
    /// The create_node_fn function is called to create the underlying FsNode
    /// for the DirEntry.
    ///
    /// If the entry already exists, create_node_fn is not called, and EEXIST is
    /// returned.
    fn create_entry<F>(
        self: &DirEntryHandle,
        name: &FsStr,
        mode: FileMode,
        dev: DeviceType,
        create_node_fn: F,
    ) -> Result<DirEntryHandle, Errno>
    where
        F: FnOnce() -> Result<FsNodeHandle, Errno>,
    {
        assert!(mode & FileMode::IFMT != FileMode::EMPTY, "mknod called without node type.");
        if DirEntry::is_reserved_name(name) {
            return error!(EEXIST);
        }
        // TODO: Do we need to check name for embedded "/" or NUL characters?
        if name.len() > NAME_MAX as usize {
            return error!(ENAMETOOLONG);
        }
        let (entry, exists) = self.get_or_create_child(name, || {
            let node = create_node_fn()?;
            let mut info = node.info_write();
            info.mode = mode;
            if mode.is_blk() || mode.is_chr() {
                info.rdev = dev;
            }
            std::mem::drop(info);
            Ok(node)
        })?;
        if exists {
            return error!(EEXIST);
        }
        self.node.touch();
        entry.node.fs().did_create_dir_entry(&entry);
        Ok(entry)
    }

    #[cfg(test)]
    pub fn create_dir(self: &DirEntryHandle, name: &FsStr) -> Result<DirEntryHandle, Errno> {
        // TODO: apply_umask
        self.create_entry(name, FileMode::IFDIR | FileMode::ALLOW_ALL, DeviceType::NONE, || {
            self.node.mkdir(name)
        })
    }

    pub fn create_node(
        self: &DirEntryHandle,
        name: &FsStr,
        mode: FileMode,
        dev: DeviceType,
    ) -> Result<DirEntryHandle, Errno> {
        self.create_entry(name, mode, dev, || {
            if mode.is_dir() {
                self.node.mkdir(name)
            } else {
                let node = self.node.mknod(name, mode)?;
                if mode.is_sock() {
                    node.set_socket(Socket::new(SocketDomain::Unix));
                }
                Ok(node)
            }
        })
    }

    pub fn bind_socket(
        self: &DirEntryHandle,
        name: &FsStr,
        socket: SocketHandle,
        socket_address: SocketAddress,
        mode: FileMode,
    ) -> Result<DirEntryHandle, Errno> {
        self.create_entry(name, mode, DeviceType::NONE, || {
            let mut locked_socket = socket.lock();
            let node = self.node.mknod(name, mode)?;
            locked_socket.bind(socket_address)?;
            node.set_socket(socket.clone());
            Ok(node)
        })
    }

    pub fn add_node_ops(
        self: &DirEntryHandle,
        name: &FsStr,
        mode: FileMode,
        ops: impl FsNodeOps + 'static,
    ) -> Result<DirEntryHandle, Errno> {
        self.create_entry(name, mode, DeviceType::NONE, || {
            Ok(self.node.fs().create_node(Box::new(ops), mode))
        })
    }

    pub fn create_symlink(
        self: &DirEntryHandle,
        name: &FsStr,
        target: &FsStr,
    ) -> Result<DirEntryHandle, Errno> {
        self.create_entry(name, FileMode::IFLNK | FileMode::ALLOW_ALL, DeviceType::NONE, || {
            self.node.create_symlink(name, target)
        })
    }

    pub fn link(self: &DirEntryHandle, name: &FsStr, child: &FsNodeHandle) -> Result<(), Errno> {
        if DirEntry::is_reserved_name(name) {
            return error!(EEXIST);
        }
        let (entry, exists) = self.get_or_create_child(name, || {
            self.node.link(name, child)?;
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
        parent_name: &NamespaceNode,
        name: &FsStr,
        kind: UnlinkKind,
    ) -> Result<(), Errno> {
        assert!(!DirEntry::is_reserved_name(name));
        let parent = &parent_name.entry;
        let mut parent_state = parent.state.write();

        let child = parent.component_lookup_locked(&mut parent_state, name)?;

        // TODO: We should hold a read lock on the mount points for this
        //       namespace to prevent the child from becoming a mount point
        //       while this function is executing.
        if parent_name.child_is_mountpoint(&child) {
            return error!(EBUSY);
        }

        let child_state = child.state.read();

        match kind {
            UnlinkKind::Directory => {
                if !child.node.is_dir() {
                    return error!(ENOTDIR);
                }
                // This check only covers whether the cache is non-empty.
                // We actually need to check whether the underlying directory is
                // empty by asking the node via remove below.
                if !child_state.children.is_empty() {
                    return error!(ENOTEMPTY);
                }
            }
            UnlinkKind::NonDirectory => {
                if child.node.is_dir() {
                    return error!(EISDIR);
                }
            }
        }

        parent.node.unlink(name, &child.node)?;
        parent_state.children.remove(name);

        std::mem::drop(child_state);
        // We drop the state lock before we drop the child so that we do
        // not trigger a deadlock in the Drop trait for FsNode, which attempts
        // to remove the FsNode from its parent's child list.
        std::mem::drop(parent_state);

        parent.node.fs().will_destroy_dir_entry(&child);

        std::mem::drop(child);
        Ok(())
    }

    /// Rename the file with old_basename in old_parent to new_basename in
    /// new_parent.
    ///
    /// old_parent and new_parent must belong to the same file system.
    pub fn rename(
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

            // We cannot simply grab the locks on old_parent and new_parent
            // independently because old_parent and new_parent might be the
            // same directory entry. Instead, we use the RenameGuard helper to
            // grab the appropriate locks.
            let mut state = RenameGuard::lock(old_parent, new_parent);

            // Now that we know the old_parent child list cannot change, we
            // establish the DirEntry that we are going to try to rename.
            let renamed = old_parent.component_lookup_locked(state.old_parent(), old_basename)?;

            // Check whether the renamed entry is a mountpoint.
            // TODO: We should hold a read lock on the mount points for this
            //       namespace to prevent the child from becoming a mount point
            //       while this function is executing.
            if old_parent_name.child_is_mountpoint(&renamed) {
                return error!(EBUSY);
            }

            // This specialized function ensure that we do not deadlock while
            // walking the parent chain of the given entry. We need to check
            // step in case we encounter old_parent or new_parent in the chain.
            let mut is_descendant_of = |entry: &DirEntryHandle, other: &DirEntryHandle| {
                let mut current = entry.clone();
                loop {
                    if Arc::ptr_eq(&current, &other) {
                        // We found |other|.
                        return true;
                    }
                    // We cannot use DirEntry::parent because that might take one of
                    // the locks we already hold on DirEntryState.
                    let maybe_parent = if Arc::ptr_eq(&current, old_parent) {
                        state.old_parent().parent.clone()
                    } else if Arc::ptr_eq(&current, new_parent) {
                        state.new_parent().parent.clone()
                    } else {
                        current.parent()
                    };
                    if let Some(next) = maybe_parent {
                        current = next;
                    } else {
                        // We reached the root of the file system.
                        return false;
                    }
                }
            };

            // If new_parent is a descendant of renamed, the operation would
            // create a cycle. That's disallowed.
            if is_descendant_of(&new_parent, &renamed) {
                return error!(EINVAL);
            }

            // We need to check if there is already a DirEntry with
            // new_basename in new_parent. If so, there are additional checks
            // we need to perform.
            let maybe_replaced =
                match new_parent.component_lookup_locked(state.new_parent(), new_basename) {
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
                            if !renamed.node.is_dir() {
                                return error!(EISDIR);
                            }

                            // We are not allowed to replace the old_parent. This check is a
                            // special case of not being allowed to replace a non-empty
                            // directory, but we perform the check separately to avoid
                            // deadlocks while trying to acquire the state lock for the
                            // replaced entry.
                            if Arc::ptr_eq(&old_parent, &replaced) {
                                return error!(ENOTEMPTY);
                            }
                            // TODO: This check only covers whether the cache is non-empty.
                            // We actually need to check whether the underlying directory is
                            // empty by asking the node. (ENOTEMPTY)
                            let replaced_state = replaced.state.write();
                            if !replaced_state.children.is_empty() {
                                return error!(ENOTEMPTY);
                            }

                            // Check whether the replaced entry is a mountpoint.
                            // TODO: We should hold a read lock on the mount points for this
                            //       namespace to prevent the child from becoming a mount point
                            //       while this function is executing.
                            if new_parent_name.child_is_mountpoint(&replaced) {
                                return error!(EBUSY);
                            }
                        } else if renamed.node.is_dir() {
                            return error!(ENOTDIR);
                        }
                        Some(replaced)
                    }
                    // It's fine for the lookup to fail to find a child.
                    Err(errno) if errno == errno!(ENOENT) => None,
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

    fn component_lookup_locked(
        self: &DirEntryHandle,
        state: &mut DirEntryState,
        name: &FsStr,
    ) -> Result<DirEntryHandle, Errno> {
        assert!(!DirEntry::is_reserved_name(name));
        let (node, _) = self.get_or_create_child_locked(state, name, || self.node.lookup(name))?;
        Ok(node)
    }

    pub fn get_children<F, T>(&self, callback: F) -> T
    where
        F: FnOnce(&BTreeMap<FsString, Weak<DirEntry>>) -> T,
    {
        let state = self.state.read();
        callback(&state.children)
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
        let state = self.state.read();
        if let Some(child) = state.children.get(name).and_then(Weak::upgrade) {
            return Ok((child, true));
        }
        std::mem::drop(state);

        let mut state = self.state.write();
        self.get_or_create_child_locked(&mut state, name, create_fn)
    }

    fn get_or_create_child_locked<F>(
        self: &DirEntryHandle,
        state: &mut DirEntryState,
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
            Ok(DirEntry::new(node, Some(self.clone()), name.to_vec()))
        };

        match state.children.entry(name.to_vec()) {
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

    // This function is only useful for tests and has some oddities.
    //
    // For example, not all the children might have been looked up yet, which
    // means the returned vector could be missing some names.
    //
    // Also, the vector might have "extra" names that are in the process of
    // being looked up. If the lookup fails, they'll be removed.
    #[cfg(test)]
    pub fn copy_child_names(&self) -> Vec<FsString> {
        self.state
            .read()
            .children
            .values()
            .filter_map(|child| Weak::upgrade(child).map(|c| c.local_name()))
            .collect()
    }

    fn internal_remove_child(&self, child: &mut DirEntry) {
        let local_name = child.local_name();
        let mut state = self.state.write();
        if let Some(weak_child) = state.children.get(&local_name) {
            // If this entry is occupied, we need to check whether child is
            // the current occupant. If so, we should remove the entry
            // because the child no longer exists.
            if std::ptr::eq(weak_child.as_ptr(), child) {
                state.children.remove(&local_name);
            }
        }
    }
}

struct RenameGuard<'a, 'b> {
    old_parent_guard: RwLockWriteGuard<'a, DirEntryState>,
    new_parent_guard: Option<RwLockWriteGuard<'b, DirEntryState>>,
}

impl<'a, 'b> RenameGuard<'a, 'b> {
    fn lock(old_parent: &'a DirEntryHandle, new_parent: &'b DirEntryHandle) -> Self {
        if Arc::ptr_eq(&old_parent, &new_parent) {
            Self { old_parent_guard: old_parent.state.write(), new_parent_guard: None }
        } else {
            // TODO: gVisor takes these locks in ancestor-to-descendant
            // order. Do we need to do that as well?
            Self {
                old_parent_guard: old_parent.state.write(),
                new_parent_guard: Some(new_parent.state.write()),
            }
        }
    }

    fn old_parent(&mut self) -> &mut DirEntryState {
        &mut self.old_parent_guard
    }

    fn new_parent(&mut self) -> &mut DirEntryState {
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
