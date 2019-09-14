// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Different kinds of registries allow nodes to interact with on another.

pub mod inode_registry;
pub mod token_registry;

use crate::directory::{entry::DirectoryEntry, entry_container::DirectlyMutable};

use {
    fidl::Handle,
    fuchsia_zircon::{Rights, Status},
    std::sync::Arc,
};

pub trait TokenRegistryClient: DirectoryEntry + DirectlyMutable + Send + Sync {}

impl<T> TokenRegistryClient for T where T: DirectoryEntry + DirectlyMutable + Send + Sync {}

pub const DEFAULT_TOKEN_RIGHTS: Rights = Rights::BASIC;

/// A `TokenRegistry` allows directories to perform "cross-directory" operations, such as renaming
/// across directories and linking entries.
pub trait TokenRegistry {
    fn get_token(&self, container: Arc<dyn TokenRegistryClient>) -> Result<Handle, Status>;
    fn get_container(&self, token: Handle) -> Result<Option<Arc<dyn TokenRegistryClient>>, Status>;
    fn unregister(&self, container: Arc<dyn TokenRegistryClient>);
}

pub trait InodeRegistryClient: DirectoryEntry + Send + Sync {}

impl<T> InodeRegistryClient for T where T: DirectoryEntry + Send + Sync {}

/// An `InodeRegistry` issues "inode ids" to `DirectoryEntry` objects, unique to the scope of the
/// registry.
pub trait InodeRegistry {
    fn get_inode(&self, node: Arc<dyn InodeRegistryClient>) -> u64;
    fn unregister(&self, node: Arc<dyn InodeRegistryClient>);
}
