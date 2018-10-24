// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_zircon_ethernet as fidl;

use bitflags::bitflags;
use serde_derive::{Deserialize, Serialize};

#[derive(PartialEq, Eq, Serialize, Deserialize, Debug)]
pub struct MacAddress {
    pub octets: [u8; 6],
}

impl From<fidl::MacAddress> for MacAddress {
    fn from(fidl::MacAddress { octets }: fidl::MacAddress) -> Self {
        Self { octets }
    }
}

impl From<fidl_fuchsia_net::MacAddress> for MacAddress {
    fn from(fidl_fuchsia_net::MacAddress { addr }: fidl_fuchsia_net::MacAddress) -> Self {
        Self { octets: addr }
    }
}

impl std::fmt::Display for MacAddress {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        let Self { octets } = self;
        for (i, byte) in octets.iter().enumerate() {
            if i > 0 {
                write!(f, ":")?;
            }
            write!(f, "{:02x}", byte)?;
        }
        Ok(())
    }
}

bitflags! {
    /// Features supported by an Ethernet device.
    #[repr(transparent)]
    pub struct EthernetFeatures: u32 {
        /// The Ethernet device is a wireless device.
        const WLAN = fidl::INFO_FEATURE_WLAN;
        /// The Ethernet device does not represent a hardware device.
        const SYNTHETIC = fidl::INFO_FEATURE_SYNTH;
        /// The Ethernet device is a loopback device.
        ///
        /// This bit should not be set outside of network stacks.
        const LOOPBACK = fidl::INFO_FEATURE_LOOPBACK;
    }
}

/// Information retrieved about an Ethernet device.
#[derive(Debug)]
pub struct EthernetInfo {
    /// The features supported by the device.
    pub features: EthernetFeatures,
    /// The maximum transmission unit (MTU) of the device.
    pub mtu: u32,
    /// The MAC address of the device.
    pub mac: fidl::MacAddress,
}

impl From<fidl::Info> for EthernetInfo {
    fn from(fidl::Info { features, mtu, mac }: fidl::Info) -> Self {
        let features = EthernetFeatures::from_bits_truncate(features);
        Self { features, mtu, mac }
    }
}

bitflags! {
    /// Status flags for an Ethernet device.
    #[repr(transparent)]
    pub struct EthernetStatus: u32 {
        /// The Ethernet device is online, meaning its physical link is up.
        const ONLINE = fidl::DEVICE_STATUS_ONLINE;
    }
}

bitflags! {
    /// Status flags describing the result of queueing a packet to an Ethernet device.
    #[repr(transparent)]
    pub struct EthernetQueueFlags: u16 {
        /// The packet was received correctly.
        const RX_OK = fidl::FIFO_RX_OK as u16;
        /// The packet was transmitted correctly.
        const TX_OK = fidl::FIFO_TX_OK as u16;
        /// The packet was out of the bounds of the memory shared with the Ethernet device driver.
        const INVALID = fidl::FIFO_INVALID as u16;
        /// The received packet was sent by this host.
        ///
        /// This bit is only set after `tx_listen_start` is called.
        const TX_ECHO = fidl::FIFO_RX_TX as u16;
    }
}
