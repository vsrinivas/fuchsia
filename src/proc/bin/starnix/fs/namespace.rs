// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::{HashMap, HashSet};
use std::fmt;
use std::hash::{Hash, Hasher};
use std::sync::{Arc, Weak};

use ref_cast::RefCast;

use super::devpts::dev_pts_fs;
use super::devtmpfs::dev_tmp_fs;
use super::proc::proc_fs;
use super::sysfs::sys_fs;
use super::tmpfs::TmpFs;
use super::*;
use crate::bpf::BpfFs;
use crate::device::BinderFs;
use crate::lock::RwLock;
use crate::mutable_state::*;
use crate::selinux::selinux_fs;
use crate::task::CurrentTask;
use crate::types::*;

/// A mount namespace.
///
/// The namespace records at which entries filesystems are mounted.
pub struct Namespace {
    root_mount: MountHandle,
}

impl Namespace {
    pub fn new(fs: FileSystemHandle) -> Arc<Namespace> {
        Arc::new(Self { root_mount: Mount::new(WhatToMount::Fs(fs), MountFlags::empty()) })
    }

    pub fn root(&self) -> NamespaceNode {
        self.root_mount.root()
    }

    pub fn clone_namespace(&self) -> Arc<Namespace> {
        Arc::new(Self { root_mount: self.root_mount.clone_mount_recursive() })
    }
}

impl fmt::Debug for Namespace {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Namespace").field("root_mount", &self.root_mount).finish()
    }
}

/// An instance of a filesystem mounted in a namespace.
///
/// At a mount, path traversal switches from one filesystem to another.
/// The client sees a composed directory structure that glues together the
/// directories from the underlying FsNodes from those filesystems.
///
/// The mounts in a namespace form a mount tree, with `mountpoint` pointing to the parent and
/// `submounts` pointing to the children.
pub struct Mount {
    root: DirEntryHandle,
    flags: MountFlags,
    _fs: FileSystemHandle,

    // Lock ordering: mount -> submount
    state: RwLock<MountState>,
    // Mount used to contain a Weak<Namespace>. It no longer does because since the mount point
    // hash was moved from Namespace to Mount, nothing actually uses it. Now that
    // Namespace::clone_namespace() is implemented in terms of Mount::clone_mount_recursive, it
    // won't be trivial to add it back. I recommend turning the mountpoint field into an enum of
    // Mountpoint or Namespace, maybe called "parent", and then traverse up to the top of the tree
    // if you need to find a Mount's Namespace.
}
type MountHandle = Arc<Mount>;

#[derive(Default)]
pub struct MountState {
    /// The namespace node that this mount is mounted on. This is a tuple instead of a
    /// NamespaceNode because the Mount pointer has to be weak because this is the pointer to the
    /// parent mount, the parent has a pointer to the children too, and making both strong would be
    /// a cycle.
    mountpoint: Option<(Weak<Mount>, DirEntryHandle)>,

    // The keys of this map are always descendants of this mount's root.
    //
    // Each directory entry can only have one mount attached. Mount shadowing works by using the
    // root of the inner mount as a mountpoint. For example, if filesystem A is mounted at /foo,
    // mounting filesystem B on /foo will create the mount as a child of the A mount, attached to
    // A's root, instead of the root mount.
    submounts: HashMap<ArcKey<DirEntry>, MountHandle>,
    peer_group: Option<Arc<PeerGroup>>,
}

/// A group of mounts. Setting MS_SHARED on a mount puts it in its own peer group. Any bind mounts
/// of a mount in the group are also added to the group. A mount created in any mount in a peer
/// group will be automatically propagated (recreated) in every other mount in the group.
#[derive(Default)]
struct PeerGroup {
    mounts: RwLock<HashSet<WeakKey<Mount>>>,
}

pub enum WhatToMount {
    Fs(FileSystemHandle),
    Bind(NamespaceNode),
}

impl Mount {
    fn new(what: WhatToMount, flags: MountFlags) -> MountHandle {
        match what {
            WhatToMount::Fs(fs) => Self::new_with_root(fs.root().clone(), flags),
            WhatToMount::Bind(node) => {
                let mount = node.mount.expect("can't bind mount from an anonymous node");
                mount.clone_mount(&node.entry, flags)
            }
        }
    }

