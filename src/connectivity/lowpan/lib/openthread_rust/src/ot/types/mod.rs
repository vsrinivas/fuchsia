// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod border_router_config;
mod castable;
mod channel_mask;
mod device_role;
mod ext_address;
mod extended_pan_id;
mod external_route_config;
mod ipv6;
mod link_mode;
mod log_region;
mod neighbor_info;
mod network_key;
mod network_name;
mod operational_dataset;
mod radio_region;
mod route_preference;
mod scan_results;
mod security_policy;

use crate::prelude_internal::*;

pub use border_router_config::*;
pub use castable::*;
pub use channel_mask::*;
pub use device_role::*;
pub use ext_address::*;
pub use extended_pan_id::*;
pub use external_route_config::*;
pub use ipv6::*;
pub use link_mode::*;
pub use log_region::*;
pub use neighbor_info::*;
pub use network_key::*;
pub use network_name::*;
pub use operational_dataset::*;
pub use radio_region::*;
pub use route_preference::*;
pub use scan_results::*;
pub use security_policy::*;

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
