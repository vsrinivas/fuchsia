// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_zircon_ethernet as fidl;

use bitflags::bitflags;
use serde_derive::{Deserialize, Serialize};

#[derive(PartialEq, Eq, Serialize, Deserialize, Debug, Clone, Copy, Hash)]
pub struct MacAddress {
    pub octets: [u8; 6],
}

impl From<fidl::MacAddress> for MacAddress {
    fn from(fidl::MacAddress { octets }: fidl::MacAddress) -> Self {
        Self { octets }
    }
}

impl Into<fidl::MacAddress> for MacAddress {
    fn into(self) -> fidl::MacAddress {
        let Self { octets } = self;
        fidl::MacAddress { octets }
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

impl std::str::FromStr for MacAddress {
    type Err = failure::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        use failure::ResultExt;

        let mut octets = [0; 6];
        let mut iter = s.split(':');
        for (i, octet) in octets.iter_mut().enumerate() {
            let next_octet = iter.next().ok_or_else(|| {
                failure::format_err!("MAC address [{}] only specifies {} out of 6 octets", s, i)
            })?;
            *octet = u8::from_str_radix(next_octet, 16)
                .with_context(|_| format!("could not parse hex integer from {}", next_octet))?;
        }
        if iter.next().is_some() {
            return Err(failure::format_err!(
                "MAC address has more than six octets: {}",
                s
            ));
        }
        Ok(MacAddress { octets })
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

impl EthernetFeatures {
    pub fn is_physical(&self) -> bool {
        !self.intersects(Self::SYNTHETIC | Self::LOOPBACK)
    }
}

impl std::str::FromStr for EthernetFeatures {
    type Err = failure::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.as_ref() {
            "synthetic" => Ok(Self::SYNTHETIC),
            "loopback" => Ok(Self::LOOPBACK),
            "wireless" => Ok(Self::WLAN),
            s => Err(failure::format_err!(
                "unknown network interface feature \"{}\"",
                s
            )),
        }
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
    pub mac: MacAddress,
}

impl From<fidl::Info> for EthernetInfo {
    fn from(fidl::Info { features, mtu, mac }: fidl::Info) -> Self {
        let features = EthernetFeatures::from_bits_truncate(features);
        let mac = mac.into();
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