    fn new_with_root(root: DirEntryHandle, flags: MountFlags) -> MountHandle {
        assert!(
            !flags.intersects(!MountFlags::STORED_FLAGS),
            "mount created with extra flags {:?}",
            flags - MountFlags::STORED_FLAGS
        );
        let fs = root.node.fs();
        Arc::new(Self { root, flags, _fs: fs, state: Default::default() })
    }

    /// A namespace node referring to the root of the mount.
    pub fn root(self: &MountHandle) -> NamespaceNode {
        NamespaceNode { mount: Some(Arc::clone(self)), entry: Arc::clone(&self.root) }
    }

    /// The NamespaceNode on which this Mount is mounted.
    fn mountpoint(&self) -> Option<NamespaceNode> {
        let state = self.state.read();
        let (ref mount, ref entry) = state.mountpoint.as_ref()?;
        Some(NamespaceNode { mount: Some(mount.upgrade()?), entry: entry.clone() })
    }

    /// Create the specified mount as a child. Also propagate it to the mount's peer group.
    fn create_submount(
        self: &MountHandle,
        dir: &DirEntryHandle,
        what: WhatToMount,
        flags: MountFlags,
    ) {
        // TODO(tbodt): Making a copy here is necessary for lock ordering, because the peer group
        // lock nests inside all mount locks (it would be impractical to reverse this because you
        // need to lock a mount to get its peer group.) But it opens the door to race conditions
        // where if a peer are concurrently being added, the mount might not get propagated to the
        // new peer. The only true solution to this is bigger locks, somehow using the same lock
        // for the peer group and all of the mounts in the group. Since peer groups are fluid and
        // can have mounts constantly joining and leaving and then joining other groups, the only
        // sensible locking option is to use a single global lock for all mounts and peer groups.
        // This is almost impossible to express in rust. Help.
        //
        // Update: Also necessary to make a copy to prevent excess replication, see the comment on
        // the following Mount::new call.
        let peers =
            self.state.read().peer_group.as_ref().map(|g| g.copy_peers()).unwrap_or_default();

        // Create the mount after copying the peer groups, because in the case of creating a bind
        // mount inside itself, the new mount would get added to our peer group during the
        // Mount::new call, but we don't want to replicate into it already. For an example see
        // MountTest.QuizBRecursion.
        let mount = Mount::new(what, flags);

        if self.read().is_shared() {
            mount.write().make_shared();
        }

        for peer in peers {
            if Arc::ptr_eq(self, &peer) {
                continue;
            }
            let clone = mount.clone_mount_recursive();
            peer.write().add_submount_internal(dir, clone);
        }

        self.write().add_submount_internal(dir, mount)
    }

    /// Create a new mount with the same filesystem, flags, and peer group. Used to implement bind
    /// mounts.
    fn clone_mount(&self, new_root: &DirEntryHandle, flags: MountFlags) -> MountHandle {
        assert!(new_root.is_descendant_of(&self.root));
        // According to mount(2) on bind mounts, all flags other than MS_REC are ignored when doing
        // a bind mount.
        let clone = Self::new_with_root(Arc::clone(new_root), self.flags);

        if flags.contains(MountFlags::REC) {
            // This is two steps because the alternative (locking clone.state while iterating over
            // self.state.submounts) trips tracing_mutex. The lock ordering is parent -> child, and
            // if the clone is eventually made a child of self, this looks like an ordering
            // violation. I'm not convinced it's a real issue, but I can't convince myself it's not
            // either.
            let mut submounts = vec![];
            for (dir, mount) in &self.state.read().submounts {
                submounts.push((dir.clone(), mount.clone_mount_recursive()));
            }
            let mut clone_state = clone.write();
            for (dir, submount) in submounts {
                clone_state.add_submount_internal(&dir, submount);
            }
        }

        // Put the clone in the same peer group
        let peer_group = self.state.read().peer_group.clone();
        if let Some(peer_group) = peer_group {
            peer_group.add(&clone);
            clone.write().peer_group = Some(peer_group);
        }

        clone
    }

