// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Main Overnet functionality.

#![deny(missing_docs)]

#[macro_use]
extern crate failure;
#[cfg(test)]
extern crate timebomb;
#[macro_use]
extern crate log;
extern crate salt_slab;

mod coding;
mod labels;
mod node;
mod node_table;
mod ping_tracker;
mod router;
mod stream_framer;

// Export selected types from modules.
pub use labels::{NodeId, NodeLinkId};
pub use node::{Node, NodeRuntime};
pub use node_table::{LinkDescription, NodeDescription, NodeStateCallback, NodeTable};
pub use router::{
    generate_node_id, LinkId, MessageReceiver, Router, RouterOptions, RouterTime, SendHandle,
    StreamId,
};
pub use stream_framer::{StreamDeframer, StreamFramer};

#[cfg(all(test, not(target_os = "fuchsia")))]
#[no_mangle]
pub extern "C" fn fidlhdl_close(_hdl: u32) {}
