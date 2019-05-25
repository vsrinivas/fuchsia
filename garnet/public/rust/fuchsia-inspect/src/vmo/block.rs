// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::vmo::{
        bitfields::{BlockHeader, Payload},
        block_type::BlockType,
        constants, utils,
    },
    byteorder::{ByteOrder, LittleEndian},
    failure::{format_err, Error},
    mapped_vmo::Mapping,
    num_derive::{FromPrimitive, ToPrimitive},
    num_traits::{FromPrimitive, ToPrimitive},
    std::{
        cmp::min,
        ptr,
        sync::atomic::{fence, Ordering},
        sync::Arc,
    },
};

/// Format in which the property will be read.
#[derive(Debug, PartialEq, Eq, FromPrimitive, ToPrimitive)]
pub enum PropertyFormat {
    String = 0,
    Bytes = 1,
}

#[derive(Debug)]
pub struct Block<T> {
    index: u32,
    container: T,
}

pub trait ReadableBlockContainer {
    fn read_bytes(&self, offset: usize, bytes: &mut [u8]) -> usize;
}

pub trait WritableBlockContainer {
    fn write_bytes(&self, offset: usize, bytes: &[u8]) -> usize;
}

pub trait BlockContainerEq<RHS = Self> {
    fn ptr_eq(&self, other: &RHS) -> bool;
}

impl ReadableBlockContainer for Arc<Mapping> {
    fn read_bytes(&self, offset: usize, bytes: &mut [u8]) -> usize {
        self.read_at(offset, bytes)
    }
}

impl ReadableBlockContainer for &[u8] {
    fn read_bytes(&self, offset: usize, bytes: &mut [u8]) -> usize {
        if offset >= self.len() {
            return 0;
        }
        let upper_bound = min(self.len(), bytes.len() + offset);
        let bytes_read = upper_bound - offset;
        bytes[..bytes_read].clone_from_slice(&self[offset..upper_bound]);
        bytes_read
    }
}

impl BlockContainerEq for Arc<Mapping> {
    fn ptr_eq(&self, other: &Arc<Mapping>) -> bool {
        Arc::ptr_eq(&self, &other)
    }
}

impl BlockContainerEq for &[u8] {
    fn ptr_eq(&self, other: &&[u8]) -> bool {
        ptr::eq(*self, *other)
    }
}

impl WritableBlockContainer for Arc<Mapping> {
    fn write_bytes(&self, offset: usize, bytes: &[u8]) -> usize {
        self.write_at(offset, bytes)
    }
}

impl<T: ReadableBlockContainer> Block<T> {
    /// Creates a new block.
    pub fn new(container: T, index: u32) -> Self {
        Block { container: container, index: index }
    }

    /// Returns index of the block in the vmo.
    pub fn index(&self) -> u32 {
        self.index
    }

    /// Returns the order of the block.
    pub fn order(&self) -> usize {
        self.read_header().order().to_usize().unwrap()
    }

    /// Returns the magic number in a HEADER block.
    pub fn header_magic(&self) -> Result<u32, Error> {
        self.check_type(BlockType::Header)?;
        Ok(self.read_header().header_magic())
    }

    /// Returns the version of a HEADER block.
    pub fn header_version(&self) -> Result<u32, Error> {
        self.check_type(BlockType::Header)?;
        Ok(self.read_header().header_version())
    }

    /// Returns the generation count of a HEADER block.
    pub fn header_generation_count(&self) -> Result<u64, Error> {
        self.check_type(BlockType::Header)?;
        Ok(self.read_payload().header_generation_count())
    }

    /// True if the header is locked, false otherwise.
    pub fn header_is_locked(&self) -> Result<bool, Error> {
        self.check_type(BlockType::Header)?;
        let payload = self.read_payload();
        Ok(payload.header_generation_count() & 1 == 1)
    }

    /// Get the double value of a DOUBLE_VALUE block.
    pub fn double_value(&self) -> Result<f64, Error> {
        self.check_type(BlockType::DoubleValue)?;
        Ok(f64::from_bits(self.read_payload().numeric_value()))
    }

    /// Get the value of an INT_VALUE block.
    pub fn int_value(&self) -> Result<i64, Error> {
        self.check_type(BlockType::IntValue)?;
        Ok(i64::from_le_bytes(self.read_payload().numeric_value().to_le_bytes()))
    }

    /// Get the unsigned value of an UINT_VALUE block.
    pub fn uint_value(&self) -> Result<u64, Error> {
        self.check_type(BlockType::UintValue)?;
        Ok(self.read_payload().numeric_value())
    }

    /// Get the index of the EXTENT of the PROPERTY block.
    pub fn property_extent_index(&self) -> Result<u32, Error> {
        self.check_type(BlockType::PropertyValue)?;
        Ok(self.read_payload().property_extent_index())
    }

    /// Get the total length of the PROPERTY block.
    pub fn property_total_length(&self) -> Result<usize, Error> {
        self.check_type(BlockType::PropertyValue)?;
        Ok(self.read_payload().property_total_length().to_usize().unwrap())
    }

