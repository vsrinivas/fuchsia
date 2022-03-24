// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/95529): Remove once the consuming CLs land.
#![allow(dead_code)]

use {crate::wire, anyhow::Error, async_trait::async_trait, virtio_device::mem::DeviceRange};

/// Represents a 512 byte sector.
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub struct Sector(u64);

impl Sector {
    /// Constructs a sector from a raw numeric value.
    pub fn from_raw_sector(sector: u64) -> Self {
        Self(sector)
    }

    /// Constructs a sector from a raw byte value. If the value is not sector aligned it will
    /// be rounded down to the nearest sector.
    pub fn from_bytes_round_down(bytes: u64) -> Self {
        Self(bytes / wire::VIRTIO_BLOCK_SECTOR_SIZE)
    }

    /// Convert the sector address to a byte address.
    ///
    /// Returns `None` if the conversion would result in overflow.
    pub fn to_bytes(&self) -> Option<u64> {
        self.0.checked_mul(wire::VIRTIO_BLOCK_SECTOR_SIZE)
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DeviceAttrs {
    /// The capacity of this device.
    pub capacity: Sector,

    /// If Some, the preferred block_size of this device in bytes. None indicates the backend has
    /// no specific preference for block_size.
    pub block_size: Option<u32>,
}

#[derive(Debug, Clone)]
pub struct Request<'a> {
    /// The offset, in bytes, from the start of the device to access.
    pub sector: Sector,

    /// A set of device memory regions that will be read from or written to based on the
    /// operation.
    pub ranges: &'a [DeviceRange<'a>],
}

#[async_trait(?Send)]
pub trait BlockBackend {
    /// Query basic attributes about the device.
    async fn get_attrs(&self) -> Result<DeviceAttrs, Error>;

    /// Read bytes from a starting sector into a set of DeviceRanges.
    async fn read<'a>(&self, request: Request<'a>) -> Result<(), Error>;

    /// Writes bytes from a starting sector into a set of DeviceRanges.
    ///
    /// Writes will be considered volatile after this operation completes, up until a subsequent
    /// flush command.
    async fn write<'a>(&self, requests: Request<'a>) -> Result<(), Error>;

    /// Commit any pending writes to non-volatile storage. The driver may consider a write to be
    /// durable after this operation completes.
    async fn flush(&self) -> Result<(), Error>;
}
