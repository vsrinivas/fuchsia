// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::FxfsError,
        object_store::{
            directory::{Directory, ObjectDescriptor},
            HandleOptions, ObjectStore,
        },
        server::{directory::FxDirectory, file::FxFile, node::FxNode},
        volume::Volume,
    },
    anyhow::{anyhow, bail, Error},
    fuchsia_zircon::Status,
    std::{
        any::Any,
        collections::HashMap,
        sync::{Arc, Weak},
    },
    vfs::{
        filesystem::{Filesystem, FilesystemRename},
        path::Path,
    },
};

struct NodeCache(HashMap<u64, Weak<dyn FxNode>>);

impl NodeCache {
    fn add(&mut self, object_id: u64, node: Weak<dyn FxNode>) {
        // This is a programming error, so panic here.
        assert!(!self.0.contains_key(&object_id), "Duplicate node for {}", object_id);
        self.0.insert(object_id, node);
    }

    fn get(&mut self, object_id: u64) -> Result<Arc<dyn FxNode>, Error> {
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
    _graveyard: Directory,
}

impl FxVolume {
    pub fn store(&self) -> &ObjectStore {
        &self.store
    }

    pub async fn add_node(&self, object_id: u64, node: Weak<dyn FxNode>) {
        self.nodes.lock().await.add(object_id, node)
    }

    pub async fn open_node(&self, object_id: u64) -> Result<Arc<dyn FxNode>, Error> {
        self.nodes.lock().await.get(object_id)
    }

    /// Attempts to open a node in the node cache. If the node wasn't present in the cache, loads
    /// the object from the object store, installing the returned node into the cache and returns
    /// the newly created FxNode backed by the loaded object.
    pub async fn open_or_load_node(
        self: &Arc<Self>,
        object_id: u64,
        object_descriptor: ObjectDescriptor,
    ) -> Result<Arc<dyn FxNode>, Error> {
        let mut nodes = self.nodes.lock().await;
        match nodes.get(object_id) {
            Ok(node) => Ok(node),
            Err(e) if FxfsError::NotFound.matches(&e) => {
                let node = match object_descriptor {
                    ObjectDescriptor::File => {
                        let file =
                            self.store.open_object(object_id, HandleOptions::default()).await?;
                        Arc::new(FxFile::new(file)) as Arc<dyn FxNode>
                    }
                    ObjectDescriptor::Directory => {
                        let directory = self.store.open_directory(object_id).await?;
                        Arc::new(FxDirectory::new(self.clone(), directory)) as Arc<dyn FxNode>
                    }
                    _ => bail!(FxfsError::Inconsistent),
                };
                nodes.add(object_id, Arc::downgrade(&node));
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
    root: Arc<dyn FxNode>,
}

impl FxVolumeAndRoot {
    pub async fn new(volume: Volume) -> Self {
        let (store, root_directory, graveyard) = volume.into();
        let root_object_id = root_directory.object_id();
        let volume = Arc::new(FxVolume {
            nodes: futures::lock::Mutex::new(NodeCache(HashMap::new())),
            store,
            _graveyard: graveyard,
        });
        let root: Arc<dyn FxNode> = Arc::new(FxDirectory::new(volume.clone(), root_directory));
        volume.add_node(root_object_id, Arc::downgrade(&root)).await;
        Self { volume, root }
    }

    pub fn volume(&self) -> &Arc<FxVolume> {
        &self.volume
    }

    pub fn root(&self) -> Arc<FxDirectory> {
        self.root.clone().into_any().downcast::<FxDirectory>().expect("Invalid type for root")
    }

    pub(super) fn into_volume(self) -> Arc<FxVolume> {
        self.volume
    }
}
