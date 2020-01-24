// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Main Overnet functionality.

#![deny(missing_docs)]

mod async_quic;
mod channel_proxy;
mod coding;
mod diagnostics_service;
mod framed_stream;
mod future_help;
mod labels;
mod link;
mod link_status_updater;
mod peer;
mod ping_tracker;
mod route_planner;
mod router;
mod routing_label;
mod runtime;
mod service_map;
mod socket_link;
mod stat_counter;
mod stream_framer;

// Export selected types from modules.
pub use future_help::log_errors;
pub use labels::{NodeId, NodeLinkId};
pub use link::Link;
pub use router::{generate_node_id, Router, RouterOptions};
pub use runtime::{run, spawn, wait_until};
pub use stream_framer::{StreamDeframer, StreamFramer};
