// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The module contains the structures that get serialized and written to image file.
//! Some of these structures are visible to users of the library when they want to
//! query the state.
//!
//! On disk layout of image file looks something like the picture below.
//! +-------------------------+
//! | Header                  |
//! +-------------------------+
//! | ExtentCluster           |
//! | +---------------------+ |
//! | | ExtentClusterHeader | |
//! | +---------------------+ |
//! | | ExtentInfo []       | |
//! | +---------------------+ |
//! | | Extent Data []      | |
//! | +---------------------+ |
//! +-------------------------+
//! | More ExtentClusters..   |
//! +-------------------------+
//! # Header
//! Header describes the layout of the image file
//!  * alignment requirements
//!  * location of first extent cluster
//!  * other options like checksum and etc
//!
//! ExtentClusterHeader
//! A Extent cluster describes and holds a set of extents, their properties and
//! their data. In addition, cluster header may also point to next extent
//! cluster.
use {
    crate::{
        error::Error,
        extent::Extent,
        properties::{DataKind, ExtentKind, DEFAULT_ALIGNMENT},
        utils::ReadAndSeek,
    },
    bitfield::bitfield,
    crc,
    std::{convert::TryFrom, io::Write},
    zerocopy::{AsBytes, FromBytes, LayoutVerified},
};

const HEADER_MAGIC1: u64 = 0xe3761f4bd6343e64u64;
const HEADER_MAGIC2: u64 = 0x8aa02bf59a6f7bc5u64;
const HEADER_VERSION: u64 = 1;
const EXTENT_CLUSTER_HEADER_MAGIC1: u64 = 0x8ecc1d9bcdb27dfbu64;
const EXTENT_CLUSTER_HEADER_MAGIC2: u64 = 0x451681f35b024d65u64;

/// `Header` describes the layout and state of the extracted image file.
///
/// Typically header is found at offset 0 within the image file.
#[repr(C)]
#[derive(Debug, PartialEq, Eq, Copy, Clone, FromBytes, AsBytes)]
pub struct Header {
    // Magic number to identify the content as extracted image.
    magic1: u64,

    // Magic number to identify the content as extracted image.
    magic2: u64,

    // Version of the extracted image.
    version: u64,

    // Points to offset within the image file where first extent cluster can be found.
    extent_cluster_offset: u64,

    // Image metadata and data is aligned to this number.
    alignment: u64,

    // crc32 of the structure. This includes crc of padding that gets added when
    // alignment is >1.
    crc32: u32,

    _padding: u32,
}

impl Default for Header {
    fn default() -> Self {
        Header::new(DEFAULT_ALIGNMENT)
    }
}

impl Header {
    pub fn new(alignment: u64) -> Header {
        Header {
            magic1: HEADER_MAGIC1,
            magic2: HEADER_MAGIC2,
            version: HEADER_VERSION,
            extent_cluster_offset: alignment as u64,
            alignment,
            crc32: 0,
            _padding: 0,
        }
    }

    /// Returns size of serialized header considering alignement.
    pub fn serialized_size(&self) -> u64 {
        assert_ne!(self.alignment, 0);
        ((self.as_bytes().len() as u64 + self.alignment - 1) / self.alignment) * self.alignment
    }

    /// Checks offset of extent cluster.
    pub fn check_extent_cluster_offset(&self, offset: u64) -> Result<(), Error> {
        if offset < self.serialized_size() {
            return Err(Error::InvalidOffset);
        }

        if offset % self.alignment != 0 {
            return Err(Error::InvalidOffset);
        }

        Ok(())
    }

    /// Updates extent cluster offset.
    pub fn set_extent_cluster_offset(&mut self, offset: u64) -> Result<(), Error> {
        self.check_extent_cluster_offset(offset)?;
        self.extent_cluster_offset = offset;
        Ok(())
    }

    /// Returns current extent cluster offset.
    pub fn get_extent_cluster_offset(&self) -> u64 {
        self.extent_cluster_offset
    }

