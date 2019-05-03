// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{format_err, Error};
use mapped_vmo::Mapping;
use num_traits::ToPrimitive;
use std::{cmp::min, rc::Rc};

use crate::vmo::block::Block;
use crate::vmo::block_type::BlockType;
use crate::vmo::constants;
use crate::vmo::utils;

pub struct Heap {
    mapping: Rc<Mapping>,
    current_size_bytes: usize,
    free_head_per_order: [u32; constants::NUM_ORDERS],
}

impl Heap {
    pub fn new(mapping: Rc<Mapping>) -> Result<Self, Error> {
        let mut heap = Heap {
            mapping: mapping,
            current_size_bytes: 0,
            free_head_per_order: [0; constants::NUM_ORDERS],
        };
        heap.grow_heap(constants::PAGE_SIZE_BYTES)?;
        Ok(heap)
    }

    pub fn size(&self) -> usize {
        self.current_size_bytes
    }

    pub fn allocate_block(&mut self, min_size: usize) -> Result<Block<Rc<Mapping>>, Error> {
        let min_fit_order = utils::fit_order(min_size);
        if min_fit_order >= constants::NUM_ORDERS {
            return Err(format_err!("order is bigger than maximum order"));
        }
        // Find free block with order >= min_fit_order
        let order_found = (min_fit_order..constants::NUM_ORDERS)
            .find(|&i| self.is_free_block(self.free_head_per_order[i], i));
        let next_order = match order_found {
            Some(order) => order,
            None => {
                self.grow_heap(self.current_size_bytes * 2)?;
                constants::NUM_ORDERS - 1
            }
        };
        let next_block_index = self.free_head_per_order[next_order];
        let block = self.get_block(next_block_index).unwrap();
        while block.order() > min_fit_order {
            self.split_block(&block)?;
        }
        self.remove_free(&block)?;
        block.become_reserved().expect("Failed to reserve make block reserved");
        Ok(block)
    }

    pub fn free_block(&mut self, block: Block<Rc<Mapping>>) -> Result<(), Error> {
        let block_type = block.block_type();
        if block_type == BlockType::Free {
            return Err(format_err!("can't free block of type {}", block_type));
        }
        let mut buddy_index = self.buddy(block.index(), block.order());
        let mut buddy_block = self.get_block(buddy_index).unwrap();
        let mut block_to_free = block;
        while buddy_block.block_type() == BlockType::Free
            && block_to_free.order() < constants::NUM_ORDERS - 1
            && block_to_free.order() == buddy_block.order()
        {
            self.remove_free(&buddy_block)?;
            if buddy_block.index() < block_to_free.index() {
                block_to_free.swap(&mut buddy_block)?;
            }
            block_to_free.set_order(block_to_free.order() + 1)?;
            buddy_index = self.buddy(block_to_free.index(), block_to_free.order());
            buddy_block = self.get_block(buddy_index).unwrap();
        }
        block_to_free.become_free(self.free_head_per_order[block_to_free.order()]);
        self.free_head_per_order[block_to_free.order()] = block_to_free.index();
        Ok(())
    }

    pub fn get_block(&self, index: u32) -> Option<Block<Rc<Mapping>>> {
        // TODO(miguelfrde): validate index
        let offset = utils::offset_for_index(index);
        if offset >= self.current_size_bytes {
            return None;
        }
        let block = Block::new(self.mapping.clone(), index);
        if self.current_size_bytes - offset < utils::order_to_size(block.order()) {
            return None;
        }
        Some(block)
    }

    fn grow_heap(&mut self, requested_size: usize) -> Result<(), Error> {
        let mapping_size = self.mapping.len();
        if self.current_size_bytes == mapping_size && requested_size > mapping_size {
            return Err(format_err!("Heap already at maxium size"));
        }
        let new_size = min(mapping_size, requested_size);
        let min_index = utils::index_for_offset(self.current_size_bytes);
        let mut last_index = self.free_head_per_order[constants::NUM_ORDERS - 1];
        let mut curr_index =
            utils::index_for_offset(new_size - new_size % constants::PAGE_SIZE_BYTES);
        loop {
            curr_index -= utils::index_for_offset(constants::MAX_ORDER_SIZE);
            Block::new_free(
                self.mapping.clone(),
                curr_index,
                constants::NUM_ORDERS - 1,
                last_index,
            )
            .expect("Failed to create free block");
            last_index = curr_index;
            if curr_index <= min_index {
                break;
            }
        }
        self.free_head_per_order[constants::NUM_ORDERS - 1] = last_index;
        self.current_size_bytes = new_size;
        Ok(())
    }

