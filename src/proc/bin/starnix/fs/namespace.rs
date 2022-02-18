// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::fmt;
use std::hash::{Hash, Hasher};
use std::sync::{Arc, Weak};

use once_cell::sync::OnceCell;
use parking_lot::RwLock;

use super::devpts::DevptsFs;
use super::devtmpfs::dev_tmp_fs;
use super::proc::proc_fs;
use super::sysfs::sys_fs;
use super::tmpfs::TmpFs;
use super::*;
use crate::error;
use crate::selinux::selinux_fs;
use crate::task::{CurrentTask, Kernel};
use crate::types::*;

/// A mount namespace.
///
/// The namespace records at which entries filesystems are mounted.
pub struct Namespace {
    root_mount: OnceCell<MountHandle>,
    // The value in this hashmap is a vector because multiple mounts can be stacked on top of the
    // same mount point. The last one in the vector shadows the others and is used for lookups.
    // Unmounting will remove the last entry. Mounting will add an entry.
    mount_points: RwLock<HashMap<NamespaceNode, Vec<MountHandle>>>,
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
                root,
                _flags: MountFlags::empty(),
                _fs: fs,
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

impl fmt::Debug for Namespace {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Namespace")
            .field("root_mount", &self.root_mount)
            .field("mount_points", &self.mount_points.read())
            .finish()
    }
}

/// An instance of a filesystem mounted in a namespace.
///
/// At a mount, path traversal switches from one filesystem to another.
/// The client sees a composed directory structure that glues together the
/// directories from the underlying FsNodes from those filesystems.
struct Mount {
    namespace: Weak<Namespace>,
    mountpoint: Option<(Weak<Mount>, DirEntryHandle)>,
    root: DirEntryHandle,
    _flags: MountFlags,
    _fs: FileSystemHandle,
}
type MountHandle = Arc<Mount>;

impl Mount {
    pub fn root(self: &MountHandle) -> NamespaceNode {
        NamespaceNode { mount: Some(Arc::clone(self)), entry: Arc::clone(&self.root) }
    }

    fn mountpoint(&self) -> Option<NamespaceNode> {
        let (ref mount, ref node) = &self.mountpoint.as_ref()?;
        Some(NamespaceNode { mount: Some(mount.upgrade()?), entry: node.clone() })
    }
}

impl fmt::Debug for Mount {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Mount")
            .field("id", &(self as *const Mount))
            .field("mountpoint", &self.mountpoint)
            .field("root", &self.root)
            .finish()
    }
}

pub enum WhatToMount {
    Fs(FileSystemHandle),
    Dir(DirEntryHandle),
}

pub fn create_filesystem(
    kernel: &Arc<Kernel>,
    _source: &FsStr,
    fs_type: &FsStr,
    _data: &FsStr,
) -> Result<WhatToMount, Errno> {
    use WhatToMount::*;
    Ok(match fs_type {
        b"devtmpfs" => Fs(dev_tmp_fs(kernel).clone()),
        b"devpts" => Fs(DevptsFs::new()),
        b"proc" => Fs(proc_fs(kernel.clone())),
        b"selinuxfs" => Fs(selinux_fs(kernel).clone()),
        b"sysfs" => Fs(sys_fs(kernel).clone()),
        b"tmpfs" => Fs(TmpFs::new()),
        _ => return error!(ENODEV),
    })
}

/// The `SymlinkMode` enum encodes how symlinks are followed during path traversal.
#[derive(PartialEq, Eq, Copy, Clone, Debug)]

/// Whether to follow a symlink at the end of a path resolution.
pub enum SymlinkMode {
    /// Follow a symlink at the end of a path resolution.
    Follow,

    /// Do not follow a symlink at the end of a path resolution.
    NoFollow,
}

/// The maximum number of symlink traversals that can be made during path resolution.
const MAX_SYMLINK_FOLLOWS: u8 = 40;

/// The context passed during namespace lookups.
///
/// Namespace lookups need to mutate a shared context in order to correctly
/// count the number of remaining symlink traversals.
pub struct LookupContext {
    /// The SymlinkMode for the lookup.
    ///
    /// As the lookup proceeds, the follow count is decremented each time the
    /// lookup traverses a symlink.
    pub symlink_mode: SymlinkMode,

