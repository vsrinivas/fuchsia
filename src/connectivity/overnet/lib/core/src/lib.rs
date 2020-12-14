// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Main Overnet functionality.

#![deny(missing_docs)]

#[macro_use]
extern crate rental;

mod async_quic;
mod coding;
mod diagnostics_service;
mod fidl_tests;
mod framed_stream;
mod future_help;
mod handle_info;
mod integration_tests;
mod labels;
mod link;
mod link_frame_label;
mod link_status_updater;
mod peer;
mod ping_tracker;
mod proxy;
mod proxy_stream;
mod proxyable_handle;
mod quic_link;
mod router;
mod routes;
mod security_context;
mod service_map;
mod socket_link;
mod stat_counter;
mod stream_framer;

// Export selected types from modules.
pub use coding::{decode_fidl, encode_fidl};
pub use future_help::log_errors;
pub use labels::{ConnectionId, Endpoint, NodeId, NodeLinkId};
pub use link::{LinkReceiver, LinkSender, MAX_FRAME_LENGTH};
pub use quic_link::{new_quic_link, QuicReceiver, QuicSender};
pub use router::{generate_node_id, ListPeersContext, Router, RouterOptions};
pub use security_context::{SecurityContext, SimpleSecurityContext};
pub use stream_framer::*;

#[cfg(not(target_os = "fuchsia"))]
pub use security_context::MemoryBuffers;

/// Utility trait to trace a variable to the log.
pub(crate) trait Trace {
    /// Trace the caller - add `msg` as text to display, and `ctx` as some context
    /// for the system that caused this value to be traced.
    fn trace(self, msg: impl std::fmt::Display, ctx: impl std::fmt::Debug) -> Self
    where
        Self: Sized;

    fn maybe_trace(
        self,
        trace: bool,
        msg: impl std::fmt::Display,
        ctx: impl std::fmt::Debug,
    ) -> Self
    where
        Self: Sized,
    {
        if trace {
            self.trace(msg, ctx)
        } else {
            self
        }
    }
}

impl<X: std::fmt::Debug> Trace for X {
    fn trace(self, msg: impl std::fmt::Display, ctx: impl std::fmt::Debug) -> Self
    where
        Self: Sized,
    {
        log::info!("[{:?}] {}: {:?}", ctx, msg, self);
        self
    }
}

#[cfg(test)]
mod test_util;
