// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::fmt;
use std::hash::{Hash, Hasher};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Weak};

use once_cell::sync::OnceCell;
use parking_lot::RwLock;

use super::{
    FileHandle, FileObject, FsContext, FsNode, FsNodeHandle, FsNodeOps, FsStr, FsString, UnlinkKind,
};
use crate::types::*;

/// A file system that can be mounted in a namespace.
pub struct FileSystem {
    root: OnceCell<FsNodeHandle>,
    next_inode: AtomicU64,
    ops: Box<dyn FileSystemOps>,
}

impl FileSystem {
    pub fn new(
        ops: impl FileSystemOps + 'static,
        root_ops: impl FsNodeOps + 'static,
    ) -> FileSystemHandle {
        // TODO(tbodt): I would like to use Arc::new_cyclic
        let fs = Self::new_no_root(ops);
        let root = FsNode::new_root(root_ops, &fs);
        if fs.root.set(root).is_err() {
            panic!("there's no way fs.root could have been set");
        }
        fs
    }

    pub fn new_no_root(ops: impl FileSystemOps + 'static) -> FileSystemHandle {
        Arc::new(FileSystem {
            root: OnceCell::new(),
            next_inode: AtomicU64::new(0),
            ops: Box::new(ops),
        })
    }

    pub fn root(&self) -> &FsNodeHandle {
        self.root.get().unwrap()
    }

    pub fn next_inode_num(&self) -> ino_t {
        self.next_inode.fetch_add(1, Ordering::Relaxed)
    }

    pub fn did_create_node(&self, node: &FsNodeHandle) {
        self.ops.did_create_node(self, node);
    }

    pub fn will_destroy_node(&self, node: &FsNodeHandle) {
        self.ops.will_destroy_node(self, node);
    }
}

/// The filesystem-implementation-specific data for FileSystem.
pub trait FileSystemOps: Send + Sync {
    fn did_create_node(&self, _fs: &FileSystem, _node: &FsNodeHandle) {}
    fn will_destroy_node(&self, _fs: &FileSystem, _node: &FsNodeHandle) {}
}

pub type FileSystemHandle = Arc<FileSystem>;

/// A mount namespace.
///
/// The namespace records at which entries filesystems are mounted.
pub struct Namespace {
    root_mount: OnceCell<MountHandle>,
    mount_points: RwLock<HashMap<NamespaceNode, MountHandle>>,
}

impl Namespace {
    pub fn new(fs: FileSystemHandle) -> Arc<Namespace> {
        // TODO(tbodt): We can avoid this OnceCell thing by using Arc::new_cyclic, but that's
        // unstable.
        let namespace = Arc::new(Self {
            root_mount: OnceCell::new(),
            mount_points: RwLock::new(HashMap::new()),
        });
        let root = fs.root().clone();
        if namespace
            .root_mount
            .set(Arc::new(Mount {
                namespace: Arc::downgrade(&namespace),
                mountpoint: None,
                _fs: fs,
                root,
            }))
            .is_err()
        {
            panic!("there's no way namespace.root_mount could have been set");
        }
        namespace
    }
    pub fn root(&self) -> NamespaceNode {
        self.root_mount.get().unwrap().root()
    }
}

/// An instance of a filesystem mounted in a namespace.
///
/// At a mount, path traversal switches from one filesystem to another.
/// The client sees a composed directory structure that glues together the
/// directories from the underlying FsNodes from those filesystems.
struct Mount {
    namespace: Weak<Namespace>,
    mountpoint: Option<(Weak<Mount>, FsNodeHandle)>,
    root: FsNodeHandle,
    _fs: FileSystemHandle,
}
type MountHandle = Arc<Mount>;

impl Mount {
    pub fn root(self: &MountHandle) -> NamespaceNode {
        NamespaceNode { mount: Some(Arc::clone(self)), node: Arc::clone(&self.root) }
    }

    fn mountpoint(&self) -> Option<NamespaceNode> {
        let (ref mount, ref node) = &self.mountpoint.as_ref()?;
        Some(NamespaceNode { mount: Some(mount.upgrade()?), node: node.clone() })
    }
}