    fn is_free_block(&self, index: u32, expected_order: usize) -> bool {
        if index.to_usize().unwrap() >= self.current_size_bytes / constants::MIN_ORDER_SIZE {
            return false;
        }
        match self.get_block(index) {
            None => false,
            Some(block) => block.block_type() == BlockType::Free && block.order() == expected_order,
        }
    }

    fn remove_free(&mut self, block: &Block<Rc<Mapping>>) -> Result<bool, Error> {
        let order = block.order();
        let index = block.index();
        if order >= constants::NUM_ORDERS {
            return Ok(false);
        }
        let mut next_index = self.free_head_per_order[order];
        if next_index == index {
            self.free_head_per_order[order] = block.free_next_index()?;
            return Ok(true);
        }
        while self.is_free_block(next_index, order) {
            let curr_block = self.get_block(next_index).unwrap();
            next_index = curr_block.free_next_index()?;
            if next_index == index {
                curr_block.set_free_next_index(block.free_next_index()?)?;
                return Ok(true);
            }
        }
        Ok(false)
    }

    fn split_block(&mut self, block: &Block<Rc<Mapping>>) -> Result<(), Error> {
        if block.order() >= constants::NUM_ORDERS {
            return Err(format_err!(
                "order {} in block {} is invalid",
                block.order(),
                block.index()
            ));
        }
        self.remove_free(&block)?;
        let order = block.order();
        let buddy_index = self.buddy(block.index(), order - 1);
        block.set_order(order - 1)?;
        block.become_free(buddy_index);

        let buddy = self.get_block(buddy_index).unwrap();
        buddy.set_order(order - 1)?;
        buddy.become_free(self.free_head_per_order[order - 1]);
        self.free_head_per_order[order - 1] = block.index();
        Ok(())
    }

    fn buddy(&self, index: u32, order: usize) -> u32 {
        index ^ utils::index_for_offset(utils::order_to_size(order))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // TODO(miguelfrde): we could move this out of the tests mod if needed.
    struct HeapIterator<'h> {
        offset: usize,
        heap: &'h Heap,
    }

    impl<'h> HeapIterator<'h> {
        fn iterate(heap: &'h Heap) -> Self {
            HeapIterator { offset: 0, heap: heap }
        }
    }

    impl<'h> Iterator for HeapIterator<'h> {
        type Item = Block<Rc<Mapping>>;

        fn next(&mut self) -> Option<Block<Rc<Mapping>>> {
            let index = utils::index_for_offset(self.offset);
            let result = self.heap.get_block(index);
            if let Some(block) = &result {
                self.offset += utils::order_to_size(block.order());
            }
            result
        }
    }

    struct BlockDebug {
        index: u32,
        order: usize,
        block_type: BlockType,
    }

    fn validate(expected: &[BlockDebug], heap: &Heap) {
        let actual: Vec<BlockDebug> = HeapIterator::iterate(&heap)
            .map(|block| BlockDebug {
                order: block.order(),
                index: block.index(),
                block_type: block.block_type(),
            })
            .collect();
        assert_eq!(expected.len(), actual.len());
        for (i, result) in actual.iter().enumerate() {
            assert_eq!(result.block_type, expected[i].block_type);
            assert_eq!(result.index, expected[i].index);
            assert_eq!(result.order, expected[i].order);
        }
    }

    #[test]
    fn new_heap() {
        let (mapping, _) = Mapping::allocate(4096).unwrap();
        let heap = Heap::new(Rc::new(mapping)).unwrap();
        assert_eq!(heap.current_size_bytes, 4096);
        assert_eq!(heap.free_head_per_order, [0; 8]);

        let expected = [
            BlockDebug { index: 0, order: 7, block_type: BlockType::Free },
            BlockDebug { index: 128, order: 7, block_type: BlockType::Free },
        ];
        validate(&expected, &heap);
        assert_eq!(heap.free_head_per_order[7], 0);
        assert_eq!(heap.get_block(0).unwrap().free_next_index().unwrap(), 128);
        assert_eq!(heap.get_block(128).unwrap().free_next_index().unwrap(), 0);
    }