    /// Serializes header to out. Computes and updates header crc.
    pub fn serialize_to(&mut self, out: &mut dyn Write) -> Result<u64, Error> {
        self.check_extent_cluster_offset(self.extent_cluster_offset)?;
        self.crc32 = 0;
        let crc32 = crc::crc32::update(0, &crc::crc32::IEEE_TABLE, self.as_bytes());
        let buffer = vec![0; self.serialized_size() as usize - self.as_bytes().len()];
        self.crc32 = crc::crc32::update(crc32, &crc::crc32::IEEE_TABLE, self.as_bytes());

        out.write_all(&self.as_bytes()).map_err(move |_| Error::WriteFailed)?;
        out.write_all(&buffer).map_err(move |_| Error::WriteFailed)?;
        Ok(self.serialized_size())
    }

    pub fn deserialize_from(in_stream: &mut dyn ReadAndSeek) -> Result<Self, Error> {
        let mut buffer = vec![0; std::mem::size_of::<Self>()];
        in_stream.read_exact(&mut buffer).map_err(move |_| Error::ReadFailed)?;
        let c_extent_or = LayoutVerified::<&[u8], Self>::new_from_prefix(&buffer);
        if c_extent_or.is_none() {
            return Err(Error::ReadFailed);
        }
        let (e, _) = c_extent_or.unwrap();
        Ok(e.clone())
    }

    #[cfg(test)]
    pub fn test_check(&self) -> bool {
        // TODO(auradkar) : check crc.
        self.magic1 == HEADER_MAGIC1
            && self.magic2 == HEADER_MAGIC2
            && self.version == HEADER_VERSION
            && self.alignment != 0
    }
}

pub const EXTENT_KIND_UNMAPPED: u8 = 0;
pub const EXTENT_KIND_UNUSED: u8 = 1;
pub const EXTENT_KIND_DATA: u8 = 2;
pub const EXTENT_KIND_PII: u8 = 3;

bitfield! {
#[repr(C)]
#[no_mangle]
#[derive(Default, Clone, Copy, PartialEq, Eq, PartialOrd, Debug, FromBytes, AsBytes)]
/// ExtentKind describes the type of the extent.
///
/// ExtentKind may mean different things based on the storage software.
/// ExtentKind priority is Unmapped<Unused<Data<Pii.
pub struct ExtentKindInfo(u8);
u8, kind, set_kind: 1, 0;
}

impl ExtentKindInfo {
    pub fn new(ekind: ExtentKind) -> Self {
        Self::from(ekind)
    }

    pub fn to_kind(&self) -> Result<ExtentKind, Error> {
        match self.kind() {
            EXTENT_KIND_UNMAPPED => Ok(ExtentKind::Unmmapped),
            EXTENT_KIND_UNUSED => Ok(ExtentKind::Unused),
            EXTENT_KIND_DATA => Ok(ExtentKind::Data),
            EXTENT_KIND_PII => Ok(ExtentKind::Pii),
            _ => Err(Error::ParseFailed),
        }
    }

    pub fn check(&self) -> Result<(), Error> {
        if self.0 > EXTENT_KIND_PII {
            return Err(Error::ParseFailed);
        }
        Ok(())
    }
}

impl TryFrom<u8> for ExtentKindInfo {
    type Error = Error;
    fn try_from(value: u8) -> Result<Self, Error> {
        if value > 3 {
            return Err(Error::InvalidArgument);
        }
        let mut skind: ExtentKindInfo = Default::default();
        skind.set_kind(value);
        Ok(skind)
    }
}

impl From<ExtentKind> for ExtentKindInfo {
    fn from(kind: ExtentKind) -> ExtentKindInfo {
        let mut info: ExtentKindInfo = Default::default();
        info.set_kind(match kind {
            ExtentKind::Unmmapped => EXTENT_KIND_UNMAPPED,
            ExtentKind::Unused => EXTENT_KIND_UNUSED,
            ExtentKind::Data => EXTENT_KIND_DATA,
            ExtentKind::Pii => EXTENT_KIND_PII,
        });
        info
    }
}

const DATA_KIND_SKIPPED: u8 = 0;
const DATA_KIND_ZEROES: u8 = 1;
const DATA_KIND_UNMODIFIED: u8 = 2;
const DATA_KIND_MODIFIED: u8 = 3;

