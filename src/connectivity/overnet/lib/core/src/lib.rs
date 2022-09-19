// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Main Overnet functionality.

#![deny(missing_docs)]

mod coding;
mod future_help;
mod handle_info;
mod labels;
mod link;
mod peer;
mod proxy;
mod router;
mod stat_counter;
mod test_util;

// Export selected types from modules.
pub use coding::{decode_fidl_with_context, encode_fidl_with_context};
pub use future_help::log_errors;
pub use labels::{ConnectionId, Endpoint, NodeId, NodeLinkId};
pub use link::{
    ConfigProducer, LinkIntroductionFacts, LinkReceiver, LinkSender, SendFrame, MAX_FRAME_LENGTH,
};
pub use router::security_context::{SecurityContext, SimpleSecurityContext};
pub use router::{generate_node_id, AscenddClientRouting, ListPeersContext, Router, RouterOptions};

pub use test_util::{test_security_context, NodeIdGenerator};

#[cfg(not(target_os = "fuchsia"))]
pub use router::security_context::MemoryBuffers;

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
        tracing::info!("[{:?}] {}: {:?}", ctx, msg, self);
        self
    }
}
