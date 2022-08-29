// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A snapshot represents all the loaded blocks of the VMO in a way that we can reconstruct the
//! implicit tree.

use {
    crate::{
        reader::{error::ReaderError, ReadSnapshot, SnapshotSource},
        Inspector,
    },
    inspect_format::{constants, utils, Block, BlockType, Container, ReadableBlockContainer},
    std::{
        cmp,
        convert::TryFrom,
        sync::{Arc, Mutex},
    },
};

#[cfg(target_os = "fuchsia")]
use {fuchsia_zircon as zx, fuchsia_zircon::Vmo, mapped_vmo::Mapping};

pub use crate::reader::tree_reader::SnapshotTree;

/// Enables to scan all the blocks in a given buffer.
#[derive(Debug)]
pub struct Snapshot {
    /// The buffer read from an Inspect VMO.
    buffer: BackingBuffer,
}

/// A scanned block.
pub type ScannedBlock<'a> = Block<&'a BackingBuffer>;

const SNAPSHOT_TRIES: u64 = 1024;

impl Snapshot {
    /// Returns an iterator that returns all the Blocks in the buffer.
    pub fn scan(&self) -> BlockIterator<'_> {
        BlockIterator::from(&self.buffer)
    }

    /// Gets the block at the given |index|.
    pub fn get_block(&self, index: u32) -> Option<ScannedBlock<'_>> {
        if utils::offset_for_index(index) < self.buffer.len() {
            Some(Block::new(&self.buffer, index))
        } else {
            None
        }
    }

    /// Try to take a consistent snapshot of the given VMO once.
    ///
    /// Returns a Snapshot on success or an Error if a consistent snapshot could not be taken.
    pub fn try_once_from_vmo<'a, T>(source: &'a T) -> Result<Snapshot, ReaderError>
    where
        T: ReadSnapshot + 'a,
        &'a T: TryInto<BackingBuffer>,
    {
        Snapshot::try_once_with_callback(source, &mut || {})
    }

    fn try_once_with_callback<'a, F, T>(
        source: &'a T,
        read_callback: &mut F,
    ) -> Result<Snapshot, ReaderError>
    where
        F: FnMut() -> (),
        T: ReadSnapshot + 'a,
        &'a T: TryInto<BackingBuffer>,
    {
        // Read the generation count one time
        let mut header_bytes: [u8; 32] = [0; 32];
        source.read_bytes(&mut header_bytes, 0)?;
        let header_block = Block::new(&header_bytes[..], 0);
        let generation = header_block.header_generation_count();

        if let Ok(gen) = generation {
            if gen == constants::VMO_FROZEN {
                match source.try_into() {
                    Ok(buffer) => return Ok(Snapshot { buffer }),
                    // on error, try and read via the full snapshot algo
                    Err(_) => {}
                }
            }

            // Read the buffer
            let order = header_block.order();
            let vmo_size = if order == constants::HeaderSize::LARGE as usize {
                cmp::min(
                    header_block.header_vmo_size()?.unwrap() as u64,
                    constants::MAX_VMO_SIZE as u64,
                )
            } else {
                cmp::min(source.size()?, constants::MAX_VMO_SIZE as u64)
            };
            let mut buffer = vec![0u8; vmo_size as usize];
            source.read_bytes(&mut buffer[..], 0)?;
            if cfg!(test) {
                read_callback();
            }

            // Read the generation count one more time to ensure the previous buffer read is
            // consistent.
            source.read_bytes(&mut header_bytes, 0)?;
            match header_generation_count(&header_bytes[..]) {
                None => return Err(ReaderError::InconsistentSnapshot),
                Some(new_generation) if new_generation != gen => {
                    return Err(ReaderError::InconsistentSnapshot);
                }
                Some(_) => return Ok(Snapshot { buffer: BackingBuffer::from(buffer) }),
            }
        }

        Err(ReaderError::InconsistentSnapshot)
    }

    fn try_from_with_callback<'a, F, T>(
        source: &'a T,
        mut read_callback: F,
    ) -> Result<Snapshot, ReaderError>
    where
        F: FnMut() -> (),
        T: ReadSnapshot + 'a,
        &'a T: TryInto<BackingBuffer>,
    {
        let mut i = 0;
        loop {
            match Snapshot::try_once_with_callback(source, &mut read_callback) {
                Ok(snapshot) => return Ok(snapshot),
                Err(e) => {
                    if i >= SNAPSHOT_TRIES {
                        return Err(e);
                    }
                }
            };
            i += 1;
        }
    }

    // Used for snapshot tests.
    #[cfg(test)]
    pub fn build(bytes: &[u8]) -> Self {
        Snapshot { buffer: BackingBuffer::from(bytes.to_vec()) }
    }
}

