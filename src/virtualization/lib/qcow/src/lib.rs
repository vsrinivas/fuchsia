// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod wire;

use {
    anyhow::{anyhow, Error},
    std::convert::TryInto,
    std::io::{Read, Seek},
    std::mem,
    zerocopy::FromBytes,
};

// Each QCOW file starts with this magic value "QFI\xfb".
const QCOW_MAGIC: u32 = 0x514649fb;

#[inline]
const fn cluster_size(cluster_bits: u32) -> u64 {
    1 << cluster_bits
}

#[inline]
const fn cluster_mask(cluster_bits: u32) -> u64 {
    cluster_size(cluster_bits) - 1
}

#[inline]
const fn l2_bits(cluster_bits: u32) -> u32 {
    assert!(cluster_bits > 3);
    cluster_bits - 3
}

#[inline]
const fn l2_size(cluster_bits: u32) -> u64 {
    1 << l2_bits(cluster_bits)
}

#[inline]
const fn l2_mask(cluster_bits: u32) -> u64 {
    l2_size(cluster_bits) - 1
}

#[inline]
fn required_l1_size(disk_size: u64, cluster_bits: u32) -> u32 {
    // l1_entry_size is the addressable disk space that is enabled by a single L1 entry.
    let l1_entry_size = cluster_size(cluster_bits) * l2_size(cluster_bits);
    // Round up disk size to the nearest l1_entry_size.
    let disk_size = disk_size + l1_entry_size - 1;
    // Return the required number of L1 entries to address the entire disk.
    (disk_size / l1_entry_size).try_into().unwrap()
}

fn read_header(file: &mut std::fs::File) -> Result<wire::Header, Error> {
    const HEADER_SIZE: usize = mem::size_of::<wire::Header>();
    let mut buf = vec![0u8; HEADER_SIZE];
    file.seek(std::io::SeekFrom::Start(0))?;
    file.read_exact(&mut buf)?;
    // Header::read_from should not fail if `buf` is of the correct size.
    Ok(wire::Header::read_from(buf.as_slice()).expect("read_from failed unexpectedly"))
}

/// Loads a translation table from a backing file.
///
/// This is used to load both the L1 and L2 tables
fn load_tranlsation_table<Entry: FromBytes + Sized>(
    file: &mut std::fs::File,
    num_entries: u64,
    table_offset: u64,
) -> Result<Vec<Entry>, Error> {
    let entry_size = std::mem::size_of::<Entry>() as u64;
    // Not explicitly needed, but in practice L1 and L2 tables are 8 bytes so we don't expect
    // this to be anything else.
    assert!(entry_size == 8);

    let bytes_to_read = num_entries * entry_size;
    let mut table = vec![0u8; bytes_to_read as usize];

    file.seek(std::io::SeekFrom::Start(table_offset))?;
    file.read_exact(&mut table)?;
    Ok(table
        // Break the bytes up into entry size slices
        .chunks_exact(entry_size as usize)
        // Deserialize and unwrap. This should never fail so long as we pass the correct
        // slice size to `chunks_exact`.
        .map(Entry::read_from)
        .map(Option::unwrap)
        .collect::<Vec<Entry>>())
}

/// Describes how bytes for a region of disk are stored in the qcow translation table.
#[derive(Debug, Clone)]
pub enum Mapping {
    /// The requested guest cluster has a corresponding physical cluster specified in the
    /// translation table.
    Mapped {
        /// The physical offset (in the QCOW file) that maps to the requested guest offset.
        physical_offset: u64,
        /// The mapping is valid for at least these many bytes. If length does not cover the range
        /// requested then a new translation should be requested for the range immediately
        /// following this one.
        length: u64,
    },
    /// The requested linear range is unmapped in the translation table for the next `length`
    /// bytes.
    ///
    /// Unmapped sectors read as zero.
    Unmapped { length: u64 },
}

impl Mapping {
    pub fn len(&self) -> u64 {
        match self {
            Mapping::Mapped { length, .. } => *length,
            Mapping::Unmapped { length } => *length,
        }
    }
}

/// Implements an iterable type over the set of mappings for a linear disk range.
///
/// See `TranslationTable::translation` for more details.
pub struct Translation<'a> {
    translation: &'a TranslationTable,
    linear_range: std::ops::Range<u64>,
}

impl<'a> Iterator for Translation<'a> {
    type Item = Mapping;

    fn next(&mut self) -> Option<Self::Item> {
        if self.linear_range.is_empty() {
            return None;
        }
        let translation = self.translation.translate_range(&self.linear_range);
        if let Some(translation) = translation.as_ref() {
            self.linear_range.start += translation.len();
        }
        translation
    }
}

