// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Labels a node with a mesh-unique address
#[derive(PartialEq, PartialOrd, Eq, Ord, Clone, Copy, Debug)]
pub struct NodeId(pub u64);

impl From<u64> for NodeId {
    fn from(id: u64) -> Self {
        NodeId(id)
    }
}

/// Labels a link with a node-unique identifier
#[derive(PartialEq, PartialOrd, Eq, Ord, Clone, Copy, Debug)]
pub struct LinkId(pub u64);

impl From<u64> for LinkId {
    fn from(id: u64) -> Self {
        LinkId(id)
    }
}

/// Labels one version of some state with a monotonically incrementing counter
#[derive(PartialEq, PartialOrd, Eq, Ord, Clone, Copy, Debug)]
pub struct VersionCounter(pub u64);

impl From<u64> for VersionCounter {
    fn from(id: u64) -> Self {
        VersionCounter(id)
    }
}

/// This state has been deleted and will receive no more updates
pub const TOMBSTONE_VERSION: VersionCounter = VersionCounter(std::u64::MAX);
/// This state has been synthesized and nothing authoritative has yet been received
pub const INITIAL_VERSION: VersionCounter = VersionCounter(0);
/// This is the first authoritative version of this state
pub const FIRST_VERSION: VersionCounter = VersionCounter(1);
