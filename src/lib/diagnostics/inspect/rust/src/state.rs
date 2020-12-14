// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        error::Error,
        format::{
            block::{ArrayFormat, Block, LinkNodeDisposition, PropertyFormat},
            block_type::BlockType,
            constants,
        },
        heap::Heap,
        utils, Inspector,
    },
    anyhow,
    derivative::Derivative,
    futures::future::BoxFuture,
    mapped_vmo::Mapping,
    num_traits::ToPrimitive,
    parking_lot::{Mutex, MutexGuard},
    std::{
        collections::HashMap,
        sync::{
            atomic::{AtomicU64, Ordering},
            Arc,
        },
    },
    tracing::error,
};

/// Callback used to fill inspector lazy nodes.
pub type LazyNodeContextFnArc =
    Arc<dyn Fn() -> BoxFuture<'static, Result<Inspector, anyhow::Error>> + Sync + Send>;

trait SafeOp {
    fn safe_sub(&self, other: Self) -> Self;
    fn safe_add(&self, other: Self) -> Self;
}

impl SafeOp for u64 {
    fn safe_sub(&self, other: u64) -> u64 {
        self.checked_sub(other).unwrap_or(0)
    }
    fn safe_add(&self, other: u64) -> u64 {
        self.checked_add(other).unwrap_or(std::u64::MAX)
    }
}

impl SafeOp for i64 {
    fn safe_sub(&self, other: i64) -> i64 {
        self.checked_sub(other).unwrap_or(std::i64::MIN)
    }
    fn safe_add(&self, other: i64) -> i64 {
        self.checked_add(other).unwrap_or(std::i64::MAX)
    }
}

impl SafeOp for f64 {
    fn safe_sub(&self, other: f64) -> f64 {
        self - other
    }
    fn safe_add(&self, other: f64) -> f64 {
        self + other
    }
}

macro_rules! locked_state_metric_fns {
    ($name:ident, $type:ident) => {
        paste::paste! {
            pub fn [<create_ $name _metric>](
                &mut self,
                name: &str,
                value: $type,
                parent_index: u32,
            ) -> Result<Block<Arc<Mapping>>, Error> {
                self.inner_lock.[<create_ $name _metric>](name, value, parent_index)
            }

            pub fn [<set_ $name _metric>](&self, block_index: u32, value: $type)
                -> Result<(), Error> {
                self.inner_lock.[<set_ $name _metric>](block_index, value)
            }

            pub fn [<add_ $name _metric>](&self, block_index: u32, value: $type)
                -> Result<(), Error> {
                self.inner_lock.[<add_ $name _metric>](block_index, value)
            }

            pub fn [<subtract_ $name _metric>](&self, block_index: u32, value: $type)
                -> Result<(), Error> {
                self.inner_lock.[<subtract_ $name _metric>](block_index, value)
            }

            pub fn [<get_ $name _metric>](&self, block_index: u32) -> Result<$type, Error> {
                self.inner_lock.[<get_ $name _metric>](block_index)
            }
        }
    };
}

/// Generate create, set, add and subtract methods for a metric.
macro_rules! metric_fns {
    ($name:ident, $type:ident) => {
        paste::paste! {
            fn [<create_ $name _metric>](
                &mut self,
                name: &str,
                value: $type,
                parent_index: u32,
            ) -> Result<Block<Arc<Mapping>>, Error> {
                let (block, name_block) = self.allocate_reserved_value(
                    name, parent_index, constants::MIN_ORDER_SIZE)?;
                block.[<become_ $name _value>](value, name_block.index(), parent_index)?;
                Ok(block)
            }

            fn [<set_ $name _metric>](&self, block_index: u32, value: $type)
                -> Result<(), Error> {
                let block = self.heap.get_block(block_index)?;
                block.[<set_ $name _value>](value)?;
                Ok(())
            }

            fn [<add_ $name _metric>](&self, block_index: u32, value: $type)
                -> Result<(), Error> {
                let block = self.heap.get_block(block_index)?;
                let current_value = block.[<$name _value>]()?;
                block.[<set_ $name _value>](current_value.safe_add(value))?;
                Ok(())
            }

            fn [<subtract_ $name _metric>](&self, block_index: u32, value: $type)
                -> Result<(), Error> {
                let block = self.heap.get_block(block_index)?;
                let current_value = block.[<$name _value>]()?;
                let new_value = current_value.safe_sub(value);
                block.[<set_ $name _value>](new_value)?;
                Ok(())
            }

            fn [<get_ $name _metric>](&self, block_index: u32) -> Result<$type, Error> {
                let block = self.heap.get_block(block_index)?;
                let current_value = block.[<$name _value>]()?;
                Ok(current_value)
            }
        }
    };
}
macro_rules! locked_state_array_fns {
    ($name:ident, $type:ident, $value:ident) => {
        paste::paste! {
            pub fn [<create_ $name _array>](
                &mut self,
                name: &str,
                slots: usize,
                array_format: ArrayFormat,
                parent_index: u32,
            ) -> Result<Block<Arc<Mapping>>, Error> {
                self.inner_lock.[<create_ $name _array>](name, slots, array_format, parent_index)
            }

            pub fn [<set_array_ $name _slot>](
                &mut self, block_index: u32, slot_index: usize, value: $type
            ) -> Result<(), Error> {
                self.inner_lock.[<set_array_ $name _slot>](block_index, slot_index, value)
            }

            pub fn [<add_array_ $name _slot>](
                &mut self, block_index: u32, slot_index: usize, value: $type
            ) -> Result<(), Error> {
                self.inner_lock.[<add_array_ $name _slot>](block_index, slot_index, value)
            }

            pub fn [<subtract_array_ $name _slot>](
                &mut self, block_index: u32, slot_index: usize, value: $type
            ) -> Result<(), Error> {
                self.inner_lock.[<subtract_array_ $name _slot>](block_index, slot_index, value)
            }
        }
    };
}

