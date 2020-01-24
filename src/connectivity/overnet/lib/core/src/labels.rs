// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[derive(Debug, PartialEq, Eq, PartialOrd, Ord, Clone, Copy)]
pub enum Endpoint {
    Client,
    Server,
}

impl Endpoint {
    pub fn quic_id_bit(&self) -> u64 {
        match self {
            Endpoint::Client => 0,
            Endpoint::Server => 1,
        }
    }

    pub fn opposite(&self) -> Endpoint {
        match self {
            Endpoint::Client => Endpoint::Server,
            Endpoint::Server => Endpoint::Client,
        }
    }
}

/// Labels a node with a mesh-unique address
#[derive(PartialEq, PartialOrd, Eq, Ord, Clone, Copy, Debug, Hash)]
pub struct NodeId(pub u64);

impl From<u64> for NodeId {
    fn from(id: u64) -> Self {
        NodeId(id)
    }
}

impl From<NodeId> for fidl_fuchsia_overnet_protocol::NodeId {
    fn from(id: NodeId) -> Self {
        Self { id: id.0 }
    }
}

/// Labels a link with a node-unique identifier
#[derive(PartialEq, PartialOrd, Eq, Ord, Clone, Copy, Debug, Hash)]
pub struct NodeLinkId(pub u64);

impl From<u64> for NodeLinkId {
    fn from(id: u64) -> Self {
        NodeLinkId(id)
    }
}
