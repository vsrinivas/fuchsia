//! MBR-related types and helper functions.
//!
//! This module provides access to low-level primitives
//! to work with Master Boot Record (MBR), also known as LBA0.

use crate::disk;
use crate::DiskDevice;
use std::io::{Read, Write};
use std::{fmt, io};

/// Protective MBR, as defined by GPT.
pub struct ProtectiveMBR {
    bootcode: [u8; 440],
    disk_signature: [u8; 4],
    unknown: u16,
    partitions: [PartRecord; 4],
    signature: [u8; 2],
}

impl fmt::Debug for ProtectiveMBR {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Protective MBR, partitions: {:#?}", self.partitions)
    }
}

impl Default for ProtectiveMBR {
    fn default() -> Self {
        Self {
            bootcode: [0x00; 440],
            disk_signature: [0x00; 4],
            unknown: 0,
            partitions: [
                PartRecord::new_protective(None),
                PartRecord::zero(),
                PartRecord::zero(),
                PartRecord::zero(),
            ],
            signature: [0x55, 0xAA],
        }
    }
}

impl ProtectiveMBR {
    /// Create a default protective-MBR object.
    pub fn new() -> Self {
        Self::default()
    }

    /// Create a protective-MBR object with a specific protective partition size (in LB).
    /// The protective partition size should be the size of the disk - 1 (because the protective
    /// partition always begins at LBA 1 (the second sector)).
    pub fn with_lb_size(lb_size: u32) -> Self {
        Self {
            bootcode: [0x00; 440],
            disk_signature: [0x00; 4],
            unknown: 0,
            partitions: [
                PartRecord::new_protective(Some(lb_size)),
                PartRecord::zero(),
                PartRecord::zero(),
                PartRecord::zero(),
            ],
            signature: [0x55, 0xAA],
        }
    }

    /// Parse input bytes into a protective-MBR object.
    pub fn from_bytes(buf: &[u8], sector_size: disk::LogicalBlockSize) -> io::Result<Self> {
        let mut pmbr = Self::new();
        let totlen: u64 = sector_size.into();

        if buf.len() != (totlen as usize) {
            return Err(io::Error::new(io::ErrorKind::Other, "invalid MBR length"));
        }

        pmbr.bootcode.copy_from_slice(&buf[0..440]);
        pmbr.disk_signature.copy_from_slice(&buf[440..444]);
        pmbr.unknown = u16::from_le_bytes(read_exact_buff!(pmbru, &buf[444..446], 2));

        for (i, p) in pmbr.partitions.iter_mut().enumerate() {
            let start = i
                .checked_mul(16)
                .ok_or_else(|| {
                    io::Error::new(
                        io::ErrorKind::Other,
                        "partition record overflow - entry start",
                    )
                })?
                .checked_add(446)
                .ok_or_else(|| {
                    io::Error::new(io::ErrorKind::Other, "partition overflow - start offset")
                })?;
            let end = start.checked_add(16).ok_or_else(|| {
                io::Error::new(
                    io::ErrorKind::Other,
                    "partition record overflow - end offset",
                )
            })?;
            *p = PartRecord::from_bytes(&buf[start..end])?;
        }

        pmbr.signature.copy_from_slice(&buf[510..512]);
        if pmbr.signature != [0x55, 0xAA] {
            return Err(io::Error::new(
                io::ErrorKind::Other,
                "invalid MBR signature",
            ));
        };
        Ok(pmbr)
    }

    /// Read the LBA0 of a disk device and parse it into a protective-MBR object.
    pub fn from_disk<D: DiskDevice>(
        device: &mut D,
        sector_size: disk::LogicalBlockSize
    ) -> io::Result<Self> {
        let totlen: u64 = sector_size.into();
        let mut buf = vec![0_u8; totlen as usize];
        let cur = device.seek(io::SeekFrom::Current(0))?;

        device.seek(io::SeekFrom::Start(0))?;
        device.read_exact(&mut buf)?;
        let pmbr = Self::from_bytes(&buf, sector_size);
        device.seek(io::SeekFrom::Start(cur))?;
        pmbr
    }

    /// Return the memory representation of this MBR as a byte vector.
    pub fn as_bytes(&self) -> io::Result<Vec<u8>> {
        let mut buf: Vec<u8> = Vec::with_capacity(512);

        buf.write_all(&self.bootcode)?;
        buf.write_all(&self.disk_signature)?;
        buf.write_all(&self.unknown.to_le_bytes())?;
        for p in &self.partitions {
            let pdata = p.as_bytes()?;
            buf.write_all(&pdata)?;
        }
        buf.write_all(&self.signature)?;
        Ok(buf)
    }