/// The `SymlinkMode` enum encodes how symlinks are followed during path traversal.
#[derive(PartialEq, Eq, Copy, Clone, Debug)]

/// A counter representing the number of remaining symlink traversals. Path resolution uses
/// this to determine whether or not symlinks can be followed.
pub enum SymlinkMode {
    Follow(u8),
    NoFollow,
}

/// The maximum number of symlink traversals that can be made during path resolution.
const MAX_SYMLINK_FOLLOWS: u8 = 40;

impl SymlinkMode {
    /// Returns a `SymlinkMode::Follow` with a count that is set to the maximum number of
    /// symlinks that can be followed during path traversal.
    pub fn max_follow() -> SymlinkMode {
        SymlinkMode::Follow(MAX_SYMLINK_FOLLOWS)
    }
}

/// A node in a mount namespace.
///
/// This tree is a composite of the mount tree and the FsNode tree.
///
/// These nodes are used when traversing paths in a namespace in order to
/// present the client the directory structure that includes the mounted
/// filesystems.
#[derive(Clone)]
pub struct NamespaceNode {
    /// The mount where this namespace node is mounted.
    ///
    /// A given FsNode can be mounted in multiple places in a namespace. This
    /// field distinguishes between them.
    mount: Option<MountHandle>,

    /// The FsNode that corresponds to this namespace entry.
    pub node: FsNodeHandle,
}

impl NamespaceNode {
    /// Create a namespace node that is not mounted in a namespace.
    ///
    /// The returned node does not have a name.
    pub fn new_anonymous(node: FsNodeHandle) -> Self {
        Self { mount: None, node }
    }

    /// Create a FileObject corresponding to this namespace node.
    ///
    /// This function is the primary way of instantiating FileObjects. Each
    /// FileObject records the NamespaceNode that created it in order to
    /// remember its path in the Namespace.
    pub fn open(&self, flags: OpenFlags) -> Result<FileHandle, Errno> {
        Ok(FileObject::new(self.node.open(flags)?, self.clone(), flags))
    }

    pub fn create_node<F>(&self, name: &FsStr, mk_callback: F) -> Result<NamespaceNode, Errno>
    where
        F: FnOnce() -> Result<FsNodeHandle, Errno>,
    {
        // TODO: Figure out what these errors should be, and if they are consistent across
        // callsites. If so, checks can be removed from, for example, sys_symlinkat.
        if name.is_empty() || name == b"." || name == b".." {
            return Err(EEXIST);
        }
        Ok(self.with_new_node(mk_callback()?))
    }

    pub fn mknod(
        &self,
        name: &FsStr,
        mode: FileMode,
        dev: DeviceType,
    ) -> Result<NamespaceNode, Errno> {
        self.create_node(name, || self.node.mknod(name, mode, dev))
    }

    pub fn symlink(&self, name: &FsStr, target: &FsStr) -> Result<NamespaceNode, Errno> {
        self.create_node(name, || self.node.create_symlink(name, target))
    }

    pub fn unlink(&self, context: &FsContext, name: &FsStr, kind: UnlinkKind) -> Result<(), Errno> {
        if name.is_empty() || name == b"." || name == b".." {
            return Err(EINVAL);
        }
        let child = self.lookup(context, name, SymlinkMode::NoFollow)?;

        let unlink = || {
            if child.mountpoint().is_some() {
                return Err(EBUSY);
            }
            self.node.unlink(name, kind)
        };

        // If this node is mounted in a namespace, we grab a read lock on the
        // mount points for the namespace to prevent a time-of-check to
        // time-of-use race between checking whether the child is a mount point
        // and removing the child.
        if let Some(ns) = self.namespace() {
            let _guard = ns.mount_points.read();
            unlink()
        } else {
            unlink()
        }
    }