    /// The number of symlinks remaining the follow.
    ///
    /// Each time path resolution calls readlink, this value is decremented.
    pub remaining_follows: u8,

    /// Whether the result of the lookup must be a directory.
    ///
    /// For example, if the path ends with a `/` or if userspace passes
    /// O_DIRECTORY. This flag can be set to true if the lookup encounters a
    /// symlink that ends with a `/`.
    pub must_be_directory: bool,
}

impl LookupContext {
    pub fn new(symlink_mode: SymlinkMode) -> LookupContext {
        LookupContext {
            symlink_mode,
            remaining_follows: MAX_SYMLINK_FOLLOWS,
            must_be_directory: false,
        }
    }

    pub fn with(&self, symlink_mode: SymlinkMode) -> LookupContext {
        LookupContext {
            symlink_mode,
            remaining_follows: self.remaining_follows,
            must_be_directory: self.must_be_directory,
        }
    }

    pub fn update_for_path<'a>(&mut self, path: &'a FsStr) -> &'a FsStr {
        if path.last() == Some(&b'/') {
            self.must_be_directory = true;
            trim_trailing_slashes(path)
        } else {
            path
        }
    }
}

impl Default for LookupContext {
    fn default() -> Self {
        LookupContext::new(SymlinkMode::Follow)
    }
}

fn trim_trailing_slashes(path: &FsStr) -> &FsStr {
    path.iter().rposition(|c| *c != b'/').map(|last| &path[..(last + 1)]).unwrap_or(b"")
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
    pub entry: DirEntryHandle,
}

impl NamespaceNode {
    /// Create a namespace node that is not mounted in a namespace.
    ///
    /// The returned node does not have a name.
    pub fn new_anonymous(node: FsNodeHandle) -> Self {
        Self { mount: None, entry: DirEntry::new(node, None, FsString::new()) }
    }

    /// Create a FileObject corresponding to this namespace node.
    ///
    /// This function is the primary way of instantiating FileObjects. Each
    /// FileObject records the NamespaceNode that created it in order to
    /// remember its path in the Namespace.
    pub fn open(&self, kernel: &Kernel, flags: OpenFlags) -> Result<FileHandle, Errno> {
        Ok(FileObject::new(self.entry.node.open(kernel, flags)?, self.clone(), flags))
    }

    pub fn create_node(
        &self,
        name: &FsStr,
        mode: FileMode,
        dev: DeviceType,
    ) -> Result<NamespaceNode, Errno> {
        Ok(self.with_new_entry(self.entry.create_node(name, mode, dev)?))
    }

    pub fn symlink(&self, name: &FsStr, target: &FsStr) -> Result<NamespaceNode, Errno> {
        Ok(self.with_new_entry(self.entry.create_symlink(name, target)?))
    }

    pub fn unlink(&self, name: &FsStr, kind: UnlinkKind) -> Result<(), Errno> {
        if DirEntry::is_reserved_name(name) {
            match kind {
                UnlinkKind::Directory => {
                    if name == b".." {
                        error!(ENOTEMPTY)
                    } else if self.parent().is_none() {
                        // The client is attempting to remove the root.
                        error!(EBUSY)
                    } else {
                        error!(EINVAL)
                    }
                }
                UnlinkKind::NonDirectory => error!(ENOTDIR),
            }
        } else {
            DirEntry::unlink(self, name, kind)
        }
    }