    /// Return the 440 bytes of BIOS bootcode.
    pub fn bootcode(&self) -> &[u8; 440] {
        &self.bootcode
    }

    /// Set the 440 bytes of BIOS bootcode.
    ///
    /// This only changes the in-memory state, without overwriting
    /// any on-disk data.
    pub fn set_bootcode(&mut self, bootcode: [u8; 440]) -> &Self {
        self.bootcode = bootcode;
        self
    }

    /// Return the 4 bytes of MBR disk signature.
    pub fn disk_signature(&self) -> &[u8; 4] {
        &self.disk_signature
    }

    /// Set the 4 bytes of MBR disk signature.
    ///
    /// This only changes the in-memory state, without overwriting
    /// any on-disk data.
    pub fn set_disk_signature(&mut self, sig: [u8; 4]) -> &Self {
        self.disk_signature = sig;
        self
    }

    /// Returns the given partition (0..=3) or None if the partition index is invalid.
    pub fn partition(&self, partition_index: usize) -> Option<PartRecord> {
        if partition_index >= self.partitions.len() {
            None
        } else {
            Some(self.partitions[partition_index])
        }
    }

    /// Set the data for the given partition.
    /// Returns the previous partition record or None if the partition index is invalid.
    ///
    /// This only changes the in-memory state, without overwriting
    /// any on-disk data.
    pub fn set_partition(&mut self, partition_index: usize, partition: PartRecord) -> Option<PartRecord> {
        if partition_index >= self.partitions.len() {
            None
        } else {
            Some(std::mem::replace(&mut self.partitions[partition_index], partition))
        }
    }

    /// Write a protective MBR to LBA0, overwriting any existing data.
    pub fn overwrite_lba0<D: DiskDevice>(&self, device: &mut D) -> io::Result<usize> {
        let cur = device.seek(io::SeekFrom::Current(0))?;
        let _ = device.seek(io::SeekFrom::Start(0))?;
        let data = self.as_bytes()?;
        device.write_all(&data)?;
        device.flush()?;

        device.seek(io::SeekFrom::Start(cur))?;
        Ok(data.len())
    }

    /// Update LBA0, preserving most bytes of any existing MBR.
    ///
    /// This overwrites the four MBR partition records and the
    /// well-known signature, leaving all other MBR bits as-is.
    pub fn update_conservative<D: DiskDevice>(&self, device: &mut D) -> io::Result<usize> {
        let cur = device.seek(io::SeekFrom::Current(0))?;
        // Seek to first partition record.
        // (GPT spec 2.7 - sec. 5.2.3 - table 15)
        let _ = device.seek(io::SeekFrom::Start(446))?;
        for p in &self.partitions {
            let pdata = p.as_bytes()?;
            device.write_all(&pdata)?;
        }
        device.write_all(&self.signature)?;
        device.flush()?;

        device.seek(io::SeekFrom::Start(cur))?;
        let bytes_updated: usize = (16 * 4) + 2;
        Ok(bytes_updated)
    }
}

/// A partition record, MBR-style.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct PartRecord {
    /// Bit 7 set if partition is active (bootable)
    pub boot_indicator: u8,
    /// CHS address of partition start: 8-bit value of head in CHS address
    pub start_head: u8,
    /// CHS address of partition start: Upper 2 bits are 8th-9th bits of cylinder, lower 6 bits are sector
    pub start_sector: u8,
    /// CHS address of partition start: Lower 8 bits of cylinder
    pub start_track: u8,
    /// Partition type. See https://www.win.tue.nl/~aeb/partitions/partition_types-1.html
    pub os_type: u8,
    /// CHS address of partition end: 8-bit value of head in CHS address
    pub end_head: u8,
    /// CHS address of partition end: Upper 2 bits are 8th-9th bits of cylinder, lower 6 bits are sector
    pub end_sector: u8,
    /// CHS address of partition end: Lower 8 bits of cylinder
    pub end_track: u8,
    /// LBA of start of partition
    pub lb_start: u32,
    /// Number of sectors in partition
    pub lb_size: u32,
}

impl PartRecord {
    /// Create a protective Partition Record object with a specific disk size (in LB).
    pub fn new_protective(lb_size: Option<u32>) -> Self {
        let size = lb_size.unwrap_or(0xFF_FF_FF_FF);
        Self {
            boot_indicator: 0x00,
            start_head: 0x00,
            start_sector: 0x02,
            start_track: 0x00,
            os_type: 0xEE,
            end_head: 0xFF,
            end_sector: 0xFF,
            end_track: 0xFF,
            lb_start: 1,
            lb_size: size,
        }
    }

