// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Main Overnet functionality.

#![deny(missing_docs)]

mod coding;
mod labels;
mod node;
mod node_table;
mod ping_tracker;
mod router;
mod stream_framer;

// Export selected types from modules.
pub use labels::{NodeId, NodeLinkId};
pub use node::{Node, NodeOptions, NodeRuntime, PhysLinkId};
pub use node_table::{LinkDescription, NodeDescription, NodeStateCallback, NodeTable};
pub use router::{
    generate_node_id, LinkId, MessageReceiver, Router, RouterOptions, RouterTime, SendHandle,
    StreamId,
};
pub use stream_framer::{StreamDeframer, StreamFramer};