    /// Traverse down a parent-to-child link in the namespace.
    pub fn lookup(
        &self,
        context: &FsContext,
        name: &FsStr,
        symlink_mode: SymlinkMode,
    ) -> Result<NamespaceNode, Errno> {
        if !self.node.info().mode.is_dir() {
            Err(ENOTDIR)
        } else if name == b"." || name == b"" {
            Ok(self.clone())
        } else if name == b".." {
            // TODO: make sure this can't escape a chroot
            Ok(self.parent().unwrap_or_else(|| self.clone()))
        } else {
            let mut child = self.with_new_node(self.node.component_lookup(name)?);
            while child.node.info().mode.is_lnk() {
                match symlink_mode {
                    SymlinkMode::Follow(count) if count > 0 => {
                        let link_target = child.node.readlink()?;
                        let link_directory = if link_target[0] == b'/' {
                            context.root.clone()
                        } else {
                            self.clone()
                        };
                        child = context.lookup_node(
                            link_directory,
                            &link_target,
                            SymlinkMode::Follow(count - 1),
                        )?;
                    }
                    SymlinkMode::Follow(0) => {
                        return Err(ELOOP);
                    }
                    _ => {
                        break;
                    }
                };
            }

            if let Some(namespace) = self.namespace() {
                if let Some(mount) = namespace.mount_points.read().get(&child) {
                    return Ok(mount.root());
                }
            }
            Ok(child)
        }
    }

    /// Traverse up a child-to-parent link in the namespace.
    ///
    /// This traversal matches the child-to-parent link in the underlying
    /// FsNode except at mountpoints, where the link switches from one
    /// filesystem to another.
    pub fn parent(&self) -> Option<NamespaceNode> {
        let current = self.mountpoint().unwrap_or_else(|| self.clone());
        Some(current.with_new_node(current.node.parent()?.clone()))
    }

    /// Returns the mountpoint at this location in the namespace.
    ///
    /// If this node is mounted in another node, this function returns the node
    /// at which this node is mounted. Otherwise, returns None.
    fn mountpoint(&self) -> Option<NamespaceNode> {
        if let Some(mount) = &self.mount {
            if Arc::ptr_eq(&self.node, &mount.root) {
                return mount.mountpoint();
            }
        }
        None
    }

    /// The path from the root of the namespace to this node.
    pub fn path(&self) -> FsString {
        if self.mount.is_none() {
            return self.node.local_name().to_vec();
        }
        let mut components = vec![];
        let mut current = self.mountpoint().unwrap_or_else(|| self.clone());
        while let Some(parent) = current.parent() {
            components.push(current.node.local_name().to_vec());
            current = parent.mountpoint().unwrap_or(parent);
        }
        if components.is_empty() {
            return b"/".to_vec();
        }
        components.push(vec![]);
        components.reverse();
        components.join(&b'/')
    }

    pub fn mount(&self, root: FsNodeHandle) -> Result<(), Errno> {
        if let Some(namespace) = self.namespace() {
            match namespace.mount_points.write().entry(self.clone()) {
                Entry::Occupied(_) => {
                    log::warn!("mount shadowing is unimplemented");
                    Err(EBUSY)
                }
                Entry::Vacant(v) => {
                    let mount = self.mount.as_ref().unwrap();
                    let fs = root.file_system();
                    v.insert(Arc::new(Mount {
                        namespace: mount.namespace.clone(),
                        mountpoint: Some((Arc::downgrade(&mount), self.node.clone())),
                        root,
                        _fs: fs,
                    }));
                    Ok(())
                }
            }
        } else {
            Err(EBUSY)
        }
    }

    fn with_new_node(&self, node: FsNodeHandle) -> NamespaceNode {
        NamespaceNode { mount: self.mount.clone(), node }
    }

    fn namespace(&self) -> Option<Arc<Namespace>> {
        self.mount.as_ref().and_then(|mount| mount.namespace.upgrade())
    }
}

impl fmt::Debug for NamespaceNode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("NamespaceNode")
            .field("node.local_name", &String::from_utf8_lossy(self.node.local_name()))
            .finish()
    }
}

// Eq/Hash impls intended for the MOUNT_POINTS hash
impl PartialEq for NamespaceNode {
    fn eq(&self, other: &Self) -> bool {
        self.mount.as_ref().map(Arc::as_ptr).eq(&other.mount.as_ref().map(Arc::as_ptr))
            && Arc::ptr_eq(&self.node, &other.node)
    }
}
impl Eq for NamespaceNode {}
impl Hash for NamespaceNode {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.mount.as_ref().map(Arc::as_ptr).hash(state);
        Arc::as_ptr(&self.node).hash(state);
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::fs::tmpfs::TmpFs;

