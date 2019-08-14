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
mod node_table;
mod router;

// Export selected types from modules.
pub use labels::{NodeId, NodeLinkId, VersionCounter, FIRST_VERSION, TOMBSTONE_VERSION};
pub use node_table::{LinkDescription, NodeDescription, NodeStateCallback, NodeTable};
pub use router::{
    LinkId, MessageReceiver, Router, RouterOptions, RouterTime, SendHandle, StreamId,
};