    /// Get the flags of a PROPERTY block.
    pub fn property_format(&self) -> Result<PropertyFormat, Error> {
        self.check_type(BlockType::PropertyValue)?;
        Ok(PropertyFormat::from_u8(self.read_payload().property_flags()).unwrap())
    }

    /// Returns the next EXTENT in an EXTENT chain.
    pub fn next_extent(&self) -> Result<u32, Error> {
        self.check_type(BlockType::Extent)?;
        Ok(self.read_header().extent_next_index())
    }

    /// Returns the payload bytes value of an EXTENT block.
    pub fn extent_contents(&self) -> Result<Vec<u8>, Error> {
        self.check_type(BlockType::Extent)?;
        let length = utils::payload_size_for_order(self.order());
        let mut bytes = vec![0u8; length];
        self.container.read_bytes(self.payload_offset(), &mut bytes);
        Ok(bytes)
    }

    /// Get the NAME block index of a *_VALUE block.
    pub fn name_index(&self) -> Result<u32, Error> {
        self.check_any_value()?;
        Ok(self.read_header().value_name_index())
    }

    /// Get the parent block index of a *_VALUE block.
    pub fn parent_index(&self) -> Result<u32, Error> {
        self.check_any_value()?;
        Ok(self.read_header().value_parent_index())
    }

    /// Get the child count of a NODE_VALUE block.
    pub fn child_count(&self) -> Result<u64, Error> {
        self.check_node_or_tombstone()?;
        Ok(self.read_payload().numeric_value())
    }

    /// Get next free block
    pub fn free_next_index(&self) -> Result<u32, Error> {
        self.check_type(BlockType::Free)?;
        Ok(self.read_header().free_next_index())
    }

    /// Get the length of the name of a NAME block
    pub fn name_length(&self) -> Result<usize, Error> {
        self.check_type(BlockType::Name)?;
        Ok(self.read_header().name_length().to_usize().unwrap())
    }

    /// Returns the contents of a NAME block.
    pub fn name_contents(&self) -> Result<String, Error> {
        self.check_type(BlockType::Name)?;
        let length = self.name_length()?;
        let mut bytes = vec![0u8; length];
        self.container.read_bytes(self.payload_offset(), &mut bytes);
        Ok(String::from(std::str::from_utf8(&bytes).unwrap()))
    }

    /// Returns the type of a block.
    pub fn block_type(&self) -> BlockType {
        BlockType::from_u8(self.read_header().block_type()).unwrap()
    }

    /// Check that the block type is |block_type|
    fn check_type(&self, block_type: BlockType) -> Result<(), Error> {
        if self.block_type() != block_type {
            Err(format_err!("Expected type {}, got type {}", block_type, self.block_type()))
        } else {
            Ok(())
        }
    }

    /// Get the block header.
    fn read_header(&self) -> BlockHeader {
        let mut bytes = [0u8; 8];
        self.container.read_bytes(self.header_offset(), &mut bytes);
        BlockHeader(u64::from_le_bytes(bytes))
    }

    /// Get the block payload.
    fn read_payload(&self) -> Payload {
        let mut bytes = [0u8; 8];
        self.container.read_bytes(self.payload_offset(), &mut bytes);
        Payload(u64::from_le_bytes(bytes))
    }

    /// Get the offset of the payload in the container.
    fn payload_offset(&self) -> usize {
        utils::offset_for_index(self.index) + constants::HEADER_SIZE_BYTES
    }

    /// Get the offset of the header in the container.
    fn header_offset(&self) -> usize {
        utils::offset_for_index(self.index)
    }

    /// Check if the HEADER block is locked (when generation count is odd).
    fn check_locked(&self, value: bool) -> Result<(), Error> {
        let payload = self.read_payload();
        if (payload.header_generation_count() & 1 == 1) != value {
            return Err(format_err!("Expected locked={}, actual={}", value, !value));
        }
        Ok(())
    }

    /// Check if the block is NODE or TOMBSTONE.
    fn check_node_or_tombstone(&self) -> Result<(), Error> {
        if self.block_type().is_node_or_tombstone() {
            return Ok(());
        }
        Err(format_err!("Expected NODE|TOMBSTONE, got: {}", self.block_type()))
    }

    /// Check if the block is of *_VALUE.
    fn check_any_value(&self) -> Result<(), Error> {
        if self.block_type().is_any_value() {
            return Ok(());
        }
        Err(format_err!("Block type {} is not *_VALUE", self.block_type()))
    }
}

impl<T: ReadableBlockContainer + WritableBlockContainer + BlockContainerEq> Block<T> {
    /// Initializes an empty reserved block.
    pub fn new_free(container: T, index: u32, order: usize, next_free: u32) -> Result<Self, Error> {
        if order >= constants::NUM_ORDERS {
            return Err(format_err!("Order {} must be less than {}", order, constants::NUM_ORDERS));
        }
        let mut header = BlockHeader(0);
        header.set_order(order.to_u8().unwrap());
        header.set_block_type(BlockType::Free.to_u8().unwrap());
        header.set_free_next_index(next_free);
        let block = Block::new(container, index);
        block.write_header(header);
        Ok(block)
    }