/// QCOW uses a 2-level translation table to map guest-clusters to host clusters.
///
/// The translation table is a way of mapping a linear disk address to a physical offset in the
/// QCOW file. Not every linear address may be mapped in the QCOW file, in which case reads to
/// those regions would read-as-zero. These mappings are done with 'cluster' granularity such that
/// a single, contiguous linear cluster maps to a contiguous region in the host file. The exact
/// size of clusters used is determined by a field in the QCOW header.
///
///  Ex: a linear address can be decomposed into 3 parts:
///
///    * l1_index - The index into the top-level L1 translation table. The entry in the L1 table
///            can either be a pointer to an L2 translation table, or the entry can indicate that
///            the entire region is un-mapped, regardless of l2_index or cluster_offset.
///    * l2_index - If the l1_index indicates that there is a valid L2 table for a translation, the
///            l2_index is offset into that L2 table that defines the per-cluster mapping for a
///            translation. This mapping can either indicate there is a physical cluster allocated
///            for a linear cluster or it can indicate that the cluster is unmapped and no
///            translation exists.
///    * cluster_offset - If there is a valid l1_table entry and a valid l2_table entry for a
///            linear disk address, that means there is physical cluster that has been allocated to
///            the linear cluster. The cluster_offset is then the remaining byte-offset into this
///            cluster.
///
pub struct TranslationTable {
    /// The number of bits in a linear address that represent the cluster offset.
    ///
    ///    cluster_size == 1 << cluster_bits
    cluster_bits: u32,
    /// The linear size of the qcow file.
    linear_size: u64,
    /// The L1 table is stored as a fully loaded vector of L2 tables. This is simple but does
    /// require that we retain all L2 tables in memory at all times.
    l1: Vec<Option<Vec<wire::L2Entry>>>,
}

impl TranslationTable {
    pub fn load(file: &mut std::fs::File) -> Result<Self, Error> {
        let mut header = read_header(file)?;
        // Every file must start with this magic value.
        if header.magic.get() != QCOW_MAGIC {
            return Err(anyhow!("File has bad magic"));
        }

        // Version check. We don't make any assumptions that we can properly load files with a
        // version greater than 3.
        let version = header.version.get();
        if version != 2 && version != 3 {
            return Err(anyhow!("QCOW file has unsupported version {}", version));
        }
        if version == 2 {
            // These were added in version 3 with the following defaults with version 2.
            header.incompatible_features.set(0);
            header.compatible_features.set(0);
            header.autoclear_features.set(0);
            header.refcount_order.set(4);
            header.header_length.set(72);
        }

        // Backing files allow for a copy-on-write shadow of a read-only backing file. We don't
        // support this feature so if we're provided an image the relies on a backing file we will
        // not be able to properly support it.
        let backing_file_size = header.backing_file_size.get();
        if backing_file_size != 0 {
            return Err(anyhow!("QCOW file has backing file, which is not supported"));
        }

        // Some guard-rails for the cluster bits.
        //
        // The QCOW specification indicates this must be at least 9 bits (512-byte clusters). The
        // spec also indicates that QEMU may not support cluster sizes above 2MiB so we also go
        // ahead an adopt that upper bound.
        let cluster_bits = header.cluster_bits.get();
        if cluster_bits < 9 || cluster_bits > 22 {
            return Err(anyhow!("cluster_bits is out of the supported range."));
        }

        // Size is the linear size of the file in bytes.
        let size = header.size.get();
        if size == 0 {
            return Err(anyhow!("QCOW file has 0 size"));
        }
        if size & cluster_mask(cluster_bits) != 0 {
            return Err(anyhow!("QCOW file size is not sector aligned"));
        }

        // QCOW files can be encrypted, but we don't support that.
        if header.crypt_method.get() != wire::QCOW_CRYPT_NONE {
            return Err(anyhow!("QCOW encryption is not supported"));
        }

        // The l1 should be large enough to cover the reported disk size.
        let l1_size = header.l1_size.get();
        if l1_size < required_l1_size(size, cluster_bits) {
            return Err(anyhow!("QCOW L1 table is not large enough to address the entire disk"));
        }

        // Load L1 Table
        //
        // First we load a vector of the 8-byte table entries.
        let l1_entries = load_tranlsation_table::<wire::L1Entry>(
            file,
            header.l1_size.get().into(),
            header.l1_table_offset.get(),
        )?;

        // Now iterate over each L1 entry and load the corresponding L2 table if necessary.
        let l1 = l1_entries
            .into_iter()
            .map(move |entry| {
                let entry: Option<Vec<wire::L2Entry>> = if let Some(offset) = entry.offset() {
                    let l2 = load_tranlsation_table::<wire::L2Entry>(
                        file,
                        l2_size(cluster_bits),
                        offset,
                    )?;
                    if l2.iter().find(|e| e.compressed()).is_some() {
                        return Err(anyhow!("QCOW contains compressed sectors"));
                    }
                    Some(l2)
                } else {
                    None
                };
                Ok::<Option<Vec<wire::L2Entry>>, Error>(entry)
            })
            .collect::<Result<Vec<Option<Vec<wire::L2Entry>>>, Error>>()?;

        Ok(Self { cluster_bits, linear_size: size, l1 })
    }

