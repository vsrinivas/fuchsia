// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::vmo::{block::Block, block_type::BlockType, constants, heap::Heap, utils},
    failure::{format_err, Error},
    mapped_vmo::Mapping,
    num_derive::{FromPrimitive, ToPrimitive},
    num_traits::ToPrimitive,
    std::sync::Arc,
};

/// Wraps a heap and implements the Inspect VMO API on top of it at a low level.
pub struct State {
    pub(in crate::vmo) heap: Heap,
    header: Block<Arc<Mapping>>,
}

/// Format in which the property will be read.
#[derive(FromPrimitive, ToPrimitive)]
pub enum PropertyFormat {
    String = 0,
    ByteVector = 1,
}

/// Locks the VMO Header blcok, executes the given codeblock and unblocks it.
macro_rules! with_header_lock {
    ($self:ident, $code:block) => {{
        $self.header.lock_header()?;
        let result = $code;
        $self.header.unlock_header()?;
        result
    }};
}

/// Generate create, set, add and subtract methods for a metric.
macro_rules! metric_fns {
    ($name:ident, $type:ident) => {
        paste::item! {
            pub fn [<create_ $name _metric>](
                &mut self,
                name: &str,
                value: $type,
                parent_index: u32,
            ) -> Result<Block<Arc<Mapping>>, Error> {
                with_header_lock!(self, {
                    let (block, name_block) = self.allocate_reserved_value(name, parent_index)?;
                    block.[<become_ $name _value>](value, name_block.index(), parent_index)?;
                    Ok(block)
                })
            }

            pub fn [<set_ $name _metric>](&self, block_index: u32, value: $type)
                -> Result<(), Error> {
                with_header_lock!(self, {
                    let block = self.heap.get_block(block_index)?;
                    block.[<set_ $name _value>](value)?;
                    Ok(())
                })
            }

            pub fn [<add_ $name _metric>](&self, block_index: u32, value: $type)
                -> Result<(), Error> {
                with_header_lock!(self, {
                    let block = self.heap.get_block(block_index)?;
                    let current_value = block.[<$name _value>]()?;
                    block.[<set_ $name _value>](current_value + value)?;
                    Ok(())
                })
            }

            pub fn [<subtract_ $name _metric>](&self, block_index: u32, value: $type)
                -> Result<(), Error> {
                with_header_lock!(self, {
                    let block = self.heap.get_block(block_index)?;
                    let current_value = block.[<$name _value>]()?;
                    block.[<set_ $name _value>](current_value - value)?;
                    Ok(())
                })
            }
        }
    };
}

impl State {
    /// Create a |State| object wrapping the given Heap. This will cause the
    /// heap to be initialized with a header.
    pub fn create(mut heap: Heap) -> Result<State, Error> {
        let mut block = heap.allocate_block(16)?;
        block.become_header()?;
        Ok(State { heap: heap, header: block })
    }

    /// Allocate a NODE block with the given |name| and |parent_index|.
    pub fn create_node(
        &mut self,
        name: &str,
        parent_index: u32,
    ) -> Result<Block<Arc<Mapping>>, Error> {
        with_header_lock!(self, {
            let (block, name_block) = self.allocate_reserved_value(name, parent_index)?;
            block.become_node(name_block.index(), parent_index)?;
            Ok(block)
        })
    }

    /// Free a *_VALUE block at the given |index|.
    pub fn free_value(&mut self, index: u32) -> Result<(), Error> {
        with_header_lock!(self, {
            let block = self.heap.get_block(index)?;
            self.delete_value(block).unwrap();
            Ok(())
        })
    }

    /// Allocate a PROPERTY block with the given |name|, |value| and |parent_index|.
    pub fn create_property(
        &mut self,
        name: &str,
        value: &[u8],
        format: PropertyFormat,
        parent_index: u32,
    ) -> Result<Block<Arc<Mapping>>, Error> {
        with_header_lock!(self, {
            let (block, name_block) = self.allocate_reserved_value(name, parent_index)?;
            block.become_property(name_block.index(), parent_index, format.to_u8().unwrap())?;
            if let Err(err) = self.inner_set_property_value(&block, &value) {
                self.heap.free_block(block).expect("Failed to free block");
                self.heap.free_block(name_block).expect("Failed to free name block");
                return Err(err);
            }
            Ok(block)
        })
    }

