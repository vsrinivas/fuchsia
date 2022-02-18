// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// TODO(bcastell): For fxfs though we need a metric collection system that can be compiled cross
// platform and is unintrusive to use, similar to how tracing is done. These metrics should be added
// to the "fs.detail" node since they will be filesystem specific, and not require any knowledge of
// how those metrics are exposed (unlike the ones defined in this file and in fs_test).
//
// TODO(bcastell): Move this file into the Rust VFS, and add a new metrics module to fxfs.
//

use {
    fuchsia_inspect::{LazyNode, Node},
    futures::FutureExt,
    std::sync::Weak,
};

pub const INFO_NODE_NAME: &'static str = "fs.info";
pub const USAGE_NODE_NAME: &'static str = "fs.usage";
pub const VOLUME_NODE_NAME: &'static str = "fs.volume";

/// Trait that Rust filesystems should implement to expose required Inspect data.
///
/// Once implemented, a filesystem can attach the Inspect data to a given root node by calling
/// `FsInspectTree::new` which will return ownership of the attached nodes/properties.
pub trait FsInspect {
    fn get_info_data(&self) -> InfoData;
    fn get_usage_data(&self) -> UsageData;
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
                    fs.get_info_data().record(inspector.root());
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
                    fs.get_usage_data().record(inspector.root());
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
                    fs.get_volume_data().record(inspector.root());
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
    pub name: &'static str,
    pub version_major: u64,
    pub version_minor: u64,
    pub oldest_minor_version: u64,
    pub block_size: u64,
    pub max_filename_length: u64,
}

impl InfoData {
    pub const ID_KEY: &'static str = "id";
    pub const FS_TYPE_KEY: &'static str = "type";
    pub const NAME_KEY: &'static str = "name";
    pub const VERSION_MAJOR_KEY: &'static str = "version_major";
    pub const VERSION_MINOR_KEY: &'static str = "version_minor";
    pub const OLDEST_MINOR_VERSION_KEY: &'static str = "oldest_minor_version";
    pub const BLOCK_SIZE: &'static str = "block_size";
    pub const MAX_FILENAME_LENGTH: &'static str = "max_filename_length";

    fn record(self, node: &Node) {
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
    pub const TOTAL_BYTES_KEY: &'static str = "total_bytes";
    pub const USED_BYTES_KEY: &'static str = "used_bytes";
    pub const TOTAL_NODES_KEY: &'static str = "total_nodes";
    pub const USED_NODES_KEY: &'static str = "used_nodes";

    fn record(self, node: &Node) {
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
    pub out_of_space_events: u64,
}

impl VolumeData {
    pub const SIZE_BYTES_KEY: &'static str = "size_bytes";
    pub const SIZE_LIMIT_BYTES_KEY: &'static str = "size_limit_bytes";
    pub const AVAILABLE_SPACE_BYTES_KEY: &'static str = "available_space_bytes";
    pub const OUT_OF_SPACE_EVENTS_KEY: &'static str = "out_of_space_events";

    fn record(self, node: &Node) {
        node.record_uint(Self::SIZE_BYTES_KEY, self.size_bytes);
        node.record_uint(Self::SIZE_LIMIT_BYTES_KEY, self.size_limit_bytes);
        node.record_uint(Self::AVAILABLE_SPACE_BYTES_KEY, self.available_space_bytes);
        node.record_uint(Self::OUT_OF_SPACE_EVENTS_KEY, self.out_of_space_events);
    }
}
