// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/95485): Remove.
#![allow(dead_code)]

use {
    anyhow::{anyhow, Error},
    num_derive::FromPrimitive,
    zerocopy::{AsBytes, FromBytes, LittleEndian, U16},
};

pub type LE16 = U16<LittleEndian>;

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