    /// Free a PROPERTY block.
    pub fn free_property(&mut self, index: u32) -> Result<(), Error> {
        with_header_lock!(self, {
            let block = self.heap.get_block(index)?;
            self.free_extents(block.property_extent_index()?)?;
            self.delete_value(block)?;
            Ok(())
        })
    }

    /// Set the |value| of a String PROPERTY block.
    pub fn set_property(&mut self, block_index: u32, value: &[u8]) -> Result<(), Error> {
        with_header_lock!(self, {
            let block = self.heap.get_block(block_index)?;
            self.inner_set_property_value(&block, value)?;
            Ok(())
        })
    }

    metric_fns!(int, i64);
    metric_fns!(uint, u64);
    metric_fns!(double, f64);

    fn allocate_reserved_value(
        &mut self,
        name: &str,
        parent_index: u32,
    ) -> Result<(Block<Arc<Mapping>>, Block<Arc<Mapping>>), Error> {
        let block = self.heap.allocate_block(constants::MIN_ORDER_SIZE)?;
        let mut bytes = name.as_bytes();
        let max_bytes = constants::MAX_ORDER_SIZE - constants::HEADER_SIZE_BYTES;
        if bytes.len() > max_bytes {
            bytes = &bytes[..max_bytes];
        }
        let name_block = match self.heap.allocate_block(utils::block_size_for_payload(bytes.len()))
        {
            Ok(b) => b,
            Err(err) => {
                self.heap.free_block(block)?;
                return Err(err);
            }
        };
        name_block.become_name(name).expect("Failed to create name block");

        let result = self.heap.get_block(parent_index).and_then(|parent_block| match parent_block
            .block_type()
        {
            BlockType::NodeValue | BlockType::Tombstone => {
                parent_block.set_child_count(parent_block.child_count().unwrap() + 1)
            }
            BlockType::Header => Ok(()),
            _ => Err(format_err!("Invalid block type:{}", parent_block.block_type())),
        });
        match result {
            Ok(()) => Ok((block, name_block)),
            Err(e) => {
                self.heap.free_block(name_block).expect("Failed to free name block");
                self.heap.free_block(block).expect("Failed to free block");
                Err(format_err!("Invalid parent index {}: {}", parent_index, e))
            }
        }
    }

    fn delete_value(&mut self, block: Block<Arc<Mapping>>) -> Result<(), Error> {
        // Decrement parent child count.
        let parent_index = block.parent_index()?;
        if parent_index != constants::HEADER_INDEX {
            let parent = self.heap.get_block(parent_index).unwrap();
            let child_count = parent.child_count().unwrap() - 1;
            if parent.block_type() == BlockType::Tombstone && child_count == 0 {
                self.heap.free_block(parent).expect("Failed to free block");
            } else {
                parent.set_child_count(child_count).unwrap();
            }
        }

        // Free the name block.
        let name_index = block.name_index()?;
        let name = self.heap.get_block(name_index).unwrap();
        self.heap.free_block(name).expect("Failed to free block");

        // If the block is a NODE and has children, make it a TOMBSTONE so that
        // it's freed when the last of its children is freed. Otherwise, free it.
        if block.block_type() == BlockType::NodeValue && block.child_count()? != 0 {
            block.become_tombstone()
        } else {
            self.heap.free_block(block)
        }
    }

    fn inner_set_property_value(
        &mut self,
        block: &Block<Arc<Mapping>>,
        value: &[u8],
    ) -> Result<(), Error> {
        self.free_extents(block.property_extent_index()?)?;
        let extent_index = self.write_extents(value)?;
        block.set_property_total_length(value.len().to_u32().unwrap())?;
        block.set_property_extent_index(extent_index)?;
        Ok(())
    }

    fn free_extents(&mut self, head_extent_index: u32) -> Result<(), Error> {
        let mut index = head_extent_index;
        while index != constants::HEADER_INDEX {
            let block = self.heap.get_block(index).unwrap();
            index = block.next_extent()?;
            self.heap.free_block(block)?;
        }
        Ok(())
    }