macro_rules! array_fns {
    ($name:ident, $type:ident, $value:ident) => {
        paste::paste! {
            pub fn [<create_ $name _array>](
                &mut self,
                name: &str,
                slots: usize,
                array_format: ArrayFormat,
                parent_index: u32,
            ) -> Result<Block<Arc<Mapping>>, Error> {
                let block_size =
                    slots as usize * std::mem::size_of::<$type>() + constants::MIN_ORDER_SIZE;
                if block_size > constants::MAX_ORDER_SIZE {
                    return Err(Error::BlockSizeTooBig(block_size))
                }
                let (block, name_block) = self.allocate_reserved_value(
                    name, parent_index, block_size)?;
                block.become_array_value(
                    slots, array_format, BlockType::$value, name_block.index(), parent_index)?;
                Ok(block)
            }

            pub fn [<set_array_ $name _slot>](
                &mut self, block_index: u32, slot_index: usize, value: $type
            ) -> Result<(), Error> {
                let block = self.heap.get_block(block_index)?;
                block.[<array_set_ $name _slot>](slot_index, value)?;
                Ok(())
            }

            pub fn [<add_array_ $name _slot>](
                &mut self, block_index: u32, slot_index: usize, value: $type
            ) -> Result<(), Error> {
                let block = self.heap.get_block(block_index)?;
                let previous_value = block.[<array_get_ $name _slot>](slot_index)?;
                let new_value = previous_value.safe_add(value);
                block.[<array_set_ $name _slot>](slot_index, new_value)?;
                Ok(())
            }

            pub fn [<subtract_array_ $name _slot>](
                &mut self, block_index: u32, slot_index: usize, value: $type
            ) -> Result<(), Error> {
                let block = self.heap.get_block(block_index)?;
                let previous_value = block.[<array_get_ $name _slot>](slot_index)?;
                let new_value = previous_value.safe_sub(value);
                block.[<array_set_ $name _slot>](slot_index, new_value)?;
                Ok(())
            }
        }
    };
}

/// In charge of performing all operations on the VMO as well as managing the lock and unlock
/// behavior.
#[derive(Clone, Debug)]
pub struct State {
    /// A reference to the header block in the VMO.
    header: Block<Arc<Mapping>>,

    /// The inner state that actually performs the operations.
    /// This should always be accessed by locking the mutex and then locking the header.
    // TODO(fxbug.dev/51298): have a single locking mechanism implemented on top of the vmo header.
    inner: Arc<Mutex<InnerState>>,
}

impl State {
    /// Create a |State| object wrapping the given Heap. This will cause the
    /// heap to be initialized with a header.
    pub fn create(mut heap: Heap) -> Result<Self, Error> {
        let mut block = heap.allocate_block(16)?;
        block.become_header()?;
        let inner = Arc::new(Mutex::new(InnerState::new(heap)));
        Ok(Self { inner, header: block })
    }

    /// Locks the state mutex and inspect vmo. The state will be unlocked on drop.
    /// This can fail when the header is already locked.
    pub fn try_lock<'a>(&'a self) -> Result<LockedStateGuard<'a>, Error> {
        let inner_lock = self.inner.lock();
        LockedStateGuard::new(&self.header, inner_lock)
    }

    /// Copies the bytes in the VMO into the returned vector.
    pub fn copy_vmo_bytes(&self) -> Vec<u8> {
        let state = self.inner.lock();
        state.heap.bytes()
    }
}

pub struct LockedStateGuard<'a> {
    header: &'a Block<Arc<Mapping>>,
    inner_lock: MutexGuard<'a, InnerState>,
}

impl<'a> LockedStateGuard<'a> {
    fn new(
        header: &'a Block<Arc<Mapping>>,
        inner_lock: MutexGuard<'a, InnerState>,
    ) -> Result<Self, Error> {
        header.lock_header()?;
        Ok(Self { header, inner_lock })
    }

    /// Returns a reference to the lazy callbacks map.
    pub fn callbacks(&self) -> &HashMap<String, LazyNodeContextFnArc> {
        &self.inner_lock.callbacks
    }

    /// Allocate a NODE block with the given |name| and |parent_index|.
    pub fn create_node(
        &mut self,
        name: &str,
        parent_index: u32,
    ) -> Result<Block<Arc<Mapping>>, Error> {
        self.inner_lock.create_node(name, parent_index)
    }

    /// Allocate a LINK block with the given |name| and |parent_index| and keep track
    /// of the callback that will fill it.
    pub fn create_lazy_node<F>(
        &mut self,
        name: &str,
        parent_index: u32,
        disposition: LinkNodeDisposition,
        callback: F,
    ) -> Result<Block<Arc<Mapping>>, Error>
    where
        F: Fn() -> BoxFuture<'static, Result<Inspector, anyhow::Error>> + Sync + Send + 'static,
    {
        self.inner_lock.create_lazy_node(name, parent_index, disposition, callback)
    }

    /// Frees a LINK block at the given |index|.
    pub fn free_lazy_node(&mut self, index: u32) -> Result<(), Error> {
        self.inner_lock.free_lazy_node(index)
    }

    /// Free a *_VALUE block at the given |index|.
    pub fn free_value(&mut self, index: u32) -> Result<(), Error> {
        self.inner_lock.free_value(index)
    }

