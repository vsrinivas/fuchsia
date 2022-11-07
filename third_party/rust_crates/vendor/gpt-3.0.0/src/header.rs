//! GPT-header object and helper functions.

use crc::{crc32, Hasher32};
use log::*;
use std::collections::BTreeMap;
use std::fmt;
use std::fs::{File, OpenOptions};
use std::io::{Cursor, Error, ErrorKind, Read, Result, Seek, SeekFrom, Write};
use std::path::Path;

use crate::disk;
use crate::partition;

/// Header describing a GPT disk.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Header {
    /// GPT header magic signature, hardcoded to "EFI PART".
    pub signature: String, // Offset  0. "EFI PART", 45h 46h 49h 20h 50h 41h 52h 54h
    /// 00 00 01 00
    pub revision: u32, // Offset  8
    /// little endian
    pub header_size_le: u32, // Offset 12
    /// CRC32 of the header with crc32 section zeroed
    pub crc32: u32, // Offset 16
    /// must be 0
    pub reserved: u32, // Offset 20
    /// For main header, 1
    pub current_lba: u64, // Offset 24
    /// LBA for backup header
    pub backup_lba: u64, // Offset 32
    /// First usable LBA for partitions (primary table last LBA + 1)
    pub first_usable: u64, // Offset 40
    /// Last usable LBA (secondary partition table first LBA - 1)
    pub last_usable: u64, // Offset 48
    /// UUID of the disk
    pub disk_guid: uuid::Uuid, // Offset 56
    /// Starting LBA of partition entries
    pub part_start: u64, // Offset 72
    /// Number of partition entries
    pub num_parts: u32, // Offset 80
    /// Size of a partition entry, usually 128
    pub part_size: u32, // Offset 84
    /// CRC32 of the partition table
    pub crc32_parts: u32, // Offset 88
}

impl Header {
    pub(crate) fn compute_new(
        primary: bool,
        pp: &BTreeMap<u32, partition::Partition>,
        guid: uuid::Uuid,
        backup_offset: u64,
        original_header: &Option<Header>,
        lb_size: disk::LogicalBlockSize,
        num_parts: Option<u32>,
    ) -> Result<Self> {
        let (cur, bak) = if primary {
            (1, backup_offset)
        } else {
            (backup_offset, 1)
        };

        // really this number should actually usually be 128, as it is the
        // TOTAL number of entries in the partition table, NOT the number USED.
        // UEFI requires space for 128 minimum, but the number can be increased or reduced.
        // If we're creating the table from scratch, make sure the table contains enough
        // room to be UEFI compliant.
        let parts = match num_parts {
            Some(p) => {p}
            None => {
                match original_header {
                    Some(header) => header.num_parts,
                    None => (pp.iter().filter(|p| p.1.is_used()).count() as u32).max(128),
                }
            }
        };
        //though usually 128, it might be a different number
        let part_size = match original_header {
            Some(header) => header.part_size,
            None => 128,
        };

        let part_array_num_bytes = u64::from(parts * part_size);
        // If not an exact multiple of a sector, round up to the next # of whole sectors.
        let lb_size_u64 = Into::<u64>::into(lb_size);
        let part_array_num_lbs = (part_array_num_bytes + (lb_size_u64 - 1)) / lb_size_u64;

        // sometimes the first usable isn't sector 34, fdisk starts at 2048
        // alternatively, if the sector size is 4096 it might not be 34 either.
        // to align partition boundaries (https://metebalci.com/blog/a-quick-tour-of-guid-partition-table-gpt/)
        let first = match num_parts {
            Some(_) => 1 + 1 + part_array_num_lbs,
            None => {
                match original_header {
                    Some(header) => header.first_usable,
                    None => 1 + 1 + part_array_num_lbs, //protective MBR + GPT header + partition array
                }
            }
        };
        let last = match num_parts {
            Some(_) => {
                // last is inclusive: end of disk is (partition array) (backup header)
                backup_offset
                .checked_sub(part_array_num_lbs + 1)
                .ok_or_else(|| Error::new(ErrorKind::Other, "header underflow - last usable"))?
            },
            None => {
                match original_header {
                    Some(header) => header.last_usable,
                    None => {
                        // last is inclusive: end of disk is (partition array) (backup header)
                        backup_offset
                            .checked_sub(part_array_num_lbs + 1)
                            .ok_or_else(|| Error::new(ErrorKind::Other, "header underflow - last usable"))?
                    }
                }
            }
        };
        // the partition entry LBA starts at 2 (usually) for primary headers and at the last_usable + 1 for backup headers
        let part_start = if primary { 2 } else { last + 1 };

        let hdr = Header {
            signature: "EFI PART".to_string(),
            revision: 65536,
            header_size_le: 92,
            crc32: 0,
            reserved: 0,
            current_lba: cur,
            backup_lba: bak,
            first_usable: first,
            last_usable: last,
            disk_guid: guid,
            part_start,
            num_parts: parts,
            part_size,
            crc32_parts: 0,
        };

        Ok(hdr)
    }