/// Reads the given 16 bytes as an Inspect Block Header and returns the
/// generation count if the header is valid: correct magic number, version number
/// and nobody is writing to it.
fn header_generation_count(bytes: &[u8]) -> Option<u64> {
    if bytes.len() < 16 {
        return None;
    }
    let block = Block::new(&bytes[..16], 0);
    if block.block_type_or().unwrap_or(BlockType::Reserved) == BlockType::Header
        && block.header_magic().unwrap() == constants::HEADER_MAGIC_NUMBER
        && block.header_version().unwrap() <= constants::HEADER_VERSION_NUMBER
        && !block.header_is_locked().unwrap()
    {
        return block.header_generation_count().ok();
    }
    None
}

/// Construct a snapshot from a byte array.
impl TryFrom<&[u8]> for Snapshot {
    type Error = ReaderError;

    fn try_from(bytes: &[u8]) -> Result<Self, Self::Error> {
        if header_generation_count(&bytes).is_some() {
            Ok(Snapshot { buffer: BackingBuffer::from(bytes.to_vec()) })
        } else {
            return Err(ReaderError::MissingHeaderOrLocked);
        }
    }
}

/// Construct a snapshot from a byte vector.
impl TryFrom<Vec<u8>> for Snapshot {
    type Error = ReaderError;

    fn try_from(bytes: Vec<u8>) -> Result<Self, Self::Error> {
        if header_generation_count(&bytes).is_some() {
            Ok(Snapshot { buffer: BackingBuffer::from(bytes) })
        } else {
            return Err(ReaderError::MissingHeaderOrLocked);
        }
    }
}

/// Construct a snapshot from a VMO.
impl TryFrom<&SnapshotSource> for Snapshot {
    type Error = ReaderError;

    fn try_from(source: &SnapshotSource) -> Result<Self, Self::Error> {
        Snapshot::try_from_with_callback(source, || {})
    }
}

impl TryFrom<&Inspector> for Snapshot {
    type Error = ReaderError;

    fn try_from(inspector: &Inspector) -> Result<Self, Self::Error> {
        #[cfg(target_os = "fuchsia")]
        return inspector
            .vmo
            .as_ref()
            .ok_or(ReaderError::NoOpInspector)
            .and_then(|vmo| Snapshot::try_from(&**vmo));

        #[cfg(not(target_os = "fuchsia"))]
        return Snapshot::try_from(
            inspector.clone_heap_container().as_ref().ok_or(ReaderError::NoOpInspector)?,
        );
    }
}

/// Iterates over a byte array containing Inspect API blocks and returns the
/// blocks in order.
pub struct BlockIterator<'a> {
    /// Current offset at which the iterator is reading.
    offset: usize,

    /// The bytes being read.
    container: &'a BackingBuffer,
}

impl<'a> From<&'a BackingBuffer> for BlockIterator<'a> {
    fn from(container: &'a BackingBuffer) -> Self {
        BlockIterator { offset: 0, container }
    }
}