    /// Allocate a PROPERTY block with the given |name|, |value| and |parent_index|.
    pub fn create_property(
        &mut self,
        name: &str,
        value: &[u8],
        format: PropertyFormat,
        parent_index: u32,
    ) -> Result<Block<Arc<Mapping>>, Error> {
        self.inner_lock.create_property(name, value, format, parent_index)
    }

    /// Free a PROPERTY block.
    pub fn free_property(&mut self, index: u32) -> Result<(), Error> {
        self.inner_lock.free_property(index)
    }

    /// Set the |value| of a String PROPERTY block.
    pub fn set_property(&mut self, block_index: u32, value: &[u8]) -> Result<(), Error> {
        self.inner_lock.set_property(block_index, value)
    }

    pub fn create_bool(
        &mut self,
        name: &str,
        value: bool,
        parent_index: u32,
    ) -> Result<Block<Arc<Mapping>>, Error> {
        self.inner_lock.create_bool(name, value, parent_index)
    }

    pub fn set_bool(&self, block_index: u32, value: bool) -> Result<(), Error> {
        self.inner_lock.set_bool(block_index, value)
    }

    locked_state_metric_fns!(int, i64);
    locked_state_metric_fns!(uint, u64);
    locked_state_metric_fns!(double, f64);

    locked_state_array_fns!(int, i64, IntValue);
    locked_state_array_fns!(uint, u64, UintValue);
    locked_state_array_fns!(double, f64, DoubleValue);

    /// Sets all slots of the array at the given index to zero
    pub fn clear_array(&mut self, block_index: u32, start_slot_index: usize) -> Result<(), Error> {
        self.inner_lock.clear_array(block_index, start_slot_index)
    }

    #[cfg(test)]
    pub fn allocate_link(
        &mut self,
        name: &str,
        content: &str,
        disposition: LinkNodeDisposition,
        parent_index: u32,
    ) -> Result<Block<Arc<Mapping>>, Error> {
        self.inner_lock.allocate_link(name, content, disposition, parent_index)
    }

    #[cfg(test)]
    pub fn heap(&self) -> &Heap {
        &self.inner_lock.heap
    }
}

impl Drop for LockedStateGuard<'_> {
    fn drop(&mut self) {
        self.header.unlock_header().unwrap_or_else(|e| {
            error!(?e, "Failed to unlock header");
        });
    }
}

/// Wraps a heap and implements the Inspect VMO API on top of it at a low level.
#[derive(Derivative)]
#[derivative(Debug)]
struct InnerState {
    heap: Heap,
    next_unique_link_id: AtomicU64,

    #[derivative(Debug = "ignore")]
    callbacks: HashMap<String, LazyNodeContextFnArc>,
}

impl InnerState {
    /// Creates a new inner state that performs all operations on the heap.
    pub fn new(heap: Heap) -> Self {
        Self { heap, next_unique_link_id: AtomicU64::new(0), callbacks: HashMap::new() }
    }

    /// Allocate a NODE block with the given |name| and |parent_index|.
    fn create_node(&mut self, name: &str, parent_index: u32) -> Result<Block<Arc<Mapping>>, Error> {
        let (block, name_block) =
            self.allocate_reserved_value(name, parent_index, constants::MIN_ORDER_SIZE)?;
        block.become_node(name_block.index(), parent_index)?;
        Ok(block)
    }

    /// Allocate a LINK block with the given |name| and |parent_index| and keep track
    /// of the callback that will fill it.
    fn create_lazy_node<F>(
        &mut self,
        name: &str,
        parent_index: u32,
        disposition: LinkNodeDisposition,
        callback: F,
    ) -> Result<Block<Arc<Mapping>>, Error>
    where
        F: Fn() -> BoxFuture<'static, Result<Inspector, anyhow::Error>> + Sync + Send + 'static,
    {
        let content = self.unique_link_name(name);
        let link = self.allocate_link(name, &content, disposition, parent_index)?;
        self.callbacks.insert(content, Arc::from(callback));
        Ok(link)
    }

    /// Frees a LINK block at the given |index|.
    fn free_lazy_node(&mut self, index: u32) -> Result<(), Error> {
        let block = self.heap.get_block(index)?;
        let content_block = self.heap.get_block(block.link_content_index()?)?;
        let content = self.heap.get_block(content_block.index())?.name_contents()?;
        self.delete_value(block)?;
        self.heap.free_block(content_block)?;
        self.callbacks.remove(&content);
        Ok(())
    }

    fn unique_link_name(&mut self, prefix: &str) -> String {
        let id = self.next_unique_link_id.fetch_add(1, Ordering::Relaxed);
        format!("{}-{}", prefix, id)
    }

    pub(in crate) fn allocate_link(
        &mut self,
        name: &str,
        content: &str,
        disposition: LinkNodeDisposition,
        parent_index: u32,
    ) -> Result<Block<Arc<Mapping>>, Error> {
        let (value_block, name_block) =
            self.allocate_reserved_value(name, parent_index, constants::MIN_ORDER_SIZE)?;
        let result = self.allocate_name(content).and_then(|content_block| {
            value_block.become_link(
                name_block.index(),
                parent_index,
                content_block.index(),
                disposition,
            )
        });
        match result {
            Ok(()) => Ok(value_block),
            Err(err) => {
                self.delete_value(value_block)?;
                Err(err)
            }
        }
    }

    /// Free a *_VALUE block at the given |index|.
    fn free_value(&mut self, index: u32) -> Result<(), Error> {
        let block = self.heap.get_block(index)?;
        self.delete_value(block).unwrap();
        Ok(())
    }