    /// Write the primary header.
    ///
    /// With a CRC32 set to zero this will set the crc32 after
    /// writing the header out.
    pub fn write_primary<D: Read + Write + Seek>(
        &self,
        file: &mut D,
        lb_size: disk::LogicalBlockSize,
    ) -> Result<usize> {
        // This is the primary header. It must start before the backup one.
        if self.current_lba >= self.backup_lba {
            debug!(
                "current lba: {} backup_lba: {}",
                self.current_lba, self.backup_lba
            );
            return Err(Error::new(
                ErrorKind::Other,
                "primary header does not start before backup one",
            ));
        }
        self.file_write_header(file, self.current_lba, lb_size)
    }

    /// Write the backup header.
    ///
    /// With a CRC32 set to zero this will set the crc32 after
    /// writing the header out.
    pub fn write_backup<D: Read + Write + Seek>(
        &self,
        file: &mut D,
        lb_size: disk::LogicalBlockSize,
    ) -> Result<usize> {
        // This is the backup header. It must start after the primary one.
        if self.current_lba <= self.backup_lba {
            debug!(
                "current lba: {} backup_lba: {}",
                self.current_lba, self.backup_lba
            );
            return Err(Error::new(
                ErrorKind::Other,
                "backup header does not start after primary one",
            ));
        }
        self.file_write_header(file, self.current_lba, lb_size)
    }

    /// Write an header to an arbitrary LBA.
    ///
    /// With a CRC32 set to zero this will set the crc32 after
    /// writing the header out.
    fn file_write_header<D: Read + Write + Seek>(
        &self,
        file: &mut D,
        lba: u64,
        lb_size: disk::LogicalBlockSize,
    ) -> Result<usize> {
        // Build up byte array in memory
        let parts_checksum = partentry_checksum(file, self, lb_size)?;
        trace!("computed partitions CRC32: {:#x}", parts_checksum);
        let bytes = self.as_bytes(None, Some(parts_checksum))?;
        trace!("bytes before checksum: {:?}", bytes);

        // Calculate the CRC32 from the byte array
        let checksum = calculate_crc32(&bytes);
        trace!("computed header CRC32: {:#x}", checksum);

        // Write it to disk in 1 shot
        let start = lba
            .checked_mul(lb_size.into())
            .ok_or_else(|| Error::new(ErrorKind::Other, "header overflow - offset"))?;
        trace!("Seeking to {}", start);
        let _ = file.seek(SeekFrom::Start(start))?;
        let mut header_bytes = self.as_bytes(Some(checksum), Some(parts_checksum))?;
        // Per the spec, the rest of the logical block must be zeros...
        header_bytes.resize(Into::<usize>::into(lb_size), 0x00);
        let len = file.write(&header_bytes)?;
        trace!("Wrote {} bytes", len);

        Ok(len)
    }

