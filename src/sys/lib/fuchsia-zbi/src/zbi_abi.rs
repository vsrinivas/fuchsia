// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    byteorder::LittleEndian,
    zerocopy::{byteorder::U32, AsBytes, FromBytes, Unaligned},
};

/// Must be in the extra field for headers of type ZbiType::Container.
pub const ZBI_CONTAINER_MAGIC: u32 = 0x868c_f7e6;

/// Every header type must have this magic value.
pub const ZBI_ITEM_MAGIC: u32 = 0xb578_1729;

/// Always required.
pub const ZBI_FLAG_VERSION: u32 = 0x0001_0000;

/// If the header contains this flag, the CRC32 field must contain a valid CRC32. Otherwise, the
/// CRC32 field must contain ZBI_ITEM_NO_CRC32.
pub const ZBI_FLAG_CRC32: u32 = 0x0002_0000;

/// The CRC32 field must be set to this when not using CRC32.
pub const ZBI_ITEM_NO_CRC32: u32 = 0x4a87_e8d6;

// Each item is padded to be a multiple of 8 bytes.
pub const ZBI_ALIGNMENT_BYTES: u32 = 0x8;

pub fn is_zbi_type_driver_metadata(zbi_type_raw: u32) -> bool {
    (zbi_type_raw & 0xFF) == ZbiType::DriverMetadata as u32
}

#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
/// Defines the types supported by the Rust ZBI parser. This is a subset from
/// zircon/system/public/zircon/boot/image.h, and should be updated as needed.
pub enum ZbiType {
    Container = 0x544f_4f42,
    Cmdline = 0x4c44_4d43,
    Crashlog = 0x4d4f_4f42,
    KernelDriver = 0x5652_444B,
    PlatformId = 0x4449_4C50,
    StorageBootfsFactory = 0x4653_4642,
    StorageRamdisk = 0x4b53_4452,
    ImageArgs = 0x4752_4149,
    SerialNumber = 0x4e4c_5253,
    BootloaderFile = 0x4C46_5442,
    DeviceTree = 0xd00d_feed,

    // DriverMetadata is a special case, where only the LSB of the u32 needs to match this
    // value. See the IsZbiTypeDriverMetadata function for details.
    DriverMetadata = 0x6D,
    Unknown,
}

#[repr(C)]
#[derive(Debug, Default, Copy, Clone, FromBytes, AsBytes, Unaligned)]
pub struct zbi_header_t {
    pub zbi_type: U32<LittleEndian>,
    pub length: U32<LittleEndian>,
    pub extra: U32<LittleEndian>,
    pub flags: U32<LittleEndian>,
    pub reserved_0: U32<LittleEndian>,
    pub reserved_1: U32<LittleEndian>,
    pub magic: U32<LittleEndian>,
    pub crc32: U32<LittleEndian>,
}