    /// Traverse down a parent-to-child link in the namespace.
    pub fn lookup_child(
        &self,
        current_task: &CurrentTask,
        context: &mut LookupContext,
        basename: &FsStr,
    ) -> Result<NamespaceNode, Errno> {
        if !self.entry.node.is_dir() {
            error!(ENOTDIR)
        } else if basename == b"." || basename == b"" {
            Ok(self.clone())
        } else if basename == b".." {
            // TODO: make sure this can't escape a chroot
            Ok(self.parent().unwrap_or_else(|| self.clone()))
        } else {
            let mut child = self.with_new_entry(self.entry.component_lookup(basename)?);
            while child.entry.node.info().mode.is_lnk() {
                match context.symlink_mode {
                    SymlinkMode::NoFollow => {
                        break;
                    }
                    SymlinkMode::Follow => {
                        if context.remaining_follows == 0 {
                            return error!(ELOOP);
                        }
                        context.remaining_follows -= 1;
                        child = match child.entry.node.readlink(current_task)? {
                            SymlinkTarget::Path(link_target) => {
                                let link_directory = if link_target[0] == b'/' {
                                    current_task.fs.root.clone()
                                } else {
                                    self.clone()
                                };
                                current_task.lookup_path(context, link_directory, &link_target)?
                            }
                            SymlinkTarget::Node(node) => node,
                        }
                    }
                };
            }

            if let Some(namespace) = self.namespace() {
                if let Some(mounts_at_point) = namespace.mount_points.read().get(&child) {
                    if let Some(mount) = mounts_at_point.last() {
                        return Ok(mount.root());
                    }
                }
            }
            Ok(child)
        }
    }

    /// Whether the child is a mount point in this NamespaceNode.
    pub fn child_is_mountpoint(&self, child_entry: &DirEntryHandle) -> bool {
        let child = self.with_new_entry(child_entry.clone());
        self.namespace().map(|ns| ns.mount_points.read().contains_key(&child)).unwrap_or(false)
    }

    /// If this is the root of a mount, go up a level and return the mount point. Otherwise return
    /// the same node.
    ///
    /// This is not exactly the same as parent(). If parent() is called on a root, it will escape
    /// the mount, but then return the parent of the mount point instead of the mount point.
    pub fn escape_mount(&self) -> NamespaceNode {
        self.mountpoint().unwrap_or_else(|| self.clone())
    }

    /// Traverse up a child-to-parent link in the namespace.
    ///
    /// This traversal matches the child-to-parent link in the underlying
    /// FsNode except at mountpoints, where the link switches from one
    /// filesystem to another.
    pub fn parent(&self) -> Option<NamespaceNode> {
        let mountpoint_or_self = self.escape_mount();
        Some(mountpoint_or_self.with_new_entry(mountpoint_or_self.entry.parent()?.clone()))
    }

    /// Returns the mountpoint at this location in the namespace.
    ///
    /// If this node is mounted in another node, this function returns the node
    /// at which this node is mounted. Otherwise, returns None.
    fn mountpoint(&self) -> Option<NamespaceNode> {
        if let Some(mount) = &self.mount {
            if Arc::ptr_eq(&self.entry, &mount.root) {
                return mount.mountpoint();
            }
        }
        None
    }

    /// The path from the root of the namespace to this node.
    pub fn path(&self) -> FsString {
        if self.mount.is_none() {
            return self.entry.local_name().to_vec();
        }
        let mut components = vec![];
        let mut current_task = self.mountpoint().unwrap_or_else(|| self.clone());
        while let Some(parent) = current_task.parent() {
            components.push(current_task.entry.local_name().to_vec());
            current_task = parent.mountpoint().unwrap_or(parent);
        }
        if components.is_empty() {
            return b"/".to_vec();
        }
        components.push(vec![]);
        components.reverse();
        components.join(&b'/')
    }

    pub fn mount(&self, root: WhatToMount, flags: MountFlags) -> Result<(), Errno> {
        if let Some(namespace) = self.namespace() {
            let mut mounts_hash = namespace.mount_points.write();
            let mounts_at_point = mounts_hash.entry(self.clone()).or_default();
            let mount = self.mount.as_ref().unwrap();
            let (fs, root) = match root {
                WhatToMount::Fs(fs) => {
                    let root = fs.root().clone();
                    (fs, root)
                }
                WhatToMount::Dir(entry) => (entry.node.fs(), entry),
            };
            mounts_at_point.push(Arc::new(Mount {
                namespace: mount.namespace.clone(),
                mountpoint: Some((Arc::downgrade(&mount), self.entry.clone())),
                root,
                _flags: flags,
                _fs: fs,
            }));
            Ok(())
        } else {
            error!(EBUSY)
        }
    }