impl<'a> Iterator for BlockIterator<'a> {
    type Item = ScannedBlock<'a>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.offset >= self.container.len() {
            return None;
        }
        let index = utils::index_for_offset(self.offset);
        let block = Block::new(self.container, index);
        if self.container.len() - self.offset < utils::order_to_size(block.order()) {
            return None;
        }
        self.offset += utils::order_to_size(block.order());
        Some(block)
    }
}

#[derive(Debug)]
pub enum BackingBuffer {
    Vector(Vec<u8>),
    Map(Container),
}

impl From<Vec<u8>> for BackingBuffer {
    fn from(v: Vec<u8>) -> Self {
        BackingBuffer::Vector(v)
    }
}

impl From<Container> for BackingBuffer {
    fn from(m: Container) -> Self {
        BackingBuffer::Map(m)
    }
}

impl BackingBuffer {
    pub fn len(&self) -> usize {
        match &self {
            BackingBuffer::Map(m) => ReadableBlockContainer::size(m),
            BackingBuffer::Vector(v) => v.len(),
        }
    }
}

impl ReadableBlockContainer for &BackingBuffer {
    fn read_bytes(&self, offset: usize, bytes: &mut [u8]) -> usize {
        match self {
            BackingBuffer::Map(ref m) => ReadableBlockContainer::read_bytes(m, offset, bytes),
            BackingBuffer::Vector(b) => (b.as_ref() as &[u8]).read_bytes(offset, bytes),
        }
    }

    fn size(&self) -> usize {
        self.len()
    }
}

#[cfg(target_os = "fuchsia")]
impl TryFrom<&Vmo> for BackingBuffer {
    type Error = ReaderError;
    fn try_from(vmo: &zx::Vmo) -> Result<Self, Self::Error> {
        let mapping = vmo
            .get_size()
            .and_then(|size| Mapping::create_from_vmo(vmo, size as usize, zx::VmarFlags::PERM_READ))
            .map_err(ReaderError::Vmo)?;
        Ok(BackingBuffer::from(Arc::new(mapping)))
    }
}

impl TryFrom<&Arc<Mutex<Vec<u8>>>> for BackingBuffer {
    type Error = ReaderError;
    fn try_from(other: &Arc<Mutex<Vec<u8>>>) -> Result<BackingBuffer, Self::Error> {
        Ok(BackingBuffer::from(
            other.try_lock().map_err(|_| ReaderError::MissingHeaderOrLocked)?.clone(),
        ))
    }
}

#[cfg(test)]
mod tests {
    use {super::*, anyhow::Error, inspect_format::WritableBlockContainer, std::sync::Arc};

    #[cfg(not(target_os = "fuchsia"))]
    use {std::sync::Mutex, std::vec::Vec};

    #[cfg(target_os = "fuchsia")]
    use mapped_vmo::Mapping;

    #[cfg(target_os = "fuchsia")]
    fn create_mapping(size: usize) -> (Arc<Mapping>, zx::Vmo) {
        let (m, v) = Mapping::allocate(size).unwrap();
        (Arc::new(m), v)
    }

    #[cfg(not(target_os = "fuchsia"))]
    fn create_mapping(size: usize) -> (Arc<Mutex<Vec<u8>>>, Arc<Mutex<Vec<u8>>>) {
        let mut data = Vec::new();
        data.resize(size, 0);
        let data = Arc::new(Mutex::new(data));
        (data.clone(), data)
    }

    #[fuchsia::test]
    fn scan() -> Result<(), Error> {
        let size = 4096;
        let (mapping_ref, vmo) = create_mapping(size);
        let mut header =
            Block::new_free(mapping_ref.clone(), 0, constants::HEADER_ORDER as usize, 0)?;
        header.become_reserved()?;
        header.become_header(size)?;

        let b = Block::new_free(mapping_ref.clone(), 2, 2, 0)?;
        b.become_reserved()?;
        b.become_extent(6)?;
        let b = Block::new_free(mapping_ref.clone(), 6, 0, 0)?;
        b.become_reserved()?;
        b.become_int_value(1, 3, 4)?;

        let snapshot = Snapshot::try_from(&vmo)?;

        // Scan blocks
        let blocks = snapshot.scan().collect::<Vec<ScannedBlock<'_>>>();

        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[0].index(), 0);
        assert_eq!(blocks[0].order(), constants::HEADER_ORDER as usize);
        assert_eq!(blocks[0].header_magic().unwrap(), constants::HEADER_MAGIC_NUMBER);
        assert_eq!(blocks[0].header_version().unwrap(), constants::HEADER_VERSION_NUMBER);

