//! Types that the netstack publicly exposes

pub use crate::device::{
    ethernet::Mac, receive_frame, DeviceId, DeviceLayerEventDispatcher, DeviceLayerTimerId,
};
pub use crate::transport::udp::UdpEventDispatcher;
pub use crate::transport::{TransportLayerEventDispatcher, TransportLayerTimerId};

/// A struct that represents a subnet.
pub struct Subnet {
    /// The address part of the subnet.
    addr: std::net::IpAddr,
    /// The prefix length.
    prefix_len: u8,
}

impl Subnet {
    /// Construct a new subnet.
    pub fn new(addr: std::net::IpAddr, prefix_len: u8) -> Self {
        Subnet { addr, prefix_len }
    }

    /// Get the address section of the subnet.
    pub fn addr(&self) -> std::net::IpAddr {
        self.addr
    }

    /// Get the prefix length of the subnet.
    pub fn prefix_len(&self) -> u8 {
        self.prefix_len
    }
}