    pub fn mount_eq(a: &NamespaceNode, b: &NamespaceNode) -> bool {
        a.mount.as_ref().map(Arc::as_ptr) == b.mount.as_ref().map(Arc::as_ptr)
    }

    fn with_new_entry(&self, entry: DirEntryHandle) -> NamespaceNode {
        NamespaceNode { mount: self.mount.clone(), entry }
    }

    fn namespace(&self) -> Option<Arc<Namespace>> {
        self.mount.as_ref().and_then(|mount| mount.namespace.upgrade())
    }
}

impl fmt::Debug for NamespaceNode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("NamespaceNode")
            .field("path", &String::from_utf8_lossy(&self.path()))
            .field("mount", &self.mount)
            .field("entry", &self.entry)
            .finish()
    }
}

// Eq/Hash impls intended for the MOUNT_POINTS hash
impl PartialEq for NamespaceNode {
    fn eq(&self, other: &Self) -> bool {
        self.mount.as_ref().map(Arc::as_ptr).eq(&other.mount.as_ref().map(Arc::as_ptr))
            && Arc::ptr_eq(&self.entry, &other.entry)
    }
}
impl Eq for NamespaceNode {}
impl Hash for NamespaceNode {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.mount.as_ref().map(Arc::as_ptr).hash(state);
        Arc::as_ptr(&self.entry).hash(state);
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::errno;
    use crate::fs::tmpfs::TmpFs;
    use crate::testing::*;

    #[test]
    fn test_namespace() -> anyhow::Result<()> {
        let (_kernel, current_task) = create_kernel_and_task();
        let root_fs = TmpFs::new();
        let root_node = Arc::clone(root_fs.root());
        let _dev_node = root_node.create_dir(b"dev").expect("failed to mkdir dev");
        let dev_fs = TmpFs::new();
        let dev_root_node = Arc::clone(dev_fs.root());
        let _dev_pts_node = dev_root_node.create_dir(b"pts").expect("failed to mkdir pts");

        let ns = Namespace::new(root_fs.clone());
        let mut context = LookupContext::default();
        let dev = ns
            .root()
            .lookup_child(&current_task, &mut context, b"dev")
            .expect("failed to lookup dev");
        dev.mount(WhatToMount::Fs(dev_fs), MountFlags::empty())
            .expect("failed to mount dev root node");

        let mut context = LookupContext::default();
        let dev = ns
            .root()
            .lookup_child(&current_task, &mut context, b"dev")
            .expect("failed to lookup dev");
        let mut context = LookupContext::default();
        let pts =
            dev.lookup_child(&current_task, &mut context, b"pts").expect("failed to lookup pts");
        let pts_parent = pts.parent().ok_or(errno!(ENOENT)).expect("failed to get parent of pts");
        assert!(Arc::ptr_eq(&pts_parent.entry, &dev.entry));

        let dev_parent = dev.parent().ok_or(errno!(ENOENT)).expect("failed to get parent of dev");
        assert!(Arc::ptr_eq(&dev_parent.entry, &ns.root().entry));
        Ok(())
    }

    #[test]
    fn test_mount_does_not_upgrade() -> anyhow::Result<()> {
        let (_kernel, current_task) = create_kernel_and_task();
        let root_fs = TmpFs::new();
        let root_node = Arc::clone(root_fs.root());
        let _dev_node = root_node.create_dir(b"dev").expect("failed to mkdir dev");
        let dev_fs = TmpFs::new();
        let dev_root_node = Arc::clone(dev_fs.root());
        let _dev_pts_node = dev_root_node.create_dir(b"pts").expect("failed to mkdir pts");

        let ns = Namespace::new(root_fs.clone());
        let mut context = LookupContext::default();
        let dev = ns
            .root()
            .lookup_child(&current_task, &mut context, b"dev")
            .expect("failed to lookup dev");
        dev.mount(WhatToMount::Fs(dev_fs), MountFlags::empty())
            .expect("failed to mount dev root node");
        let mut context = LookupContext::default();
        let new_dev = ns
            .root()
            .lookup_child(&current_task, &mut context, b"dev")
            .expect("failed to lookup dev again");
        assert!(!Arc::ptr_eq(&dev.entry, &new_dev.entry));
        assert_ne!(&dev, &new_dev);

        let mut context = LookupContext::default();
        let _new_pts = new_dev
            .lookup_child(&current_task, &mut context, b"pts")
            .expect("failed to lookup pts");
        let mut context = LookupContext::default();
        assert!(dev.lookup_child(&current_task, &mut context, b"pts").is_err());

        Ok(())
    }

