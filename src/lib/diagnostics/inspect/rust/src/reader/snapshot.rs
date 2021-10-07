// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A snapshot represents all the loaded blocks of the VMO in a way that we can reconstruct the
//! implicit tree.

use {
    crate::{reader::error::ReaderError, Inspector},
    fuchsia_zircon::Vmo,
    inspect_format::{constants, utils, Block, BlockType, ReadableBlockContainer},
    mapped_vmo::Mapping,
    std::convert::TryFrom,
    std::sync::Arc,
};

pub use crate::reader::tree_reader::SnapshotTree;

/// Enables to scan all the blocks in a given buffer.
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
    pub fn try_once_from_vmo(vmo: &Vmo) -> Result<Snapshot, ReaderError> {
        Snapshot::try_once_with_callback(vmo, &mut || {})
    }

    fn try_once_with_callback<F>(vmo: &Vmo, read_callback: &mut F) -> Result<Snapshot, ReaderError>
    where
        F: FnMut() -> (),
    {
        // Read the generation count one time
        let mut header_bytes: [u8; 16] = [0; 16];
        vmo.read(&mut header_bytes, 0).map_err(ReaderError::Vmo)?;
        let generation = header_generation_count(&header_bytes[..]);

        if let Some(gen) = generation {
            // Read the buffer
            let size = vmo.get_size().map_err(ReaderError::Vmo)?;
            let mut buffer = vec![0u8; size as usize];
            vmo.read(&mut buffer[..], 0).map_err(ReaderError::Vmo)?;
            if cfg!(test) {
                read_callback();
            }

            // Read the generation count one more time to ensure the previous buffer read is
            // consistent.
            vmo.read(&mut header_bytes, 0).map_err(ReaderError::Vmo)?;
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

    fn try_from_with_callback<F>(vmo: &Vmo, mut read_callback: F) -> Result<Snapshot, ReaderError>
    where
        F: FnMut() -> (),
    {
        let mut i = 0;
        loop {
            match Snapshot::try_once_with_callback(&vmo, &mut read_callback) {
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
        Snapshot::try_from(&bytes[..])
    }
}

/// Construct a snapshot from a VMO.
impl TryFrom<&Vmo> for Snapshot {
    type Error = ReaderError;

    fn try_from(vmo: &Vmo) -> Result<Self, Self::Error> {
        Snapshot::try_from_with_callback(vmo, || {})
    }
}

impl TryFrom<&Inspector> for Snapshot {
    type Error = ReaderError;

    fn try_from(inspector: &Inspector) -> Result<Self, Self::Error> {
        inspector
            .vmo
            .as_ref()
            .ok_or(ReaderError::NoOpInspector)
            .and_then(|vmo| Snapshot::try_from(&**vmo))
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

pub enum BackingBuffer {
    Map(Arc<Mapping>),
    Vector(Vec<u8>),
}

impl From<Vec<u8>> for BackingBuffer {
    fn from(v: Vec<u8>) -> Self {
        BackingBuffer::Vector(v)
    }
}

impl From<Arc<Mapping>> for BackingBuffer {
    fn from(m: Arc<Mapping>) -> Self {
        BackingBuffer::Map(m)
    }
}

impl BackingBuffer {
    pub fn len(&self) -> usize {
        match &self {
            BackingBuffer::Map(m) => m.len(),
            BackingBuffer::Vector(v) => v.len(),
        }
    }
}

impl ReadableBlockContainer for &BackingBuffer {
    fn read_bytes(&self, offset: usize, bytes: &mut [u8]) -> usize {
        match self {
            BackingBuffer::Map(m) => m.read_bytes(offset, bytes),
            BackingBuffer::Vector(b) => (b.as_ref() as &[u8]).read_bytes(offset, bytes),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, anyhow::Error, inspect_format::WritableBlockContainer, mapped_vmo::Mapping,
        std::sync::Arc,
    };

    #[test]
    fn scan() -> Result<(), Error> {
        let (mapping, vmo) = Mapping::allocate(4096)?;
        let mapping_ref = Arc::new(mapping);
        let mut header = Block::new_free(mapping_ref.clone(), 0, 0, 0)?;
        header.become_reserved()?;
        header.become_header()?;

        let b = Block::new_free(mapping_ref.clone(), 1, 2, 0)?;
        b.become_reserved()?;
        b.become_extent(5)?;
        let b = Block::new_free(mapping_ref.clone(), 5, 0, 0)?;
        b.become_reserved()?;
        b.become_int_value(1, 2, 3)?;

        let snapshot = Snapshot::try_from(&vmo)?;

        // Scan blocks
        let blocks = snapshot.scan().collect::<Vec<ScannedBlock<'_>>>();

        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[0].index(), 0);
        assert_eq!(blocks[0].order(), 0);
        assert_eq!(blocks[0].header_magic().unwrap(), constants::HEADER_MAGIC_NUMBER);
        assert_eq!(blocks[0].header_version().unwrap(), constants::HEADER_VERSION_NUMBER);

        assert_eq!(blocks[1].block_type(), BlockType::Extent);
        assert_eq!(blocks[1].index(), 1);
        assert_eq!(blocks[1].order(), 2);
        assert_eq!(blocks[1].next_extent().unwrap(), 5);

        assert_eq!(blocks[2].block_type(), BlockType::IntValue);
        assert_eq!(blocks[2].index(), 5);
        assert_eq!(blocks[2].order(), 0);
        assert_eq!(blocks[2].name_index().unwrap(), 2);
        assert_eq!(blocks[2].parent_index().unwrap(), 3);
        assert_eq!(blocks[2].int_value().unwrap(), 1);
        assert!(blocks[6..].iter().all(|b| b.block_type() == BlockType::Free));

        // Verify get_block
        assert_eq!(snapshot.get_block(0).unwrap().block_type(), BlockType::Header);
        assert_eq!(snapshot.get_block(1).unwrap().block_type(), BlockType::Extent);
        assert_eq!(snapshot.get_block(5).unwrap().block_type(), BlockType::IntValue);
        assert_eq!(snapshot.get_block(6).unwrap().block_type(), BlockType::Free);
        assert!(snapshot.get_block(4096).is_none());

        Ok(())
    }

    #[test]
    fn scan_bad_header() -> Result<(), Error> {
        let (mapping, vmo) = Mapping::allocate(4096)?;
        let mapping_ref = Arc::new(mapping);

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

    #[test]
    fn invalid_type() -> Result<(), Error> {
        let (mapping, vmo) = Mapping::allocate(4096)?;
        let mapping_ref = Arc::new(mapping);
        mapping_ref.write_bytes(0, &[0x00, 0xff, 0x01]);
        assert!(Snapshot::try_from(&vmo).is_err());
        Ok(())
    }

    #[test]
    fn invalid_order() -> Result<(), Error> {
        let (mapping, vmo) = Mapping::allocate(4096)?;
        let mapping_ref = Arc::new(mapping);
        mapping_ref.write_bytes(0, &[0xff, 0xff]);
        assert!(Snapshot::try_from(&vmo).is_err());
        Ok(())
    }

    #[test]
    fn invalid_pending_write() -> Result<(), Error> {
        let (mapping, vmo) = Mapping::allocate(4096)?;
        let mapping_ref = Arc::new(mapping);
        let mut header = Block::new_free(mapping_ref.clone(), 0, 0, 0)?;
        header.become_reserved()?;
        header.become_header()?;
        header.lock_header()?;
        assert!(Snapshot::try_from(&vmo).is_err());
        Ok(())
    }

    #[test]
    fn invalid_magic_number() -> Result<(), Error> {
        let (mapping, vmo) = Mapping::allocate(4096)?;
        let mapping_ref = Arc::new(mapping);
        let mut header = Block::new_free(mapping_ref.clone(), 0, 0, 0)?;
        header.become_reserved()?;
        header.become_header()?;
        header.set_header_magic(3)?;
        assert!(Snapshot::try_from(&vmo).is_err());
        Ok(())
    }

    #[test]
    fn invalid_generation_count() -> Result<(), Error> {
        let (mapping, vmo) = Mapping::allocate(4096)?;
        let mapping_ref = Arc::new(mapping);
        let mut header = Block::new_free(mapping_ref.clone(), 0, 0, 0)?;
        header.become_reserved()?;
        header.become_header()?;
        assert!(Snapshot::try_from_with_callback(&vmo, || {
            header.lock_header().unwrap();
            header.unlock_header().unwrap();
        })
        .is_err());
        Ok(())
    }

    #[test]
    fn snapshot_from_few_bytes() {
        let values = (0u8..16).collect::<Vec<u8>>();
        assert!(Snapshot::try_from(&values[..]).is_err());
        assert!(Snapshot::try_from(values).is_err());
        assert!(Snapshot::try_from(vec![]).is_err());
        assert!(Snapshot::try_from(vec![0u8, 1, 2, 3, 4]).is_err());
    }
}