    /// Do a clone of the full mount hierarchy below this mount. Used for creating mount
    /// namespaces and creating copies to use for propagation.
    fn clone_mount_recursive(&self) -> MountHandle {
        self.clone_mount(&self.root, MountFlags::REC)
    }

    pub fn change_propagation(self: &MountHandle, flag: MountFlags, recursive: bool) {
        let mut state = self.write();
        match flag {
            MountFlags::SHARED => state.make_shared(),
            MountFlags::PRIVATE => state.make_private(),
            _ => {
                tracing::warn!("mount propagation {:?}", flag);
                return;
            }
        }

        if recursive {
            for mount in state.submounts.values() {
                mount.change_propagation(flag, recursive);
            }
        }
    }

    state_accessor!(Mount, state);
}

#[apply(state_implementation!)]
impl MountState<Base = Mount> {
    /// Add a child mount *without propagating it to the peer group*. For internal use only.
    fn add_submount_internal(&mut self, dir: &DirEntryHandle, mount: MountHandle) {
        if !dir.is_descendant_of(&self.base.root) {
            return;
        }

        dir.register_mount();
        let old_mountpoint =
            mount.state.write().mountpoint.replace((Arc::downgrade(self.base), Arc::clone(dir)));
        assert!(old_mountpoint.is_none(), "add_submount can only take a newly created mount");
        // Mount shadowing is implemented by mounting onto the root of the first mount, not by
        // creating two mounts on the same mountpoint.
        let old_mount = self.submounts.insert(ArcKey(dir.clone()), Arc::clone(&mount));

        // In rare cases, mount propagation might result in a request to mount on a directory where
        // something is already mounted. MountTest.LotsOfShadowing will trigger this. Linux handles
        // this by inserting the new mount between the old mount and the current mount.
        if let Some(old_mount) = old_mount {
            // Previous state: self[dir] = old_mount
            // New state: self[dir] = new_mount, new_mount[new_mount.root] = old_mount
            // The new mount has already been inserted into self, now just update the old mount to
            // be a child of the new mount.
            old_mount.write().mountpoint = Some((Arc::downgrade(&mount), Arc::clone(dir)));
            mount.write().submounts.insert(ArcKey(Arc::clone(&mount.root)), old_mount);
        }
    }

    /// Is the mount in a peer group? Corresponds to MS_SHARED.
    pub fn is_shared(&self) -> bool {
        self.peer_group.is_some()
    }

    /// Put the mount in a peer group. Implements MS_SHARED.
    pub fn make_shared(&mut self) {
        if self.is_shared() {
            return;
        }
        let peer_group = PeerGroup::new();
        peer_group.add(self.base);
        self.peer_group = Some(peer_group);
    }

    /// Take the mount out of its peer group. Implements MS_PRIVATE
    pub fn make_private(&mut self) {
        if let Some(peer_group) = self.peer_group.take() {
            peer_group.remove(&**self.base);
        }
    }
}

impl PeerGroup {
    fn new() -> Arc<Self> {
        Default::default()
    }

    fn add(&self, mount: &Arc<Mount>) {
        self.mounts.write().insert(WeakKey::from(mount));
    }

    fn remove(&self, mount: impl std::borrow::Borrow<Mount>) {
        self.mounts.write().remove(&(mount.borrow() as *const Mount));
    }

    fn copy_peers(&self) -> Vec<MountHandle> {
        self.mounts.read().iter().filter_map(|m| m.0.upgrade()).collect()
    }
}

impl Drop for Mount {
    fn drop(&mut self) {
        let mut state = self.state.write();
        if let Some((_mount, node)) = &state.mountpoint {
            node.unregister_mount()
        }
        if let Some(peer_group) = &mut state.peer_group {
            peer_group.remove(&*self);
        }
    }
}

impl fmt::Debug for Mount {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let state = self.state.read();
        f.debug_struct("Mount")
            .field("id", &(self as *const Mount))
            .field("root", &self.root)
            .field("mountpoint", &state.mountpoint)
            .field("submounts", &state.submounts)
            .finish()
    }
}

