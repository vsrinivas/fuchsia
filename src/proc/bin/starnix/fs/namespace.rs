// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::hash::{Hash, Hasher};
use std::sync::{Arc, Weak};

use parking_lot::RwLock;

use super::{FileHandle, FileObject, FsNodeHandle, FsStr};
use crate::types::*;

/// A mount namespace.
///
/// The namespace records at which entries filesystems are mounted.
pub struct Namespace {
    root_mount: RwLock<Option<MountHandle>>,
    mount_points: RwLock<HashMap<NamespaceNode, MountHandle>>,
}

impl Namespace {
    pub fn new(root: FsNodeHandle) -> Arc<Namespace> {
        // TODO(tbodt): We can avoid this RwLock<Option thing by using Arc::new_cyclic, but that's
        // unstable.
        let namespace = Arc::new(Self {
            root_mount: RwLock::new(None),
            mount_points: RwLock::new(HashMap::new()),
        });
        *namespace.root_mount.write() =
            Some(Arc::new(Mount { namespace: Arc::downgrade(&namespace), mountpoint: None, root }));
        namespace
    }
    pub fn root(&self) -> NamespaceNode {
        self.root_mount.read().as_ref().unwrap().root()
    }
}

/// An instance of a filesystem mounted in a namespace.
///
/// At a mount, path traversal switches from one filesystem to another.
/// The client sees a composed directory structure that glues together the
/// directories from the underlying FsNodes from those filesystems.
pub struct Mount {
    namespace: Weak<Namespace>,
    mountpoint: Option<(Weak<Mount>, FsNodeHandle)>,
    root: FsNodeHandle,
}
pub type MountHandle = Arc<Mount>;

impl Mount {
    pub fn root(self: &MountHandle) -> NamespaceNode {
        NamespaceNode { mount: Some(Arc::clone(self)), node: Arc::clone(&self.root) }
    }

    fn mountpoint(&self) -> Option<NamespaceNode> {
        let (ref mount, ref node) = &self.mountpoint.as_ref()?;
        Some(NamespaceNode { mount: Some(mount.upgrade()?), node: node.clone() })
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

    /// The FsNode that cooresponds to this namespace entry.
    pub node: FsNodeHandle,
}

impl NamespaceNode {
    /// Create a namespace node that is not mounted in a namespace.
    ///
    /// The returned node does not have a name.
    pub fn new_unmounted(node: FsNodeHandle) -> Self {
        Self { mount: None, node }
    }

    /// Create a FileObject cooresponding to this namespace node.
    ///
    /// This function is the primary way of instantiating FileObjects. Each
    /// FileObject records the NamespaceNode that created it in order to
    /// remember its path in the Namespace.
    pub fn open(&self) -> Result<FileHandle, Errno> {
        Ok(FileObject::new(self.node.open()?, self.clone()))
    }

    /// Traverse down a parent-to-child link in the namespace.
    ///
    /// This traversal matches the parent-to-child link in the underlying
    /// FsNode except at mount points, where the link switches from one
    /// filesystem to another.
    pub fn lookup(&self, name: &FsStr) -> Result<NamespaceNode, Errno> {
        let child = self.with_new_node(self.node.component_lookup(name)?);
        if let Some(namespace) = self.namespace() {
            if let Some(mount) = namespace.mount_points.read().get(&child) {
                return Ok(mount.root());
            }
        }
        Ok(child)
    }

    /// Traverse up a child-to-parent link in the namespace.
    ///
    /// This traversal matches the child-to-parent link in the underlying
    /// FsNode except at mount points, where the link switches from one
    /// filesystem to another.
    pub fn parent(&self) -> Option<NamespaceNode> {
        if let Some(mount) = &self.mount {
            if Arc::ptr_eq(&self.node, &mount.root) {
                return mount.mountpoint();
            }
        }
        Some(self.with_new_node(self.node.parent()?.clone()))
    }

    #[cfg(test)]
    pub fn mount(&self, node: &FsNodeHandle) -> Result<(), Errno> {
        if let Some(namespace) = self.namespace() {
            match namespace.mount_points.write().entry(self.clone()) {
                Entry::Occupied(_) => {
                    log::warn!("mount shadowing is unimplemented");
                    Err(EBUSY)
                }
                Entry::Vacant(v) => {
                    let mount = self.mount.as_ref().unwrap();
                    v.insert(Arc::new(Mount {
                        namespace: mount.namespace.clone(),
                        mountpoint: Some((Arc::downgrade(&mount), self.node.clone())),
                        root: node.clone(),
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
    use crate::fs::tmp::new_tmpfs;
    #[test]
    fn test_namespace() -> anyhow::Result<()> {
        let root_node = new_tmpfs();
        let _dev_node = root_node.mkdir(b"dev".to_vec())?;
        let dev_root_node = new_tmpfs();
        let _dev_pts_node = dev_root_node.mkdir(b"pts".to_vec())?;

        let ns = Namespace::new(root_node);
        ns.root().lookup(b"dev")?.mount(&dev_root_node)?;

        ns.root().lookup(b"dev")?.lookup(b"pts")?;
        Ok(())
    }
}