    /// The logical size of the QCOW disk as specified in the header.
    pub fn linear_size(&self) -> u64 {
        self.linear_size
    }

    /// Looks up translations for a linear disk range.
    ///
    /// This takes a `linear_range` describing a region of the qcow file to read from and returns
    /// an iterator over `Mapping`s of that region.
    ///
    /// The returned iterator will yield mappings that indicate how the linear rante is represented
    /// in the qcow file. This can be a combination of physical cluster mappings and also unmapped
    /// regions if the translation table contains no data for the linear range.
    ///
    /// If any part of `linear_range` extends beyond the disk (bounded by `linear_size()`) then
    /// the iterator will not yield any mappings for those regions. In other words, no Mapping is a
    /// distinct situation for a `Mapping::Unmapped`. The former means there is no logical disk
    /// backing the range and the latter means that the linear range is valid but no physical disk
    /// clusters have been allocated to it.
    pub fn translate<'a>(&'a self, linear_range: std::ops::Range<u64>) -> Translation<'a> {
        Translation { linear_range: linear_range.clone(), translation: self }
    }

    fn translate_range(&self, linear_range: &std::ops::Range<u64>) -> Option<Mapping> {
        if linear_range.start >= self.linear_size() {
            return None;
        }

        // cluster offset is the offset into the translated cluster.
        let offset = linear_range.start;
        let cluster_offset = offset & cluster_mask(self.cluster_bits);

        // Now shift off the cluster bits and compute the L2 index
        let offset = offset >> self.cluster_bits;
        let l2_index = offset & l2_mask(self.cluster_bits);

        // Now compute the l1 index
        //
        // The l1 table index contains the remaining most-significant bits of the linear address.
        let l1_index = (offset >> l2_bits(self.cluster_bits)) as u32;

        // Now walk the tables
        //
        // First find the L2 table by looking at the corresponding index in the L1 table. If this
        // is None, the entire linear range covered by that L1 entry is unallocated.
        let maybe_physical_cluster = self.l1[l1_index as usize]
            .as_ref()
            // If the L1 entry is valid, then we have an L2 table that defines per-cluster
            // translations. This will just lookup the L2 translation entry for the requested
            // sector.
            .map(|l2_table| &l2_table[l2_index as usize])
            // The specific L2 entry can indicate this cluster is either mapped to some physical
            // cluster or it is an unallocated. `L2Entry::offset` will handle decoding the table
            // entry and will the physical ofset for the cluster if it exists.
            .and_then(|entry| entry.offset());

        // The mapping length is valid to the end of the cluster, limited to the end of the range
        // requested by the caller.
        //
        // TODO: As a refinement, we could detect contiguous physical clusters and coalesce
        // contiguous sectors into a single range.
        let length = std::cmp::min(
            linear_range.end - linear_range.start,
            cluster_size(self.cluster_bits) - cluster_offset,
        );

        // This will contain a physical cluster that maps to the start of the requested
        // `linear_range` if a cluster is allocated to that region.
        let transation = match maybe_physical_cluster {
            Some(physical_cluster) => {
                Mapping::Mapped { physical_offset: physical_cluster + cluster_offset, length }
            }
            None => Mapping::Unmapped { length },
        };
        Some(transation)
    }
}

/// A very simple interface for reading from a qcow file.
#[cfg(test)]
struct QcowFileReadOnly {
    file: std::cell::RefCell<std::fs::File>,
    translation: TranslationTable,
}

#[cfg(test)]
impl QcowFileReadOnly {
    pub fn new(mut file: std::fs::File) -> Result<Self, Error> {
        Ok(Self {
            translation: TranslationTable::load(&mut file)?,
            file: std::cell::RefCell::new(file),
        })
    }

    pub fn size(&self) -> u64 {
        self.translation.linear_size()
    }