    #[test]
    fn allocate_and_free() {
        let (mapping, _) = Mapping::allocate(4096).unwrap();
        let mut heap = Heap::new(Rc::new(mapping)).unwrap();

        // Allocate some small blocks and ensure they are all in order.
        for i in 0..=5 {
            let block = heap.allocate_block(constants::MIN_ORDER_SIZE).unwrap();
            assert_eq!(block.index(), i);
        }

        // Free some blocks. Leaving some in the middle.
        assert!(heap.free_block(heap.get_block(2).unwrap()).is_ok());
        assert!(heap.free_block(heap.get_block(4).unwrap()).is_ok());
        assert!(heap.free_block(heap.get_block(0).unwrap()).is_ok());

        // Allocate more small blocks and ensure we get the same ones in reverse
        // order.
        let b = heap.allocate_block(constants::MIN_ORDER_SIZE).unwrap();
        assert_eq!(b.index(), 0);
        let b = heap.allocate_block(constants::MIN_ORDER_SIZE).unwrap();
        assert_eq!(b.index(), 4);
        let b = heap.allocate_block(constants::MIN_ORDER_SIZE).unwrap();
        assert_eq!(b.index(), 2);

        // Free everything except the first two.
        assert!(heap.free_block(heap.get_block(4).unwrap()).is_ok());
        assert!(heap.free_block(heap.get_block(2).unwrap()).is_ok());
        assert!(heap.free_block(heap.get_block(3).unwrap()).is_ok());
        assert!(heap.free_block(heap.get_block(5).unwrap()).is_ok());

        let expected = [
            BlockDebug { index: 0, order: 0, block_type: BlockType::Reserved },
            BlockDebug { index: 1, order: 0, block_type: BlockType::Reserved },
            BlockDebug { index: 2, order: 1, block_type: BlockType::Free },
            BlockDebug { index: 4, order: 2, block_type: BlockType::Free },
            BlockDebug { index: 8, order: 3, block_type: BlockType::Free },
            BlockDebug { index: 16, order: 4, block_type: BlockType::Free },
            BlockDebug { index: 32, order: 5, block_type: BlockType::Free },
            BlockDebug { index: 64, order: 6, block_type: BlockType::Free },
            BlockDebug { index: 128, order: 7, block_type: BlockType::Free },
        ];
        validate(&expected, &heap);
        assert!(heap.free_head_per_order.iter().enumerate().skip(2).all(|(i, &j)| (1 << i) == j));
        assert!(HeapIterator::iterate(&heap).skip(2).all(|b| b.free_next_index().unwrap() == 0));

        // Ensure a large block takes the first free large one.
        assert!(heap.free_block(heap.get_block(0).unwrap()).is_ok());
        let b = heap.allocate_block(2048).unwrap();
        assert_eq!(b.index(), 128);

        // Free last small allocation, next large takes first half of the
        // buffer.
        assert!(heap.free_block(heap.get_block(1).unwrap()).is_ok());
        let b = heap.allocate_block(2048).unwrap();
        assert_eq!(b.index(), 0);

        let expected = [
            BlockDebug { index: 0, order: 7, block_type: BlockType::Reserved },
            BlockDebug { index: 128, order: 7, block_type: BlockType::Reserved },
        ];
        validate(&expected, &heap);

        // Allocate twice in the first half, free in reverse order to ensure
        // freeing works left to right and right to left.
        assert!(heap.free_block(heap.get_block(0).unwrap()).is_ok());
        let b = heap.allocate_block(1024).unwrap();
        assert_eq!(b.index(), 0);
        let b = heap.allocate_block(1024).unwrap();
        assert_eq!(b.index(), 64);
        assert!(heap.free_block(heap.get_block(0).unwrap()).is_ok());
        assert!(heap.free_block(heap.get_block(64).unwrap()).is_ok());

        // Ensure freed blocks are merged int a big one and that we can use all
        // space at 0.
        let b = heap.allocate_block(2048).unwrap();
        assert_eq!(b.index(), 0);
        assert!(heap.free_block(heap.get_block(0).unwrap()).is_ok());

        let expected = [
            BlockDebug { index: 0, order: 7, block_type: BlockType::Free },
            BlockDebug { index: 128, order: 7, block_type: BlockType::Reserved },
        ];
        validate(&expected, &heap);
        assert_eq!(heap.free_head_per_order[7], 0);
        assert_eq!(heap.get_block(0).unwrap().free_next_index().unwrap(), 0);

        assert!(heap.free_block(heap.get_block(128).unwrap()).is_ok());
        let expected = [
            BlockDebug { index: 0, order: 7, block_type: BlockType::Free },
            BlockDebug { index: 128, order: 7, block_type: BlockType::Free },
        ];
        validate(&expected, &heap);
        assert_eq!(heap.free_head_per_order[7], 128);
        assert_eq!(heap.get_block(0).unwrap().free_next_index().unwrap(), 0);
        assert_eq!(heap.get_block(128).unwrap().free_next_index().unwrap(), 0);
    }

