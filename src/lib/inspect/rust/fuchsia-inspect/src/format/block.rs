// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        format::{
            bitfields::{BlockHeader, Payload},
            block_type::BlockType,
            constants,
            container::{BlockContainerEq, ReadableBlockContainer, WritableBlockContainer},
        },
        utils,
    },
    anyhow::{format_err, Error},
    byteorder::{ByteOrder, LittleEndian},
    num_derive::{FromPrimitive, ToPrimitive},
    num_traits::{FromPrimitive, ToPrimitive},
    std::{
        cmp::min,
        sync::atomic::{fence, Ordering},
    },
};

pub use fuchsia_inspect_node_hierarchy::{ArrayFormat, LinkNodeDisposition};

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

impl<T: ReadableBlockContainer> Block<T> {
    /// Creates a new block.
    pub fn new(container: T, index: u32) -> Self {
        Block { container, index }
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

    /// Gets the double value of a DOUBLE_VALUE block.
    pub fn double_value(&self) -> Result<f64, Error> {
        self.check_type(BlockType::DoubleValue)?;
        Ok(f64::from_bits(self.read_payload().numeric_value()))
    }

    /// Gets the value of an INT_VALUE block.
    pub fn int_value(&self) -> Result<i64, Error> {
        self.check_type(BlockType::IntValue)?;
        Ok(i64::from_le_bytes(self.read_payload().numeric_value().to_le_bytes()))
    }

    /// Gets the unsigned value of a UINT_VALUE block.
    pub fn uint_value(&self) -> Result<u64, Error> {
        self.check_type(BlockType::UintValue)?;
        Ok(self.read_payload().numeric_value())
    }

    /// Gets the bool values of a BOOL_VALUE block.
    pub fn bool_value(&self) -> Result<bool, Error> {
        self.check_type(BlockType::BoolValue)?;
        Ok(self.read_payload().numeric_value() != 0)
    }

    /// Gets the index of the EXTENT of the PROPERTY block.
    pub fn property_extent_index(&self) -> Result<u32, Error> {
        self.check_type(BlockType::BufferValue)?;
        Ok(self.read_payload().property_extent_index())
    }

    /// Gets the total length of a PROPERTY block.
    pub fn property_total_length(&self) -> Result<usize, Error> {
        self.check_type(BlockType::BufferValue)?;
        Ok(self.read_payload().property_total_length().to_usize().unwrap())
    }

    /// Gets the flags of a PROPERTY block.
    pub fn property_format(&self) -> Result<PropertyFormat, Error> {
        self.check_type(BlockType::BufferValue)?;
        let raw_format = self.read_payload().property_flags();
        PropertyFormat::from_u8(raw_format).ok_or_else(|| {
            format_err!("Invalid property format {} at index {}", raw_format, self.index())
        })
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

    /// Gets the NAME block index of a *_VALUE block.
    pub fn name_index(&self) -> Result<u32, Error> {
        self.check_any_value()?;
        Ok(self.read_header().value_name_index())
    }

    /// Gets the format of an ARRAY_VALUE block.
    pub fn array_format(&self) -> Result<ArrayFormat, Error> {
        self.check_type(BlockType::ArrayValue)?;
        let raw_flags = self.read_payload().array_flags();
        ArrayFormat::from_u8(raw_flags).ok_or_else(|| {
            format_err!("Invalid array format {} at index {}", raw_flags, self.index())
        })
    }

    /// Gets the number of slots in an ARRAY_VALUE block.
    pub fn array_slots(&self) -> Result<usize, Error> {
        self.check_type(BlockType::ArrayValue)?;
        self.read_payload()
            .array_slots_count()
            .to_usize()
            .ok_or(format_err!("failed to convert to usize"))
    }

    /// Gets the type of each slot in an ARRAY_VALUE block.
    pub fn array_entry_type(&self) -> Result<BlockType, Error> {
        self.check_type(BlockType::ArrayValue)?;
        let array_type_raw = self.read_payload().array_entry_type();
        let array_type = BlockType::from_u8(array_type_raw).ok_or_else(|| {
            format_err!("Array entry type isn't a block type: {}", array_type_raw)
        })?;
        if !array_type.is_numeric_value() {
            return Err(format_err!("Array type is non-numeric {:?}", array_type));
        }
        Ok(array_type)
    }

    /// Gets the value of an int ARRAY_VALUE slot.
    pub fn array_get_int_slot(&self, slot_index: usize) -> Result<i64, Error> {
        self.check_array_entry_type(BlockType::IntValue)?;
        self.check_array_index(slot_index)?;
        let mut bytes = [0u8; 8];
        self.container
            .read_bytes(utils::offset_for_index(self.index + 1) + slot_index * 8, &mut bytes);
        Ok(i64::from_le_bytes(bytes))
    }

    /// Gets the value of a double ARRAY_VALUE slot.
    pub fn array_get_double_slot(&self, slot_index: usize) -> Result<f64, Error> {
        self.check_array_entry_type(BlockType::DoubleValue)?;
        self.check_array_index(slot_index)?;
        let mut bytes = [0u8; 8];
        self.container
            .read_bytes(utils::offset_for_index(self.index + 1) + slot_index * 8, &mut bytes);
        Ok(f64::from_bits(u64::from_le_bytes(bytes)))
    }

    /// Gets the value of a uint ARRAY_VALUE slot.
    pub fn array_get_uint_slot(&self, slot_index: usize) -> Result<u64, Error> {
        self.check_array_entry_type(BlockType::UintValue)?;
        self.check_array_index(slot_index)?;
        let mut bytes = [0u8; 8];
        self.container
            .read_bytes(utils::offset_for_index(self.index + 1) + slot_index * 8, &mut bytes);
        Ok(u64::from_le_bytes(bytes))
    }

    /// Gets the index of the content of this LINK_VALUE block.
    pub fn link_content_index(&self) -> Result<u32, Error> {
        self.check_type(BlockType::LinkValue)?;
        let payload = self.read_payload();
        Ok(payload.content_index())
    }

    /// Gets the node disposition of a LINK_VALUE block.
    pub fn link_node_disposition(&self) -> Result<LinkNodeDisposition, Error> {
        self.check_type(BlockType::LinkValue)?;
        let payload = self.read_payload();
        let flag = payload.disposition_flags();
        LinkNodeDisposition::from_u8(flag).ok_or_else(|| {
            format_err!("Invalid disposition type {} at index {}", flag, self.index())
        })
    }

    /// Ensures the type of the array is the expected one.
    fn check_array_entry_type(&self, expected: BlockType) -> Result<(), Error> {
        if cfg!(debug_assertions) {
            let actual = self.array_entry_type()?;
            if actual == expected {
                return Ok(());
            } else {
                return Err(format_err!(
                    "Invalid array entry type. Expected: {}, got: {}",
                    expected,
                    actual
                ));
            }
        }
        Ok(())
    }

    /// Ensure that the index is within the array bounds.
    fn check_array_index(&self, slot_index: usize) -> Result<(), Error> {
        if slot_index >= self.array_slots()? {
            return Err(format_err!("Index out of bounds: {}", slot_index));
        }
        Ok(())
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
        Ok(String::from(std::str::from_utf8(&bytes)?))
    }

    /// Returns the type of a block. Panics on an invalid value.
    pub fn block_type(&self) -> BlockType {
        let block_type = self.read_header().block_type();
        BlockType::from_u8(block_type).unwrap()
    }

    /// Returns the type of a block or an error if invalid.
    pub fn block_type_or(&self) -> Result<BlockType, Error> {
        let raw_type = self.read_header().block_type();
        BlockType::from_u8(raw_type)
            .ok_or_else(|| format_err!("Invalid block type {} at index {}", raw_type, self.index()))
    }

    /// Check that the block type is |block_type|
    fn check_type(&self, block_type: BlockType) -> Result<(), Error> {
        if cfg!(debug_assertions) {
            let self_type = self.read_header().block_type();
            return self.check_type_eq(self_type, block_type);
        }
        Ok(())
    }

    fn check_type_eq(&self, actual: u8, expected: BlockType) -> Result<(), Error> {
        if cfg!(debug_assertions) {
            let actual = BlockType::from_u8(actual).ok_or(format_err!("Invalid block type"))?;
            if actual != expected {
                return Err(format_err!("Expected type {}, got type {}", expected, actual));
            }
        }
        Ok(())
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
    pub(in crate) fn check_locked(&self, value: bool) -> Result<(), Error> {
        let payload = self.read_payload();
        if (payload.header_generation_count() & 1 == 1) != value {
            return Err(format_err!("Expected locked={}, actual={}", value, !value));
        }
        Ok(())
    }

    /// Check if the block is NODE or TOMBSTONE.
    fn check_node_or_tombstone(&self) -> Result<(), Error> {
        if cfg!(debug_assertions) {
            if self.block_type().is_node_or_tombstone() {
                return Ok(());
            }
            return Err(format_err!("Expected NODE|TOMBSTONE, got: {}", self.block_type()));
        }
        Ok(())
    }

    /// Check if the block is of *_VALUE.
    fn check_any_value(&self) -> Result<(), Error> {
        if cfg!(debug_assertions) {
            if self.block_type().is_any_value() {
                return Ok(());
            }
            return Err(format_err!("Block type {} is not *_VALUE", self.block_type()));
        }
        Ok(())
    }
}

impl<T: ReadableBlockContainer + WritableBlockContainer + BlockContainerEq> Block<T> {
    /// Initializes an empty free block.
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
    pub(crate) fn set_header_magic(&self, value: u32) -> Result<(), Error> {
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
        let header = self.read_header();
        self.check_type_eq(header.block_type(), BlockType::NodeValue)?;
        let mut new_header = BlockHeader(0);
        new_header.set_order(header.order());
        new_header.set_block_type(BlockType::Tombstone.to_u8().unwrap());
        self.write_header(new_header);
        Ok(())
    }

    /// Converts a FREE block to a RESERVED block
    pub fn become_reserved(&self) -> Result<(), Error> {
        let header = self.read_header();
        self.check_type_eq(header.block_type(), BlockType::Free)?;
        let mut new_header = BlockHeader(0);
        new_header.set_order(header.order());
        new_header.set_block_type(BlockType::Reserved.to_u8().unwrap());
        self.write_header(new_header);
        Ok(())
    }

    // TODO(fxb/39975): Uncomment or delete the next line depending on fxb/40012.
    // const ZERO_BUFFER: [u8; 2048] = [0; constants::MAX_ORDER_SIZE];

    /// Converts a block to a FREE block
    pub fn become_free(&self, next: u32) {
        let header = self.read_header();
        let mut new_header = BlockHeader(0);
        new_header.set_order(header.order());
        new_header.set_block_type(BlockType::Free.to_u8().unwrap());
        new_header.set_free_next_index(next);
        self.write_header(new_header);
        // TODO(fxb/39975): Uncomment or delete the next lines depending on the resolution of
        // fxb/40012. They've been verified to pass the Validator test for cleared Free payload.
        //self.container.write_bytes(utils::offset_for_index(self.index) + 8,
        //    &Self::ZERO_BUFFER[..utils::payload_size_for_order(self.order())]);
    }

    /// Converts a block to an *_ARRAY_VALUE block
    pub fn become_array_value(
        &self,
        slots: usize,
        format: ArrayFormat,
        entry_type: BlockType,
        name_index: u32,
        parent_index: u32,
    ) -> Result<(), Error> {
        if !entry_type.is_numeric_value() {
            return Err(format_err!("Invalid entry type"));
        }
        let order = self.order();
        let max_capacity = utils::array_capacity(order);
        if slots > max_capacity {
            return Err(format_err!(
                "{} exceeds the maximum number of slots for order {}: {}",
                slots,
                order,
                max_capacity
            ));
        }
        self.write_value_header(BlockType::ArrayValue, name_index, parent_index)?;
        let mut payload = Payload(0);
        payload.set_array_entry_type(entry_type.to_u8().unwrap());
        payload.set_array_flags(format.to_u8().unwrap());
        payload.set_array_slots_count(slots.to_u8().unwrap());
        self.write_payload(payload);
        Ok(())
    }

    /// Sets all values of the array to zero starting on `start_slot_index` (inclusive).
    pub fn array_clear(&self, start_slot_index: usize) -> Result<(), Error> {
        let array_slots = self.array_slots()? - start_slot_index;
        let values = vec![0u8; array_slots * 8]; // *8 given that it's 64bit values
        self.container
            .write_bytes(utils::offset_for_index(self.index + 1) + start_slot_index * 8, &values);
        Ok(())
    }

    /// Sets the value of an int ARRAY_VALUE block.
    pub fn array_set_int_slot(&self, slot_index: usize, value: i64) -> Result<(), Error> {
        self.check_array_entry_type(BlockType::IntValue)?;
        self.check_array_index(slot_index)?;
        self.container.write_bytes(
            utils::offset_for_index(self.index + 1) + slot_index * 8,
            &value.to_le_bytes(),
        );
        Ok(())
    }

    /// Sets the value of a double ARRAY_VALUE block.
    pub fn array_set_double_slot(&self, slot_index: usize, value: f64) -> Result<(), Error> {
        self.check_array_entry_type(BlockType::DoubleValue)?;
        self.check_array_index(slot_index)?;
        self.container.write_bytes(
            utils::offset_for_index(self.index + 1) + slot_index * 8,
            &value.to_bits().to_le_bytes(),
        );
        Ok(())
    }

    /// Sets the value of a uint ARRAY_VALUE block.
    pub fn array_set_uint_slot(&self, slot_index: usize, value: u64) -> Result<(), Error> {
        self.check_array_entry_type(BlockType::UintValue)?;
        self.check_array_index(slot_index)?;
        self.container.write_bytes(
            utils::offset_for_index(self.index + 1) + slot_index * 8,
            &value.to_le_bytes(),
        );
        Ok(())
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

    /// Set the payload of an EXTENT block. The number of bytes written will be returned.
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
        let mut payload = Payload(0);
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
        let mut payload = Payload(0);
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
        let mut payload = Payload(0);
        payload.set_numeric_value(value);
        self.write_payload(payload);
        Ok(())
    }

    /// Converts a block into a BOOL_VALUE block.
    pub fn become_bool_value(
        &self,
        value: bool,
        name_index: u32,
        parent_index: u32,
    ) -> Result<(), Error> {
        self.write_value_header(BlockType::BoolValue, name_index, parent_index)?;
        self.set_bool_value(value)
    }

    /// Sets the value of a BOOL_VALUE block.
    pub fn set_bool_value(&self, value: bool) -> Result<(), Error> {
        self.check_type(BlockType::BoolValue)?;
        let mut payload = Payload(0);
        payload.set_numeric_value(value as u64);
        self.write_payload(payload);
        Ok(())
    }

    /// Initializes a NODE_VALUE block.
    pub fn become_node(&self, name_index: u32, parent_index: u32) -> Result<(), Error> {
        self.write_value_header(BlockType::NodeValue, name_index, parent_index)?;
        self.write_payload(Payload(0));
        Ok(())
    }

    /// Converts a *_VALUE block into a BUFFER_VALUE block.
    pub fn become_property(
        &self,
        name_index: u32,
        parent_index: u32,
        format: PropertyFormat,
    ) -> Result<(), Error> {
        self.write_value_header(BlockType::BufferValue, name_index, parent_index)?;
        let mut payload = Payload(0);
        payload.set_property_flags(format.to_u8().unwrap());
        self.write_payload(payload);
        Ok(())
    }

    /// Sets the total length of a BUFFER_VALUE block.
    pub fn set_property_total_length(&self, length: u32) -> Result<(), Error> {
        self.check_type(BlockType::BufferValue)?;
        let mut payload = self.read_payload();
        payload.set_property_total_length(length);
        self.write_payload(payload);
        Ok(())
    }

    /// Sets the index of the EXTENT of a BUFFER_VALUE block.
    pub fn set_property_extent_index(&self, index: u32) -> Result<(), Error> {
        self.check_type(BlockType::BufferValue)?;
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
            // Make sure we didn't split a multibyte UTF-8 character; if so, delete the fragment.
            while bytes[bytes.len() - 1] & 0x80 != 0 {
                bytes = &bytes[..bytes.len() - 1];
            }
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

    /// Creates a LINK block.
    pub fn become_link(
        &self,
        name_index: u32,
        parent_index: u32,
        content_index: u32,
        disposition_flags: LinkNodeDisposition,
    ) -> Result<(), Error> {
        self.write_value_header(BlockType::LinkValue, name_index, parent_index)?;
        let mut payload = Payload(0);
        payload.set_content_index(content_index);
        payload.set_disposition_flags(disposition_flags.to_u8().unwrap());
        self.write_payload(payload);
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
        let header = self.read_header();
        self.check_type_eq(header.block_type(), BlockType::Reserved)?;
        let mut new_header = BlockHeader(0);
        new_header.set_order(header.order());
        new_header.set_block_type(block_type.to_u8().unwrap());
        new_header.set_value_name_index(name_index);
        new_header.set_value_parent_index(parent_index);
        self.write_header(new_header);
        Ok(())
    }

    /// Writes the given header and payload to the block in the container.
    pub(in crate) fn write(&self, header: BlockHeader, payload: Payload) {
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
        self.write_payload(payload);
        fence(Ordering::Release);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeSet;
    use std::iter::FromIterator;

    fn create_with_type(container: &[u8], index: u32, block_type: BlockType) -> Block<&[u8]> {
        let block = Block::new(container, index);
        let mut header = BlockHeader(0);
        header.set_block_type(block_type.to_u8().unwrap());
        header.set_order(2);
        block.write_header(header);
        block
    }

    fn test_error_types<T>(
        f: fn(&Block<&[u8]>) -> Result<T, Error>,
        error_types: &BTreeSet<BlockType>,
    ) {
        if cfg!(debug_assertions) {
            let container = [0u8; constants::MIN_ORDER_SIZE * 3];
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
    }

    fn test_ok_types<T>(
        f: fn(&mut Block<&[u8]>) -> Result<T, Error>,
        ok_types: &BTreeSet<BlockType>,
    ) {
        if cfg!(debug_assertions) {
            let container = [0u8; constants::MIN_ORDER_SIZE * 3];
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
        assert_eq!(container[..8], [0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00]);
        assert_eq!(container[8..], [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
    }

    #[test]
    fn test_block_type_or() {
        let mut container = [0u8; constants::MIN_ORDER_SIZE];
        container[1] = 0x02;
        let block = Block::new(&container[..], 0);
        assert_eq!(block.block_type_or().unwrap(), BlockType::Header);
        let mut container = [0u8; constants::MIN_ORDER_SIZE];
        container[1] = 0x0f;
        let block = Block::new(&container[..], 0);
        assert!(block.block_type_or().is_err());
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
        if cfg!(debug_assertions) {
            assert!(block1.free_next_index().is_err());
        }
        assert_eq!(block2.index(), 0);
        assert_eq!(block2.order(), 1);
        assert_eq!(block2.block_type(), BlockType::Free);
        assert_eq!(block2.free_next_index().unwrap(), 2);

        assert_eq!(container[..8], [0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00]);
        assert_eq!(container[8..16], [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        assert_eq!(container[16..24], [0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        assert_eq!(container[24..32], [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        assert_eq!(container[32..40], [0x03, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00]);
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
        assert_eq!(container[..8], [0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
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
        assert_eq!(container[..8], [0x00, 0x02, 0x01, 0x00, 0x49, 0x4e, 0x53, 0x50]);
        assert_eq!(container[8..], [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);

        test_ok_types(move |b| b.become_header(), &BTreeSet::from_iter(vec![BlockType::Reserved]));
        test_ok_types(move |b| b.header_magic(), &BTreeSet::from_iter(vec![BlockType::Header]));
        test_ok_types(move |b| b.header_version(), &BTreeSet::from_iter(vec![BlockType::Header]));
    }

    #[test]
    fn test_lock_unlock_header() {
        let container = [0u8; constants::MIN_ORDER_SIZE];
        let block = get_header(&container);
        let header_bytes: [u8; 8] = [0x00, 0x02, 0x01, 0x00, 0x49, 0x4e, 0x53, 0x50];
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
        assert_eq!(container[..8], [0x01, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
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
        assert_eq!(container[..8], [0x01, 0x03, 0x03, 0x00, 0x00, 0x02, 0x00, 0x00]);
        assert_eq!(container[8..], [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        assert!(block.set_child_count(4).is_ok());
        assert_eq!(block.child_count().unwrap(), 4);
        assert_eq!(container[..8], [0x01, 0x03, 0x03, 0x00, 0x00, 0x02, 0x00, 0x00]);
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
        assert_eq!(container[..8], [0x01, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00]);
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
        assert_eq!(container[..8], [0x01, 0x08, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00]);
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
            BlockType::BufferValue,
            BlockType::ArrayValue,
            BlockType::LinkValue,
            BlockType::BoolValue,
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
        assert_eq!(container[..8], [0x01, 0x06, 0x03, 0x00, 0x00, 0x02, 0x00, 0x00]);
        assert_eq!(container[8..], [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f]);

        assert!(block.set_double_value(5.0).is_ok());
        assert_eq!(block.double_value().unwrap(), 5.0);
        assert_eq!(container[..8], [0x01, 0x06, 0x03, 0x00, 0x00, 0x02, 0x00, 0x00]);
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
        assert_eq!(container[..8], [0x1, 0x04, 0x03, 0x00, 0x00, 0x02, 0x00, 0x00]);
        assert_eq!(container[8..], [0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);

        assert!(block.set_int_value(-5).is_ok());
        assert_eq!(block.int_value().unwrap(), -5);
        assert_eq!(container[..8], [0x1, 0x04, 0x03, 0x00, 0x00, 0x02, 0x00, 0x00]);
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
        assert_eq!(container[..8], [0x01, 0x05, 0x03, 0x00, 0x00, 0x02, 0x00, 0x00]);
        assert_eq!(container[8..], [0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);

        assert!(block.set_uint_value(5).is_ok());
        assert_eq!(block.uint_value().unwrap(), 5);
        assert_eq!(container[..8], [0x01, 0x05, 0x03, 0x00, 0x00, 0x02, 0x00, 0x00]);
        assert_eq!(container[8..], [0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        test_ok_types(move |b| b.uint_value(), &BTreeSet::from_iter(vec![BlockType::UintValue]));
        test_ok_types(
            move |b| b.set_uint_value(3),
            &BTreeSet::from_iter(vec![BlockType::UintValue]),
        );
    }

    #[test]
    fn test_bool_value() {
        test_ok_types(
            move |b| b.become_bool_value(true, 1, 2),
            &BTreeSet::from_iter(vec![BlockType::Reserved]),
        );
        let container = [0u8; constants::MIN_ORDER_SIZE];
        let block = get_reserved(&container);
        assert!(block.become_bool_value(false, 2, 3).is_ok());
        assert_eq!(block.block_type(), BlockType::BoolValue);
        assert_eq!(block.name_index().unwrap(), 2);
        assert_eq!(block.parent_index().unwrap(), 3);
        assert_eq!(block.bool_value().unwrap(), false);
        assert_eq!(container[..8], [0x01, 0x0D, 0x03, 0x00, 0x00, 0x02, 0x00, 0x00]);
        assert_eq!(container[8..], [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);

        assert!(block.set_bool_value(true).is_ok());
        assert_eq!(block.bool_value().unwrap(), true);
        assert_eq!(container[..8], [0x01, 0x0D, 0x03, 0x00, 0x00, 0x02, 0x00, 0x00]);
        assert_eq!(container[8..], [0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        test_ok_types(move |b| b.bool_value(), &BTreeSet::from_iter(vec![BlockType::BoolValue]));
        test_ok_types(
            move |b| b.set_bool_value(true),
            &BTreeSet::from_iter(vec![BlockType::BoolValue]),
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
        assert_eq!(container[..8], [0x01, 0x03, 0x03, 0x00, 0x00, 0x02, 0x00, 0x00]);
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
        assert_eq!(block.block_type(), BlockType::BufferValue);
        assert_eq!(block.name_index().unwrap(), 2);
        assert_eq!(block.parent_index().unwrap(), 3);
        assert_eq!(block.property_format().unwrap(), PropertyFormat::Bytes);
        assert_eq!(container[..8], [0x01, 0x07, 0x03, 0x00, 0x00, 0x02, 0x00, 0x00]);
        assert_eq!(container[8..], [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10]);

        let mut bad_format_bytes = [0u8; constants::MIN_ORDER_SIZE];
        bad_format_bytes.copy_from_slice(&container);
        bad_format_bytes[15] = 0x30;
        let bad_block = Block::new(&bad_format_bytes[..], 0);
        assert!(bad_block.property_format().is_err()); // Make sure we get Error not panic

        assert!(block.set_property_extent_index(4).is_ok());
        assert_eq!(block.property_extent_index().unwrap(), 4);
        assert_eq!(container[..8], [0x01, 0x07, 0x03, 0x00, 0x00, 0x02, 0x00, 0x00]);
        assert_eq!(container[8..], [0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x10]);

        assert!(block.set_property_total_length(10).is_ok());
        assert_eq!(block.property_total_length().unwrap(), 10);
        assert_eq!(container[..8], [0x01, 0x07, 0x03, 0x00, 0x00, 0x02, 0x00, 0x00]);
        assert_eq!(container[8..], [0x0a, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x10]);

        let types = BTreeSet::from_iter(vec![BlockType::BufferValue]);
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
        assert_eq!(container[..8], [0x01, 0x09, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00]);
        assert_eq!(&container[8..25], "test-rust-inspect".as_bytes());
        assert_eq!(container[25..], [0, 0, 0, 0, 0, 0, 0]);
        let mut bad_name_bytes = [0u8; constants::MIN_ORDER_SIZE * 2];
        bad_name_bytes.copy_from_slice(&container);
        bad_name_bytes[24] = 0xff;
        let bad_block = Block::new(&bad_name_bytes[..], 0);
        assert_eq!(bad_block.name_length().unwrap(), 17); // Sanity check we copied correctly
        assert!(bad_block.name_contents().is_err()); // Make sure we get Error not panic
        let types = BTreeSet::from_iter(vec![BlockType::Name]);
        test_ok_types(move |b| b.name_length(), &types);
        test_ok_types(move |b| b.name_contents(), &types);
        test_ok_types(
            move |b| b.become_name("test"),
            &BTreeSet::from_iter(vec![BlockType::Reserved]),
        );
        // Test to make sure UTF8 strings are truncated safely if they're too long for the block,
        // even if the last character is multibyte and would be chopped in the middle.
        let container = [0u8; constants::MIN_ORDER_SIZE * 2];
        let block = get_reserved(&container);
        assert!(block.become_name("abcdefghijklmnopqrstuvwxyz").is_ok());
        assert_eq!(block.name_contents().unwrap(), "abcdefghijklmnopqrstuvwx");
        let container = [0u8; constants::MIN_ORDER_SIZE * 2];
        let block = get_reserved(&container);
        assert!(block.become_name("😀abcdefghijklmnopqrstuvwxyz").is_ok());
        assert_eq!(block.name_contents().unwrap(), "😀abcdefghijklmnopqrst");
        let container = [0u8; constants::MIN_ORDER_SIZE * 2];
        let block = get_reserved(&container);
        assert!(block.become_name("abcdefghijklmnopqrstu😀").is_ok());
        assert_eq!(block.name_contents().unwrap(), "abcdefghijklmnopqrstu");
        assert_eq!(container[31], 0);
    }

    #[test]
    fn uint_array_value() {
        let container = [0u8; constants::MIN_ORDER_SIZE * 4];
        let block = Block::new_free(&container[..], 0, 2, 0).unwrap();
        assert!(block.become_reserved().is_ok());
        assert!(block
            .become_array_value(4, ArrayFormat::LinearHistogram, BlockType::UintValue, 3, 2)
            .is_ok());

        assert_eq!(block.block_type(), BlockType::ArrayValue);
        assert_eq!(block.parent_index().unwrap(), 2);
        assert_eq!(block.name_index().unwrap(), 3);
        assert_eq!(block.array_format().unwrap(), ArrayFormat::LinearHistogram);
        assert_eq!(block.array_slots().unwrap(), 4);
        assert_eq!(block.array_entry_type().unwrap(), BlockType::UintValue);

        for i in 0..4 {
            assert!(block.array_set_uint_slot(i, (i as u64 + 1) * 5).is_ok());
        }
        assert!(block.array_set_uint_slot(4, 3).is_err());
        assert!(block.array_set_uint_slot(7, 5).is_err());
        assert_eq!(container[..8], [0x02, 0x0b, 0x02, 0x00, 0x00, 0x03, 0x00, 0x00]);
        assert_eq!(container[8..16], [0x15, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        for i in 0..4 {
            assert_eq!(
                container[8 * (i + 2)..8 * (i + 3)],
                [(i as u8 + 1) * 5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
            );
        }

        let mut bad_bytes = [0u8; constants::MIN_ORDER_SIZE * 4];
        bad_bytes.copy_from_slice(&container);
        bad_bytes[8] = 0x12; // LinearHistogram; Header
        let bad_block = Block::new(&bad_bytes[..], 0);
        assert_eq!(bad_block.array_format().unwrap(), ArrayFormat::LinearHistogram);
        // Make sure we get Error not panic or BlockType::Header
        assert!(bad_block.array_entry_type().is_err());

        bad_bytes[8] = 0xef; // Not in enum; Not in enum
        let bad_block = Block::new(&bad_bytes[..], 0);
        assert!(bad_block.array_format().is_err());
        assert!(bad_block.array_entry_type().is_err());

        for i in 0..4 {
            assert_eq!(block.array_get_uint_slot(i).unwrap(), (i as u64 + 1) * 5);
        }
        assert!(block.array_get_uint_slot(4).is_err());

        let types = BTreeSet::from_iter(vec![BlockType::ArrayValue]);
        test_ok_types(move |b| b.array_format(), &types);
        test_ok_types(move |b| b.array_slots(), &types);
        test_ok_types(
            move |b| {
                b.become_array_value(2, ArrayFormat::Default, BlockType::UintValue, 1, 2)?;
                b.array_set_uint_slot(0, 3)?;
                b.array_get_uint_slot(0)
            },
            &BTreeSet::from_iter(vec![BlockType::Reserved]),
        );
    }

    #[test]
    fn array_slots_bigger_than_block_order() {
        let container = [0u8; constants::MIN_ORDER_SIZE * 8];
        // A block of size 7 (max) can hold 254 values: 2048B - 8B (header) - 8B (array metadata)
        // gives 2032, which means 254 values of 8 bytes each maximum.
        let block = Block::new_free(&container[..], 0, 7, 0).unwrap();
        block.become_reserved().unwrap();
        assert!(block
            .become_array_value(257, ArrayFormat::Default, BlockType::IntValue, 1, 2)
            .is_err());
        assert!(block
            .become_array_value(254, ArrayFormat::Default, BlockType::IntValue, 1, 2)
            .is_ok());

        // A block of size 2 can hold 6 values: 64B - 8B (header) - 8B (array metadata)
        // gives 48, which means 6 values of 8 bytes each maximum.
        let block = Block::new_free(&container[..], 0, 2, 0).unwrap();
        block.become_reserved().unwrap();
        assert!(block
            .become_array_value(8, ArrayFormat::Default, BlockType::IntValue, 1, 2)
            .is_err());
        assert!(block
            .become_array_value(6, ArrayFormat::Default, BlockType::IntValue, 1, 2)
            .is_ok());
    }

    #[test]
    fn array_clear() {
        let mut container = [0u8; constants::MIN_ORDER_SIZE * 4];

        // Write some dummy data in the container after the slot fields.
        let dummy = vec![0xff, 0xff, 0xff];
        container[48..51].copy_from_slice(&dummy);

        let block = Block::new_free(&container[..], 0, 2, 0).expect("new free");
        assert!(block.become_reserved().is_ok());
        assert!(block
            .become_array_value(4, ArrayFormat::LinearHistogram, BlockType::UintValue, 3, 2)
            .is_ok());

        for i in 0..4 {
            block.array_set_uint_slot(i, (i + 1) as u64).expect("set uint");
        }

        block.array_clear(1).expect("clear array");

        assert_eq!(1, block.array_get_uint_slot(0).expect("get uint 0"));
        assert_eq!(container[16..24], [0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        for i in 1..4 {
            assert_eq!(0, block.array_get_uint_slot(i).expect("get uint"));
            assert_eq!(
                container[16 + (i * 8)..24 + (i * 8)],
                [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
            );
        }

        // Dummy data shouldn't have been overwritten
        assert_eq!(&container[48..51], &dummy[..]);
    }

    #[test]
    fn become_link() {
        let container = [0u8; constants::MIN_ORDER_SIZE];
        let block = get_reserved(&container);
        assert!(block.become_link(1, 2, 3, LinkNodeDisposition::Inline).is_ok());
        assert_eq!(block.name_index().unwrap(), 1);
        assert_eq!(block.parent_index().unwrap(), 2);
        assert_eq!(block.link_content_index().unwrap(), 3);
        assert_eq!(block.block_type(), BlockType::LinkValue);
        assert_eq!(block.link_node_disposition().unwrap(), LinkNodeDisposition::Inline);
        assert_eq!(container[..8], [0x01, 0x0c, 0x02, 0x00, 0x00, 0x01, 0x00, 0x00]);
        assert_eq!(container[8..], [0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10]);

        let types = BTreeSet::from_iter(vec![BlockType::LinkValue]);
        test_ok_types(move |b| b.link_content_index(), &types);
        test_ok_types(move |b| b.link_node_disposition(), &types);
        test_ok_types(
            move |b| b.become_link(1, 2, 3, LinkNodeDisposition::Inline),
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