    fn as_bytes(
        &self,
        header_checksum: Option<u32>,
        partitions_checksum: Option<u32>,
    ) -> Result<Vec<u8>> {
        let mut buff: Vec<u8> = Vec::new();
        let disk_guid_fields = self.disk_guid.as_fields();

        buff.write_all(self.signature.as_bytes())?;
        buff.write_all(&self.revision.to_le_bytes())?;
        buff.write_all(&self.header_size_le.to_le_bytes())?;
        match header_checksum {
            Some(c) => buff.write_all(&c.to_le_bytes())?,
            None => buff.write_all(&[0_u8; 4])?,
        };
        buff.write_all(&[0; 4])?;
        buff.write_all(&self.current_lba.to_le_bytes())?;
        buff.write_all(&self.backup_lba.to_le_bytes())?;
        buff.write_all(&self.first_usable.to_le_bytes())?;
        buff.write_all(&self.last_usable.to_le_bytes())?;
        buff.write_all(&disk_guid_fields.0.to_le_bytes())?;
        buff.write_all(&disk_guid_fields.1.to_le_bytes())?;
        buff.write_all(&disk_guid_fields.2.to_le_bytes())?;
        buff.write_all(disk_guid_fields.3)?;
        buff.write_all(&self.part_start.to_le_bytes())?;
        buff.write_all(&self.num_parts.to_le_bytes())?;
        buff.write_all(&self.part_size.to_le_bytes())?;
        match partitions_checksum {
            Some(c) => buff.write_all(&c.to_le_bytes())?,
            None => buff.write_all(&[0_u8; 4])?,
        };
        Ok(buff)
    }
}

/// Parses a uuid with first 3 portions in little endian.
pub fn parse_uuid(rdr: &mut Cursor<&[u8]>) -> Result<uuid::Uuid> {
    let d1: u32 = u32::from_le_bytes(read_exact_buff!(d1b, rdr, 4));
    let d2: u16 = u16::from_le_bytes(read_exact_buff!(d2b, rdr, 2));
    let d3: u16 = u16::from_le_bytes(read_exact_buff!(d3b, rdr, 2));
    let uuid = uuid::Uuid::from_fields(
        d1,
        d2,
        d3,
        &rdr.get_ref()[rdr.position() as usize..rdr.position() as usize + 8],
    );
    rdr.seek(SeekFrom::Current(8))?;

    match uuid {
        Ok(uuid) => Ok(uuid),
        Err(_) => Err(Error::new(ErrorKind::Other, "Invalid Disk UUID?")),
    }
}

impl fmt::Display for Header {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "Disk:\t\t{}\nCRC32:\t\t{}\nTable CRC:\t{}",
            self.disk_guid, self.crc32, self.crc32_parts
        )
    }
}

/// Read a GPT header from a given path.
///
/// ## Example
///
/// ```rust,no_run
/// use gpt::header::read_header;
///
/// let lb_size = gpt::disk::DEFAULT_SECTOR_SIZE;
/// let diskpath = std::path::Path::new("/dev/sdz");
///
/// let h = read_header(diskpath, lb_size).unwrap();
/// ```
pub fn read_header(
    path: impl AsRef<Path>,
    sector_size: disk::LogicalBlockSize
) -> Result<Header> {
    let mut file = File::open(path)?;
    read_primary_header(&mut file, sector_size)
}

/// Read a GPT header from any device capable of reading and seeking.
pub fn read_header_from_arbitrary_device<D: Read + Seek>(
    device: &mut D,
    sector_size: disk::LogicalBlockSize,
) -> Result<Header> {
    read_primary_header(device, sector_size)
}

pub(crate) fn read_primary_header<D: Read + Seek>(
    file: &mut D,
    sector_size: disk::LogicalBlockSize,
) -> Result<Header> {
    let cur = file.seek(SeekFrom::Current(0)).unwrap_or(0);
    let offset: u64 = sector_size.into();
    let res = file_read_header(file, offset);
    let _ = file.seek(SeekFrom::Start(cur));
    res
}

pub(crate) fn read_backup_header<D: Read + Seek>(
    file: &mut D,
    sector_size: disk::LogicalBlockSize,
) -> Result<Header> {
    let cur = file.seek(SeekFrom::Current(0)).unwrap_or(0);
    let h2sect = find_backup_lba(file, sector_size)?;
    let offset = h2sect
        .checked_mul(sector_size.into())
        .ok_or_else(|| Error::new(ErrorKind::Other, "backup header overflow - offset"))?;
    let res = file_read_header(file, offset);
    let _ = file.seek(SeekFrom::Start(cur));
    res
}

