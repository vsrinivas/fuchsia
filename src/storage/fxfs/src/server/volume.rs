// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        object_store::ObjectStore,
        server::{directory::FxDirectory, node::FxNode},
        volume::Volume,
    },
    anyhow::{anyhow, Error},
    fuchsia_zircon::Status,
    std::{
        any::Any,
        collections::HashMap,
        sync::{Arc, Mutex},
    },
    vfs::{
        filesystem::{Filesystem, FilesystemRename},
        path::Path,
    },
};
/// FxVolume represents an opened volume. This also serves as the cache for all currently opened
/// nodes within the volume.
pub struct FxVolume {
    nodes: Mutex<HashMap<u64, FxNode>>,
    store: Arc<ObjectStore>,
}

impl FxVolume {
    pub fn store(&self) -> &ObjectStore {
        &self.store
    }

    pub fn add_node(&self, object_id: u64, node: FxNode) -> Result<FxNode, Error> {
        let mut nodes = self.nodes.lock().unwrap();
        // This is a programming error, so panic here.
        assert!(!nodes.contains_key(&object_id), "Duplicate node for {}", object_id);
        nodes.insert(object_id, node.clone());
        Ok(node)
    }

    pub fn open_node(&self, object_id: u64) -> Result<FxNode, Error> {
        self.nodes
            .lock()
            .unwrap()
            .get(&object_id)
            .ok_or(anyhow!("No node found with oid {}", object_id))
            .map(|x| x.clone())
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
        let volume = Arc::new(FxVolume { nodes: Mutex::new(HashMap::new()), store });
        let root = FxNode::Dir(Arc::new(FxDirectory::new(volume.clone(), root_directory)));
        Self { volume, root }
    }

    pub fn volume(&self) -> &Arc<FxVolume> {
        &self.volume
    }

    pub fn root(&self) -> &Arc<FxDirectory> {
        if let FxNode::Dir(dir) = &self.root {
            dir
        } else {
            panic!("Invalid type for root");
        }
    }
}