bitfield! {
/// DataKind describes the type of the data within an extent.
/// DataKind priority is Skipped<Zeroes<Unmodified<Modified.
#[repr(C)]
#[no_mangle]
#[derive(Default, Clone, Copy, PartialEq, Eq, PartialOrd, Debug, FromBytes, AsBytes)]
pub struct DataKindInfo(u8);
pub u8, kind, set_kind: 1, 0;
}

impl DataKindInfo {
    pub fn new(kind: DataKind) -> Self {
        Self::from(kind)
    }

    pub fn to_kind(&self) -> Result<DataKind, Error> {
        match self.kind() {
            DATA_KIND_SKIPPED => Ok(DataKind::Skipped),
            DATA_KIND_ZEROES => Ok(DataKind::Zeroes),
            DATA_KIND_UNMODIFIED => Ok(DataKind::Unmodified),
            DATA_KIND_MODIFIED => Ok(DataKind::Modified),
            _ => Err(Error::ParseFailed),
        }
    }

    pub fn check(&self) -> Result<(), Error> {
        if self.0 > DATA_KIND_MODIFIED {
            return Err(Error::ParseFailed);
        }
        Ok(())
    }
}

impl TryFrom<u8> for DataKindInfo {
    type Error = Error;
    fn try_from(value: u8) -> Result<Self, Error> {
        if value > 3 {
            return Err(Error::InvalidArgument);
        }
        let mut dkind: DataKindInfo = Default::default();
        dkind.set_kind(value);
        Ok(dkind)
    }
}

impl From<DataKind> for DataKindInfo {
    fn from(kind: DataKind) -> DataKindInfo {
        let mut dkind: Self = Default::default();
        dkind.set_kind(match kind {
            DataKind::Skipped => DATA_KIND_SKIPPED,
            DataKind::Zeroes => DATA_KIND_ZEROES,
            DataKind::Unmodified => DATA_KIND_UNMODIFIED,
            DataKind::Modified => DATA_KIND_MODIFIED,
        });
        dkind
    }
}

/// Serializable extent info
#[repr(C)]
#[derive(Debug, Clone, AsBytes, FromBytes)]
pub struct ExtentInfo {
    /// Start offset, in bytes, where this extent maps into the disk.
    /// This is not an offset within the image file.
    pub start: u64,

    /// End offset, in bytes, where this extent maps into the disk.
    /// This is not an offset within the image file.
    pub end: u64,

    /// ExtentKind describes the type of the extent.
    ///
    /// ExtentKind may mean different things based on the storage software.
    /// ExtentKind priority is Unmapped<Unused<Data<Pii.
    /// See [`ExtentKind`]
    pub extent: ExtentKindInfo,

    /// DataKind describes the type of the data within an extent.
    /// DataKind priority is Skipped<Zeroes<Unmodified<Modified.
    /// See [`DataKind`]
    pub data: DataKindInfo,

    // Make zerocopy happy. zerocopy expects packed, aligned structure.
    _padding: [u8; 6],
}

impl From<Extent> for ExtentInfo {
    fn from(extent: Extent) -> Self {
        ExtentInfo {
            start: extent.storage_range().start,
            end: extent.storage_range().end,
            extent: ExtentKindInfo::new(extent.properties().extent_kind),
            data: DataKindInfo::new(extent.properties().data_kind),
            _padding: [0; 6],
        }
    }
}

impl ExtentInfo {
    pub fn check(&self) -> Result<(), Error> {
        if self.start >= self.end {
            return Err(Error::ParseFailed);
        }
        self.data.check()?;
        self.extent.check()?;

        Ok(())
    }

    pub fn serialized_size(&self) -> u64 {
        self.as_bytes().len() as u64
    }

    /// Serializes extent cluster to out.
    pub fn serialize_to(&self, out: &mut dyn Write) -> Result<u64, Error> {
        out.write_all(&self.as_bytes()).map_err(move |_| Error::WriteFailed)?;
        Ok(self.as_bytes().len() as u64)
    }

    pub fn deserialize_from(in_stream: &mut dyn ReadAndSeek) -> Result<Self, Error> {
        let mut buffer = vec![0; std::mem::size_of::<Self>()];
        in_stream.read_exact(&mut buffer).map_err(move |_| Error::ReadFailed)?;
        let c_extent_or = LayoutVerified::<&[u8], Self>::new_from_prefix(&buffer);
        if c_extent_or.is_none() {
            return Err(Error::ReadFailed);
        }
        let (e, _) = c_extent_or.unwrap();
        Ok(e.clone())
    }
}