pub(crate) fn file_read_header<D: Read + Seek>(file: &mut D, offset: u64) -> Result<Header> {
    let _ = file.seek(SeekFrom::Start(offset));
    let mut hdr: [u8; 92] = [0; 92];

    let _ = file.read_exact(&mut hdr);
    let mut reader = Cursor::new(&hdr[..]);

    let sigstr = String::from_utf8_lossy(
        &reader.get_ref()[reader.position() as usize..reader.position() as usize + 8],
    );
    reader.seek(SeekFrom::Current(8))?;

    if sigstr != "EFI PART" {
        return Err(Error::new(ErrorKind::Other, "invalid GPT signature"));
    };

    let h = Header {
        signature: sigstr.to_string(),
        revision: u32::from_le_bytes(read_exact_buff!(rev, reader, 4)),
        header_size_le: u32::from_le_bytes(read_exact_buff!(hsle, reader, 4)),
        crc32: u32::from_le_bytes(read_exact_buff!(crc32, reader, 4)),
        reserved: u32::from_le_bytes(read_exact_buff!(reserv, reader, 4)),
        current_lba: u64::from_le_bytes(read_exact_buff!(clba, reader, 8)),
        backup_lba: u64::from_le_bytes(read_exact_buff!(blba, reader, 8)),
        first_usable: u64::from_le_bytes(read_exact_buff!(fusable, reader, 8)),
        last_usable: u64::from_le_bytes(read_exact_buff!(lusable, reader, 8)),
        disk_guid: parse_uuid(&mut reader)?,
        part_start: u64::from_le_bytes(read_exact_buff!(pstart, reader, 8)),
        // Note: this will always return the total number of partition entries
        // in the array, not how many are actually used
        num_parts: u32::from_le_bytes(read_exact_buff!(nparts, reader, 4)),
        part_size: u32::from_le_bytes(read_exact_buff!(partsize, reader, 4)),
        crc32_parts: u32::from_le_bytes(read_exact_buff!(crc32parts, reader, 4)),
    };
    trace!("header: {:?}", &hdr[..]);
    trace!("header gpt: {}", h.disk_guid.to_hyphenated().to_string());
    let mut hdr_crc = hdr;
    for crc_byte in hdr_crc.iter_mut().skip(16).take(4) {
        *crc_byte = 0;
    }
    let c = calculate_crc32(&hdr_crc);
    trace!("header CRC32: {:#x} - computed CRC32: {:#x}", h.crc32, c);
    if c == h.crc32 {
        Ok(h)
    } else {
        Err(Error::new(ErrorKind::Other, "invalid CRC32 checksum"))
    }
}

pub(crate) fn find_backup_lba<D: Read + Seek>(
    f: &mut D,
    sector_size: disk::LogicalBlockSize,
) -> Result<u64> {
    trace!("querying file size to find backup header location");
    let lb_size: u64 = sector_size.into();
    let old_pos = f.seek(std::io::SeekFrom::Current(0))?;
    let len = f.seek(std::io::SeekFrom::End(0))?;
    f.seek(std::io::SeekFrom::Start(old_pos))?;
    if len <= lb_size {
        return Err(Error::new(
            ErrorKind::Other,
            "disk image too small for backup header",
        ));
    }
    let bak_offset = len.saturating_sub(lb_size);
    let bak_lba = bak_offset / lb_size;
    trace!(
        "backup header: LBA={}, bytes offset={}",
        bak_lba,
        bak_offset
    );

    Ok(bak_lba)
}

fn calculate_crc32(b: &[u8]) -> u32 {
    let mut digest = crc32::Digest::new(crc32::IEEE);
    trace!("Writing buffer to digest calculator");
    digest.write(b);

    digest.sum32()
}