    /// Swaps two blocks if they are the same order.
    pub fn swap(&mut self, other: &mut Block<T>) -> Result<(), Error> {
        if self.order() != other.order() || !self.container.ptr_eq(&other.container) {
            return Err(format_err!("cannot swap blocks of different order or container"));
        }
        std::mem::swap(&mut self.index, &mut other.index);
        Ok(())
    }

    /// Set the order of the block.
    pub fn set_order(&self, order: usize) -> Result<(), Error> {
        if order >= constants::NUM_ORDERS {
            return Err(format_err!(
                "Order {} must be less than max {}",
                order,
                constants::NUM_ORDERS
            ));
        }
        let mut header = self.read_header();
        header.set_order(order.to_u8().unwrap());
        self.write_header(header);
        Ok(())
    }

    /// Initializes a HEADER block.
    pub fn become_header(&mut self) -> Result<(), Error> {
        self.check_type(BlockType::Reserved)?;
        self.index = 0;
        let mut header = BlockHeader(0);
        header.set_order(0);
        header.set_block_type(BlockType::Header.to_u8().unwrap());
        header.set_header_magic(constants::HEADER_MAGIC_NUMBER);
        header.set_header_version(constants::HEADER_VERSION_NUMBER);
        self.write(header, Payload(0));
        Ok(())
    }

    #[cfg(test)]
    pub(super) fn set_header_magic(&self, value: u32) -> Result<(), Error> {
        self.check_type(BlockType::Header)?;
        let mut header = self.read_header();
        header.set_header_magic(value);
        self.write_header(header);
        Ok(())
    }

    /// Lock a HEADER block
    pub fn lock_header(&self) -> Result<(), Error> {
        self.check_type(BlockType::Header)?;
        self.check_locked(false)?;
        self.increment_generation_count();
        Ok(())
    }

    /// Unlock a HEADER block
    pub fn unlock_header(&self) -> Result<(), Error> {
        self.check_type(BlockType::Header)?;
        self.check_locked(true)?;
        self.increment_generation_count();
        Ok(())
    }

    /// Initializes a TOMBSTONE block.
    pub fn become_tombstone(&self) -> Result<(), Error> {
        self.check_type(BlockType::NodeValue)?;
        let mut header = self.read_header();
        header.set_block_type(BlockType::Tombstone.to_u8().unwrap());
        self.write_header(header);
        Ok(())
    }

    /// Converts a FREE block to a RESERVED block
    pub fn become_reserved(&self) -> Result<(), Error> {
        self.check_type(BlockType::Free)?;
        let mut header = self.read_header();
        header.set_block_type(BlockType::Reserved.to_u8().unwrap());
        self.write_header(header);
        Ok(())
    }

    /// Converts a block to a FREE block
    pub fn become_free(&self, next: u32) {
        let mut header = self.read_header();
        header.set_block_type(BlockType::Free.to_u8().unwrap());
        header.set_free_next_index(next);
        self.write_header(header);
    }

    /// Converts a block to an EXTENT block.
    pub fn become_extent(&self, next_extent_index: u32) -> Result<(), Error> {
        self.check_type(BlockType::Reserved)?;
        let mut header = self.read_header();
        header.set_block_type(BlockType::Extent.to_u8().unwrap());
        header.set_extent_next_index(next_extent_index);
        self.write_header(header);
        Ok(())
    }

    /// Sets the index of the next EXTENT in the chain.
    pub fn set_extent_next_index(&self, next_extent_index: u32) -> Result<(), Error> {
        self.check_type(BlockType::Extent)?;
        let mut header = self.read_header();
        header.set_extent_next_index(next_extent_index);
        self.write_header(header);
        Ok(())
    }

    /// Set the payload of an EXTENT block. The bytes written will be returned.
    pub fn extent_set_contents(&self, value: &[u8]) -> Result<usize, Error> {
        self.check_type(BlockType::Extent)?;
        let max_bytes = utils::payload_size_for_order(self.order());
        let mut bytes = value;
        if bytes.len() > max_bytes {
            bytes = &bytes[..min(bytes.len(), max_bytes)];
        }
        self.write_payload_from_bytes(bytes);
        Ok(bytes.len())
    }

    /// Converts a RESERVED block into a DOUBLE_VALUE block.
    pub fn become_double_value(
        &self,
        value: f64,
        name_index: u32,
        parent_index: u32,
    ) -> Result<(), Error> {
        self.write_value_header(BlockType::DoubleValue, name_index, parent_index)?;
        self.set_double_value(value)
    }