    #[test]
    fn test_path() -> anyhow::Result<()> {
        let (_kernel, current_task) = create_kernel_and_task();
        let root_fs = TmpFs::new();
        let root_node = Arc::clone(root_fs.root());
        let _dev_node = root_node.create_dir(b"dev").expect("failed to mkdir dev");
        let dev_fs = TmpFs::new();
        let dev_root_node = Arc::clone(dev_fs.root());
        let _dev_pts_node = dev_root_node.create_dir(b"pts").expect("failed to mkdir pts");

        let ns = Namespace::new(root_fs.clone());
        let mut context = LookupContext::default();
        let dev = ns
            .root()
            .lookup_child(&current_task, &mut context, b"dev")
            .expect("failed to lookup dev");
        dev.mount(WhatToMount::Fs(dev_fs), MountFlags::empty())
            .expect("failed to mount dev root node");

        let mut context = LookupContext::default();
        let dev = ns
            .root()
            .lookup_child(&current_task, &mut context, b"dev")
            .expect("failed to lookup dev");
        let mut context = LookupContext::default();
        let pts =
            dev.lookup_child(&current_task, &mut context, b"pts").expect("failed to lookup pts");

        assert_eq!(b"/".to_vec(), ns.root().path());
        assert_eq!(b"/dev".to_vec(), dev.path());
        assert_eq!(b"/dev/pts".to_vec(), pts.path());
        Ok(())
    }

    #[test]
    fn test_shadowing() -> anyhow::Result<()> {
        let (_kernel, current_task) = create_kernel_and_task();
        let root_fs = TmpFs::new();
        let ns = Namespace::new(root_fs.clone());
        let _foo_node = root_fs.root().create_dir(b"foo")?;
        let mut context = LookupContext::default();
        let foo_dir = ns.root().lookup_child(&current_task, &mut context, b"foo")?;

        let foofs1 = TmpFs::new();
        foo_dir.mount(WhatToMount::Fs(foofs1.clone()), MountFlags::empty())?;
        let mut context = LookupContext::default();
        assert!(Arc::ptr_eq(
            &ns.root().lookup_child(&current_task, &mut context, b"foo")?.entry,
            foofs1.root()
        ));

        let foofs2 = TmpFs::new();
        foo_dir.mount(WhatToMount::Fs(foofs2.clone()), MountFlags::empty())?;
        let mut context = LookupContext::default();
        assert!(Arc::ptr_eq(
            &ns.root().lookup_child(&current_task, &mut context, b"foo")?.entry,
            foofs2.root()
        ));

        Ok(())
    }

    #[test]
    fn test_trim_trailing_slashes() {
        assert_eq!(b"", trim_trailing_slashes(b""));
        assert_eq!(b"", trim_trailing_slashes(b"/"));
        assert_eq!(b"", trim_trailing_slashes(b"/////"));
        assert_eq!(b"abc", trim_trailing_slashes(b"abc"));
        assert_eq!(b"abc", trim_trailing_slashes(b"abc/"));
        assert_eq!(b"abc", trim_trailing_slashes(b"abc/////"));
        assert_eq!(b"abc///xyz", trim_trailing_slashes(b"abc///xyz//"));
        assert_eq!(b"abc///xyz", trim_trailing_slashes(b"abc///xyz/"));
        assert_eq!(b"////abc///xyz", trim_trailing_slashes(b"////abc///xyz/"));
    }
}