#[repr(C)]
#[derive(Debug, PartialEq, Eq, Copy, Clone, AsBytes, FromBytes)]
pub struct ExtentClusterHeader {
    // Magic number to identify the content as extracted image.
    magic1: u64,

    // Magic number to identify the content as extracted image.
    magic2: u64,

    // Number of extents in this extent cluster.
    extent_count: u64,

    // Points to next extent cluster. 0 indiates no more extent clusters.
    // This is offset relative to beginning of the image.
    next_cluster_offset: u64,

    // If cluster has footer, then this is where it can be found.
    // 0 indiates no footer.
    footer_offset: u64,

    // crc of the extent cluster and all the extents in this cluster.
    // The padding, if any, needs to be zeroed.
    crc32: u32,

    // Explicit padding.
    _padding: u32,
}

impl ExtentClusterHeader {
    fn new(extent_count: u64, next_cluster_offset: u64) -> ExtentClusterHeader {
        let mut e = ExtentClusterHeader {
            magic1: EXTENT_CLUSTER_HEADER_MAGIC1,
            magic2: EXTENT_CLUSTER_HEADER_MAGIC2,
            extent_count,
            next_cluster_offset,
            crc32: 0,
            footer_offset: 0,
            _padding: 0,
        };
        e.crc32 = crc::crc32::checksum_ieee(e.as_bytes());
        e
    }

    pub fn serialized_size(&self) -> u64 {
        self.as_bytes().len() as u64
    }

    /// Serializes extent cluster to out.
    pub fn serialize_to(
        extent_count: u64,
        next_cluster_offset: u64,
        out: &mut dyn Write,
    ) -> Result<u64, Error> {
        let cluster = Self::new(extent_count, next_cluster_offset);
        out.write_all(&cluster.as_bytes()).map_err(move |_| Error::WriteFailed)?;
        Ok(cluster.as_bytes().len() as u64)
    }

    pub fn deserialize_from(in_stream: &mut dyn ReadAndSeek) -> Result<Self, Error> {
        let mut buffer = vec![0; std::mem::size_of::<Self>()];
        in_stream.read_exact(&mut buffer).map_err(move |_| Error::ReadFailed)?;
        let c_extent_or = LayoutVerified::<&[u8], Self>::new_from_prefix(&buffer);
        if c_extent_or.is_none() {
            return Err(Error::ReadFailed);
        }
        let (e, _) = c_extent_or.unwrap();
        Ok(e.clone())
    }