pub fn create_filesystem(
    task: &CurrentTask,
    _source: &FsStr,
    fs_type: &FsStr,
    data: &FsStr,
) -> Result<WhatToMount, Errno> {
    let kernel = task.kernel();
    let fs = match fs_type {
        b"devtmpfs" => dev_tmp_fs(task).clone(),
        b"devpts" => dev_pts_fs(kernel).clone(),
        b"proc" => proc_fs(kernel.clone()),
        b"selinuxfs" => selinux_fs(kernel).clone(),
        b"sysfs" => sys_fs(kernel).clone(),
        b"tmpfs" => TmpFs::new_fs_with_data(kernel, data),
        b"binder" => BinderFs::new_fs(kernel)?,
        b"bpf" => BpfFs::new_fs(kernel)?,
        _ => return error!(ENODEV, String::from_utf8_lossy(fs_type)),
    };

    if kernel.selinux_enabled() {
        (|| {
            let label = match fs_type {
                b"tmpfs" => b"u:object_r:tmpfs:s0",
                _ => return,
            };
            fs.selinux_context.set(label.to_vec()).unwrap();
        })();
    }

    Ok(WhatToMount::Fs(fs))
}

/// The `SymlinkMode` enum encodes how symlinks are followed during path traversal.
#[derive(PartialEq, Eq, Copy, Clone, Debug)]
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
    pub fn new_anonymous(dir_entry: DirEntryHandle) -> Self {
        Self { mount: None, entry: dir_entry }
    }

    /// Create a FileObject corresponding to this namespace node.
    ///
    /// This function is the primary way of instantiating FileObjects. Each
    /// FileObject records the NamespaceNode that created it in order to
    /// remember its path in the Namespace.
    pub fn open(
        &self,
        current_task: &CurrentTask,
        flags: OpenFlags,
        check_access: bool,
    ) -> Result<FileHandle, Errno> {
        Ok(FileObject::new(
            self.entry.node.open(current_task, flags, check_access)?,
            self.clone(),
            flags,
        ))
    }

    pub fn create_node(
        &self,
        current_task: &CurrentTask,
        name: &FsStr,
        mode: FileMode,
        dev: DeviceType,
    ) -> Result<NamespaceNode, Errno> {
        let owner = current_task.as_fscred();
        let mode = current_task.fs().apply_umask(mode);
        Ok(self.with_new_entry(self.entry.create_node(current_task, name, mode, dev, owner)?))
    }

    pub fn symlink(
        &self,
        current_task: &CurrentTask,
        name: &FsStr,
        target: &FsStr,
    ) -> Result<NamespaceNode, Errno> {
        let owner = current_task.as_fscred();
        Ok(self.with_new_entry(self.entry.create_symlink(current_task, name, target, owner)?))
    }

    pub fn unlink(
        &self,
        current_task: &CurrentTask,
        name: &FsStr,
        kind: UnlinkKind,
    ) -> Result<(), Errno> {
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
            self.entry.unlink(current_task, name, kind)
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
            // Make sure this can't escape a chroot
            if *self == current_task.fs().root() {
                return Ok(self.clone());
            }
            Ok(self.parent().unwrap_or_else(|| self.clone()))
        } else {
            let mut child =
                self.with_new_entry(self.entry.component_lookup(current_task, basename)?);
            while child.entry.node.is_lnk() {
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
                                    current_task.fs().root()
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

            Ok(child.enter_mount())
        }
    }

    /// Traverse up a child-to-parent link in the namespace.
    ///
    /// This traversal matches the child-to-parent link in the underlying
    /// FsNode except at mountpoints, where the link switches from one
    /// filesystem to another.
    pub fn parent(&self) -> Option<NamespaceNode> {
        let mountpoint_or_self = self.escape_mount();
        Some(mountpoint_or_self.with_new_entry(mountpoint_or_self.entry.parent()?))
    }

    /// If this is a mount point, return the root of the mount. Otherwise return self.
    fn enter_mount(&self) -> NamespaceNode {
        // While the child is a mountpoint, replace child with the mount's root.
        fn enter_one_mount(node: &NamespaceNode) -> Option<NamespaceNode> {
            if let Some(mount) = &node.mount {
                if let Some(mount) = mount.state.read().submounts.get(ArcKey::ref_cast(&node.entry))
                {
                    return Some(mount.root());
                }
            }
            None
        }
        let mut inner = self.clone();
        while let Some(inner_root) = enter_one_mount(&inner) {
            inner = inner_root;
        }
        inner
    }

    /// If this is the root of a mount, return the mount point. Otherwise return self.
    ///
    /// This is not exactly the same as parent(). If parent() is called on a root, it will escape
    /// the mount, but then return the parent of the mount point instead of the mount point.
    fn escape_mount(&self) -> NamespaceNode {
        let mut mountpoint_or_self = self.clone();
        while let Some(mountpoint) = mountpoint_or_self.mountpoint() {
            mountpoint_or_self = mountpoint;
        }
        mountpoint_or_self
    }

    /// If this node is the root of a mount, return it. Otherwise EINVAL.
    pub fn mount_if_root(&self) -> Result<&MountHandle, Errno> {
        if let Some(mount) = &self.mount {
            if Arc::ptr_eq(&self.entry, &mount.root) {
                return Ok(mount);
            }
        }
        error!(EINVAL)
    }

    /// Returns the mountpoint at this location in the namespace.
    ///
    /// If this node is mounted in another node, this function returns the node
    /// at which this node is mounted. Otherwise, returns None.
    fn mountpoint(&self) -> Option<NamespaceNode> {
        self.mount_if_root().ok()?.mountpoint()
    }

    /// The path from the root of the namespace to this node.
    pub fn path(&self) -> FsString {
        if self.mount.is_none() {
            return self.entry.local_name().to_vec();
        }
        let mut components = vec![];
        let mut current = self.escape_mount();
        while let Some(parent) = current.parent() {
            components.push(current.entry.local_name().to_vec());
            current = parent.escape_mount();
        }
        if components.is_empty() {
            return b"/".to_vec();
        }
        components.push(vec![]);
        components.reverse();
        components.join(&b'/')
    }

    pub fn mount(&self, what: WhatToMount, flags: MountFlags) -> Result<(), Errno> {
        let mountpoint = self.enter_mount();
        let mount = mountpoint.mount.as_ref().expect("a mountpoint must be part of a mount");
        mount.create_submount(&mountpoint.entry, what, flags);
        Ok(())
    }

    /// If this is the root of a filesystem, unmount. Otherwise return EINVAL.
    pub fn unmount(&self) -> Result<(), Errno> {
        let mountpoint = self.enter_mount().mountpoint().ok_or_else(|| errno!(EINVAL))?;
        let mount = mountpoint.mount.as_ref().expect("a mountpoint must be part of a mount");
        let mut mount_state = mount.state.write();
        // TODO(tbodt): EBUSY
        mount_state.submounts.remove(mountpoint.mount_hash_key()).ok_or_else(|| errno!(EINVAL))?;
        Ok(())
    }

    pub fn mount_eq(a: &NamespaceNode, b: &NamespaceNode) -> bool {
        a.mount.as_ref().map(Arc::as_ptr) == b.mount.as_ref().map(Arc::as_ptr)
    }

    fn with_new_entry(&self, entry: DirEntryHandle) -> NamespaceNode {
        NamespaceNode { mount: self.mount.clone(), entry }
    }

    fn mount_hash_key(&self) -> &ArcKey<DirEntry> {
        ArcKey::ref_cast(&self.entry)
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
    use crate::fs::tmpfs::TmpFs;
    use crate::testing::*;

    #[::fuchsia::test]
    fn test_namespace() -> anyhow::Result<()> {
        let (kernel, current_task) = create_kernel_and_task();
        let root_fs = TmpFs::new_fs(&kernel);
        let root_node = Arc::clone(root_fs.root());
        let _dev_node = root_node.create_dir(&current_task, b"dev").expect("failed to mkdir dev");
        let dev_fs = TmpFs::new_fs(&kernel);
        let dev_root_node = Arc::clone(dev_fs.root());
        let _dev_pts_node =
            dev_root_node.create_dir(&current_task, b"pts").expect("failed to mkdir pts");

        let ns = Namespace::new(root_fs);
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
        let pts_parent =
            pts.parent().ok_or_else(|| errno!(ENOENT)).expect("failed to get parent of pts");
        assert!(Arc::ptr_eq(&pts_parent.entry, &dev.entry));

        let dev_parent =
            dev.parent().ok_or_else(|| errno!(ENOENT)).expect("failed to get parent of dev");
        assert!(Arc::ptr_eq(&dev_parent.entry, &ns.root().entry));
        Ok(())
    }

    #[::fuchsia::test]
    fn test_mount_does_not_upgrade() -> anyhow::Result<()> {
        let (kernel, current_task) = create_kernel_and_task();
        let root_fs = TmpFs::new_fs(&kernel);
        let root_node = Arc::clone(root_fs.root());
        let _dev_node = root_node.create_dir(&current_task, b"dev").expect("failed to mkdir dev");
        let dev_fs = TmpFs::new_fs(&kernel);
        let dev_root_node = Arc::clone(dev_fs.root());
        let _dev_pts_node =
            dev_root_node.create_dir(&current_task, b"pts").expect("failed to mkdir pts");

        let ns = Namespace::new(root_fs);
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

    #[::fuchsia::test]
    fn test_path() -> anyhow::Result<()> {
        let (kernel, current_task) = create_kernel_and_task();
        let root_fs = TmpFs::new_fs(&kernel);
        let root_node = Arc::clone(root_fs.root());
        let _dev_node = root_node.create_dir(&current_task, b"dev").expect("failed to mkdir dev");
        let dev_fs = TmpFs::new_fs(&kernel);
        let dev_root_node = Arc::clone(dev_fs.root());
        let _dev_pts_node =
            dev_root_node.create_dir(&current_task, b"pts").expect("failed to mkdir pts");

        let ns = Namespace::new(root_fs);
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

    #[::fuchsia::test]
    fn test_shadowing() -> anyhow::Result<()> {
        let (kernel, current_task) = create_kernel_and_task();
        let root_fs = TmpFs::new_fs(&kernel);
        let ns = Namespace::new(root_fs.clone());
        let _foo_node = root_fs.root().create_dir(&current_task, b"foo")?;
        let mut context = LookupContext::default();
        let foo_dir = ns.root().lookup_child(&current_task, &mut context, b"foo")?;

        let foofs1 = TmpFs::new_fs(&kernel);
        foo_dir.mount(WhatToMount::Fs(foofs1.clone()), MountFlags::empty())?;
        let mut context = LookupContext::default();
        assert!(Arc::ptr_eq(
            &ns.root().lookup_child(&current_task, &mut context, b"foo")?.entry,
            foofs1.root()
        ));
        let foo_dir = ns.root().lookup_child(&current_task, &mut context, b"foo")?;

        let ns_clone = ns.clone_namespace();

        let foofs2 = TmpFs::new_fs(&kernel);
        foo_dir.mount(WhatToMount::Fs(foofs2.clone()), MountFlags::empty())?;
        let mut context = LookupContext::default();
        assert!(Arc::ptr_eq(
            &ns.root().lookup_child(&current_task, &mut context, b"foo")?.entry,
            foofs2.root()
        ));

        assert!(Arc::ptr_eq(
            &ns_clone
                .root()
                .lookup_child(&current_task, &mut LookupContext::default(), b"foo")?
                .entry,
            foofs1.root()
        ));

        Ok(())
    }

    #[::fuchsia::test]
    fn test_unlink_mounted_directory() -> anyhow::Result<()> {
        let (kernel, current_task) = create_kernel_and_task();
        let root_fs = TmpFs::new_fs(&kernel);
        let ns1 = Namespace::new(root_fs.clone());
        let ns2 = Namespace::new(root_fs.clone());
        let _foo_node = root_fs.root().create_dir(&current_task, b"foo")?;
        let mut context = LookupContext::default();
        let foo_dir = ns1.root().lookup_child(&current_task, &mut context, b"foo")?;

        let foofs = TmpFs::new_fs(&kernel);
        foo_dir.mount(WhatToMount::Fs(foofs), MountFlags::empty())?;

        assert_eq!(
            errno!(EBUSY),
            ns2.root().unlink(&current_task, b"foo", UnlinkKind::Directory).unwrap_err()
        );

        Ok(())
    }

    #[::fuchsia::test]
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
