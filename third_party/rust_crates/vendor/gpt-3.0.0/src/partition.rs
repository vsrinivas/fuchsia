//! Partition-related types and helper functions.
//!
//! This module provides access to low-level primitives
//! to work with GPT partitions.

use bitflags::*;
use crc::crc32;
use log::*;
use std::collections::BTreeMap;
use std::convert::TryFrom;
use std::fmt;
use std::fs::{File, OpenOptions};
use std::io::{Cursor, Error, ErrorKind, Read, Result, Seek, SeekFrom, Write};
use std::path::Path;
use std::str::FromStr;

use crate::disk;
use crate::header::{parse_uuid, Header};
use crate::partition_types::Type;
use crate::DiskDevice;

bitflags! {
    /// Partition entry attributes, defined for UEFI.
    pub struct PartitionAttributes: u64 {
        /// Required platform partition.
        const PLATFORM   = 1;
        /// No Block-IO protocol.
        const EFI        = (1 << 1);
        /// Legacy-BIOS bootable partition.
        const BOOTABLE   = (1 << 2);
    }
}

/// A partition entry in a GPT partition table.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Partition {
    /// GUID of the partition type.
    pub part_type_guid: Type,
    /// UUID of the partition.
    pub part_guid: uuid::Uuid,
    /// First LBA of the partition.
    pub first_lba: u64,
    /// Last LBA of the partition.
    pub last_lba: u64,
    /// Partition flags.
    pub flags: u64,
    /// Partition name.
    pub name: String,
}

impl Partition {
    /// Create a partition entry of type "unused", whose bytes are all 0s.
    pub fn zero() -> Self {
        Self {
            part_type_guid: crate::partition_types::UNUSED,
            part_guid: uuid::Uuid::nil(),
            first_lba: 0,
            last_lba: 0,
            flags: 0,
            name: "".to_string(),
        }
    }

    /// Serialize this partition entry to its bytes representation.
    fn as_bytes(&self, entry_size: u32) -> Result<Vec<u8>> {
        let mut buf: Vec<u8> = Vec::with_capacity(entry_size as usize);

        // Type GUID.
        let tyguid = uuid::Uuid::from_str(self.part_type_guid.guid).map_err(|e| {
            Error::new(ErrorKind::Other, format!("Invalid guid: {}", e.to_string()))
        })?;
        let tyguid = tyguid.as_fields();
        buf.write_all(&tyguid.0.to_le_bytes())?;
        buf.write_all(&tyguid.1.to_le_bytes())?;
        buf.write_all(&tyguid.2.to_le_bytes())?;
        buf.write_all(tyguid.3)?;

        // Partition GUID.
        let pguid = self.part_guid.as_fields();
        buf.write_all(&pguid.0.to_le_bytes())?;
        buf.write_all(&pguid.1.to_le_bytes())?;
        buf.write_all(&pguid.2.to_le_bytes())?;
        buf.write_all(pguid.3)?;

        // LBAs and flags.
        buf.write_all(&self.first_lba.to_le_bytes())?;
        buf.write_all(&self.last_lba.to_le_bytes())?;
        buf.write_all(&self.flags.to_le_bytes())?;

        // Partition name as UTF16-LE.
        for utf16_char in self.name.encode_utf16().take(36) {
            buf.write_all(&utf16_char.to_le_bytes())?; // TODO: Check this
        }

        // Resize buffer to exact entry size.
        buf.resize(usize::try_from(entry_size).unwrap(), 0x00);

        Ok(buf)
    }

    /// Write the partition entry to the partitions area in the given file.
    /// NOTE: does not update partitions array crc32 in the headers!
    pub fn write(
        &self,
        p: &Path,
        partition_index: u64,
        start_lba: u64,
        lb_size: disk::LogicalBlockSize,
    ) -> Result<()> {
        let mut file = OpenOptions::new().write(true).read(true).open(p)?;
        self.write_to_device(&mut file, partition_index, start_lba, lb_size, 128)
    }