    pub fn read_at(&self, length: u64, offset: u64) -> Result<Vec<u8>, Error> {
        // Iterate over the set of translations for this linear range and accumulate the result
        // into a Vec.
        self.translation
            .translate(std::ops::Range { start: offset, end: offset + length })
            .try_fold(Vec::new(), |mut result, translation| -> Result<Vec<u8>, Error> {
                // 0-extend our result vector to add capacity for this translation.
                let result_len = result.len();
                result.resize(result_len + translation.len() as usize, 0);

                match translation {
                    // For translations that have a physical cluster mapping we can read the bytes
                    // from the file using the physica offset.
                    Mapping::Mapped { physical_offset, .. } => {
                        self.file.borrow_mut().seek(std::io::SeekFrom::Start(physical_offset))?;
                        self.file.borrow_mut().read_exact(&mut result[result_len..])?;
                    }
                    // If there exists no translation then the bytes should read-as-zero. This is
                    // a no-op here because we have already 0-extended the result vector.
                    Mapping::Unmapped { .. } => {}
                }
                Ok(result)
            })
    }
}

#[cfg(test)]
mod test {
    use {super::*, std::fs::File};

    fn open_qcow_file(path: &str) -> QcowFileReadOnly {
        let test_image = File::open(path).expect("Failed to open file");
        QcowFileReadOnly::new(test_image).expect("Failed to create QcowFileReadOnly")
    }

    fn check_range(file: &QcowFileReadOnly, start: u64, length: u64, value: u8) {
        let bytes = file.read_at(length, start).expect("Failed to read from file");
        assert_eq!(bytes.len() as u64, length);
        for byte in bytes {
            assert_eq!(byte, value);
        }
    }

    #[test]
    fn test_empty_1gb() {
        const SIZE: u64 = 1 * 1024 * 1024 * 1024;
        let qcow = open_qcow_file("/pkg/data/empty_1gb.qcow2");

        assert_eq!(SIZE, qcow.size());
        check_range(&qcow, 0, 1024, 0);
        check_range(&qcow, SIZE - 1024, 1024, 0);
    }

    #[test]
    fn test_read_basic() {
        const SIZE: u64 = 1 * 1024 * 1024 * 1024;
        let qcow = open_qcow_file("/pkg/data/sparse.qcow2");

        assert_eq!(SIZE, qcow.size());

        // Verify we can read the expected data clusters.
        {
            const REGION_START: u64 = 0;
            check_range(&qcow, REGION_START, 1024, 0xaa);
            check_range(&qcow, REGION_START + 1024, 1024, 0);
        }
        {
            const REGION_START: u64 = 512 * 1024 * 1024;
            check_range(&qcow, REGION_START - 1024, 1024, 0);
            check_range(&qcow, REGION_START, 1024, 0xcc);
            check_range(&qcow, REGION_START + 1024, 1024, 0);
        }
        {
            const REGION_START: u64 = 1 * 1024 * 1024 * 1024 - 1024;
            check_range(&qcow, REGION_START - 1024, 1024, 0);
            check_range(&qcow, REGION_START, 1024, 0xbb);
        }
    }

    #[test]
    fn test_read_across_translations() {
        const SIZE: u64 = 1 * 1024 * 1024 * 1024;
        let qcow = open_qcow_file("/pkg/data/sparse.qcow2");

        assert_eq!(SIZE, qcow.size());

        // Test reading a buffer that is partially translated and partially not translated.
        let bytes = qcow.read_at(4096, 0).expect("Failed to read the last byte from file");

        assert_eq!(bytes[0..1024], vec![0xaa; 1024]);
        assert_eq!(bytes[1024..2048], vec![0; 1024]);
        assert_eq!(bytes[2048..3072], vec![0xab; 1024]);
        assert_eq!(bytes[3072..4096], vec![0; 1024]);
    }

    #[test]
    fn test_read_short() {
        const SIZE: u64 = 1 * 1024 * 1024 * 1024;
        let qcow = open_qcow_file("/pkg/data/sparse.qcow2");

        assert_eq!(SIZE, qcow.size());

        // Test reading past the end of the file.
        //
        // Behavior here is Similar to std::io::Read in that the read_at call will not fail but may
        // be short. For read_at calls that are beyond the end of the file this will result in a
        // 0-byte Ok result.
        let bytes = qcow.read_at(1, SIZE - 1).expect("Failed to read the last byte from file");
        assert_eq!(1, bytes.len());
        assert_eq!(0xbb, bytes[0]);

        // Reading past the end of the file should be short.
        let bytes =
            qcow.read_at(10, SIZE - 1).expect("Failed to read 1 byte past the end of the file");
        assert_eq!(1, bytes.len());

        let bytes =
            qcow.read_at(100, SIZE).expect("Failed to read entire buffer past the end of the file");
        assert_eq!(0, bytes.len());

        let bytes =
            qcow.read_at(100, 2 * SIZE).expect("Failed to read far past the end of the file");
        assert_eq!(0, bytes.len());
    }
}
