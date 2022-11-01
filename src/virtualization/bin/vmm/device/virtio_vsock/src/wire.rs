// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    bitflags::bitflags,
    num_derive::FromPrimitive,
    std::convert::TryFrom,
    zerocopy::{AsBytes, FromBytes},
};

pub use zerocopy::byteorder::little_endian::{U16 as LE16, U32 as LE32, U64 as LE64};

// 5.10.2 Virtqueues
//
// Note that these queues are from the perspective of the driver, so the RX queue is for device
// to driver communication, etc.
pub const RX_QUEUE_IDX: u16 = 0;
pub const TX_QUEUE_IDX: u16 = 1;
pub const EVENT_QUEUE_IDX: u16 = 2;

// 5.10.6.5 Stream Sockets
//
// These flags are hints about future connection behavior. Note that these flags are permanent once
// received, and subsequent packets without these flags do not clear these flags.
bitflags! {
    #[derive(Default)]
    pub struct VirtioVsockFlags: u32 {
        const SHUTDOWN_RECIEVE = 0b01;
        const SHUTDOWN_SEND    = 0b10;
        const SHUTDOWN_BOTH    = Self::SHUTDOWN_RECIEVE.bits | Self::SHUTDOWN_SEND.bits;
    }
}

// 5.10.4 Device configuration layout
#[repr(C, packed)]
pub struct VirtioVsockConfig {
    pub guest_cid: LE64,
}

impl VirtioVsockConfig {
    pub fn new_with_default_cid() -> Self {
        Self { guest_cid: LE64::new(fidl_fuchsia_virtualization::DEFAULT_GUEST_CID.into()) }
    }
}

// 5.10.6 Device Operation
#[repr(C, packed)]
#[derive(Default, Debug, AsBytes, FromBytes, PartialEq)]
pub struct VirtioVsockHeader {
    pub src_cid: LE64,
    pub dst_cid: LE64,
    pub src_port: LE32,
    pub dst_port: LE32,
    pub len: LE32,
    pub vsock_type: LE16,
    pub op: LE16,
    pub flags: LE32,
    pub buf_alloc: LE32,
    pub fwd_cnt: LE32,
}

#[repr(u16)]
#[derive(Clone, Copy, Debug, FromPrimitive, PartialEq)]
pub enum VsockType {
    Stream = 1,    // VIRTIO_VSOCK_TYPE_STREAM
    SeqPacket = 2, // VIRTIO_VSOCK_TYPE_SEQPACKET
}

impl Into<u16> for VsockType {
    fn into(self) -> u16 {
        self as u16
    }
}

impl TryFrom<u16> for VsockType {
    type Error = Error;

    fn try_from(n: u16) -> Result<Self, Self::Error> {
        <Self as num_traits::FromPrimitive>::from_u16(n)
            .ok_or(anyhow!("Unrecognized VsockType: {}", n))
    }
}

#[repr(u16)]
#[derive(Clone, Copy, Debug, Eq, Hash, FromPrimitive, PartialEq)]
pub enum OpType {
    Invalid = 0,       // VIRTIO_VSOCK_OP_INVALID
    Request = 1,       // VIRTIO_VSOCK_OP_REQUEST
    Response = 2,      // VIRTIO_VSOCK_OP_RESPONSE
    Reset = 3,         // VIRTIO_VSOCK_OP_RST
    Shutdown = 4,      // VIRTIO_VSOCK_OP_SHUTDOWN
    ReadWrite = 5,     // VIRTIO_VSOCK_OP_RW
    CreditUpdate = 6,  // VIRTIO_VSOCK_OP_CREDIT_UPDATE
    CreditRequest = 7, // VIRTIO_VSOCK_OP_CREDIT_REQUEST
}

impl Into<u16> for OpType {
    fn into(self) -> u16 {
        self as u16
    }
}

impl TryFrom<u16> for OpType {
    type Error = Error;

    fn try_from(n: u16) -> Result<Self, Self::Error> {
        <Self as num_traits::FromPrimitive>::from_u16(n)
            .ok_or(anyhow!("Unrecognized OpType: {}", n))
    }
}