    #[test]
    fn allocate_merge() {
        let (mapping, _) = Mapping::allocate(4096).unwrap();
        let mut heap = Heap::new(Rc::new(mapping)).unwrap();
        for i in 0..=3 {
            let block = heap.allocate_block(constants::MIN_ORDER_SIZE).unwrap();
            assert_eq!(block.index(), i);
        }

        assert!(heap.free_block(heap.get_block(2).unwrap()).is_ok());
        assert!(heap.free_block(heap.get_block(0).unwrap()).is_ok());
        assert!(heap.free_block(heap.get_block(1).unwrap()).is_ok());

        let expected = [
            BlockDebug { index: 0, order: 1, block_type: BlockType::Free },
            BlockDebug { index: 2, order: 0, block_type: BlockType::Free },
            BlockDebug { index: 3, order: 0, block_type: BlockType::Reserved },
            BlockDebug { index: 4, order: 2, block_type: BlockType::Free },
            BlockDebug { index: 8, order: 3, block_type: BlockType::Free },
            BlockDebug { index: 16, order: 4, block_type: BlockType::Free },
            BlockDebug { index: 32, order: 5, block_type: BlockType::Free },
            BlockDebug { index: 64, order: 6, block_type: BlockType::Free },
            BlockDebug { index: 128, order: 7, block_type: BlockType::Free },
        ];
        validate(&expected, &heap);
        assert!(heap.free_head_per_order.iter().enumerate().skip(3).all(|(i, &j)| (1 << i) == j));
        assert!(HeapIterator::iterate(&heap).skip(3).all(|b| b.free_next_index().unwrap() == 0));
        assert_eq!(heap.free_head_per_order[1], 0);
        assert_eq!(heap.free_head_per_order[0], 2);
        assert_eq!(heap.get_block(0).unwrap().free_next_index().unwrap(), 0);
        assert_eq!(heap.get_block(2).unwrap().free_next_index().unwrap(), 0);

        assert!(heap.free_block(heap.get_block(3).unwrap()).is_ok());
        let expected = [
            BlockDebug { index: 0, order: 7, block_type: BlockType::Free },
            BlockDebug { index: 128, order: 7, block_type: BlockType::Free },
        ];
        validate(&expected, &heap);
        assert_eq!(heap.free_head_per_order[1], 0);
        assert_eq!(heap.get_block(0).unwrap().free_next_index().unwrap(), 128);
        assert_eq!(heap.get_block(128).unwrap().free_next_index().unwrap(), 0);
    }