    /// Sets the value of a DOUBLE_VALUE block.
    pub fn set_double_value(&self, value: f64) -> Result<(), Error> {
        self.check_type(BlockType::DoubleValue)?;
        let mut payload = self.read_payload();
        payload.set_numeric_value(value.to_bits());
        self.write_payload(payload);
        Ok(())
    }

    /// Converts a RESERVED block into a INT_VALUE block.
    pub fn become_int_value(
        &self,
        value: i64,
        name_index: u32,
        parent_index: u32,
    ) -> Result<(), Error> {
        self.write_value_header(BlockType::IntValue, name_index, parent_index)?;
        self.set_int_value(value)
    }

    /// Sets the value of an INT_VALUE block.
    pub fn set_int_value(&self, value: i64) -> Result<(), Error> {
        self.check_type(BlockType::IntValue)?;
        let mut payload = self.read_payload();
        payload.set_numeric_value(LittleEndian::read_u64(&value.to_le_bytes()));
        self.write_payload(payload);
        Ok(())
    }

    /// Converts a block into a UINT_VALUE block.
    pub fn become_uint_value(
        &self,
        value: u64,
        name_index: u32,
        parent_index: u32,
    ) -> Result<(), Error> {
        self.write_value_header(BlockType::UintValue, name_index, parent_index)?;
        self.set_uint_value(value)
    }

    /// Sets the value of a UINT_VALUE block.
    pub fn set_uint_value(&self, value: u64) -> Result<(), Error> {
        self.check_type(BlockType::UintValue)?;
        let mut payload = self.read_payload();
        payload.set_numeric_value(value);
        self.write_payload(payload);
        Ok(())
    }

    /// Initializes a NODE_VALUE block.
    pub fn become_node(&self, name_index: u32, parent_index: u32) -> Result<(), Error> {
        self.write_value_header(BlockType::NodeValue, name_index, parent_index)?;
        self.write_payload(Payload(0));
        Ok(())
    }

    /// Converts a *_VALUE block into a PROPERTY_VALUE block.
    pub fn become_property(
        &self,
        name_index: u32,
        parent_index: u32,
        format: PropertyFormat,
    ) -> Result<(), Error> {
        self.write_value_header(BlockType::PropertyValue, name_index, parent_index)?;
        let mut payload = Payload(0);
        payload.set_property_flags(format.to_u8().unwrap());
        self.write_payload(payload);
        Ok(())
    }

    /// Sets the total length of a PROPERTY_VALUE block.
    pub fn set_property_total_length(&self, length: u32) -> Result<(), Error> {
        self.check_type(BlockType::PropertyValue)?;
        let mut payload = self.read_payload();
        payload.set_property_total_length(length);
        self.write_payload(payload);
        Ok(())
    }

    /// Sets the index of the EXTENT of a PROPERTY_VALUE block.
    pub fn set_property_extent_index(&self, index: u32) -> Result<(), Error> {
        self.check_type(BlockType::PropertyValue)?;
        let mut payload = self.read_payload();
        payload.set_property_extent_index(index);
        self.write_payload(payload);
        Ok(())
    }

    /// Set the child count of a NODE_VALUE block.
    pub fn set_child_count(&self, count: u64) -> Result<(), Error> {
        self.check_node_or_tombstone()?;
        self.write_payload(Payload(count));
        Ok(())
    }

    /// Creates a NAME block.
    pub fn become_name(&self, name: &str) -> Result<(), Error> {
        self.check_type(BlockType::Reserved)?;
        let mut bytes = name.as_bytes();
        let max_len = utils::payload_size_for_order(self.order());
        if bytes.len() > max_len {
            bytes = &bytes[..min(bytes.len(), max_len)];
        }
        let mut header = self.read_header();
        header.set_block_type(BlockType::Name.to_u8().unwrap());
        header.set_name_length(u16::from_usize(bytes.len()).unwrap());
        self.write_header(header);
        self.write_payload_from_bytes(bytes);
        Ok(())
    }

    /// Set the next free block.
    pub fn set_free_next_index(&self, next_free: u32) -> Result<(), Error> {
        self.check_type(BlockType::Free)?;
        let mut header = self.read_header();
        header.set_free_next_index(next_free);
        self.write_header(header);
        Ok(())
    }

    /// Initializes a *_VALUE block header.
    fn write_value_header(
        &self,
        block_type: BlockType,
        name_index: u32,
        parent_index: u32,
    ) -> Result<(), Error> {
        if !block_type.is_any_value() {
            return Err(format_err!("Block type {} is not *_VALUE", block_type));
        }
        self.check_type(BlockType::Reserved)?;
        let mut header = self.read_header();
        header.set_block_type(block_type.to_u8().unwrap());
        header.set_value_name_index(name_index);
        header.set_value_parent_index(parent_index);
        self.write_header(header);
        Ok(())
    }

    /// Writes the given header and payload to the block in the container.
    pub(in crate::vmo) fn write(&self, header: BlockHeader, payload: Payload) {
        self.write_header(header);
        self.write_payload(payload);
    }

