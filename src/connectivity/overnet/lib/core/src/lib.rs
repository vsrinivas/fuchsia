// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Main Overnet functionality.

#![deny(missing_docs)]

#[cfg(test)]
extern crate timebomb;

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

#[cfg(all(test, not(target_os = "fuchsia")))]
mod test_fakes_for_fidlhdl {
    use fidl::{FidlHdlPairCreateResult, FidlHdlWriteResult, Handle};
    use fuchsia_zircon_status as zx_status;

    #[no_mangle]
    pub extern "C" fn fidlhdl_close(_hdl: u32) {}

    #[no_mangle]
    pub extern "C" fn fidlhdl_channel_create() -> FidlHdlPairCreateResult {
        FidlHdlPairCreateResult::new_err(zx_status::Status::NOT_SUPPORTED)
    }

    #[no_mangle]
    pub extern "C" fn fidlhdl_channel_write(
        _hdl: u32,
        _bytes: *const u8,
        _handles: *mut Handle,
        _num_bytes: usize,
        _num_handles: usize,
    ) -> FidlHdlWriteResult {
        FidlHdlWriteResult::PeerClosed
    }
}