    /// Create an all-zero Partition Record.
    pub fn zero() -> Self {
        Self {
            boot_indicator: 0x00,
            start_head: 0x00,
            start_sector: 0x00,
            start_track: 0x00,
            os_type: 0x00,
            end_head: 0x00,
            end_sector: 0x00,
            end_track: 0x00,
            lb_start: 0,
            lb_size: 0,
        }
    }

    /// Parse input bytes into a Partition Record.
    pub fn from_bytes(buf: &[u8]) -> io::Result<Self> {
        if buf.len() != 16 {
            return Err(io::Error::new(
                io::ErrorKind::Other,
                "invalid length for a partition record",
            ));
        };
        let pr = Self {
            boot_indicator: buf[0],
            start_head: buf[1],
            start_sector: buf[2],
            start_track: buf[3],
            os_type: buf[4],
            end_head: buf[5],
            end_sector: buf[6],
            end_track: buf[7],
            lb_start: u32::from_le_bytes(read_exact_buff!(lbs, &buf[8..12], 4)),
            lb_size: u32::from_le_bytes(read_exact_buff!(lbsize, &buf[12..16], 4) ),
        };
        Ok(pr)
    }

    /// Return the memory representation of this Partition Record as a byte vector.
    pub fn as_bytes(&self) -> io::Result<Vec<u8>> {
        let mut buf: Vec<u8> = Vec::with_capacity(16);

        buf.write_all(&self.boot_indicator.to_le_bytes())?;

        buf.write_all(&self.start_head.to_le_bytes())?;
        buf.write_all(&self.start_sector.to_le_bytes())?;
        buf.write_all(&self.start_track.to_le_bytes())?;

        buf.write_all(&self.os_type.to_le_bytes())?;

        buf.write_all(&self.end_head.to_le_bytes())?;
        buf.write_all(&self.end_sector.to_le_bytes())?;
        buf.write_all(&self.end_track.to_le_bytes())?;

        buf.write_all(&self.lb_start.to_le_bytes())?;
        buf.write_all(&self.lb_size.to_le_bytes())?;

        Ok(buf)
    }
}

/// Return the 440 bytes of BIOS bootcode.
pub fn read_bootcode<D: DiskDevice>(device: &mut D) -> io::Result<[u8; 440]> {
    let bootcode_offset = 0;
    let cur = device.seek(io::SeekFrom::Current(0))?;
    let _ = device.seek(io::SeekFrom::Start(bootcode_offset))?;
    let mut bootcode = [0x00; 440];
    device.read_exact(&mut bootcode)?;

    device.seek(io::SeekFrom::Start(cur))?;
    Ok(bootcode)
}

/// Write the 440 bytes of BIOS bootcode.
pub fn write_bootcode<D: DiskDevice>(device: &mut D, bootcode: &[u8; 440]) -> io::Result<()> {
    let bootcode_offset = 0;
    let cur = device.seek(io::SeekFrom::Current(0))?;
    let _ = device.seek(io::SeekFrom::Start(bootcode_offset))?;
    device.write_all(bootcode)?;
    device.flush()?;

    device.seek(io::SeekFrom::Start(cur))?;
    Ok(())
}

/// Read the 4 bytes of MBR disk signature.
pub fn read_disk_signature<D: DiskDevice>(device: &mut D) -> io::Result<[u8; 4]> {
    let dsig_offset = 440;
    let cur = device.seek(io::SeekFrom::Current(0))?;
    let _ = device.seek(io::SeekFrom::Start(dsig_offset))?;
    let mut dsig = [0x00; 4];
    device.read_exact(&mut dsig)?;

    device.seek(io::SeekFrom::Start(cur))?;
    Ok(dsig)
}

/// Write the 4 bytes of MBR disk signature.
#[cfg_attr(feature = "cargo-clippy", allow(clippy::trivially_copy_pass_by_ref))]
pub fn write_disk_signature<D: DiskDevice>(device: &mut D, sig: &[u8; 4]) -> io::Result<()> {
    let dsig_offset = 440;
    let cur = device.seek(io::SeekFrom::Current(0))?;
    let _ = device.seek(io::SeekFrom::Start(dsig_offset))?;
    device.write_all(sig)?;
    device.flush()?;

    device.seek(io::SeekFrom::Start(cur))?;
    Ok(())
}