        assert_eq!(blocks[1].block_type(), BlockType::Extent);
        assert_eq!(blocks[1].index(), 2);
        assert_eq!(blocks[1].order(), 2);
        assert_eq!(blocks[1].next_extent().unwrap(), 6);

        assert_eq!(blocks[2].block_type(), BlockType::IntValue);
        assert_eq!(blocks[2].index(), 6);
        assert_eq!(blocks[2].order(), 0);
        assert_eq!(blocks[2].name_index().unwrap(), 3);
        assert_eq!(blocks[2].parent_index().unwrap(), 4);
        assert_eq!(blocks[2].int_value().unwrap(), 1);

        assert!(blocks[3..].iter().all(|b| b.block_type() == BlockType::Free));

        // Verify get_block
        assert_eq!(snapshot.get_block(0).unwrap().block_type(), BlockType::Header);
        assert_eq!(snapshot.get_block(2).unwrap().block_type(), BlockType::Extent);
        assert_eq!(snapshot.get_block(6).unwrap().block_type(), BlockType::IntValue);
        assert_eq!(snapshot.get_block(7).unwrap().block_type(), BlockType::Free);
        assert!(snapshot.get_block(4096).is_none());

        Ok(())
    }

    #[fuchsia::test]
    fn scan_bad_header() -> Result<(), Error> {
        let (mapping_ref, vmo) = create_mapping(4096);

        // create a header block with an invalid version number
        mapping_ref.write_bytes(
            0,
            &[
                0x00, /* order/reserved */
                0x02, /* type */
                0xff, /* invalid version number */
                'I' as u8, 'N' as u8, 'S' as u8, 'P' as u8,
            ],
        );
        let snapshot = Snapshot::try_from(&vmo);
        assert!(snapshot.is_err());
        Ok(())
    }

    #[fuchsia::test]
    fn invalid_type() -> Result<(), Error> {
        let (mapping_ref, vmo) = create_mapping(4096);
        mapping_ref.write_bytes(0, &[0x00, 0xff, 0x01]);
        assert!(Snapshot::try_from(&vmo).is_err());
        Ok(())
    }

    #[fuchsia::test]
    fn invalid_order() -> Result<(), Error> {
        let (mapping_ref, vmo) = create_mapping(4096);
        mapping_ref.write_bytes(0, &[0xff, 0xff]);
        assert!(Snapshot::try_from(&vmo).is_err());
        Ok(())
    }

    #[fuchsia::test]
    fn invalid_pending_write() -> Result<(), Error> {
        let size = 4096;
        let (mapping_ref, vmo) = create_mapping(size);
        let mut header =
            Block::new_free(mapping_ref.clone(), 0, constants::HEADER_ORDER as usize, 0)?;
        header.become_reserved()?;
        header.become_header(size)?;
        header.lock_header()?;
        assert!(Snapshot::try_from(&vmo).is_err());
        Ok(())
    }

    #[fuchsia::test]
    fn invalid_magic_number() -> Result<(), Error> {
        let size = 4096;
        let (mapping_ref, vmo) = create_mapping(size);
        let mut header =
            Block::new_free(mapping_ref.clone(), 0, constants::HEADER_ORDER as usize, 0)?;
        header.become_reserved()?;
        header.become_header(size)?;
        header.set_header_magic(3)?;
        assert!(Snapshot::try_from(&vmo).is_err());
        Ok(())
    }

    #[fuchsia::test]
    fn invalid_generation_count() -> Result<(), Error> {
        let size = 4096;
        let (mapping_ref, vmo) = create_mapping(size);
        let mut header =
            Block::new_free(mapping_ref.clone(), 0, constants::HEADER_ORDER as usize, 0)?;
        header.become_reserved()?;
        header.become_header(size)?;
        assert!(Snapshot::try_from_with_callback(&vmo, || {
            header.lock_header().unwrap();
            header.unlock_header().unwrap();
        })
        .is_err());
        Ok(())
    }

    #[fuchsia::test]
    fn snapshot_from_few_bytes() {
        let values = (0u8..16).collect::<Vec<u8>>();
        assert!(Snapshot::try_from(&values[..]).is_err());
        assert!(Snapshot::try_from(values).is_err());
        assert!(Snapshot::try_from(vec![]).is_err());
        assert!(Snapshot::try_from(vec![0u8, 1, 2, 3, 4]).is_err());
    }

    #[fuchsia::test]
    fn snapshot_frozen_vmo() -> Result<(), Error> {
        let size = 4096;
        let (mapping_ref, vmo) = create_mapping(size);
        let mut header =
            Block::new_free(mapping_ref.clone(), 0, constants::HEADER_ORDER as usize, 0)?;
        header.become_reserved()?;
        header.become_header(size)?;
        mapping_ref.write_bytes(8, &[0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF]);

        let snapshot = Snapshot::try_from(&vmo)?;

        #[cfg(target_os = "fuchsia")]
        assert!(matches!(snapshot.buffer, BackingBuffer::Map(_)));

        #[cfg(not(target_os = "fuchsia"))]
        assert!(matches!(snapshot.buffer, BackingBuffer::Vector(_)));

        mapping_ref.write_bytes(8, &[2u8; 8]);
        let snapshot = Snapshot::try_from(&vmo)?;
        assert!(matches!(snapshot.buffer, BackingBuffer::Vector(_)));

        Ok(())
    }

    #[fuchsia::test]
    fn snapshot_vmo_with_unused_space() -> Result<(), Error> {
        let size = 4 * constants::PAGE_SIZE_BYTES;
        let (mapping_ref, vmo) = create_mapping(size);
        let mut header =
            Block::new_free(mapping_ref.clone(), 0, constants::HEADER_ORDER as usize, 0)?;
        header.become_reserved()?;
        header.become_header(constants::PAGE_SIZE_BYTES)?;

        let snapshot = Snapshot::try_from(&vmo)?;
        assert_eq!(snapshot.buffer.len(), constants::PAGE_SIZE_BYTES);

        Ok(())
    }

    #[fuchsia::test]
    fn snapshot_vmo_with_very_large_vmo() -> Result<(), Error> {
        let size = 2 * constants::MAX_VMO_SIZE;
        let (mapping_ref, vmo) = create_mapping(size);
        let mut header =
            Block::new_free(mapping_ref.clone(), 0, constants::HEADER_ORDER as usize, 0)?;
        header.become_reserved()?;
        header.become_header(size)?;

        let snapshot = Snapshot::try_from(&vmo)?;
        assert_eq!(snapshot.buffer.len(), constants::MAX_VMO_SIZE);

        Ok(())
    }

    #[fuchsia::test]
    fn snapshot_vmo_with_header_without_size_info() -> Result<(), Error> {
        let size = 2 * constants::PAGE_SIZE_BYTES;
        let (mapping_ref, vmo) = create_mapping(size);
        let mut header = Block::new_free(mapping_ref.clone(), 0, 0, 0)?;
        header.become_reserved()?;
        header.become_header(constants::PAGE_SIZE_BYTES)?;
        header.set_order(0)?;

        let snapshot = Snapshot::try_from(&vmo)?;
        assert_eq!(snapshot.buffer.len(), size);

        Ok(())
    }
}
