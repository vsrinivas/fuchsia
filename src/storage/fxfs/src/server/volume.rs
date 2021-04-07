// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::FxfsError,
        object_store::{directory::ObjectDescriptor, ObjectStore},
        server::{
            directory::FxDirectory,
            node::{FxNode, WeakFxNode},
        },
        volume::Volume,
    },
    anyhow::{anyhow, bail, Error},
    fuchsia_zircon::Status,
    std::{any::Any, collections::HashMap, sync::Arc},
    vfs::{
        filesystem::{Filesystem, FilesystemRename},
        path::Path,
    },
};

struct NodeCache(HashMap<u64, WeakFxNode>);

impl NodeCache {
    fn add(&mut self, object_id: u64, node: WeakFxNode) {
        // This is a programming error, so panic here.
        assert!(!self.0.contains_key(&object_id), "Duplicate node for {}", object_id);
        self.0.insert(object_id, node);
    }

    fn get(&mut self, object_id: u64) -> Result<FxNode, Error> {
        if let Some(node) = self.0.get(&object_id) {
            if let Some(node) = node.upgrade() {
                Ok(node)
            } else {
                // TODO(jfsulliv): This should be done in FxNode::drop (or FxDirectory/FxFile).
                self.0.remove(&object_id);
                Err(anyhow!(FxfsError::NotFound).context("Node was closed"))
            }
        } else {
            Err(anyhow!(FxfsError::NotFound).context(format!("No node with id {}", object_id)))
        }
    }
}

/// FxVolume represents an opened volume. It is also a (weak) cache for all opened Nodes within the
/// volume.
pub struct FxVolume {
    // We need a futures-aware mutex here to make open_or_load_node atomic, since it might require
    // asynchronously loading the node and then inserting it into the cache.
    // TODO(jfsulliv): This is horribly inefficient. Fix this by inserting a placeholder object that
    // we then go update in place.
    nodes: futures::lock::Mutex<NodeCache>,
    store: Arc<ObjectStore>,
}

impl FxVolume {
    pub fn store(&self) -> &ObjectStore {
        &self.store
    }

    pub async fn add_node(&self, object_id: u64, node: WeakFxNode) {
        self.nodes.lock().await.add(object_id, node)
    }

    pub async fn open_node(&self, object_id: u64) -> Result<FxNode, Error> {
        self.nodes.lock().await.get(object_id)
    }

    pub async fn open_or_load_node(
        self: &Arc<Self>,
        object_id: u64,
        object_descriptor: ObjectDescriptor,
    ) -> Result<FxNode, Error> {
        let mut nodes = self.nodes.lock().await;
        match nodes.get(object_id) {
            Ok(node) => Ok(node),
            Err(e) if FxfsError::NotFound.matches(&e) => {
                let node = match object_descriptor {
                    ObjectDescriptor::File => bail!("Files not implemented yet"),
                    ObjectDescriptor::Directory => {
                        let directory = self.store.open_directory(object_id).await?;
                        FxNode::Dir(Arc::new(FxDirectory::new(self.clone(), directory)))
                    }
                    _ => bail!(FxfsError::Inconsistent),
                };
                nodes.add(object_id, node.downgrade());
                Ok(node)
            }
            Err(e) => return Err(e),
        }
    }
}

impl FilesystemRename for FxVolume {
    fn rename(
        &self,
        _src_dir: Arc<dyn Any + Sync + Send + 'static>,
        _src_name: Path,
        _dst_dir: Arc<dyn Any + Sync + Send + 'static>,
        _dst_name: Path,
    ) -> Result<(), Status> {
        Err(Status::NOT_SUPPORTED)
    }
}

impl Filesystem for FxVolume {}

pub struct FxVolumeAndRoot {
    volume: Arc<FxVolume>,
    root: FxNode,
}

impl FxVolumeAndRoot {
    pub fn new(volume: Volume) -> Self {
        let (store, root_directory) = volume.into();
        let volume = Arc::new(FxVolume {
            nodes: futures::lock::Mutex::new(NodeCache(HashMap::new())),
            store,
        });
        let root = FxNode::Dir(Arc::new(FxDirectory::new(volume.clone(), root_directory)));
        Self { volume, root }
    }

    pub fn volume(&self) -> &Arc<FxVolume> {
        &self.volume
    }

    pub fn root(&self) -> &Arc<FxDirectory> {
        let FxNode::Dir(dir) = &self.root;
        dir
    }

    pub(super) fn into_volume(self) -> Arc<FxVolume> {
        self.volume
    }
}