    fn write_extents(&mut self, value: &[u8]) -> Result<u32, Error> {
        if value.len() == 0 {
            // Invalid index
            return Ok(constants::HEADER_INDEX);
        }
        let mut offset = 0;
        let total_size = value.len().to_usize().unwrap();
        let mut extent_block =
            self.heap.allocate_block(utils::block_size_for_payload(total_size - offset))?;
        let head_extent_index = extent_block.index();
        while offset < total_size {
            extent_block.become_extent(0)?;
            let bytes_written = extent_block.extent_set_contents(&value[offset..])?;
            offset += bytes_written;
            if offset < total_size {
                let block =
                    self.heap.allocate_block(utils::block_size_for_payload(total_size - offset))?;
                extent_block.set_extent_next_index(block.index())?;
                extent_block = block;
            }
        }
        Ok(head_extent_index)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::vmo::reader::BlockIterator;

    #[test]
    fn test_create() {
        let state = get_state(4096);
        let bytes = &state.heap.bytes()[..];
        let blocks: Vec<Block<&[u8]>> = BlockIterator::new(&bytes).collect();
        assert_eq!(blocks.len(), 9);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[test]
    fn test_node() {
        let mut state = get_state(4096);

        // Create a node value and verify its fields
        let block = state.create_node("test-node", 0).unwrap();
        assert_eq!(block.block_type(), BlockType::NodeValue);
        assert_eq!(block.index(), 1);
        assert_eq!(block.child_count().unwrap(), 0);
        assert_eq!(block.name_index().unwrap(), 2);
        assert_eq!(block.parent_index().unwrap(), 0);

        // Verify name block.
        let name_block = state.heap.get_block(2).unwrap();
        assert_eq!(name_block.block_type(), BlockType::Name);
        assert_eq!(name_block.name_length().unwrap(), 9);
        assert_eq!(name_block.name_contents().unwrap(), "test-node");

        // Verify blocks.
        let bytes = &state.heap.bytes()[..];
        let blocks: Vec<Block<&[u8]>> = BlockIterator::new(&bytes).collect();
        assert_eq!(blocks.len(), 9);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::NodeValue);
        assert_eq!(blocks[2].block_type(), BlockType::Name);
        assert!(blocks[3..].iter().all(|b| b.block_type() == BlockType::Free));

        // Create a child of the node and verify child counts.
        let child_block = state.create_node("child1", 1).unwrap();
        assert_eq!(block.child_count().unwrap(), 1);

        // Create a child of the child and verify child counts.
        let child11_block = state.create_node("child1-1", 4).unwrap();
        assert_eq!(child11_block.child_count().unwrap(), 0);
        assert_eq!(child_block.child_count().unwrap(), 1);
        assert_eq!(block.child_count().unwrap(), 1);

        assert!(state.free_value(child11_block.index()).is_ok());
        assert_eq!(child_block.child_count().unwrap(), 0);

        // Add a couple more children to the block and verify count.
        let child_block2 = state.create_node("child2", 1).unwrap();
        let child_block3 = state.create_node("child3", 1).unwrap();
        assert_eq!(block.child_count().unwrap(), 3);

        // Free children and verify count.
        assert!(state.free_value(child_block.index()).is_ok());
        assert!(state.free_value(child_block2.index()).is_ok());
        assert!(state.free_value(child_block3.index()).is_ok());
        assert_eq!(block.child_count().unwrap(), 0);

        // Free node.
        assert!(state.free_value(block.index()).is_ok());
        let bytes = &state.heap.bytes()[..];
        let blocks: Vec<Block<&[u8]>> = BlockIterator::new(&bytes).collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[test]
    fn test_int_metric() {
        let mut state = get_state(4096);

        // Creates with value
        let block = state.create_int_metric("test", 3, 0).unwrap();
        assert_eq!(block.block_type(), BlockType::IntValue);
        assert_eq!(block.index(), 1);
        assert_eq!(block.int_value().unwrap(), 3);
        assert_eq!(block.name_index().unwrap(), 2);
        assert_eq!(block.parent_index().unwrap(), 0);

        let name_block = state.heap.get_block(2).unwrap();
        assert_eq!(name_block.block_type(), BlockType::Name);
        assert_eq!(name_block.name_length().unwrap(), 4);
        assert_eq!(name_block.name_contents().unwrap(), "test");

        let bytes = &state.heap.bytes()[..];
        let blocks: Vec<Block<&[u8]>> = BlockIterator::new(&bytes).collect();
        assert_eq!(blocks.len(), 10);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::IntValue);
        assert_eq!(blocks[2].block_type(), BlockType::Name);
        assert!(blocks[3..].iter().all(|b| b.block_type() == BlockType::Free));

        assert!(state.add_int_metric(block.index(), 10).is_ok());
        assert_eq!(block.int_value().unwrap(), 13);

        assert!(state.subtract_int_metric(block.index(), 5).is_ok());
        assert_eq!(block.int_value().unwrap(), 8);

        assert!(state.set_int_metric(block.index(), -6).is_ok());
        assert_eq!(block.int_value().unwrap(), -6);

        // Free metric.
        assert!(state.free_value(block.index()).is_ok());
        let bytes = &state.heap.bytes()[..];
        let blocks: Vec<Block<&[u8]>> = BlockIterator::new(&bytes).collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[test]
    fn test_uint_metric() {
        let mut state = get_state(4096);

        // Creates with value
        let block = state.create_uint_metric("test", 3, 0).unwrap();
        assert_eq!(block.block_type(), BlockType::UintValue);
        assert_eq!(block.index(), 1);
        assert_eq!(block.uint_value().unwrap(), 3);
        assert_eq!(block.name_index().unwrap(), 2);
        assert_eq!(block.parent_index().unwrap(), 0);

        let name_block = state.heap.get_block(2).unwrap();
        assert_eq!(name_block.block_type(), BlockType::Name);
        assert_eq!(name_block.name_length().unwrap(), 4);
        assert_eq!(name_block.name_contents().unwrap(), "test");

        let bytes = &state.heap.bytes()[..];
        let blocks: Vec<Block<&[u8]>> = BlockIterator::new(&bytes).collect();
        assert_eq!(blocks.len(), 10);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::UintValue);
        assert_eq!(blocks[2].block_type(), BlockType::Name);
        assert!(blocks[3..].iter().all(|b| b.block_type() == BlockType::Free));

        assert!(state.add_uint_metric(block.index(), 10).is_ok());
        assert_eq!(block.uint_value().unwrap(), 13);

        assert!(state.subtract_uint_metric(block.index(), 5).is_ok());
        assert_eq!(block.uint_value().unwrap(), 8);

        assert!(state.set_uint_metric(block.index(), 6).is_ok());
        assert_eq!(block.uint_value().unwrap(), 6);

        // Free metric.
        assert!(state.free_value(block.index()).is_ok());
        let bytes = &state.heap.bytes()[..];
        let blocks: Vec<Block<&[u8]>> = BlockIterator::new(&bytes).collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[test]
    fn test_double_metric() {
        let mut state = get_state(4096);

        // Creates with value
        let block = state.create_double_metric("test", 3.0, 0).unwrap();
        assert_eq!(block.block_type(), BlockType::DoubleValue);
        assert_eq!(block.index(), 1);
        assert_eq!(block.double_value().unwrap(), 3.0);
        assert_eq!(block.name_index().unwrap(), 2);
        assert_eq!(block.parent_index().unwrap(), 0);

        let name_block = state.heap.get_block(2).unwrap();
        assert_eq!(name_block.block_type(), BlockType::Name);
        assert_eq!(name_block.name_length().unwrap(), 4);
        assert_eq!(name_block.name_contents().unwrap(), "test");

        let bytes = &state.heap.bytes()[..];
        let blocks: Vec<Block<&[u8]>> = BlockIterator::new(&bytes).collect();
        assert_eq!(blocks.len(), 10);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::DoubleValue);
        assert_eq!(blocks[2].block_type(), BlockType::Name);
        assert!(blocks[3..].iter().all(|b| b.block_type() == BlockType::Free));

        assert!(state.add_double_metric(block.index(), 10.5).is_ok());
        assert_eq!(block.double_value().unwrap(), 13.5);

        assert!(state.subtract_double_metric(block.index(), 5.1).is_ok());
        assert_eq!(block.double_value().unwrap(), 8.4);

        assert!(state.set_double_metric(block.index(), -6.0).is_ok());
        assert_eq!(block.double_value().unwrap(), -6.0);

        // Free metric.
        assert!(state.free_value(block.index()).is_ok());
        let bytes = &state.heap.bytes()[..];
        let blocks: Vec<Block<&[u8]>> = BlockIterator::new(&bytes).collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[test]
    fn test_string_property() {
        let mut state = get_state(4096);

        // Creates with value
        let block =
            state.create_property("test", b"test-property", PropertyFormat::String, 0).unwrap();
        assert_eq!(block.block_type(), BlockType::PropertyValue);
        assert_eq!(block.index(), 1);
        assert_eq!(block.parent_index().unwrap(), 0);
        assert_eq!(block.name_index().unwrap(), 2);
        assert_eq!(block.property_total_length().unwrap(), 13);
        assert_eq!(block.property_flags().unwrap(), 0);

        let name_block = state.heap.get_block(2).unwrap();
        assert_eq!(name_block.block_type(), BlockType::Name);
        assert_eq!(name_block.name_length().unwrap(), 4);
        assert_eq!(name_block.name_contents().unwrap(), "test");

        let extent_block = state.heap.get_block(4).unwrap();
        assert_eq!(extent_block.block_type(), BlockType::Extent);
        assert_eq!(extent_block.next_extent().unwrap(), 0);
        assert_eq!(
            String::from_utf8(extent_block.extent_contents().unwrap()).unwrap(),
            "test-property\0\0\0\0\0\0\0\0\0\0\0"
        );

        let bytes = &state.heap.bytes()[..];
        let blocks: Vec<Block<&[u8]>> = BlockIterator::new(&bytes).collect();
        assert_eq!(blocks.len(), 11);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::PropertyValue);
        assert_eq!(blocks[2].block_type(), BlockType::Name);
        assert_eq!(blocks[3].block_type(), BlockType::Free);
        assert_eq!(blocks[4].block_type(), BlockType::Extent);
        assert!(blocks[5..].iter().all(|b| b.block_type() == BlockType::Free));

        // Free property.
        assert!(state.free_property(block.index()).is_ok());
        let bytes = &state.heap.bytes()[..];
        let blocks: Vec<Block<&[u8]>> = BlockIterator::new(&bytes).collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[test]
    fn test_bytevector_property() {
        let mut state = get_state(4096);

        // Creates with value
        let block =
            state.create_property("test", b"test-property", PropertyFormat::ByteVector, 0).unwrap();
        assert_eq!(block.block_type(), BlockType::PropertyValue);
        assert_eq!(block.index(), 1);
        assert_eq!(block.parent_index().unwrap(), 0);
        assert_eq!(block.name_index().unwrap(), 2);
        assert_eq!(block.property_total_length().unwrap(), 13);
        assert_eq!(block.property_extent_index().unwrap(), 4);
        assert_eq!(block.property_flags().unwrap(), 1);

        let name_block = state.heap.get_block(2).unwrap();
        assert_eq!(name_block.block_type(), BlockType::Name);
        assert_eq!(name_block.name_length().unwrap(), 4);
        assert_eq!(name_block.name_contents().unwrap(), "test");

        let extent_block = state.heap.get_block(4).unwrap();
        assert_eq!(extent_block.block_type(), BlockType::Extent);
        assert_eq!(extent_block.next_extent().unwrap(), 0);
        assert_eq!(
            String::from_utf8(extent_block.extent_contents().unwrap()).unwrap(),
            "test-property\0\0\0\0\0\0\0\0\0\0\0"
        );

        let bytes = &state.heap.bytes()[..];
        let blocks: Vec<Block<&[u8]>> = BlockIterator::new(&bytes).collect();
        assert_eq!(blocks.len(), 11);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::PropertyValue);
        assert_eq!(blocks[2].block_type(), BlockType::Name);
        assert_eq!(blocks[3].block_type(), BlockType::Free);
        assert_eq!(blocks[4].block_type(), BlockType::Extent);
        assert!(blocks[5..].iter().all(|b| b.block_type() == BlockType::Free));

        // Free property.
        assert!(state.free_property(block.index()).is_ok());
        let bytes = &state.heap.bytes()[..];
        let blocks: Vec<Block<&[u8]>> = BlockIterator::new(&bytes).collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[test]
    fn test_multi_extent_property() {
        let mut state = get_state(10000);

        let chars = ['a', 'b', 'c', 'd', 'e', 'f', 'g'];
        let data = chars.iter().cycle().take(6000).collect::<String>();
        let block =
            state.create_property("test", data.as_bytes(), PropertyFormat::String, 0).unwrap();
        assert_eq!(block.block_type(), BlockType::PropertyValue);
        assert_eq!(block.index(), 1);
        assert_eq!(block.parent_index().unwrap(), 0);
        assert_eq!(block.name_index().unwrap(), 2);
        assert_eq!(block.property_total_length().unwrap(), 6000);
        assert_eq!(block.property_extent_index().unwrap(), 128);
        assert_eq!(block.property_flags().unwrap(), 0);

        let name_block = state.heap.get_block(2).unwrap();
        assert_eq!(name_block.block_type(), BlockType::Name);
        assert_eq!(name_block.name_length().unwrap(), 4);
        assert_eq!(name_block.name_contents().unwrap(), "test");

        let extent_block = state.heap.get_block(128).unwrap();
        assert_eq!(extent_block.block_type(), BlockType::Extent);
        assert_eq!(extent_block.order(), 7);
        assert_eq!(extent_block.next_extent().unwrap(), 256);
        assert_eq!(
            extent_block.extent_contents().unwrap(),
            chars.iter().cycle().take(2040).map(|&c| c as u8).collect::<Vec<u8>>()
        );

        let extent_block = state.heap.get_block(256).unwrap();
        assert_eq!(extent_block.block_type(), BlockType::Extent);
        assert_eq!(extent_block.order(), 7);
        assert_eq!(extent_block.next_extent().unwrap(), 384);
        assert_eq!(
            extent_block.extent_contents().unwrap(),
            chars.iter().cycle().skip(2040).take(2040).map(|&c| c as u8).collect::<Vec<u8>>()
        );

        let extent_block = state.heap.get_block(384).unwrap();
        assert_eq!(extent_block.block_type(), BlockType::Extent);
        assert_eq!(extent_block.order(), 7);
        assert_eq!(extent_block.next_extent().unwrap(), 0);
        assert_eq!(
            extent_block.extent_contents().unwrap()[..1920],
            chars.iter().cycle().skip(4080).take(1920).map(|&c| c as u8).collect::<Vec<u8>>()[..]
        );
        assert_eq!(extent_block.extent_contents().unwrap()[1920..], [0u8; 120][..]);

        let bytes = &state.heap.bytes()[..];
        let blocks: Vec<Block<&[u8]>> = BlockIterator::new(&bytes).collect();
        assert_eq!(blocks.len(), 12);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::PropertyValue);
        assert_eq!(blocks[2].block_type(), BlockType::Name);
        assert!(blocks[3..9].iter().all(|b| b.block_type() == BlockType::Free));
        assert_eq!(blocks[9].block_type(), BlockType::Extent);
        assert_eq!(blocks[10].block_type(), BlockType::Extent);
        assert_eq!(blocks[11].block_type(), BlockType::Extent);

        // Free property.
        assert!(state.free_property(block.index()).is_ok());
        let bytes = &state.heap.bytes()[..];
        let blocks: Vec<Block<&[u8]>> = BlockIterator::new(&bytes).collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[test]
    fn test_tombstone() {
        let mut state = get_state(4096);

        // Create a node value and verify its fields
        let block = state.create_node("root-node", 0).unwrap();
        let child_block = state.create_node("child-node", block.index()).unwrap();

        // Node still has children, so will become a tombstone.
        assert!(state.free_value(block.index()).is_ok());

        let bytes = &state.heap.bytes()[..];
        let blocks: Vec<Block<&[u8]>> = BlockIterator::new(&bytes).collect();
        assert!(blocks[0].block_type() == BlockType::Header);
        assert!(blocks[1].block_type() == BlockType::Tombstone);
        assert!(blocks[2].block_type() == BlockType::Free);
        assert!(blocks[3].block_type() == BlockType::NodeValue);
        assert!(blocks[4].block_type() == BlockType::Free);
        assert!(blocks[5].block_type() == BlockType::Name);
        assert!(blocks[6..].iter().all(|b| b.block_type() == BlockType::Free));

        // Freeing the child, causes all blocks to be freed.
        assert!(state.free_value(child_block.index()).is_ok());
        let bytes = &state.heap.bytes()[..];
        let blocks: Vec<Block<&[u8]>> = BlockIterator::new(&bytes).collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    fn get_state(size: usize) -> State {
        let (mapping, _) = Mapping::allocate(size).unwrap();
        let heap = Heap::new(Arc::new(mapping)).unwrap();
        State::create(heap).unwrap()
    }
}
