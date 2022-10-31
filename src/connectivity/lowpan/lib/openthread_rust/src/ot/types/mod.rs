// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod backbone_router_multicast_listener_event;
mod backbone_router_multicast_listener_info;
mod border_router_config;
mod castable;
mod channel_mask;
mod device_role;
mod ext_address;
mod extended_pan_id;
mod external_route_config;
mod ip_counters;
mod ipv6;
mod leader_data;
mod link_mode;
mod log_region;
mod mac_counters;
mod neighbor_info;
mod network_key;
mod network_name;
mod operational_dataset;
mod radio_coex_metrics;
mod radio_region;
mod route_preference;
mod router_info;
mod scan_results;
mod security_policy;
mod timestamp;
mod tlv;

use crate::prelude_internal::*;

pub use backbone_router_multicast_listener_event::*;
pub use backbone_router_multicast_listener_info::*;
pub use border_router_config::*;
pub use castable::*;
pub use channel_mask::*;
pub use device_role::*;
pub use ext_address::*;
pub use extended_pan_id::*;
pub use external_route_config::*;
pub use ip_counters::*;
pub use ipv6::*;
pub use leader_data::*;
pub use link_mode::*;
pub use log_region::*;
pub use mac_counters::*;
pub use neighbor_info::*;
pub use network_key::*;
pub use network_name::*;
pub use operational_dataset::*;
pub use radio_coex_metrics::*;
pub use radio_region::*;
pub use route_preference::*;
pub use router_info::*;
pub use scan_results::*;
pub use security_policy::*;
pub use timestamp::*;
pub use tlv::*;

/// 802.15.4 PAN Identifier. Same type as [`otsys::otPanId`](crate::otsys::otPanId).
pub type PanId = otPanId;

/// 802.15.4 Short Address. Same type as [`otsys::otShortAddress`](crate::otsys::otShortAddress).
pub type ShortAddress = otShortAddress;

/// Type for representing decibels, such as RSSI or transmit power.
pub type Decibels = i8;

/// Channel index value. Identifies an individual radio channel for transmitting and receiving.
pub type ChannelIndex = u8;

/// Mesh-Local Prefix.
///
/// Same type as [`Ip6NetworkPrefix`]. Functional equivalent of [`otsys::otMeshLocalPrefix`](crate::otsys::otMeshLocalPrefix).
pub type MeshLocalPrefix = Ip6NetworkPrefix;

/// Network Interface Identifier.
pub type NetifIndex = u32;

/// Unspecified network index.
pub const NETIF_INDEX_UNSPECIFIED: NetifIndex = 0;

/// Unspecified power
pub const DECIBELS_UNSPECIFIED: Decibels = -128;

/// The largest child ID supported by OpenThread, 511.
pub const MAX_CHILD_ID: u16 = 0x1FF;

/// The bit offset to the router ID in an RLOC16.
pub const ROUTER_ID_OFFSET: usize = 9;

/// Extracts the child ID from an RLOC16.
pub fn rloc16_to_child_id(rloc16: u16) -> u16 {
    rloc16 & MAX_CHILD_ID
}

/// Extracts the router ID from an RLOC16.
pub fn rloc16_to_router_id(rloc16: u16) -> u8 {
    (rloc16 >> ROUTER_ID_OFFSET) as u8
}