pub(crate) fn partentry_checksum<D: Read + Seek>(
    file: &mut D,
    hdr: &Header,
    lb_size: disk::LogicalBlockSize,
) -> Result<u32> {
    // Seek to start of partition table.
    trace!("Computing partition checksum");
    let start = hdr
        .part_start
        .checked_mul(lb_size.into())
        .ok_or_else(|| Error::new(ErrorKind::Other, "header overflow - partition table start"))?;
    trace!("Seek to {}", start);
    let _ = file.seek(SeekFrom::Start(start))?;

    // Read partition table.
    let pt_len = u64::from(hdr.num_parts)
        .checked_mul(hdr.part_size.into())
        .ok_or_else(|| Error::new(ErrorKind::Other, "partition table - size"))?;
    trace!("Reading {} bytes", pt_len);
    let mut buf = vec![0; pt_len as usize];
    file.read_exact(&mut buf)?;

    //trace!("Buffer before checksum: {:?}", buf);
    // Compute CRC32 over all table bits.
    Ok(calculate_crc32(&buf))
}

/// A helper function to create a new header and write it to disk.
/// If the uuid isn't given a random one will be generated.  Use
/// this in conjunction with Partition::write()
// TODO: Move this to Header::new() and Header::write to write it
// that will match the Partition::write() API
pub fn write_header(
    p: impl AsRef<Path>,
    uuid: Option<uuid::Uuid>,
    sector_size: disk::LogicalBlockSize,
) -> Result<uuid::Uuid> {
    debug!("opening {} for writing", p.as_ref().display());
    let mut file = OpenOptions::new().write(true).read(true).open(p)?;
    let bak = find_backup_lba(&mut file, sector_size)?;
    let guid = match uuid {
        Some(u) => u,
        None => {
            let u = uuid::Uuid::new_v4();
            debug!("Generated random uuid: {}", u);
            u
        }
    };

    let hdr = Header::compute_new(true, &BTreeMap::new(), guid, bak, &None, sector_size, None)?;
    debug!("new header: {:#?}", hdr);
    hdr.write_primary(&mut file, sector_size)?;

    Ok(guid)
}

#[test]
// test compute new with fdisk'd image, without giving original header
fn test_compute_new_fdisk_no_header() {
    use tempfile;
    let lb_size = disk::DEFAULT_SECTOR_SIZE;
    let diskpath = Path::new("tests/fixtures/test.img");
    let h = read_header(diskpath, lb_size).unwrap();
    let cfg = crate::GptConfig::new().writable(false).initialized(true);
    let disk = cfg.open(diskpath).unwrap();
    println!("original Disk {:#?}", disk);
    let partitions: BTreeMap<u32, partition::Partition> = BTreeMap::new();
    let mut file = std::fs::OpenOptions::new()
        .write(false)
        .read(true)
        .open(diskpath)
        .unwrap();
    let bak = find_backup_lba(&mut file, *disk.logical_block_size()).unwrap();
    println!("Back offset {}", bak);
    let mut tempdisk = tempfile::tempfile().expect("failed to create tempfile disk");
    {
        let data: [u8; 4096] = [0; 4096];
        println!("Creating blank header file for testing");
        // This should be large enough to contain the backup partition array,
        // or computing the checksum when writing the backup header will fail.
        let min_file_size = (bak * Into::<u64>::into(lb_size)) + Into::<u64>::into(lb_size);
        for _ in 0..((min_file_size + 4095) / 4096) {
            tempdisk.write_all(&data).unwrap();
        }
    };
    let new_primary =
        Header::compute_new(true, &partitions, uuid::Uuid::new_v4(), bak, &None, lb_size, None).unwrap();
    println!("new primary header {:#?}", new_primary);
    let new_backup =
        Header::compute_new(false, &partitions, uuid::Uuid::new_v4(), bak, &None, lb_size, None).unwrap();
    println!("new backup header {:#?}", new_backup);
    new_primary
        .write_primary(&mut tempdisk, lb_size)
        .unwrap();
    new_backup
        .write_backup(&mut tempdisk, lb_size)
        .unwrap();
    let mbr = crate::mbr::ProtectiveMBR::new();
    mbr.overwrite_lba0(&mut tempdisk).unwrap();
    assert_eq!(h.signature, new_primary.signature);
    assert_eq!(h.revision, new_primary.revision);
    assert_eq!(h.header_size_le, new_primary.header_size_le);
    assert_eq!(h.reserved, new_primary.reserved);
    assert_eq!(h.current_lba, new_primary.current_lba);
    assert_eq!(h.backup_lba, new_primary.backup_lba);
    assert_eq!(34, new_primary.first_usable); // since we did not include the original header, the first usable defaults to 34
    assert_eq!(h.last_usable, new_primary.last_usable);
    assert_ne!(h.disk_guid, new_primary.disk_guid); //writing new disk => new guid
    assert_eq!(2, new_primary.part_start);
    // when creating a new table from scratch, we should always have a minimum of 128 entries to be UEFI compliant
    assert_eq!(128, new_primary.num_parts);
    assert_eq!(128, new_primary.part_size); //standard size (it is possibly different, but usually 128)

    let bh = read_backup_header(&mut file, *disk.logical_block_size()).unwrap();
    //backup header tests
    //current_lba and backup_lba should be flipped
    assert_eq!(h.backup_lba, new_backup.current_lba);
    assert_eq!(h.current_lba, new_backup.backup_lba);
    // also, the backup header should match
    assert_eq!(bh.current_lba, new_backup.current_lba);
    assert_eq!(bh.backup_lba, new_backup.backup_lba);
    assert_eq!(bh.part_start, new_backup.part_start);
}