    /// Allocate a PROPERTY block with the given |name|, |value| and |parent_index|.
    fn create_property(
        &mut self,
        name: &str,
        value: &[u8],
        format: PropertyFormat,
        parent_index: u32,
    ) -> Result<Block<Arc<Mapping>>, Error> {
        let (block, name_block) =
            self.allocate_reserved_value(name, parent_index, constants::MIN_ORDER_SIZE)?;
        block.become_property(name_block.index(), parent_index, format)?;
        if let Err(err) = self.inner_set_property_value(&block, &value) {
            self.heap.free_block(block).expect("Failed to free block");
            self.heap.free_block(name_block).expect("Failed to free name block");
            return Err(err);
        }
        Ok(block)
    }

    /// Free a PROPERTY block.
    fn free_property(&mut self, index: u32) -> Result<(), Error> {
        let block = self.heap.get_block(index)?;
        self.free_extents(block.property_extent_index()?)?;
        self.delete_value(block)?;
        Ok(())
    }

    /// Set the |value| of a String PROPERTY block.
    fn set_property(&mut self, block_index: u32, value: &[u8]) -> Result<(), Error> {
        let block = self.heap.get_block(block_index)?;
        self.inner_set_property_value(&block, value)?;
        Ok(())
    }

    fn create_bool(
        &mut self,
        name: &str,
        value: bool,
        parent_index: u32,
    ) -> Result<Block<Arc<Mapping>>, Error> {
        let (block, name_block) =
            self.allocate_reserved_value(name, parent_index, constants::MIN_ORDER_SIZE)?;
        block.become_bool_value(value, name_block.index(), parent_index)?;
        Ok(block)
    }

    fn set_bool(&self, block_index: u32, value: bool) -> Result<(), Error> {
        let block = self.heap.get_block(block_index)?;
        block.set_bool_value(value)?;
        Ok(())
    }

    metric_fns!(int, i64);
    metric_fns!(uint, u64);
    metric_fns!(double, f64);

    array_fns!(int, i64, IntValue);
    array_fns!(uint, u64, UintValue);
    array_fns!(double, f64, DoubleValue);

    /// Sets all slots of the array at the given index to zero
    fn clear_array(&mut self, block_index: u32, start_slot_index: usize) -> Result<(), Error> {
        let block = self.heap.get_block(block_index)?;
        block.array_clear(start_slot_index)
    }

    fn allocate_reserved_value(
        &mut self,
        name: &str,
        parent_index: u32,
        block_size: usize,
    ) -> Result<(Block<Arc<Mapping>>, Block<Arc<Mapping>>), Error> {
        let block = self.heap.allocate_block(block_size)?;
        let name_block = match self.allocate_name(name) {
            Ok(b) => b,
            Err(err) => {
                self.heap.free_block(block)?;
                return Err(err);
            }
        };

        let result = self.heap.get_block(parent_index).and_then(|parent_block| match parent_block
            .block_type()
        {
            BlockType::NodeValue | BlockType::Tombstone => {
                parent_block.set_child_count(parent_block.child_count().unwrap() + 1)
            }
            BlockType::Header => Ok(()),
            _ => {
                return Err(Error::InvalidBlockType(
                    parent_index as usize,
                    parent_block.block_type(),
                ))
            }
        });
        match result {
            Ok(()) => Ok((block, name_block)),
            Err(err) => {
                self.heap.free_block(name_block)?;
                self.heap.free_block(block)?;
                Err(err)
            }
        }
    }