    #[test]
    fn test_namespace() -> anyhow::Result<()> {
        let root_fs = TmpFs::new();
        let root_node = Arc::clone(root_fs.root());
        let _dev_node = root_node.mkdir(b"dev").expect("failed to mkdir dev");
        let dev_fs = TmpFs::new();
        let dev_root_node = Arc::clone(dev_fs.root());
        let _dev_pts_node = dev_root_node.mkdir(b"pts").expect("failed to mkdir pts");

        let ns = Namespace::new(root_fs.clone());
        let context = FsContext::new(root_fs);
        let dev = ns
            .root()
            .lookup(&context, b"dev", SymlinkMode::max_follow())
            .expect("failed to lookup dev");
        dev.mount(dev_fs.root().clone()).expect("failed to mount dev root node");

        let dev = ns
            .root()
            .lookup(&context, b"dev", SymlinkMode::max_follow())
            .expect("failed to lookup dev");
        let pts =
            dev.lookup(&context, b"pts", SymlinkMode::max_follow()).expect("failed to lookup pts");
        let pts_parent = pts.parent().ok_or(ENOENT).expect("failed to get parent of pts");
        assert!(Arc::ptr_eq(&pts_parent.node, &dev.node));

        let dev_parent = dev.parent().ok_or(ENOENT).expect("failed to get parent of dev");
        assert!(Arc::ptr_eq(&dev_parent.node, &ns.root().node));
        Ok(())
    }

    #[test]
    fn test_mount_does_not_upgrade() -> anyhow::Result<()> {
        let root_fs = TmpFs::new();
        let root_node = Arc::clone(root_fs.root());
        let _dev_node = root_node.mkdir(b"dev").expect("failed to mkdir dev");
        let dev_fs = TmpFs::new();
        let dev_root_node = Arc::clone(dev_fs.root());
        let _dev_pts_node = dev_root_node.mkdir(b"pts").expect("failed to mkdir pts");

        let ns = Namespace::new(root_fs.clone());
        let context = FsContext::new(root_fs);
        let dev = ns
            .root()
            .lookup(&context, b"dev", SymlinkMode::max_follow())
            .expect("failed to lookup dev");
        dev.mount(dev_fs.root().clone()).expect("failed to mount dev root node");
        let new_dev = ns
            .root()
            .lookup(&context, b"dev", SymlinkMode::max_follow())
            .expect("failed to lookup dev again");
        assert!(!Arc::ptr_eq(&dev.node, &new_dev.node));
        assert_ne!(&dev, &new_dev);

        let _new_pts = new_dev
            .lookup(&context, b"pts", SymlinkMode::max_follow())
            .expect("failed to lookup pts");
        assert!(dev.lookup(&context, b"pts", SymlinkMode::max_follow()).is_err());

        Ok(())
    }

    #[test]
    fn test_path() -> anyhow::Result<()> {
        let root_fs = TmpFs::new();
        let root_node = Arc::clone(root_fs.root());
        let _dev_node = root_node.mkdir(b"dev").expect("failed to mkdir dev");
        let dev_fs = TmpFs::new();
        let dev_root_node = Arc::clone(dev_fs.root());
        let _dev_pts_node = dev_root_node.mkdir(b"pts").expect("failed to mkdir pts");

        let ns = Namespace::new(root_fs.clone());
        let context = FsContext::new(root_fs);
        let dev = ns
            .root()
            .lookup(&context, b"dev", SymlinkMode::max_follow())
            .expect("failed to lookup dev");
        dev.mount(dev_fs.root().clone()).expect("failed to mount dev root node");

        let dev = ns
            .root()
            .lookup(&context, b"dev", SymlinkMode::max_follow())
            .expect("failed to lookup dev");
        let pts =
            dev.lookup(&context, b"pts", SymlinkMode::max_follow()).expect("failed to lookup pts");

        assert_eq!(b"/".to_vec(), ns.root().path());
        assert_eq!(b"/dev".to_vec(), dev.path());
        assert_eq!(b"/dev/pts".to_vec(), pts.path());
        Ok(())
    }
}