#[test]
// test compute new with fdisk'd image, giving original header
// Note: if you would like to save to a file to check the headers
// manually, use OpenOptions with write/create/truncate/read.  without the
// read the checksum will not be able to read the tempdisk
fn test_compute_new_fdisk_pass_header() {
    let diskpath = Path::new("tests/fixtures/test.img");
    let h = read_header(diskpath, disk::DEFAULT_SECTOR_SIZE).unwrap();
    let cfg = crate::GptConfig::new().writable(false).initialized(true);
    let disk = cfg.open(diskpath).unwrap();
    println!("original Disk {:#?}", disk);
    let partitions: BTreeMap<u32, partition::Partition> = BTreeMap::new();
    let mut file = std::fs::OpenOptions::new()
        .write(false)
        .read(true)
        .open(diskpath)
        .unwrap();
    let bak = find_backup_lba(&mut file, *disk.logical_block_size()).unwrap();
    println!("Back offset {}", bak);
    let mut tempdisk = tempfile::tempfile().expect("failed to create tempfile disk");
    {
        let data: [u8; 4096] = [0; 4096];
        println!("Creating copy of test header file for testing");
        for _ in 0..2560 {
            tempdisk.write_all(&data).unwrap();
        }
    };
    let bh = read_backup_header(&mut file, *disk.logical_block_size()).unwrap();
    let mbr = crate::mbr::ProtectiveMBR::new();
    mbr.overwrite_lba0(&mut tempdisk).unwrap();
    let new_primary = Header::compute_new(
        true,
        &partitions,
        uuid::Uuid::new_v4(),
        bak,
        &Some(h.clone()),
        disk::DEFAULT_SECTOR_SIZE,
        None,
    )
    .unwrap();
    println!("new primary header {:#?}", new_primary);
    let new_backup = Header::compute_new(
        false,
        &partitions,
        uuid::Uuid::new_v4(),
        bak,
        &Some(h.clone()),
        disk::DEFAULT_SECTOR_SIZE,
        None,
    )
    .unwrap();
    println!("new backup header {:#?}", new_backup);
    new_primary
        .write_primary(&mut tempdisk, disk::DEFAULT_SECTOR_SIZE)
        .unwrap();
    new_backup
        .write_backup(&mut tempdisk, disk::DEFAULT_SECTOR_SIZE)
        .unwrap();
    assert_eq!(h.signature, new_primary.signature);
    assert_eq!(h.revision, new_primary.revision);
    assert_eq!(h.header_size_le, new_primary.header_size_le);
    assert_eq!(h.reserved, new_primary.reserved);
    assert_eq!(h.current_lba, new_primary.current_lba);
    assert_eq!(h.backup_lba, new_primary.backup_lba);
    assert_eq!(h.first_usable, new_primary.first_usable); // since we did not include the original header, the first usable defaults to 34
    assert_eq!(h.last_usable, new_primary.last_usable);
    assert_ne!(h.disk_guid, new_primary.disk_guid); //writing new disk => new guid
    assert_eq!(2, new_primary.part_start);
    //if we do a write disk this wouldn't actually be able to write a new partition with fdisk unless you created a new partition table on it
    assert_eq!(h.num_parts, new_primary.num_parts);
    assert_eq!(h.part_size, new_primary.part_size); //standard size (it is possibly different, but usually 128)

    //backup header tests
    //current_lba and backup_lba should be flipped
    assert_eq!(h.backup_lba, new_backup.current_lba);
    assert_eq!(h.current_lba, new_backup.backup_lba);
    // also, the backup header should match
    assert_eq!(bh.current_lba, new_backup.current_lba);
    assert_eq!(bh.backup_lba, new_backup.backup_lba);
    assert_eq!(bh.part_start, new_backup.part_start);
}