    /// Write the partition entry to the partitions area in the given device.
    /// NOTE: does not update partitions array crc32 in the headers!
    pub fn write_to_device<D: DiskDevice>(
        &self,
        device: &mut D,
        partition_index: u64,
        start_lba: u64,
        lb_size: disk::LogicalBlockSize,
        bytes_per_partition: u32,
    ) -> Result<()> {
        debug!("writing partition to: {:?}", device);
        let pstart = start_lba
            .checked_mul(lb_size.into())
            .ok_or_else(|| Error::new(ErrorKind::Other, "partition overflow - start offset"))?;
        // The offset is bytes_per_partition * partition_index
        let offset = partition_index
            .checked_mul(u64::from(bytes_per_partition))
            .ok_or_else(|| Error::new(ErrorKind::Other, "partition overflow"))?;
        trace!("seeking to partition start: {}", pstart + offset);
        device.seek(SeekFrom::Start(pstart + offset))?;
        trace!("writing {:?}", &self.as_bytes(bytes_per_partition));
        device.write_all(&self.as_bytes(bytes_per_partition)?)?;

        Ok(())
    }

    /// Write empty partition entries starting at the given index in the partition array
    /// for the given number of entries...
    pub fn write_zero_entries_to_device<D: DiskDevice>(
        device: &mut D,
        starting_partition_index: u64,
        number_entries: u64,
        start_lba: u64,
        lb_size: disk::LogicalBlockSize,
        bytes_per_partition: u32,
    ) -> Result<()> {
        trace!("writing {} unused partition entries starting at index {}, start_lba={}",
            number_entries, starting_partition_index, start_lba);
        let pstart = start_lba
            .checked_mul(lb_size.into())
            .ok_or_else(|| Error::new(ErrorKind::Other, "partition overflow - start offset"))?;
        let offset = starting_partition_index
            .checked_mul(u64::from(bytes_per_partition))
            .ok_or_else(|| Error::new(ErrorKind::Other, "partition overflow"))?;
        trace!("seeking to starting partition start: {}", pstart + offset);
        device.seek(SeekFrom::Start(pstart + offset))?;
        let bytes_to_zero = u64::from(bytes_per_partition)
            .checked_mul(number_entries)
            .and_then(|x| usize::try_from(x).ok())
            .ok_or_else(|| Error::new(ErrorKind::Other, "partition overflow - bytes to zero"))?;
        device.write_all(&vec![0_u8; bytes_to_zero])?;
        Ok(())
    }

    /// Return the length (in bytes) of this partition.
    /// Partition size is calculated as (last_lba + 1 - first_lba) * block_size
    /// Bounds are inclusive, meaning we add one to account for the full last logical block
    pub fn bytes_len(&self, lb_size: disk::LogicalBlockSize) -> Result<u64> {
        let len = self
            .last_lba
            .checked_sub(self.first_lba)
            .ok_or_else(|| Error::new(ErrorKind::Other, "partition length underflow - sectors"))?
            .checked_add(1)
            .ok_or_else(|| Error::new(ErrorKind::Other, "partition length overflow - sectors"))?
            .checked_mul(lb_size.into())
            .ok_or_else(|| Error::new(ErrorKind::Other, "partition length overflow - bytes"))?;
        Ok(len)
    }

    /// Return the starting offset (in bytes) of this partition.
    pub fn bytes_start(&self, lb_size: disk::LogicalBlockSize) -> Result<u64> {
        let len = self
            .first_lba
            .checked_mul(lb_size.into())
            .ok_or_else(|| Error::new(ErrorKind::Other, "partition start overflow - bytes"))?;
        Ok(len)
    }

    /// Check whether this partition is in use.
    pub fn is_used(&self) -> bool {
        self.part_type_guid.guid != crate::partition_types::UNUSED.guid
    }

    /// Return the number of sectors in the partition.
    pub fn size(&self) -> Result<u64> {
        match self.last_lba.checked_sub(self.first_lba) {
            Some(size) => Ok(size),
            None => Err(Error::new(
                ErrorKind::Other,
                "Invalid partition.  last_lba < first_lba",
            )),
        }
    }
}

