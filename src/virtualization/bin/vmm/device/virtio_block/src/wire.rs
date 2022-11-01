// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use zerocopy::{AsBytes, FromBytes};

pub const VIRTIO_BLOCK_REQUEST_QUEUE: u16 = 0;
pub const VIRTIO_BLOCK_SECTOR_SIZE: u64 = 512;

// Values for VirtioBlockHeader::request_type
pub const VIRTIO_BLK_T_IN: u32 = 0;
pub const VIRTIO_BLK_T_OUT: u32 = 1;
pub const VIRTIO_BLK_T_FLUSH: u32 = 4;
pub const VIRTIO_BLK_T_GET_ID: u32 = 8;
pub const VIRTIO_BLK_T_DISCARD: u32 = 11;
pub const VIRTIO_BLK_T_WRITE_ZEROES: u32 = 13;

// Not public directly, but exposed though `VirtioBlockStatus::to_wire`
const VIRTIO_BLK_S_OK: u8 = 0;
const VIRTIO_BLK_S_IOERR: u8 = 1;
const VIRTIO_BLK_S_UNSUPP: u8 = 2;

pub const VIRTIO_BLK_ID_LEN: usize = 20;

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum VirtioBlockStatus {
    Ok,
    IoError,
    Unsupported,
}

impl VirtioBlockStatus {
    pub fn to_wire(&self) -> u8 {
        match self {
            VirtioBlockStatus::Ok => VIRTIO_BLK_S_OK,
            VirtioBlockStatus::IoError => VIRTIO_BLK_S_IOERR,
            VirtioBlockStatus::Unsupported => VIRTIO_BLK_S_UNSUPP,
        }
    }
}

pub use zerocopy::byteorder::little_endian::{U32 as LE32, U64 as LE64};

#[derive(Debug, Copy, Clone, AsBytes, FromBytes)]
#[repr(C, packed)]
pub struct VirtioBlockHeader {
    pub request_type: LE32,
    pub reserved: LE32,
    pub sector: LE64,
}

#[derive(Debug, Copy, Clone, AsBytes, FromBytes)]
#[repr(C, packed)]
pub struct VirtioBlockFooter {
    pub status: u8,
}
