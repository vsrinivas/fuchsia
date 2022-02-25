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
    fuchsia_inspect::{LazyNode, Node},
    futures::FutureExt,
    std::{string::String, sync::Weak},
};

const INFO_NODE_NAME: &'static str = "fs.info";
const USAGE_NODE_NAME: &'static str = "fs.usage";
const VOLUME_NODE_NAME: &'static str = "fs.volume";

/// Trait that Rust filesystems should implement to expose required Inspect data.
///
/// Once implemented, a filesystem can attach the Inspect data to a given root node by calling
/// `FsInspectTree::new` which will return ownership of the attached nodes/properties.
pub trait FsInspect {
    fn get_info_data(&self) -> InfoData;
    fn get_usage_data(&self) -> UsageData;
    // TODO(fxbug.dev/85419): Provide default impl for non-FVM based filesystems.
    fn get_volume_data(&self) -> VolumeData;
}

/// Maintains ownership of the various inspect nodes/properties. Will be removed from the root node
/// they were attached to when dropped.
pub struct FsInspectTree {
    _info: LazyNode,
    _usage: LazyNode,
    _volume: LazyNode,
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
                    fs.get_usage_data().record_into(inspector.root());
                }
                Ok(inspector)
            }
            .boxed()
        });

        let fs_clone = fs.clone();
        let volume_node = root.create_lazy_child(VOLUME_NODE_NAME, move || {
            let fs_clone = fs_clone.clone();
            async move {
                let inspector = fuchsia_inspect::Inspector::new();
                if let Some(fs) = fs_clone.upgrade() {
                    fs.get_volume_data().record_into(inspector.root());
                }
                Ok(inspector)
            }
            .boxed()
        });

        FsInspectTree { _info: info_node, _usage: usage_node, _volume: volume_node }
    }
}

/// fs.info Properties
pub struct InfoData {
    pub id: u64,
    pub fs_type: u64,
    pub name: String,
    pub version_major: u64,
    pub version_minor: u64,
    pub oldest_minor_version: u64,
    pub block_size: u64,
    pub max_filename_length: u64,
}

impl InfoData {
    const ID_KEY: &'static str = "id";
    const FS_TYPE_KEY: &'static str = "type";
    const NAME_KEY: &'static str = "name";
    const VERSION_MAJOR_KEY: &'static str = "version_major";
    const VERSION_MINOR_KEY: &'static str = "version_minor";
    const OLDEST_MINOR_VERSION_KEY: &'static str = "oldest_minor_version";
    const BLOCK_SIZE: &'static str = "block_size";
    const MAX_FILENAME_LENGTH: &'static str = "max_filename_length";

    fn record_into(self, node: &Node) {
        node.record_uint(Self::ID_KEY, self.id);
        node.record_uint(Self::FS_TYPE_KEY, self.fs_type);
        node.record_string(Self::NAME_KEY, self.name);
        node.record_uint(Self::VERSION_MAJOR_KEY, self.version_major);
        node.record_uint(Self::VERSION_MINOR_KEY, self.version_minor);
        node.record_uint(Self::OLDEST_MINOR_VERSION_KEY, self.oldest_minor_version);
        node.record_uint(Self::BLOCK_SIZE, self.block_size);
        node.record_uint(Self::MAX_FILENAME_LENGTH, self.max_filename_length);
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

/// fs.volume Properties.
pub struct VolumeData {
    pub size_bytes: u64,
    pub size_limit_bytes: u64,
    pub available_space_bytes: u64,
    // TODO(fxbug.dev/85419): Move out_of_space_events to fs.usage, and rename node to fs.fvm_stats.
    pub out_of_space_events: u64,
}

impl VolumeData {
    const SIZE_BYTES_KEY: &'static str = "size_bytes";
    const SIZE_LIMIT_BYTES_KEY: &'static str = "size_limit_bytes";
    const AVAILABLE_SPACE_BYTES_KEY: &'static str = "available_space_bytes";
    const OUT_OF_SPACE_EVENTS_KEY: &'static str = "out_of_space_events";

    fn record_into(self, node: &Node) {
        node.record_uint(Self::SIZE_BYTES_KEY, self.size_bytes);
        node.record_uint(Self::SIZE_LIMIT_BYTES_KEY, self.size_limit_bytes);
        node.record_uint(Self::AVAILABLE_SPACE_BYTES_KEY, self.available_space_bytes);
        node.record_uint(Self::OUT_OF_SPACE_EVENTS_KEY, self.out_of_space_events);
    }
}