#[test]
// test compute new with fdisk'd image, without giving original header
fn test_compute_new_gpt_no_header() {
    use tempfile;
    let lb_size = disk::DEFAULT_SECTOR_SIZE;
    let diskpath = Path::new("tests/fixtures/gpt-linux-disk-01.img");
    let h = read_header(diskpath, lb_size).unwrap();
    let cfg = crate::GptConfig::new().writable(false).initialized(true);
    let disk = cfg.open(diskpath).unwrap();
    println!("original Disk {:#?}", disk);
    let partitions: BTreeMap<u32, partition::Partition> = BTreeMap::new();
    let mut file = std::fs::OpenOptions::new()
        .write(false)
        .read(true)
        .open(diskpath)
        .unwrap();
    let bak = find_backup_lba(&mut file, *disk.logical_block_size()).unwrap();
    println!("Back offset {}", bak);
    let mut tempdisk = tempfile::tempfile().expect("failed to create tempfile disk");
    {
        let data: [u8; 4096] = [0; 4096];
        println!("Creating blank header file for testing");
        for _ in 0..100 {
            tempdisk.write_all(&data).unwrap();
        }
    };
    let new_primary =
        Header::compute_new(true, &partitions, uuid::Uuid::new_v4(), bak, &None, lb_size, None).unwrap();
    println!("new primary header {:#?}", new_primary);
    let new_backup =
        Header::compute_new(false, &partitions, uuid::Uuid::new_v4(), bak, &None, lb_size, None).unwrap();
    println!("new backup header {:#?}", new_backup);
    new_primary
        .write_primary(&mut tempdisk, lb_size)
        .unwrap();
    new_backup
        .write_backup(&mut tempdisk, lb_size)
        .unwrap();
    let mbr = crate::mbr::ProtectiveMBR::new();
    mbr.overwrite_lba0(&mut tempdisk).unwrap();
    assert_eq!(h.signature, new_primary.signature);
    assert_eq!(h.revision, new_primary.revision);
    assert_eq!(h.header_size_le, new_primary.header_size_le);
    assert_eq!(h.reserved, new_primary.reserved);
    assert_eq!(h.current_lba, new_primary.current_lba);
    assert_eq!(h.backup_lba, new_primary.backup_lba);
    assert_eq!(34, new_primary.first_usable); // since we did not include the original header, the first usable defaults to 34
    assert_eq!(h.last_usable, new_primary.last_usable);
    assert_ne!(h.disk_guid, new_primary.disk_guid); //writing new disk => new guid
    assert_eq!(2, new_primary.part_start);
    // when creating a new table from scratch, we should always have a minimum of 128 entries to be UEFI compliant
    assert_eq!(128, new_primary.num_parts);
    assert_eq!(128, new_primary.part_size); //standard size (it is possibly different, but usually 128)

    let bh = read_backup_header(&mut file, *disk.logical_block_size()).unwrap();
    //backup header tests
    //current_lba and backup_lba should be flipped
    assert_eq!(h.backup_lba, new_backup.current_lba);
    assert_eq!(h.current_lba, new_backup.backup_lba);
    // also, the backup header should match
    assert_eq!(bh.current_lba, new_backup.current_lba);
    assert_eq!(bh.backup_lba, new_backup.backup_lba);
    assert_eq!(bh.part_start, new_backup.part_start);
}

