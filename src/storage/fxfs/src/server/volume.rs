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
        server::{
            directory::FxDirectory,
            file::FxFile,
            node::{FxNode, GetResult, NodeCache},
        },
        volume::Volume,
    },
    anyhow::{bail, Error},
    async_trait::async_trait,
    fuchsia_zircon::Status,
    std::{any::Any, sync::Arc},
    vfs::{
        filesystem::{Filesystem, FilesystemRename},
        path::Path,
    },
};

/// FxVolume represents an opened volume. It is also a (weak) cache for all opened Nodes within the
/// volume.
pub struct FxVolume {
    cache: NodeCache,
    store: Arc<ObjectStore>,
    graveyard: Directory,
}

impl FxVolume {
    pub fn new(store: Arc<ObjectStore>, graveyard: Directory) -> Self {
        Self { cache: NodeCache::new(), store, graveyard }
    }

    pub fn store(&self) -> &Arc<ObjectStore> {
        &self.store
    }

    pub fn graveyard(&self) -> &Directory {
        &self.graveyard
    }

    pub fn cache(&self) -> &NodeCache {
        &self.cache
    }

    /// Attempts to get a node from the node cache. If the node wasn't present in the cache, loads
    /// the object from the object store, installing the returned node into the cache and returns the
    /// newly created FxNode backed by the loaded object.
    pub async fn get_or_load_node(
        self: &Arc<Self>,
        object_id: u64,
        object_descriptor: ObjectDescriptor,
    ) -> Result<Arc<dyn FxNode>, Error> {
        match self.cache.get_or_reserve(object_id).await {
            GetResult::Node(node) => Ok(node),
            GetResult::Placeholder(placeholder) => {
                let node = match object_descriptor {
                    ObjectDescriptor::File => {
                        let file =
                            self.store.open_object(object_id, HandleOptions::default()).await?;
                        Arc::new(FxFile::new(file, self.clone())) as Arc<dyn FxNode>
                    }
                    ObjectDescriptor::Directory => {
                        let directory = self.store.open_directory(object_id).await?;
                        Arc::new(FxDirectory::new(self.clone(), directory)) as Arc<dyn FxNode>
                    }
                    _ => bail!(FxfsError::Inconsistent),
                };
                placeholder.commit(&node);
                Ok(node)
            }
        }
    }
}

#[async_trait]
impl FilesystemRename for FxVolume {
    async fn rename(
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
        let volume = Arc::new(FxVolume::new(store, graveyard));
        let root: Arc<dyn FxNode> = Arc::new(FxDirectory::new(volume.clone(), root_directory));
        match volume.cache.get_or_reserve(root_object_id).await {
            GetResult::Node(_) => unreachable!(),
            GetResult::Placeholder(placeholder) => placeholder.commit(&root),
        }
        Self { volume, root }
    }

    pub fn volume(&self) -> &Arc<FxVolume> {
        &self.volume
    }

    pub fn root(&self) -> Arc<FxDirectory> {
        self.root.clone().into_any().downcast::<FxDirectory>().expect("Invalid type for root")
    }

    #[cfg(test)]
    pub(super) fn into_volume(self) -> Arc<FxVolume> {
        self.volume
    }
}
