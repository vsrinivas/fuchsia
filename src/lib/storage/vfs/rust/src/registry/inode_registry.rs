// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A simple implementation of the [`InodeRegistry`] trait.

use super::{InodeRegistry, InodeRegistryClient};

use {
    fidl_fuchsia_io::INO_UNKNOWN,
    std::collections::hash_map::HashMap,
    std::sync::{Arc, Mutex, Weak},
};

pub struct Simple {
    inner: Mutex<Inner>,
}

struct Inner {
    /// Stores inode ids for directory entries.
    ///
    /// `usize` is actually `*const dyn InodeRegistryClient`, but as pointers are `!Send` and
    /// `!Sync`, we store it as `usize` - never need to dereference it.
    ///
    /// `Weak` is necessary to make sure that the pointer (stored as `usize`) is valid.  Even
    /// though we do not dereference the pointer, it is proper to makes sure that another entry is
    /// not reallocated at the same address.  While at least one instance of `Weak` is alive,
    /// memory allocated for the contained object is not freed.
    ///
    /// Ideally we would want
    ///
    ///     HashMap<Weak<dyn TokenRegistryClient>, Handle>
    ///
    /// but `Weak` does not implement `Hash` and does not provide access to the address of the
    /// stored data.  See [`token_registry::Inner::container_to_token`] comment for details.
    ///
    assigned: HashMap<usize, (u64, Weak<dyn InodeRegistryClient>)>,
    next_inode: u64,
}

/// `*const InodeRegistryClient` is `!Send`, as all pointers are `!Send` by default.  `Inner` only
/// uses pointers to do bookkeeping, we never actually dereference them.
unsafe impl Send for Inner {}

impl Simple {
    pub fn new() -> Arc<Simple> {
        Arc::new(Simple { inner: Mutex::new(Inner { assigned: HashMap::new(), next_inode: 0 }) })
    }
}

impl InodeRegistry for Simple {
    fn get_inode(&self, node: Arc<dyn InodeRegistryClient>) -> u64 {
        let node_id = node.as_ref() as *const dyn InodeRegistryClient as *const usize as usize;

        let mut this = if let Ok(this) = self.inner.lock() {
            this
        } else {
            debug_assert!(false, "Another thread has panicked while holding the `inner` lock");
            return INO_UNKNOWN;
        };

        match this.assigned.get(&node_id) {
            Some((inode, _node)) => *inode,
            None => {
                let inode = this.next_inode;
                this.next_inode += 1;

                this.assigned.insert(node_id, (inode, Arc::downgrade(&node)));

                inode
            }
        }
    }

    fn unregister(&self, node: Arc<dyn InodeRegistryClient>) {
        let node_id = node.as_ref() as *const dyn InodeRegistryClient as *const usize as usize;

        let mut this = if let Ok(this) = self.inner.lock() {
            this
        } else {
            debug_assert!(false, "Another thread has panicked while holding the `inner` lock");
            return;
        };

        match this.assigned.remove(&node_id) {
            Some((_inode, _node)) => (),
            None => {
                debug_assert!(false, "`unregister()` has been already called for this node");
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::Simple;

    use self::mocks::MockFile;

    use crate::registry::InodeRegistry;

    #[test]
    fn get_inode_twice() {
        let file = MockFile::new();
        let registry = Simple::new();

        let inode1 = registry.get_inode(file.clone());
        let inode2 = registry.get_inode(file.clone());
        assert_eq!(inode1, inode2);

        registry.unregister(file);
    }

    #[test]
    fn two_files() {
        let file1 = MockFile::new();
        let file2 = MockFile::new();
        let registry = Simple::new();

        let inode1 = registry.get_inode(file1.clone());
        let inode2 = registry.get_inode(file2.clone());
        assert_ne!(inode1, inode2);

        assert_eq!(inode1, registry.get_inode(file1.clone()));
        assert_eq!(inode2, registry.get_inode(file2.clone()));

        registry.unregister(file1);
        registry.unregister(file2);
    }

    mod mocks {
        use crate::{
            directory::entry::{DirectoryEntry, EntryInfo},
            execution_scope::ExecutionScope,
            path::Path,
        };

        use {
            fidl::endpoints::ServerEnd,
            fidl_fuchsia_io::{NodeMarker, DIRENT_TYPE_FILE, INO_UNKNOWN},
            std::sync::Arc,
        };

        pub(super) struct MockFile {}

        impl MockFile {
            pub(super) fn new() -> Arc<Self> {
                Arc::new(Self {})
            }
        }

        impl DirectoryEntry for MockFile {
            fn open(
                self: Arc<Self>,
                _scope: ExecutionScope,
                _flags: u32,
                _mode: u32,
                path: Path,
                _server_end: ServerEnd<NodeMarker>,
            ) {
                assert!(path.is_empty());
            }

            fn entry_info(&self) -> EntryInfo {
                EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)
            }

            fn can_hardlink(&self) -> bool {
                true
            }
        }
    }
}
