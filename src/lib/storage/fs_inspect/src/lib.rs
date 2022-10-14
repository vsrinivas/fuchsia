// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains the `FsInspect` trait which filesystems can implement in order to expose
//! Inspect metrics in a standardized hierarchy. Once `FsInspect` has been implemented, a
//! filesystem can attach itself to a root node via `FsInspectTree::new`.
//!
//! A filesystem's inspect tree can be tested via `fs_test` by enabling the `supports_inspect`
//! option. This will validate that the inspect tree hierarchy is consistent and that basic
//! information is reported correctly. See `src/storage/fs_test/inspect.cc` for details.

use {
    async_trait::async_trait,
    fuchsia_inspect::{LazyNode, Node},
    futures::FutureExt,
    std::{
        collections::HashMap,
        string::String,
        sync::{Mutex, Weak},
    },
};

const INFO_NODE_NAME: &'static str = "fs.info";
const USAGE_NODE_NAME: &'static str = "fs.usage";
const VOLUMES_NODE_NAME: &'static str = "fs.volumes";

/// Trait that Rust filesystems should implement to expose required Inspect data.
///
/// Once implemented, a filesystem can attach the Inspect data to a given root node by calling
/// `FsInspectTree::new` which will return ownership of the attached nodes/properties.
#[async_trait]
pub trait FsInspect {
    fn get_info_data(&self) -> InfoData;
    async fn get_usage_data(&self) -> UsageData;
}

/// Trait that Rust filesystems which are multi-volume should implement for each volume.
#[async_trait]
pub trait FsInspectVolume {
    async fn get_volume_data(&self) -> VolumeData;
}

/// Maintains ownership of the various inspect nodes/properties. Will be removed from the root node
/// they were attached to when dropped.
pub struct FsInspectTree {
    _info: LazyNode,
    _usage: LazyNode,
    volumes_node: Node,
    volumes: Mutex<HashMap<String, LazyNode>>,
}

impl FsInspectTree {
    /// Attaches Inspect nodes following a standard hierarchy, returning ownership of the newly
    /// created LazyNodes.
    pub fn new(fs: Weak<dyn FsInspect + Send + Sync + 'static>, root: &Node) -> FsInspectTree {
        let fs_clone = fs.clone();
        let info_node = root.create_lazy_child(INFO_NODE_NAME, move || {
            let fs_clone = fs_clone.clone();
            async move {
                let inspector = fuchsia_inspect::Inspector::new();
                if let Some(fs) = fs_clone.upgrade() {
                    fs.get_info_data().record_into(inspector.root());
                }
                Ok(inspector)
            }
            .boxed()
        });

        let fs_clone = fs.clone();
        let usage_node = root.create_lazy_child(USAGE_NODE_NAME, move || {
            let fs_clone = fs_clone.clone();
            async move {
                let inspector = fuchsia_inspect::Inspector::new();
                if let Some(fs) = fs_clone.upgrade() {
                    fs.get_usage_data().await.record_into(inspector.root());
                }
                Ok(inspector)
            }
            .boxed()
        });

        let volumes_node = root.create_child(VOLUMES_NODE_NAME);

        FsInspectTree {
            _info: info_node,
            _usage: usage_node,
            volumes_node,
            volumes: Mutex::new(HashMap::new()),
        }
    }

    /// Registers a provider for per-volume data.  If `volume` is dropped, the node will remain
    /// present in the inspect tree but yield no data, until `Self::unregister_volume` is called.
    pub fn register_volume(
        &self,
        name: String,
        volume: Weak<dyn FsInspectVolume + Send + Sync + 'static>,
    ) {
        self.volumes.lock().unwrap().insert(
            name.clone(),
            self.volumes_node.create_lazy_child(name, move || {
                let volume = volume.clone();
                async move {
                    let inspector = fuchsia_inspect::Inspector::new();
                    if let Some(volume) = volume.upgrade() {
                        volume.get_volume_data().await.record_into(inspector.root());
                    }
                    Ok(inspector)
                }
                .boxed()
            }),
        );
    }

    pub fn unregister_volume(&self, name: String) {
        self.volumes.lock().unwrap().remove(&name);
    }
}

/// fs.info Properties
pub struct InfoData {
    pub id: u64,
    pub fs_type: u64,
    pub name: String,
    pub version_major: u64,
    pub version_minor: u64,
    pub block_size: u64,
    pub max_filename_length: u64,
    pub oldest_version: Option<String>,
}

impl InfoData {
    fn record_into(self, node: &Node) {
        node.record_uint("id", self.id);
        node.record_uint("type", self.fs_type);
        node.record_string("name", self.name);
        node.record_uint("version_major", self.version_major);
        node.record_uint("version_minor", self.version_minor);
        node.record_uint("block_size", self.block_size);
        node.record_uint("max_filename_length", self.max_filename_length);
        if self.oldest_version.is_some() {
            node.record_string("oldest_version", self.oldest_version.as_ref().unwrap());
        }
    }
}

/// fs.usage Properties
pub struct UsageData {
    pub total_bytes: u64,
    pub used_bytes: u64,
    pub total_nodes: u64,
    pub used_nodes: u64,
}

impl UsageData {
    fn record_into(self, node: &Node) {
        node.record_uint("total_bytes", self.total_bytes);
        node.record_uint("used_bytes", self.used_bytes);
        node.record_uint("total_nodes", self.total_nodes);
        node.record_uint("used_nodes", self.used_nodes);
    }
}

/// fs.volume/{name} roperties
pub struct VolumeData {
    pub used_bytes: u64,
    pub bytes_limit: Option<u64>,
    pub used_nodes: u64,
    pub encrypted: bool,
}

impl VolumeData {
    fn record_into(self, node: &Node) {
        node.record_uint("used_bytes", self.used_bytes);
        if let Some(bytes_limit) = self.bytes_limit {
            node.record_uint("bytes_limit", bytes_limit);
        }
        node.record_uint("used_nodes", self.used_nodes);
        node.record_bool("encrypted", self.encrypted);
    }
}
