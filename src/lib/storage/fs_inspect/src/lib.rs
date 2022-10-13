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
    std::{string::String, sync::Weak},
};

const INFO_NODE_NAME: &'static str = "fs.info";
const USAGE_NODE_NAME: &'static str = "fs.usage";

/// Trait that Rust filesystems should implement to expose required Inspect data.
///
/// Once implemented, a filesystem can attach the Inspect data to a given root node by calling
/// `FsInspectTree::new` which will return ownership of the attached nodes/properties.
#[async_trait]
pub trait FsInspect {
    fn get_info_data(&self) -> InfoData;
    async fn get_usage_data(&self) -> UsageData;
}

/// Maintains ownership of the various inspect nodes/properties. Will be removed from the root node
/// they were attached to when dropped.
pub struct FsInspectTree {
    _info: LazyNode,
    _usage: LazyNode,
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

        FsInspectTree { _info: info_node, _usage: usage_node }
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
    const ID_KEY: &'static str = "id";
    const FS_TYPE_KEY: &'static str = "type";
    const NAME_KEY: &'static str = "name";
    const VERSION_MAJOR_KEY: &'static str = "version_major";
    const VERSION_MINOR_KEY: &'static str = "version_minor";
    const BLOCK_SIZE: &'static str = "block_size";
    const MAX_FILENAME_LENGTH: &'static str = "max_filename_length";
    const OLDEST_VERSION_KEY: &'static str = "oldest_version";

    fn record_into(self, node: &Node) {
        node.record_uint(Self::ID_KEY, self.id);
        node.record_uint(Self::FS_TYPE_KEY, self.fs_type);
        node.record_string(Self::NAME_KEY, self.name);
        node.record_uint(Self::VERSION_MAJOR_KEY, self.version_major);
        node.record_uint(Self::VERSION_MINOR_KEY, self.version_minor);
        node.record_uint(Self::BLOCK_SIZE, self.block_size);
        node.record_uint(Self::MAX_FILENAME_LENGTH, self.max_filename_length);
        if self.oldest_version.is_some() {
            node.record_string(Self::OLDEST_VERSION_KEY, self.oldest_version.as_ref().unwrap());
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
    const TOTAL_BYTES_KEY: &'static str = "total_bytes";
    const USED_BYTES_KEY: &'static str = "used_bytes";
    const TOTAL_NODES_KEY: &'static str = "total_nodes";
    const USED_NODES_KEY: &'static str = "used_nodes";

    fn record_into(self, node: &Node) {
        node.record_uint(Self::TOTAL_BYTES_KEY, self.total_bytes);
        node.record_uint(Self::USED_BYTES_KEY, self.used_bytes);
        node.record_uint(Self::TOTAL_NODES_KEY, self.total_nodes);
        node.record_uint(Self::USED_NODES_KEY, self.used_nodes);
    }
}