    fn allocate_name(&mut self, name: &str) -> Result<Block<Arc<Mapping>>, Error> {
        let mut bytes = name.as_bytes();
        let max_bytes = constants::MAX_ORDER_SIZE - constants::HEADER_SIZE_BYTES;
        if bytes.len() > max_bytes {
            bytes = &bytes[..max_bytes];
        }
        let name_block = self.heap.allocate_block(utils::block_size_for_payload(bytes.len()))?;
        name_block.become_name(name)?;
        Ok(name_block)
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
    use {
        super::*,
        crate::{
            assert_inspect_tree,
            reader::{snapshot::Snapshot, PartialNodeHierarchy},
            Inspector,
        },
        fuchsia_async as fasync,
        futures::prelude::*,
        std::convert::TryFrom,
    };

    #[test]
    fn test_create() {
        let state = get_state(4096);
        let snapshot = Snapshot::try_from(state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<Block<&[u8]>> = snapshot.scan().collect();
        assert_eq!(blocks.len(), 9);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[test]
    fn test_node() {
        let core_state = get_state(4096);
        let block = {
            let mut state = core_state.try_lock().expect("lock state");

            // Create a node value and verify its fields
            let block = state.create_node("test-node", 0).unwrap();
            assert_eq!(block.block_type(), BlockType::NodeValue);
            assert_eq!(block.index(), 1);
            assert_eq!(block.child_count().unwrap(), 0);
            assert_eq!(block.name_index().unwrap(), 2);
            assert_eq!(block.parent_index().unwrap(), 0);

            // Verify name block.
            let name_block = state.heap().get_block(2).unwrap();
            assert_eq!(name_block.block_type(), BlockType::Name);
            assert_eq!(name_block.name_length().unwrap(), 9);
            assert_eq!(name_block.name_contents().unwrap(), "test-node");
            block
        };

        // Verify blocks.
        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<Block<&[u8]>> = snapshot.scan().collect();
        assert_eq!(blocks.len(), 9);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::NodeValue);
        assert_eq!(blocks[2].block_type(), BlockType::Name);
        assert!(blocks[3..].iter().all(|b| b.block_type() == BlockType::Free));

        {
            let mut state = core_state.try_lock().expect("lock state");
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
        }
        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<Block<&[u8]>> = snapshot.scan().collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[test]
    fn test_int_metric() {
        let core_state = get_state(4096);
        let block = {
            let mut state = core_state.try_lock().expect("lock state");

            // Creates with value
            let block = state.create_int_metric("test", 3, 0).unwrap();
            assert_eq!(block.block_type(), BlockType::IntValue);
            assert_eq!(block.index(), 1);
            assert_eq!(block.int_value().unwrap(), 3);
            assert_eq!(block.name_index().unwrap(), 2);
            assert_eq!(block.parent_index().unwrap(), 0);

            let name_block = state.heap().get_block(2).unwrap();
            assert_eq!(name_block.block_type(), BlockType::Name);
            assert_eq!(name_block.name_length().unwrap(), 4);
            assert_eq!(name_block.name_contents().unwrap(), "test");
            block
        };

        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<Block<&[u8]>> = snapshot.scan().collect();
        assert_eq!(blocks.len(), 10);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::IntValue);
        assert_eq!(blocks[2].block_type(), BlockType::Name);
        assert!(blocks[3..].iter().all(|b| b.block_type() == BlockType::Free));

        {
            let mut state = core_state.try_lock().expect("lock state");
            assert!(state.add_int_metric(block.index(), 10).is_ok());
            assert_eq!(block.int_value().unwrap(), 13);

            assert!(state.subtract_int_metric(block.index(), 5).is_ok());
            assert_eq!(block.int_value().unwrap(), 8);

            assert!(state.set_int_metric(block.index(), -6).is_ok());
            assert_eq!(block.int_value().unwrap(), -6);
            assert_eq!(state.get_int_metric(block.index()).unwrap(), -6);

            assert!(state.subtract_int_metric(block.index(), std::i64::MAX).is_ok());
            assert_eq!(block.int_value().unwrap(), std::i64::MIN);
            assert!(state.set_int_metric(block.index(), std::i64::MAX).is_ok());

            assert!(state.add_int_metric(block.index(), 2).is_ok());
            assert_eq!(block.int_value().unwrap(), std::i64::MAX);

            // Free metric.
            assert!(state.free_value(block.index()).is_ok());
        }
        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<Block<&[u8]>> = snapshot.scan().collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[test]
    fn test_uint_metric() {
        let core_state = get_state(4096);
        let block = {
            let mut state = core_state.try_lock().expect("try lock");

            // Creates with value
            let block = state.create_uint_metric("test", 3, 0).unwrap();
            assert_eq!(block.block_type(), BlockType::UintValue);
            assert_eq!(block.index(), 1);
            assert_eq!(block.uint_value().unwrap(), 3);
            assert_eq!(block.name_index().unwrap(), 2);
            assert_eq!(block.parent_index().unwrap(), 0);

            let name_block = state.heap().get_block(2).unwrap();
            assert_eq!(name_block.block_type(), BlockType::Name);
            assert_eq!(name_block.name_length().unwrap(), 4);
            assert_eq!(name_block.name_contents().unwrap(), "test");

            block
        };
        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<Block<&[u8]>> = snapshot.scan().collect();
        assert_eq!(blocks.len(), 10);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::UintValue);
        assert_eq!(blocks[2].block_type(), BlockType::Name);
        assert!(blocks[3..].iter().all(|b| b.block_type() == BlockType::Free));

        {
            let mut state = core_state.try_lock().expect("try lock");
            assert!(state.add_uint_metric(block.index(), 10).is_ok());
            assert_eq!(block.uint_value().unwrap(), 13);

            assert!(state.subtract_uint_metric(block.index(), 5).is_ok());
            assert_eq!(block.uint_value().unwrap(), 8);

            assert!(state.set_uint_metric(block.index(), 0).is_ok());
            assert_eq!(block.uint_value().unwrap(), 0);
            assert_eq!(state.get_uint_metric(block.index()).unwrap(), 0);

            assert!(state.subtract_uint_metric(block.index(), std::u64::MAX).is_ok());
            assert_eq!(block.uint_value().unwrap(), 0);

            assert!(state.set_uint_metric(block.index(), 3).is_ok());
            assert!(state.add_uint_metric(block.index(), std::u64::MAX).is_ok());
            assert_eq!(block.uint_value().unwrap(), std::u64::MAX);

            // Free metric.
            assert!(state.free_value(block.index()).is_ok());
        }
        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<Block<&[u8]>> = snapshot.scan().collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[test]
    fn test_double_metric() {
        let core_state = get_state(4096);

        // Creates with value
        let block = {
            let mut state = core_state.try_lock().expect("lock state");
            let block = state.create_double_metric("test", 3.0, 0).unwrap();
            assert_eq!(block.block_type(), BlockType::DoubleValue);
            assert_eq!(block.index(), 1);
            assert_eq!(block.double_value().unwrap(), 3.0);
            assert_eq!(block.name_index().unwrap(), 2);
            assert_eq!(block.parent_index().unwrap(), 0);

            let name_block = state.heap().get_block(2).unwrap();
            assert_eq!(name_block.block_type(), BlockType::Name);
            assert_eq!(name_block.name_length().unwrap(), 4);
            assert_eq!(name_block.name_contents().unwrap(), "test");
            block
        };

        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<Block<&[u8]>> = snapshot.scan().collect();
        assert_eq!(blocks.len(), 10);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::DoubleValue);
        assert_eq!(blocks[2].block_type(), BlockType::Name);
        assert!(blocks[3..].iter().all(|b| b.block_type() == BlockType::Free));

        {
            let mut state = core_state.try_lock().expect("lock state");
            assert!(state.add_double_metric(block.index(), 10.5).is_ok());
            assert_eq!(block.double_value().unwrap(), 13.5);

            assert!(state.subtract_double_metric(block.index(), 5.1).is_ok());
            assert_eq!(block.double_value().unwrap(), 8.4);

            assert!(state.set_double_metric(block.index(), -6.0).is_ok());
            assert_eq!(block.double_value().unwrap(), -6.0);
            assert_eq!(state.get_double_metric(block.index()).unwrap(), -6.0);

            // Free metric.
            assert!(state.free_value(block.index()).is_ok());
        }
        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<Block<&[u8]>> = snapshot.scan().collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[test]
    fn test_string_property() {
        let core_state = get_state(4096);
        let block = {
            let mut state = core_state.try_lock().expect("lock state");

            // Creates with value
            let block =
                state.create_property("test", b"test-property", PropertyFormat::String, 0).unwrap();
            assert_eq!(block.block_type(), BlockType::BufferValue);
            assert_eq!(block.index(), 1);
            assert_eq!(block.parent_index().unwrap(), 0);
            assert_eq!(block.name_index().unwrap(), 2);
            assert_eq!(block.property_total_length().unwrap(), 13);
            assert_eq!(block.property_format().unwrap(), PropertyFormat::String);

            let name_block = state.heap().get_block(2).unwrap();
            assert_eq!(name_block.block_type(), BlockType::Name);
            assert_eq!(name_block.name_length().unwrap(), 4);
            assert_eq!(name_block.name_contents().unwrap(), "test");

            let extent_block = state.heap().get_block(4).unwrap();
            assert_eq!(extent_block.block_type(), BlockType::Extent);
            assert_eq!(extent_block.next_extent().unwrap(), 0);
            assert_eq!(
                String::from_utf8(extent_block.extent_contents().unwrap()).unwrap(),
                "test-property\0\0\0\0\0\0\0\0\0\0\0"
            );
            block
        };

        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<Block<&[u8]>> = snapshot.scan().collect();
        assert_eq!(blocks.len(), 11);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::BufferValue);
        assert_eq!(blocks[2].block_type(), BlockType::Name);
        assert_eq!(blocks[3].block_type(), BlockType::Free);
        assert_eq!(blocks[4].block_type(), BlockType::Extent);
        assert!(blocks[5..].iter().all(|b| b.block_type() == BlockType::Free));

        {
            let mut state = core_state.try_lock().expect("lock state");
            // Free property.
            assert!(state.free_property(block.index()).is_ok());
        }
        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<Block<&[u8]>> = snapshot.scan().collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[test]
    fn test_bytevector_property() {
        let core_state = get_state(4096);

        // Creates with value
        let block = {
            let mut state = core_state.try_lock().expect("lock state");
            let block =
                state.create_property("test", b"test-property", PropertyFormat::Bytes, 0).unwrap();
            assert_eq!(block.block_type(), BlockType::BufferValue);
            assert_eq!(block.index(), 1);
            assert_eq!(block.parent_index().unwrap(), 0);
            assert_eq!(block.name_index().unwrap(), 2);
            assert_eq!(block.property_total_length().unwrap(), 13);
            assert_eq!(block.property_extent_index().unwrap(), 4);
            assert_eq!(block.property_format().unwrap(), PropertyFormat::Bytes);

            let name_block = state.heap().get_block(2).unwrap();
            assert_eq!(name_block.block_type(), BlockType::Name);
            assert_eq!(name_block.name_length().unwrap(), 4);
            assert_eq!(name_block.name_contents().unwrap(), "test");

            let extent_block = state.heap().get_block(4).unwrap();
            assert_eq!(extent_block.block_type(), BlockType::Extent);
            assert_eq!(extent_block.next_extent().unwrap(), 0);
            assert_eq!(
                String::from_utf8(extent_block.extent_contents().unwrap()).unwrap(),
                "test-property\0\0\0\0\0\0\0\0\0\0\0"
            );
            block
        };

        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<Block<&[u8]>> = snapshot.scan().collect();
        assert_eq!(blocks.len(), 11);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::BufferValue);
        assert_eq!(blocks[2].block_type(), BlockType::Name);
        assert_eq!(blocks[3].block_type(), BlockType::Free);
        assert_eq!(blocks[4].block_type(), BlockType::Extent);
        assert!(blocks[5..].iter().all(|b| b.block_type() == BlockType::Free));

        // Free property.
        {
            let mut state = core_state.try_lock().expect("lock state");
            assert!(state.free_property(block.index()).is_ok());
        }
        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<Block<&[u8]>> = snapshot.scan().collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[test]
    fn test_bool() {
        let core_state = get_state(4096);
        let block = {
            let mut state = core_state.try_lock().expect("lock state");

            // Creates with value
            let block = state.create_bool("test", true, 0).unwrap();
            assert_eq!(block.block_type(), BlockType::BoolValue);
            assert_eq!(block.index(), 1);
            assert_eq!(block.bool_value().unwrap(), true);
            assert_eq!(block.name_index().unwrap(), 2);
            assert_eq!(block.parent_index().unwrap(), 0);

            let name_block = state.heap().get_block(2).unwrap();
            assert_eq!(name_block.block_type(), BlockType::Name);
            assert_eq!(name_block.name_length().unwrap(), 4);
            assert_eq!(name_block.name_contents().unwrap(), "test");
            block
        };

        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<Block<&[u8]>> = snapshot.scan().collect();
        assert_eq!(blocks.len(), 10);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::BoolValue);
        assert_eq!(blocks[2].block_type(), BlockType::Name);
        assert!(blocks[3..].iter().all(|b| b.block_type() == BlockType::Free));

        // Free metric.
        {
            let mut state = core_state.try_lock().expect("lock state");
            assert!(state.free_value(block.index()).is_ok());
        }
        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<Block<&[u8]>> = snapshot.scan().collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[test]
    fn test_int_array() {
        let core_state = get_state(4096);
        let block = {
            let mut state = core_state.try_lock().expect("lock state");
            let block = state.create_int_array("test", 5, ArrayFormat::Default, 0).unwrap();
            assert_eq!(block.block_type(), BlockType::ArrayValue);
            assert_eq!(block.order(), 2);
            assert_eq!(block.index(), 4);
            assert_eq!(block.name_index().unwrap(), 1);
            assert_eq!(block.parent_index().unwrap(), 0);
            assert_eq!(block.array_slots().unwrap(), 5);
            assert_eq!(block.array_format().unwrap(), ArrayFormat::Default);
            assert_eq!(block.array_entry_type().unwrap(), BlockType::IntValue);

            let name_block = state.heap().get_block(1).unwrap();
            assert_eq!(name_block.block_type(), BlockType::Name);
            assert_eq!(name_block.name_length().unwrap(), 4);
            assert_eq!(name_block.name_contents().unwrap(), "test");

            for i in 0..5 {
                state.set_array_int_slot(block.index(), i, 3 * i as i64).unwrap();
            }
            for i in 0..5 {
                assert_eq!(block.array_get_int_slot(i).unwrap(), 3 * i as i64);
            }
            block
        };

        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<Block<&[u8]>> = snapshot.scan().collect();
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::Name);
        assert_eq!(blocks[2].block_type(), BlockType::Free);
        assert_eq!(blocks[3].block_type(), BlockType::ArrayValue);
        assert!(blocks[4..].iter().all(|b| b.block_type() == BlockType::Free));

        // Free the array.
        {
            let mut state = core_state.try_lock().expect("lock state");
            assert!(state.free_value(block.index()).is_ok());
        }
        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<Block<&[u8]>> = snapshot.scan().collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[test]
    fn test_multi_extent_property() {
        let core_state = get_state(10000);
        let block = {
            let mut state = core_state.try_lock().expect("lock state");

            let chars = ['a', 'b', 'c', 'd', 'e', 'f', 'g'];
            let data = chars.iter().cycle().take(6000).collect::<String>();
            let block =
                state.create_property("test", data.as_bytes(), PropertyFormat::String, 0).unwrap();
            assert_eq!(block.block_type(), BlockType::BufferValue);
            assert_eq!(block.index(), 1);
            assert_eq!(block.parent_index().unwrap(), 0);
            assert_eq!(block.name_index().unwrap(), 2);
            assert_eq!(block.property_total_length().unwrap(), 6000);
            assert_eq!(block.property_extent_index().unwrap(), 128);
            assert_eq!(block.property_format().unwrap(), PropertyFormat::String);

            let name_block = state.heap().get_block(2).unwrap();
            assert_eq!(name_block.block_type(), BlockType::Name);
            assert_eq!(name_block.name_length().unwrap(), 4);
            assert_eq!(name_block.name_contents().unwrap(), "test");

            let extent_block = state.heap().get_block(128).unwrap();
            assert_eq!(extent_block.block_type(), BlockType::Extent);
            assert_eq!(extent_block.order(), 7);
            assert_eq!(extent_block.next_extent().unwrap(), 256);
            assert_eq!(
                extent_block.extent_contents().unwrap(),
                chars.iter().cycle().take(2040).map(|&c| c as u8).collect::<Vec<u8>>()
            );

            let extent_block = state.heap().get_block(256).unwrap();
            assert_eq!(extent_block.block_type(), BlockType::Extent);
            assert_eq!(extent_block.order(), 7);
            assert_eq!(extent_block.next_extent().unwrap(), 384);
            assert_eq!(
                extent_block.extent_contents().unwrap(),
                chars.iter().cycle().skip(2040).take(2040).map(|&c| c as u8).collect::<Vec<u8>>()
            );

            let extent_block = state.heap().get_block(384).unwrap();
            assert_eq!(extent_block.block_type(), BlockType::Extent);
            assert_eq!(extent_block.order(), 7);
            assert_eq!(extent_block.next_extent().unwrap(), 0);
            assert_eq!(
                extent_block.extent_contents().unwrap()[..1920],
                chars.iter().cycle().skip(4080).take(1920).map(|&c| c as u8).collect::<Vec<u8>>()[..]
            );
            assert_eq!(extent_block.extent_contents().unwrap()[1920..], [0u8; 120][..]);
            block
        };

        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<Block<&[u8]>> = snapshot.scan().collect();
        assert_eq!(blocks.len(), 12);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::BufferValue);
        assert_eq!(blocks[2].block_type(), BlockType::Name);
        assert!(blocks[3..9].iter().all(|b| b.block_type() == BlockType::Free));
        assert_eq!(blocks[9].block_type(), BlockType::Extent);
        assert_eq!(blocks[10].block_type(), BlockType::Extent);
        assert_eq!(blocks[11].block_type(), BlockType::Extent);

        // Free property.
        {
            let mut state = core_state.try_lock().expect("lock state");
            assert!(state.free_property(block.index()).is_ok());
        }
        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<Block<&[u8]>> = snapshot.scan().collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[test]
    fn test_tombstone() {
        let core_state = get_state(4096);
        let child_block = {
            let mut state = core_state.try_lock().expect("lock state");

            // Create a node value and verify its fields
            let block = state.create_node("root-node", 0).unwrap();
            let child_block = state.create_node("child-node", block.index()).unwrap();

            // Node still has children, so will become a tombstone.
            assert!(state.free_value(block.index()).is_ok());
            child_block
        };

        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<Block<&[u8]>> = snapshot.scan().collect();
        assert!(blocks[0].block_type() == BlockType::Header);
        assert!(blocks[1].block_type() == BlockType::Tombstone);
        assert!(blocks[2].block_type() == BlockType::Free);
        assert!(blocks[3].block_type() == BlockType::NodeValue);
        assert!(blocks[4].block_type() == BlockType::Free);
        assert!(blocks[5].block_type() == BlockType::Name);
        assert!(blocks[6..].iter().all(|b| b.block_type() == BlockType::Free));

        // Freeing the child, causes all blocks to be freed.
        {
            let mut state = core_state.try_lock().expect("lock state");
            assert!(state.free_value(child_block.index()).is_ok());
        }
        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<Block<&[u8]>> = snapshot.scan().collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[test]
    fn test_with_header_lock() {
        let state = get_state(4096);
        // Initial generation count is 0
        assert_eq!(state.header.header_generation_count().unwrap(), 0);

        // Lock the state
        let mut lock_guard = state.try_lock().expect("lock state");
        assert!(state.header.check_locked(true).is_ok());
        assert_eq!(state.header.header_generation_count().unwrap(), 1);
        // Operations on the lock  guard do not change the generation counter.
        let _ = lock_guard.create_node("test", 0).unwrap();
        let _ = lock_guard.create_node("test2", 1).unwrap();
        assert_eq!(state.header.header_generation_count().unwrap(), 1);

        // Dropping the guard releases the lock.
        drop(lock_guard);
        assert_eq!(state.header.header_generation_count().unwrap(), 2);
        assert!(state.header.check_locked(false).is_ok());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_link() {
        // Intialize state and create a link block.
        let state = get_state(4096);
        let block = {
            let mut state_guard = state.try_lock().expect("lock state");
            let block = state_guard
                .create_lazy_node("link-name", 0, LinkNodeDisposition::Inline, || {
                    async move {
                        let inspector = Inspector::new();
                        inspector.root().record_uint("a", 1);
                        Ok(inspector)
                    }
                    .boxed()
                })
                .unwrap();

            // Verify the callback was properly saved.
            assert!(state_guard.callbacks().get("link-name-0").is_some());
            let callback = state_guard.callbacks().get("link-name-0").unwrap();
            match callback().await {
                Ok(inspector) => {
                    let hierarchy =
                        PartialNodeHierarchy::try_from(&*inspector.vmo.unwrap()).unwrap();
                    assert_inspect_tree!(hierarchy, root: {
                        a: 1u64,
                    });
                }
                Err(_) => assert!(false),
            }

            // Verify link block.
            assert_eq!(block.block_type(), BlockType::LinkValue);
            assert_eq!(block.index(), 1);
            assert_eq!(block.parent_index().unwrap(), 0);
            assert_eq!(block.name_index().unwrap(), 2);
            assert_eq!(block.link_content_index().unwrap(), 4);
            assert_eq!(block.link_node_disposition().unwrap(), LinkNodeDisposition::Inline);

            // Verify link's name block.
            let name_block = state_guard.heap().get_block(2).unwrap();
            assert_eq!(name_block.block_type(), BlockType::Name);
            assert_eq!(name_block.name_length().unwrap(), 9);
            assert_eq!(name_block.name_contents().unwrap(), "link-name");

            // Verify link's content block.
            let content_block = state_guard.heap().get_block(4).unwrap();
            assert_eq!(content_block.block_type(), BlockType::Name);
            assert_eq!(content_block.name_length().unwrap(), 11);
            assert_eq!(content_block.name_contents().unwrap(), "link-name-0");
            block
        };

        // Verfiy all the VMO blocks.
        let snapshot = Snapshot::try_from(state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<Block<&[u8]>> = snapshot.scan().collect();
        assert_eq!(blocks.len(), 10);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::LinkValue);
        assert_eq!(blocks[2].block_type(), BlockType::Name);
        assert_eq!(blocks[3].block_type(), BlockType::Name);
        assert!(blocks[4..].iter().all(|b| b.block_type() == BlockType::Free));

        // Free link
        {
            let mut state_guard = state.try_lock().expect("lock state");
            assert!(state_guard.free_lazy_node(block.index()).is_ok());

            // Verify the callback was cleared on free link.
            assert!(state_guard.callbacks().get("link-name-0").is_none());
        }
        let snapshot = Snapshot::try_from(state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<Block<&[u8]>> = snapshot.scan().collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));

        // Verify adding another link generates a different ID regardless of the params.
        let mut state_guard = state.try_lock().expect("lock state");
        state_guard
            .create_lazy_node("link-name", 0, LinkNodeDisposition::Inline, || {
                async move { Ok(Inspector::new()) }.boxed()
            })
            .unwrap();
        let content_block = state_guard.heap().get_block(4).unwrap();
        assert_eq!(content_block.name_contents().unwrap(), "link-name-1");
    }

    fn get_state(size: usize) -> State {
        let (mapping, _) = Mapping::allocate(size).unwrap();
        let heap = Heap::new(Arc::new(mapping)).unwrap();
        State::create(heap).unwrap()
    }
}