impl fmt::Display for Partition {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "Partition:\t\t{}\nPartition GUID:\t\t{}\nPartition Type:\t\t{}\n\
             Span:\t\t\t{} - {}\nFlags:\t\t\t{}",
            self.name,
            self.part_guid,
            self.part_type_guid.guid,
            self.first_lba,
            self.last_lba,
            self.flags,
        )
    }
}

fn read_part_name(rdr: &mut Cursor<&[u8]>) -> Result<String> {
    trace!("Reading partition name");
    let mut namebytes: Vec<u16> = Vec::new();
    for _ in 0..36 {
        let b = u16::from_le_bytes(read_exact_buff!(bbuff, rdr, 2));
        if b == 0 {
            break
        }
        namebytes.push(b);
    }

    Ok(String::from_utf16_lossy(&namebytes))
}

/// Read a GPT partition table.
///
/// ## Example
///
/// ```rust,no_run
/// use gpt::{header, disk, partition};
/// use std::path::Path;
///
/// let lb_size = disk::DEFAULT_SECTOR_SIZE;
/// let diskpath = Path::new("/dev/sdz");
/// let hdr = header::read_header(diskpath, lb_size).unwrap();
/// let partitions = partition::read_partitions(diskpath, &hdr, lb_size).unwrap();
/// println!("{:#?}", partitions);
/// ```
pub fn read_partitions(
    path: impl AsRef<Path>,
    header: &Header,
    lb_size: disk::LogicalBlockSize,
) -> Result<BTreeMap<u32, Partition>> {
    debug!("reading partitions from file: {}", path.as_ref().display());
    let mut file = File::open(path)?;
    file_read_partitions(&mut file, header, lb_size)
}

/// Read a GPT partition table from an open `Read` + `Seek` object.
pub fn file_read_partitions<D: Read + Seek>(
    file: &mut D,
    header: &Header,
    lb_size: disk::LogicalBlockSize,
) -> Result<BTreeMap<u32, Partition>> {
    let pstart = header
        .part_start
        .checked_mul(lb_size.into())
        .ok_or_else(|| Error::new(ErrorKind::Other, "partition overflow - start offset"))?;
    trace!("seeking to partitions start: {:#x}", pstart);
    let _ = file.seek(SeekFrom::Start(pstart))?;
    let mut parts: BTreeMap<u32, Partition> = BTreeMap::new();

    trace!("scanning {} partitions", header.num_parts);
    let mut count = 0;
    for i in 0..header.num_parts {
        let mut bytes: [u8; 56] = [0; 56];
        let mut nameraw: [u8; 72] = [0; 72];

        file.read_exact(&mut bytes)?;
        file.read_exact(&mut nameraw)?;

        let test: [u8; 56] = [0; 56];
        let test2: [u8; 72] = [0; 72];
        // Note: unused partition entries are zeroed, so skip them
        if bytes.eq(&test[0..]) && nameraw.eq(&test2[0..]) {
            count += 1;
        } else {
            let mut reader = Cursor::new(&bytes[..]);
            let type_guid = parse_uuid(&mut reader)?;
            let part_guid = parse_uuid(&mut reader)?;

            let partname = read_part_name(&mut Cursor::new(&nameraw[..]))?;
            let p = Partition {
                part_type_guid: Type::from_uuid(&type_guid).unwrap_or_default(),
                part_guid,
                first_lba: u64::from_le_bytes(read_exact_buff!(flba, reader, 8)),
                last_lba: u64::from_le_bytes(read_exact_buff!(llba, reader, 8)),
                flags: u64::from_le_bytes(read_exact_buff!(flagbuff, reader, 8)),
                name: partname.to_string(),
            };

            parts.insert(i + 1, p);
        }
    }
    debug!("Num Zeroed partitions {:?}\n\n", count);

    debug!("checking partition table CRC");
    let _ = file.seek(SeekFrom::Start(pstart))?;
    let pt_len = u64::from(header.num_parts)
        .checked_mul(header.part_size.into())
        .ok_or_else(|| Error::new(ErrorKind::Other, "partitions - size"))?;
    let mut table = vec![0; pt_len as usize];
    file.read_exact(&mut table)?;

    let comp_crc = crc32::checksum_ieee(&table);
    if comp_crc != header.crc32_parts {
        return Err(Error::new(ErrorKind::Other, "partition table CRC mismatch"));
    }

    Ok(parts)
}

