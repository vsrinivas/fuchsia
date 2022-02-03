// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod error;
pub use self::error::{FidlReturn, NetstackError};

use fidl_fuchsia_net_stack as fidl;

pub struct ForwardingEntry {
    pub subnet: fidl_fuchsia_net_ext::Subnet,
    pub device_id: u64,
    pub next_hop: Option<fidl_fuchsia_net_ext::IpAddress>,
    pub metric: u32,
}

impl From<fidl::ForwardingEntry> for ForwardingEntry {
    fn from(forwarding_entry: fidl::ForwardingEntry) -> Self {
        let fidl::ForwardingEntry { subnet, device_id, next_hop, metric } = forwarding_entry;
        let subnet = subnet.into();
        let next_hop = next_hop.map(|next_hop| (*next_hop).into());
        Self { subnet, device_id, next_hop, metric }
    }
}
