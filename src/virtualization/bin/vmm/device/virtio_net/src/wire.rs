// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    num_derive::FromPrimitive,
    zerocopy::{AsBytes, FromBytes},
};

pub use zerocopy::byteorder::little_endian::U16 as LE16;

// 5.1.2 Virtqueues
//
// Note that these queues are from the perspective of the driver, so the RX queue is for device
// to driver communication, etc.
pub const RX_QUEUE_IDX: u16 = 0;
pub const TX_QUEUE_IDX: u16 = 1;

// 5.1.6 Device Operation
//
// Note that this combines the legacy and current headers into one structure (equivalent to when
// VIRTIO_F_VERSION_1 is set).
#[repr(C, packed)]
#[derive(Default, Debug, AsBytes, FromBytes, PartialEq)]
pub struct VirtioNetHeader {
    pub flags: u8,
    pub gso_type: u8,
    pub hdr_len: LE16,
    pub gso_size: LE16,
    pub csum_start: LE16,
    pub csum_offset: LE16,
    pub num_buffers: LE16,
}

// 5.1.6 Device Operation
#[repr(u8)]
#[derive(Clone, Copy, Debug, FromPrimitive, PartialEq)]
pub enum GsoType {
    None = 0,   // VIRTIO_NET_HDR_GSO_NONE
    Tcpv4 = 1,  // VIRTIO_NET_HDR_GSO_TCPV4
    Udp = 3,    // VIRTIO_NET_HDR_GSO_UDP
    Tcpv6 = 4,  // VIRTIO_NET_HDR_GSO_TCPV6
    Ecn = 0x80, // VIRTIO_NET_HDR_GSO_ECN
}

impl Into<u8> for GsoType {
    fn into(self) -> u8 {
        self as u8
    }
}

impl TryFrom<u8> for GsoType {
    type Error = Error;

    fn try_from(n: u8) -> Result<Self, Self::Error> {
        <Self as num_traits::FromPrimitive>::from_u8(n)
            .ok_or(anyhow!("Unrecognized GsoType: {}", n))
    }
}

// 5.1.6.3.1 Driver Requirements: Setting Up Receive Buffers
//
// VIRTIO_NET_F_MRG_RXBUF is not negotiated AND VIRTIO_NET_F_GUEST_TSO4 OR VIRTIO_NET_F_GUEST_TSO6
// OR VIRTIO_NET_F_GUEST_UFO are not negotiated, so the guest must place single descriptor buffers
// of 1526 bytes (MTU + ethernet header + virtio net header).
pub const REQUIRED_RX_BUFFER_SIZE: usize = 1526;