    /// Checks/validates extent cluster.
    /// TODO(auradkar) : verify crc.
    #[cfg(test)]
    pub fn test_check(&self, extent_count: u64, next_cluster_offset: u64) -> bool {
        self.magic1 == EXTENT_CLUSTER_HEADER_MAGIC1
            && self.magic2 == EXTENT_CLUSTER_HEADER_MAGIC2
            && self.extent_count == extent_count
            && self.next_cluster_offset == next_cluster_offset
            && self.crc32 != 0
            && self.footer_offset == 0
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::properties::ExtentProperties;
    use std::io::Cursor;

    #[test]
    fn test_header_serialized_size() {
        // Default header alignment is 8KiB. Serialized size should be greater than the size of
        // header which is much smaller in size.
        let header: Header = Default::default();
        assert!(header.serialized_size() as usize > std::mem::size_of::<Header>());
        assert!(header.serialized_size() >= DEFAULT_ALIGNMENT);
        assert!(header.serialized_size() % DEFAULT_ALIGNMENT == 0);

        // Smaller alignment.
        let alignment = 32;
        let header2 = Header::new(alignment);
        assert!(header2.serialized_size() as usize >= std::mem::size_of::<Header>());
        assert!(header2.serialized_size() >= alignment);
        assert!(header2.serialized_size() % alignment == 0);
        assert!(header.serialized_size() > header2.serialized_size());

        // No alignment - aka 1 byte alignment.
        let alignment = 1;
        let header3 = Header::new(alignment);
        assert!(header3.serialized_size() >= alignment);
        assert!(header.serialized_size() > header3.serialized_size());
    }

    #[test]
    fn test_check_extent_cluster_offset() {
        let header: Header = Default::default();
        assert!(header.check_extent_cluster_offset(0).err().unwrap() == Error::InvalidOffset);
        assert!(
            header.check_extent_cluster_offset(header.serialized_size() - 1).err().unwrap()
                == Error::InvalidOffset
        );
        assert!(
            header.check_extent_cluster_offset(header.serialized_size() + 1).err().unwrap()
                == Error::InvalidOffset
        );
        assert!(header.check_extent_cluster_offset(header.serialized_size()).is_ok());
        assert!(header
            .check_extent_cluster_offset(header.serialized_size() + DEFAULT_ALIGNMENT)
            .is_ok());
    }

    #[test]
    fn test_set_extent_cluster_offset() {
        let mut header: Header = Default::default();
        let default_offset = header.get_extent_cluster_offset();
        assert!(header.set_extent_cluster_offset(0).err().unwrap() == Error::InvalidOffset);
        assert!(header.get_extent_cluster_offset() == default_offset);

        assert!(
            header.set_extent_cluster_offset(header.serialized_size() - 1).err().unwrap()
                == Error::InvalidOffset
        );
        assert!(header.get_extent_cluster_offset() == default_offset);

        assert!(
            header.set_extent_cluster_offset(header.serialized_size() + 1).err().unwrap()
                == Error::InvalidOffset
        );
        assert!(header.get_extent_cluster_offset() == default_offset);

        assert!(header.set_extent_cluster_offset(header.serialized_size()).is_ok());
        assert!(header.get_extent_cluster_offset() == header.serialized_size());

        assert!(header
            .set_extent_cluster_offset(header.serialized_size() + DEFAULT_ALIGNMENT)
            .is_ok());
        assert!(header.get_extent_cluster_offset() == header.serialized_size() + DEFAULT_ALIGNMENT);
    }

    #[test]
    fn test_header_serialize() {
        let mut header: Header = Default::default();
        let mut out_buffer = Cursor::new(Vec::new());
        let len = header.serialize_to(&mut out_buffer);
        assert!(len.is_ok());
        assert_eq!(out_buffer.get_ref().len(), header.serialized_size() as usize);
        assert_eq!(out_buffer.get_ref().len(), len.unwrap() as usize);
        assert_eq!(out_buffer.get_ref().len() % header.alignment as usize, 0);
        out_buffer.set_position(0);
        let read_header: Header = Header::deserialize_from(&mut out_buffer).unwrap();
        assert_eq!(header, read_header);
    }

    #[test]
    fn test_extent_kind_info_to_kind() {
        assert_eq!(
            ExtentKindInfo::new(ExtentKind::Unmmapped).to_kind().unwrap(),
            ExtentKind::Unmmapped
        );
        assert_eq!(ExtentKindInfo::new(ExtentKind::Unused).to_kind().unwrap(), ExtentKind::Unused);
        assert_eq!(ExtentKindInfo::new(ExtentKind::Data).to_kind().unwrap(), ExtentKind::Data);
        assert_eq!(ExtentKindInfo::new(ExtentKind::Pii).to_kind().unwrap(), ExtentKind::Pii);
    }

    #[test]
    fn test_extent_kind_info_try_from() {
        assert_eq!(
            ExtentKindInfo::try_from(EXTENT_KIND_UNMAPPED).unwrap().to_kind().unwrap(),
            ExtentKind::Unmmapped
        );
        assert_eq!(
            ExtentKindInfo::try_from(EXTENT_KIND_UNUSED).unwrap().to_kind().unwrap(),
            ExtentKind::Unused
        );
        assert_eq!(
            ExtentKindInfo::try_from(EXTENT_KIND_DATA).unwrap().to_kind().unwrap(),
            ExtentKind::Data
        );
        assert_eq!(
            ExtentKindInfo::try_from(EXTENT_KIND_PII).unwrap().to_kind().unwrap(),
            ExtentKind::Pii
        );
        assert_eq!(
            ExtentKindInfo::try_from(EXTENT_KIND_PII + 1).err().unwrap(),
            Error::InvalidArgument
        );
        assert_eq!(ExtentKindInfo::try_from(10).err().unwrap(), Error::InvalidArgument);
    }

    #[test]
    fn test_data_kind_info_to_kind() {
        assert_eq!(DataKindInfo::new(DataKind::Skipped).to_kind().unwrap(), DataKind::Skipped);
        assert_eq!(DataKindInfo::new(DataKind::Zeroes).to_kind().unwrap(), DataKind::Zeroes);
        assert_eq!(
            DataKindInfo::new(DataKind::Unmodified).to_kind().unwrap(),
            DataKind::Unmodified
        );
        assert_eq!(DataKindInfo::new(DataKind::Modified).to_kind().unwrap(), DataKind::Modified);
    }

    #[test]
    fn test_data_kind_info_try_from() {
        assert_eq!(
            DataKindInfo::try_from(DATA_KIND_SKIPPED).unwrap().to_kind().unwrap(),
            DataKind::Skipped
        );
        assert_eq!(
            DataKindInfo::try_from(DATA_KIND_ZEROES).unwrap().to_kind().unwrap(),
            DataKind::Zeroes
        );
        assert_eq!(
            DataKindInfo::try_from(DATA_KIND_MODIFIED).unwrap().to_kind().unwrap(),
            DataKind::Modified
        );
        assert_eq!(
            DataKindInfo::try_from(DATA_KIND_UNMODIFIED).unwrap().to_kind().unwrap(),
            DataKind::Unmodified
        );
        assert_eq!(
            DataKindInfo::try_from(DATA_KIND_MODIFIED + 1).err().unwrap(),
            Error::InvalidArgument
        );
        assert_eq!(DataKindInfo::try_from(10).err().unwrap(), Error::InvalidArgument);
    }

    #[test]
    fn test_extent_info_deserialie() {
        let extent = Extent::new(
            10..20,
            ExtentProperties { extent_kind: ExtentKind::Pii, data_kind: DataKind::Modified },
            None,
        )
        .unwrap();

        let info = ExtentInfo::from(extent.clone());
        let mut buffer = Cursor::new(Vec::new());
        info.serialize_to(&mut buffer).unwrap();
        assert_eq!(buffer.get_ref().len() as u64, info.serialized_size());
        buffer.set_position(0);
        let read_extent = ExtentInfo::deserialize_from(&mut buffer).unwrap();
        assert_eq!(read_extent.start, extent.start());
        assert_eq!(read_extent.end, extent.end());
        assert_eq!(read_extent.data.to_kind().unwrap(), extent.properties.data_kind);
        assert_eq!(read_extent.extent.to_kind().unwrap(), extent.properties.extent_kind);
    }

    #[test]
    fn test_extent_cluster_header_serialize_all_zeroes() {
        let mut buffer = Cursor::new(Vec::new());
        ExtentClusterHeader::serialize_to(0, 0, &mut buffer).unwrap();
        assert!(buffer.get_ref().len() > 0);
        buffer.set_position(0);
        let cluster = ExtentClusterHeader::deserialize_from(&mut buffer).unwrap();
        assert_eq!(cluster.magic1, EXTENT_CLUSTER_HEADER_MAGIC1);
        assert_eq!(cluster.magic2, EXTENT_CLUSTER_HEADER_MAGIC2);
        assert_eq!(cluster.extent_count, 0);
        assert_eq!(cluster.next_cluster_offset, 0);
        assert_ne!(cluster.crc32, 0);
    }

    #[test]
    fn test_extent_cluster_header_serialize() {
        let mut buffer = Cursor::new(Vec::new());
        let len = ExtentClusterHeader::serialize_to(50, 100, &mut buffer).unwrap();
        buffer.set_position(0);
        assert_eq!(buffer.get_ref().len() as u64, len);
        let cluster = ExtentClusterHeader::deserialize_from(&mut buffer).unwrap();
        assert_eq!(buffer.get_ref().len(), cluster.serialized_size() as usize);
        assert_eq!(cluster.magic1, EXTENT_CLUSTER_HEADER_MAGIC1);
        assert_eq!(cluster.magic2, EXTENT_CLUSTER_HEADER_MAGIC2);
        assert_eq!(cluster.extent_count, 50);
        assert_eq!(cluster.next_cluster_offset, 100);
        assert_ne!(cluster.crc32, 0);
    }
}