#[test]
// test compute new with fdisk'd image, giving original header
// Note: if you would like to save to a file to check the headers
// manually, use OpenOptions with write/create/truncate/read.  without the
// read the checksum will not be able to read the tempdisk
fn test_compute_new_fdisk_gpt_header() {
    let diskpath = Path::new("tests/fixtures/gpt-linux-disk-01.img");
    let h = read_header(diskpath, disk::DEFAULT_SECTOR_SIZE).unwrap();
    let cfg = crate::GptConfig::new().writable(false).initialized(true);
    let disk = cfg.open(diskpath).unwrap();
    println!("original Disk {:#?}", disk);
    let partitions: BTreeMap<u32, partition::Partition> = BTreeMap::new();
    let mut file = std::fs::OpenOptions::new()
        .write(false)
        .read(true)
        .open(diskpath)
        .unwrap();
    let bak = find_backup_lba(&mut file, *disk.logical_block_size()).unwrap();
    println!("Back offset {}", bak);
    let mut tempdisk = tempfile::tempfile().expect("failed to create tempfile disk");
    {
        let data: [u8; 4096] = [0; 4096];
        println!("Creating copy of test header file for testing");
        for _ in 0..2560 {
            tempdisk.write_all(&data).unwrap();
        }
    };
    let bh = read_backup_header(&mut file, *disk.logical_block_size()).unwrap();
    let mbr = crate::mbr::ProtectiveMBR::new();
    mbr.overwrite_lba0(&mut tempdisk).unwrap();
    let new_primary = Header::compute_new(
        true,
        &partitions,
        uuid::Uuid::new_v4(),
        bak,
        &Some(h.clone()),
        disk::DEFAULT_SECTOR_SIZE,
        None,
    )
    .unwrap();
    println!("new primary header {:#?}", new_primary);
    let new_backup = Header::compute_new(
        false,
        &partitions,
        uuid::Uuid::new_v4(),
        bak,
        &Some(h.clone()),
        disk::DEFAULT_SECTOR_SIZE,
        None,
    )
    .unwrap();
    println!("new backup header {:#?}", new_backup);
    new_primary
        .write_primary(&mut tempdisk, disk::DEFAULT_SECTOR_SIZE)
        .unwrap();
    new_backup
        .write_backup(&mut tempdisk, disk::DEFAULT_SECTOR_SIZE)
        .unwrap();
    assert_eq!(h.signature, new_primary.signature);
    assert_eq!(h.revision, new_primary.revision);
    assert_eq!(h.header_size_le, new_primary.header_size_le);
    assert_eq!(h.reserved, new_primary.reserved);
    assert_eq!(h.current_lba, new_primary.current_lba);
    assert_eq!(h.backup_lba, new_primary.backup_lba);
    assert_eq!(h.first_usable, new_primary.first_usable); // since we did not include the original header, the first usable defaults to 34
    assert_eq!(h.last_usable, new_primary.last_usable);
    assert_ne!(h.disk_guid, new_primary.disk_guid); //writing new disk => new guid
    assert_eq!(2, new_primary.part_start);
    //if we do a write disk this wouldn't actually be able to write a new partition with fdisk unless you created a new partition table on it
    assert_eq!(h.num_parts, new_primary.num_parts);
    assert_eq!(h.part_size, new_primary.part_size); //standard size (it is possibly different, but usually 128)

    //backup header tests
    //current_lba and backup_lba should be flipped
    assert_eq!(h.backup_lba, new_backup.current_lba);
    assert_eq!(h.current_lba, new_backup.backup_lba);
    // also, the backup header should match
    assert_eq!(bh.current_lba, new_backup.current_lba);
    assert_eq!(bh.backup_lba, new_backup.backup_lba);
    assert_eq!(bh.part_start, new_backup.part_start);
}