    #[test]
    fn extend() {
        let (mapping, _) = Mapping::allocate(8 * 2048).unwrap();
        let mut heap = Heap::new(Rc::new(mapping)).unwrap();

        let b = heap.allocate_block(2048).unwrap();
        assert_eq!(b.index(), 0);
        let b = heap.allocate_block(2048).unwrap();
        assert_eq!(b.index(), 128);
        let b = heap.allocate_block(2048).unwrap();
        assert_eq!(b.index(), 256);

        let expected = [
            BlockDebug { index: 0, order: 7, block_type: BlockType::Reserved },
            BlockDebug { index: 128, order: 7, block_type: BlockType::Reserved },
            BlockDebug { index: 256, order: 7, block_type: BlockType::Reserved },
            BlockDebug { index: 384, order: 7, block_type: BlockType::Free },
        ];
        validate(&expected, &heap);
        assert_eq!(heap.free_head_per_order[7], 384);
        assert_eq!(heap.get_block(384).unwrap().free_next_index().unwrap(), 0);

        let b = heap.allocate_block(2048).unwrap();
        assert_eq!(b.index(), 384);
        let b = heap.allocate_block(2048).unwrap();
        assert_eq!(b.index(), 512);

        assert!(heap.free_block(heap.get_block(0).unwrap()).is_ok());
        assert!(heap.free_block(heap.get_block(128).unwrap()).is_ok());
        assert!(heap.free_block(heap.get_block(256).unwrap()).is_ok());
        assert!(heap.free_block(heap.get_block(384).unwrap()).is_ok());
        assert!(heap.free_block(heap.get_block(512).unwrap()).is_ok());

        let expected = [
            BlockDebug { index: 0, order: 7, block_type: BlockType::Free },
            BlockDebug { index: 128, order: 7, block_type: BlockType::Free },
            BlockDebug { index: 256, order: 7, block_type: BlockType::Free },
            BlockDebug { index: 384, order: 7, block_type: BlockType::Free },
            BlockDebug { index: 512, order: 7, block_type: BlockType::Free },
            BlockDebug { index: 640, order: 7, block_type: BlockType::Free },
            BlockDebug { index: 768, order: 7, block_type: BlockType::Free },
            BlockDebug { index: 896, order: 7, block_type: BlockType::Free },
        ];
        validate(&expected, &heap);
        assert_eq!(heap.free_head_per_order[7], 512);
        assert_eq!(heap.get_block(512).unwrap().free_next_index().unwrap(), 384);
        assert_eq!(heap.get_block(384).unwrap().free_next_index().unwrap(), 256);
        assert_eq!(heap.get_block(256).unwrap().free_next_index().unwrap(), 128);
        assert_eq!(heap.get_block(128).unwrap().free_next_index().unwrap(), 0);
        assert_eq!(heap.get_block(0).unwrap().free_next_index().unwrap(), 640);
        assert_eq!(heap.get_block(640).unwrap().free_next_index().unwrap(), 768);
        assert_eq!(heap.get_block(768).unwrap().free_next_index().unwrap(), 896);
        assert_eq!(heap.get_block(896).unwrap().free_next_index().unwrap(), 0);
    }

    #[test]
    fn extend_fail() {
        let (mapping, _) = Mapping::allocate(4 * 2048).unwrap();
        let mut heap = Heap::new(Rc::new(mapping)).unwrap();

        let b = heap.allocate_block(2048).unwrap();
        assert_eq!(b.index(), 0);
        let b = heap.allocate_block(2048).unwrap();
        assert_eq!(b.index(), 128);
        let b = heap.allocate_block(2048).unwrap();
        assert_eq!(b.index(), 256);

        let expected = [
            BlockDebug { index: 0, order: 7, block_type: BlockType::Reserved },
            BlockDebug { index: 128, order: 7, block_type: BlockType::Reserved },
            BlockDebug { index: 256, order: 7, block_type: BlockType::Reserved },
            BlockDebug { index: 384, order: 7, block_type: BlockType::Free },
        ];
        validate(&expected, &heap);

        let b = heap.allocate_block(2048).unwrap();
        assert_eq!(b.index(), 384);
        assert!(heap.allocate_block(2048).is_err());

        assert!(heap.free_block(heap.get_block(0).unwrap()).is_ok());
        assert!(heap.free_block(heap.get_block(128).unwrap()).is_ok());
        assert!(heap.free_block(heap.get_block(256).unwrap()).is_ok());
        assert!(heap.free_block(heap.get_block(384).unwrap()).is_ok());

        let expected = [
            BlockDebug { index: 0, order: 7, block_type: BlockType::Free },
            BlockDebug { index: 128, order: 7, block_type: BlockType::Free },
            BlockDebug { index: 256, order: 7, block_type: BlockType::Free },
            BlockDebug { index: 384, order: 7, block_type: BlockType::Free },
        ];
        validate(&expected, &heap);
    }
}