#[cfg(test)]
mod tests {
    use crate::disk;
    use crate::partition;

    #[test]
    fn test_zero_part() {
        let p0 = partition::Partition::zero();

        let b128 = p0.as_bytes(128).unwrap();
        assert_eq!(b128.len(), 128);
        assert_eq!(b128, vec![0_u8; 128]);

        let b256 = p0.as_bytes(256).unwrap();
        assert_eq!(b256.len(), 256);
        assert_eq!(b256, vec![0_u8; 256]);
    }

    #[test]
    fn test_part_bytes_len() {
        {
            // Zero.
            let p0 = partition::Partition::zero();
            let b512len = p0.bytes_len(disk::LogicalBlockSize::Lb512).unwrap();
            let b4096len = p0.bytes_len(disk::LogicalBlockSize::Lb4096).unwrap();

            // The lower bound of partition size is equal to the logical block size.
            // This is because the bounds for the partition size are inclusive and use
            // logical block addressing, meaning that even when the lba_start and lba_end
            // are set to the same block, you still have the space within that block.
            assert_eq!(b512len, 512);
            assert_eq!(b4096len, 4096);
        }

        {
            // Negative length.
            let mut p1 = partition::Partition::zero();
            p1.first_lba = p1.last_lba + 1;
            p1.bytes_len(disk::LogicalBlockSize::Lb512).unwrap_err();
            p1.bytes_len(disk::LogicalBlockSize::Lb4096).unwrap_err();
        }

        {
            // Overflowing u64 length.
            let mut p2 = partition::Partition::zero();
            p2.last_lba = <u64>::max_value();
            p2.bytes_len(disk::LogicalBlockSize::Lb512).unwrap_err();
            p2.bytes_len(disk::LogicalBlockSize::Lb4096).unwrap_err();
        }

        {
            // Positive value.
            let mut p3 = partition::Partition::zero();
            p3.first_lba = 2;
            p3.last_lba = 3;
            let b512len = p3.bytes_len(disk::LogicalBlockSize::Lb512).unwrap();
            let b4096len = p3.bytes_len(disk::LogicalBlockSize::Lb4096).unwrap();

            assert_eq!(b512len, 2 * 512);
            assert_eq!(b4096len, 2 * 4096);
        }
    }

    #[test]
    fn test_part_bytes_start() {
        {
            // Zero.
            let p0 = partition::Partition::zero();
            let b512len = p0.bytes_start(disk::LogicalBlockSize::Lb512).unwrap();
            let b4096len = p0.bytes_start(disk::LogicalBlockSize::Lb4096).unwrap();

            assert_eq!(b512len, 0);
            assert_eq!(b4096len, 0);
        }

        {
            // Overflowing u64 start.
            let mut p1 = partition::Partition::zero();
            p1.first_lba = <u64>::max_value();
            p1.bytes_len(disk::LogicalBlockSize::Lb512).unwrap_err();
            p1.bytes_len(disk::LogicalBlockSize::Lb4096).unwrap_err();
        }

        {
            // Positive value.
            let mut p2 = partition::Partition::zero();
            p2.first_lba = 2;
            let b512start = p2.bytes_start(disk::LogicalBlockSize::Lb512).unwrap();
            let b4096start = p2.bytes_start(disk::LogicalBlockSize::Lb4096).unwrap();

            assert_eq!(b512start, 2 * 512);
            assert_eq!(b4096start, 2 * 4096);
        }
    }
}
