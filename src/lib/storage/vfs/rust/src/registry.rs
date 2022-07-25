// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Different kinds of registries allow nodes to interact with on another.

pub mod inode_registry;
pub mod token_registry;

use crate::directory::entry::DirectoryEntry;

use {fuchsia_zircon::Rights, std::sync::Arc};

pub const DEFAULT_TOKEN_RIGHTS: Rights = Rights::BASIC;

/// An `InodeRegistry` issues "inode ids" to `DirectoryEntry` objects, unique to the scope of the
/// registry.
pub trait InodeRegistry {
    fn get_inode(&self, node: Arc<dyn DirectoryEntry>) -> u64;
    fn unregister(&self, node: Arc<dyn DirectoryEntry>);
}