    /// Writes the given header to the block in the container.
    fn write_header(&self, header: BlockHeader) {
        self.container.write_bytes(self.header_offset(), &header.value().to_le_bytes());
    }

    /// Writes the given payload to the block in the container.
    fn write_payload(&self, payload: Payload) {
        self.write_payload_from_bytes(&payload.value().to_le_bytes());
    }

    /// Write |bytes| to the payload section of the block in the container.
    fn write_payload_from_bytes(&self, bytes: &[u8]) {
        self.container.write_bytes(self.payload_offset(), bytes);
    }

    /// Increment generation counter in a HEADER block for locking/unlocking
    fn increment_generation_count(&self) {
        let mut payload = self.read_payload();
        let value = payload.header_generation_count();
        let new_value = if value == u64::max_value() { 0 } else { value + 1 };
        payload.set_header_generation_count(new_value);
        if new_value % 2 != 0 {
            fence(Ordering::Acquire);
        }
        self.write_payload(payload);
        if new_value % 2 == 0 {
            fence(Ordering::Release);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeSet;
    use std::iter::FromIterator;
    use std::ptr::copy_nonoverlapping;

    impl WritableBlockContainer for &[u8] {
        fn write_bytes(&self, offset: usize, bytes: &[u8]) -> usize {
            if offset >= self.len() {
                return 0;
            }
            let bytes_written = min(self.len() - offset, bytes.len());
            let base = (self.as_ptr() as usize).checked_add(offset).unwrap() as *mut u8;
            unsafe { copy_nonoverlapping(bytes.as_ptr(), base, bytes_written) };
            bytes_written
        }
    }

    fn create_with_type(container: &[u8], index: u32, block_type: BlockType) -> Block<&[u8]> {
        let block = Block::new(container, index);
        let mut header = BlockHeader(0);
        header.set_block_type(block_type.to_u8().unwrap());
        block.write_header(header);
        block
    }

    fn test_error_types<T>(
        f: fn(&Block<&[u8]>) -> Result<T, Error>,
        error_types: &BTreeSet<BlockType>,
    ) {
        let container = [0u8; constants::MIN_ORDER_SIZE];
        for block_type in BlockType::all().iter() {
            let block = create_with_type(&container[..], 0, block_type.clone());
            let result = f(&block);
            if error_types.contains(&block_type) {
                assert!(result.is_err());
            } else {
                assert!(result.is_ok());
            }
        }
    }

    fn test_ok_types<T>(
        f: fn(&mut Block<&[u8]>) -> Result<T, Error>,
        ok_types: &BTreeSet<BlockType>,
    ) {
        let container = [0u8; constants::MIN_ORDER_SIZE];
        for block_type in BlockType::all().iter() {
            let mut block = create_with_type(&container[..], 0, block_type.clone());
            let result = f(&mut block);
            if ok_types.contains(&block_type) {
                assert!(result.is_ok());
            } else {
                assert!(result.is_err());
            }
        }
    }

    #[test]
    fn test_new_free() {
        let container = [0u8; constants::MIN_ORDER_SIZE];
        assert!(Block::new_free(&container[..], 3, constants::NUM_ORDERS, 1).is_err());

        let res = Block::new_free(&container[..], 0, 3, 1);
        assert!(res.is_ok());
        let block = res.unwrap();
        assert_eq!(block.index(), 0);
        assert_eq!(block.order(), 3);
        assert_eq!(block.free_next_index().unwrap(), 1);
        assert_eq!(block.block_type(), BlockType::Free);
        assert_eq!(container[..8], [0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        assert_eq!(container[8..], [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
    }

    #[test]
    fn test_swap() {
        let container = [0u8; constants::MIN_ORDER_SIZE * 3];
        let mut block1 = Block::new_free(&container[..], 0, 1, 2).unwrap();
        let mut block2 = Block::new_free(&container[..], 1, 1, 0).unwrap();
        let mut block3 = Block::new_free(&container[..], 2, 3, 4).unwrap();

        // Can't swap with block of different order
        assert!(block1.swap(&mut block3).is_err());

        assert!(block2.become_reserved().is_ok());

        assert!(block1.swap(&mut block2).is_ok());

        assert_eq!(block1.index(), 1);
        assert_eq!(block1.order(), 1);
        assert_eq!(block1.block_type(), BlockType::Reserved);
        assert!(block1.free_next_index().is_err());
        assert_eq!(block2.index(), 0);
        assert_eq!(block2.order(), 1);
        assert_eq!(block2.block_type(), BlockType::Free);
        assert_eq!(block2.free_next_index().unwrap(), 2);

        assert_eq!(container[..8], [0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        assert_eq!(container[8..16], [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        assert_eq!(container[16..24], [0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        assert_eq!(container[24..32], [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        assert_eq!(container[32..40], [0x03, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        assert_eq!(container[40..48], [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
    }

    #[test]
    fn test_set_order() {
        test_error_types(move |b| b.set_order(1), &BTreeSet::new());
        let container = [0u8; constants::MIN_ORDER_SIZE];
        let block = Block::new_free(&container[..], 0, 1, 1).unwrap();
        assert!(block.set_order(3).is_ok());
        assert_eq!(block.order(), 3);
    }

    #[test]
    fn test_become_reserved() {
        test_ok_types(move |b| b.become_reserved(), &BTreeSet::from_iter(vec![BlockType::Free]));
        let container = [0u8; constants::MIN_ORDER_SIZE];
        let block = Block::new_free(&container[..], 0, 1, 2).unwrap();
        assert!(block.become_reserved().is_ok());
        assert_eq!(block.block_type(), BlockType::Reserved);
        assert_eq!(container[..8], [0x11, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        assert_eq!(container[8..], [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
    }

    #[test]
    fn test_become_header() {
        let container = [0u8; constants::MIN_ORDER_SIZE];
        let mut block = get_reserved(&container);
        assert!(block.become_header().is_ok());
        assert_eq!(block.block_type(), BlockType::Header);
        assert_eq!(block.index(), 0);
        assert_eq!(block.order(), 0);
        assert_eq!(block.header_magic().unwrap(), constants::HEADER_MAGIC_NUMBER);
        assert_eq!(block.header_version().unwrap(), constants::HEADER_VERSION_NUMBER);
        assert_eq!(container[..8], [0x20, 0x00, 0x00, 0x00, 0x49, 0x4e, 0x53, 0x50]);
        assert_eq!(container[8..], [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);

        test_ok_types(move |b| b.become_header(), &BTreeSet::from_iter(vec![BlockType::Reserved]));
        test_ok_types(move |b| b.header_magic(), &BTreeSet::from_iter(vec![BlockType::Header]));
        test_ok_types(move |b| b.header_version(), &BTreeSet::from_iter(vec![BlockType::Header]));
    }

    #[test]
    fn test_lock_unlock_header() {
        let container = [0u8; constants::MIN_ORDER_SIZE];
        let block = get_header(&container);
        let header_bytes: [u8; 8] = [0x20, 0x00, 0x00, 0x00, 0x49, 0x4e, 0x53, 0x50];
        // Can't unlock unlocked header.
        assert!(block.unlock_header().is_err());
        assert!(block.lock_header().is_ok());
        assert!(block.header_is_locked().unwrap());
        assert_eq!(block.header_generation_count().unwrap(), 1);
        assert_eq!(container[..8], header_bytes[..]);
        assert_eq!(container[8..], [0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        // Can't lock locked header.
        assert!(block.lock_header().is_err());
        assert!(block.unlock_header().is_ok());
        assert!(!block.header_is_locked().unwrap());
        assert_eq!(block.header_generation_count().unwrap(), 2);
        assert_eq!(container[..8], header_bytes[..]);
        assert_eq!(container[8..], [0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);

        // Test overflow: set payload bytes to max u64 value. Ensure we cannot lock
        // and after unlocking, the value is zero.
        (&container[..]).write_bytes(8, &u64::max_value().to_le_bytes());
        assert!(block.lock_header().is_err());
        assert!(block.unlock_header().is_ok());
        assert_eq!(block.header_generation_count().unwrap(), 0);
        assert_eq!(container[..8], header_bytes[..]);
        assert_eq!(container[8..], [0, 0, 0, 0, 0, 0, 0, 0]);
        test_ok_types(
            move |b| {
                b.header_generation_count()?;
                b.lock_header()?;
                b.unlock_header()
            },
            &BTreeSet::from_iter(vec![BlockType::Header]),
        );
    }

    #[test]
    fn test_become_tombstone() {
        let container = [0u8; constants::MIN_ORDER_SIZE];
        let block = get_reserved(&container);
        assert!(block.become_node(2, 3).is_ok());
        assert!(block.set_child_count(4).is_ok());
        assert!(block.become_tombstone().is_ok());
        assert_eq!(block.block_type(), BlockType::Tombstone);
        assert_eq!(block.child_count().unwrap(), 4);
        assert_eq!(container[..8], [0xa1, 0x03, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00]);
        assert_eq!(container[8..], [0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        test_ok_types(
            move |b| b.become_tombstone(),
            &BTreeSet::from_iter(vec![BlockType::NodeValue]),
        );
    }

    #[test]
    fn test_child_count() {
        let container = [0u8; constants::MIN_ORDER_SIZE];
        let block = get_reserved(&container);
        assert!(block.become_node(2, 3).is_ok());
        assert_eq!(container[..8], [0x31, 0x03, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00]);
        assert_eq!(container[8..], [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        assert!(block.set_child_count(4).is_ok());
        assert_eq!(block.child_count().unwrap(), 4);
        assert_eq!(container[..8], [0x31, 0x03, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00]);
        assert_eq!(container[8..], [0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        let types = BTreeSet::from_iter(vec![BlockType::Tombstone, BlockType::NodeValue]);
        test_ok_types(move |b| b.child_count(), &types);
        test_ok_types(move |b| b.set_child_count(3), &types);
    }

    #[test]
    fn test_free() {
        let container = [0u8; constants::MIN_ORDER_SIZE];
        let block = Block::new_free(&container[..], 0, 1, 1).unwrap();
        assert!(block.set_free_next_index(3).is_ok());
        assert_eq!(block.free_next_index().unwrap(), 3);
        assert_eq!(container[..8], [0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        assert_eq!(container[8..], [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        test_error_types(
            move |b| {
                b.become_free(1);
                Ok(())
            },
            &BTreeSet::new(),
        );
    }

    #[test]
    fn test_extent() {
        let container = [0u8; constants::MIN_ORDER_SIZE * 2];
        let block = get_reserved(&container);
        assert!(block.become_extent(3).is_ok());
        assert_eq!(block.block_type(), BlockType::Extent);
        assert_eq!(block.next_extent().unwrap(), 3);
        assert_eq!(container[..8], [0x81, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        assert_eq!(container[8..16], [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);

        assert_eq!(block.extent_set_contents(&"test-rust-inspect".as_bytes()).unwrap(), 17);
        assert_eq!(
            String::from_utf8(block.extent_contents().unwrap()).unwrap(),
            "test-rust-inspect\0\0\0\0\0\0\0"
        );
        assert_eq!(&container[8..25], "test-rust-inspect".as_bytes());
        assert_eq!(container[25..], [0, 0, 0, 0, 0, 0, 0]);

        assert!(block.set_extent_next_index(4).is_ok());
        assert_eq!(block.next_extent().unwrap(), 4);

        test_ok_types(move |b| b.become_extent(1), &BTreeSet::from_iter(vec![BlockType::Reserved]));
        test_ok_types(move |b| b.next_extent(), &BTreeSet::from_iter(vec![BlockType::Extent]));
        test_ok_types(
            move |b| b.set_extent_next_index(4),
            &BTreeSet::from_iter(vec![BlockType::Extent]),
        );
        test_ok_types(
            move |b| b.extent_set_contents(&"test".as_bytes()),
            &BTreeSet::from_iter(vec![BlockType::Extent]),
        );
    }

    #[test]
    fn test_any_value() {
        let any_value = &BTreeSet::from_iter(vec![
            BlockType::DoubleValue,
            BlockType::IntValue,
            BlockType::UintValue,
            BlockType::NodeValue,
            BlockType::PropertyValue,
        ]);
        test_ok_types(move |b| b.name_index(), &any_value);
        test_ok_types(move |b| b.parent_index(), &any_value);
    }

    #[test]
    fn test_double_value() {
        let container = [0u8; constants::MIN_ORDER_SIZE];
        let block = get_reserved(&container);
        assert!(block.become_double_value(1.0, 2, 3).is_ok());
        assert_eq!(block.block_type(), BlockType::DoubleValue);
        assert_eq!(block.name_index().unwrap(), 2);
        assert_eq!(block.parent_index().unwrap(), 3);
        assert_eq!(block.double_value().unwrap(), 1.0);
        assert_eq!(container[..8], [0x61, 0x03, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00]);
        assert_eq!(container[8..], [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f]);

        assert!(block.set_double_value(5.0).is_ok());
        assert_eq!(block.double_value().unwrap(), 5.0);
        assert_eq!(container[..8], [0x61, 0x03, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00]);
        assert_eq!(container[8..], [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x40]);

        let types = BTreeSet::from_iter(vec![BlockType::DoubleValue]);
        test_ok_types(move |b| b.double_value(), &types);
        test_ok_types(move |b| b.set_double_value(3.0), &types);
        test_ok_types(
            move |b| b.become_double_value(1.0, 1, 2),
            &BTreeSet::from_iter(vec![BlockType::Reserved]),
        );
    }

    #[test]
    fn test_int_value() {
        test_ok_types(
            move |b| b.become_int_value(1, 1, 2),
            &BTreeSet::from_iter(vec![BlockType::Reserved]),
        );
        let container = [0u8; constants::MIN_ORDER_SIZE];
        let block = get_reserved(&container);
        assert!(block.become_int_value(1, 2, 3).is_ok());
        assert_eq!(block.block_type(), BlockType::IntValue);
        assert_eq!(block.name_index().unwrap(), 2);
        assert_eq!(block.parent_index().unwrap(), 3);
        assert_eq!(block.int_value().unwrap(), 1);
        assert_eq!(container[..8], [0x41, 0x03, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00]);
        assert_eq!(container[8..], [0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);

        assert!(block.set_int_value(-5).is_ok());
        assert_eq!(block.int_value().unwrap(), -5);
        assert_eq!(container[..8], [0x41, 0x03, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00]);
        assert_eq!(container[8..], [0xfb, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff]);

        let types = BTreeSet::from_iter(vec![BlockType::IntValue]);
        test_ok_types(move |b| b.int_value(), &types);
        test_ok_types(move |b| b.set_int_value(3), &types);
    }

    #[test]
    fn test_uint_value() {
        test_ok_types(
            move |b| b.become_uint_value(1, 1, 2),
            &BTreeSet::from_iter(vec![BlockType::Reserved]),
        );
        let container = [0u8; constants::MIN_ORDER_SIZE];
        let block = get_reserved(&container);
        assert!(block.become_uint_value(1, 2, 3).is_ok());
        assert_eq!(block.block_type(), BlockType::UintValue);
        assert_eq!(block.name_index().unwrap(), 2);
        assert_eq!(block.parent_index().unwrap(), 3);
        assert_eq!(block.uint_value().unwrap(), 1);
        assert_eq!(container[..8], [0x51, 0x03, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00]);
        assert_eq!(container[8..], [0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);

        assert!(block.set_uint_value(5).is_ok());
        assert_eq!(block.uint_value().unwrap(), 5);
        assert_eq!(container[..8], [0x51, 0x03, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00]);
        assert_eq!(container[8..], [0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        test_ok_types(move |b| b.uint_value(), &BTreeSet::from_iter(vec![BlockType::UintValue]));
        test_ok_types(
            move |b| b.set_uint_value(3),
            &BTreeSet::from_iter(vec![BlockType::UintValue]),
        );
    }

    #[test]
    fn test_become_node() {
        let container = [0u8; constants::MIN_ORDER_SIZE];
        let block = get_reserved(&container);
        assert!(block.become_node(2, 3).is_ok());
        assert_eq!(block.block_type(), BlockType::NodeValue);
        assert_eq!(block.name_index().unwrap(), 2);
        assert_eq!(block.parent_index().unwrap(), 3);
        assert_eq!(container[..8], [0x31, 0x03, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00]);
        assert_eq!(container[8..], [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        test_ok_types(
            move |b| b.become_node(1, 2),
            &BTreeSet::from_iter(vec![BlockType::Reserved]),
        );
    }

    #[test]
    fn test_property() {
        let container = [0u8; constants::MIN_ORDER_SIZE];
        let block = get_reserved(&container);
        assert!(block.become_property(2, 3, PropertyFormat::Bytes).is_ok());
        assert_eq!(block.block_type(), BlockType::PropertyValue);
        assert_eq!(block.name_index().unwrap(), 2);
        assert_eq!(block.parent_index().unwrap(), 3);
        assert_eq!(block.property_format().unwrap(), PropertyFormat::Bytes);
        assert_eq!(container[..8], [0x71, 0x03, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00]);
        assert_eq!(container[8..], [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10]);

        assert!(block.set_property_extent_index(4).is_ok());
        assert_eq!(block.property_extent_index().unwrap(), 4);
        assert_eq!(container[..8], [0x71, 0x03, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00]);
        assert_eq!(container[8..], [0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x10]);

        assert!(block.set_property_total_length(10).is_ok());
        assert_eq!(block.property_total_length().unwrap(), 10);
        assert_eq!(container[..8], [0x71, 0x03, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00]);
        assert_eq!(container[8..], [0x0a, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x10]);

        let types = BTreeSet::from_iter(vec![BlockType::PropertyValue]);
        test_ok_types(move |b| b.set_property_extent_index(4), &types);
        test_ok_types(move |b| b.set_property_total_length(4), &types);
        test_ok_types(move |b| b.property_extent_index(), &types);
        test_ok_types(move |b| b.property_format(), &types);
        test_ok_types(
            move |b| b.become_property(2, 3, PropertyFormat::Bytes),
            &BTreeSet::from_iter(vec![BlockType::Reserved]),
        );
    }

    #[test]
    fn test_name() {
        let container = [0u8; constants::MIN_ORDER_SIZE * 2];
        let block = get_reserved(&container);
        assert!(block.become_name("test-rust-inspect").is_ok());
        assert_eq!(block.block_type(), BlockType::Name);
        assert_eq!(block.name_length().unwrap(), 17);
        assert_eq!(block.name_contents().unwrap(), "test-rust-inspect");
        assert_eq!(container[..8], [0x91, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        assert_eq!(&container[8..25], "test-rust-inspect".as_bytes());
        assert_eq!(container[25..], [0, 0, 0, 0, 0, 0, 0]);
        let types = BTreeSet::from_iter(vec![BlockType::Name]);
        test_ok_types(move |b| b.name_length(), &types);
        test_ok_types(move |b| b.name_contents(), &types);
        test_ok_types(
            move |b| b.become_name("test"),
            &BTreeSet::from_iter(vec![BlockType::Reserved]),
        );
    }

    fn get_header(container: &[u8]) -> Block<&[u8]> {
        let mut block = get_reserved(container);
        assert!(block.become_header().is_ok());
        block
    }

    fn get_reserved(container: &[u8]) -> Block<&[u8]> {
        let block = Block::new_free(container, 0, 1, 0).unwrap();
        assert!(block.become_reserved().is_ok());
        block
    }
}
