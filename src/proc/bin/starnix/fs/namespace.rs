// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::hash::{Hash, Hasher};
use std::sync::{Arc, Weak};

use parking_lot::RwLock;

use super::{FsNodeHandle, FsStr};
use crate::types::*;

/// A mount namespace.
pub struct Namespace {
    root_mount: RwLock<Option<MountHandle>>,
    mount_points: RwLock<HashMap<NamespaceNode, MountHandle>>,
}

impl Namespace {
    pub fn new(root: FsNodeHandle) -> Arc<Namespace> {
        // TODO(tbodt): We can avoid this RwLock<Option thing by using Arc::new_cyclic, but that's
        // unstable.
        let namespace = Arc::new(Self { root_mount: RwLock::new(None), mount_points: RwLock::new(HashMap::new()) });
        *namespace.root_mount.write() = Some(Arc::new(Mount { namespace: Arc::downgrade(&namespace), mountpoint: None, root }));
        namespace
    }
    pub fn root(&self) -> NamespaceNode {
        self.root_mount.read().as_ref().unwrap().root()
    }
}

pub struct Mount {
    namespace: Weak<Namespace>,
    mountpoint: Option<(Weak<Mount>, FsNodeHandle)>,
    root: FsNodeHandle,
}
pub type MountHandle = Arc<Mount>;

impl Mount {
    pub fn root(self: &MountHandle) -> NamespaceNode {
        NamespaceNode { mount: Arc::clone(self), node: Arc::clone(&self.root) }
    }

    fn mountpoint(&self) -> Option<NamespaceNode> {
        let (ref mount, ref node) = &self.mountpoint.as_ref()?;
        Some(NamespaceNode { mount: mount.upgrade()?, node: node.clone() })
    }
}

/// A node in a mount namespace. This tree is a composite of the mount tree and the FsNode tree.
#[derive(Clone)]
pub struct NamespaceNode {
    pub mount: MountHandle,
    pub node: FsNodeHandle,
}

impl NamespaceNode {
    pub fn lookup(&self, name: &FsStr) -> Result<NamespaceNode, Errno> {
        let child = self.with_new_node(self.node.component_lookup(name)?);
        if let Some(mount) = self.namespace().mount_points.read().get(&child) {
            Ok(mount.root())
        } else {
            Ok(child)
        }
    }

    pub fn parent(&self) -> Option<NamespaceNode> {
        if Arc::ptr_eq(&self.node, &self.mount.root) {
            self.mount.mountpoint()
        } else {
            Some(self.with_new_node(self.node.parent()?.clone()))
        }
    }

    #[cfg(test)]
    pub fn mount(&self, node: &FsNodeHandle) -> Result<(), Errno> {
        match self.namespace().mount_points.write().entry(self.clone()) {
            Entry::Occupied(_) => {
                log::info!("mount shadowing is unimplemented");
                Err(EBUSY)
            }
            Entry::Vacant(v) => {
                v.insert(Arc::new(Mount {
                    namespace: self.mount.namespace.clone(),
                    mountpoint: Some((Arc::downgrade(&self.mount), self.node.clone())),
                    root: node.clone(),
                }));
                Ok(())
            }
        }
    }

    fn with_new_node(&self, node: FsNodeHandle) -> NamespaceNode {
        NamespaceNode { mount: self.mount.clone(), node }
    }

    fn namespace(&self) -> Arc<Namespace> {
        self.mount.namespace.upgrade().expect("namespace was dropped before its mounts!")
    }
}

// Eq/Hash impls intended for the MOUNT_POINTS hash
impl PartialEq for NamespaceNode {
    fn eq(&self, other: &Self) -> bool {
        Arc::ptr_eq(&self.mount, &other.mount) && Arc::ptr_eq(&self.node, &other.node)
    }
}
impl Eq for NamespaceNode {}
impl Hash for NamespaceNode {
    fn hash<H: Hasher>(&self, state: &mut H) {
        Arc::as_ptr(&self.mount).hash(state);
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
