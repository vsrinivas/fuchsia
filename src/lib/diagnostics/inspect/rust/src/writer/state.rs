// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::writer::{error::Error, heap::Heap, Inspector, StringReference},
    anyhow,
    derivative::Derivative,
    fuchsia_zircon as zx,
    futures::future::BoxFuture,
    inspect_format::{
        constants, utils, BlockType, Error as FormatError,
        {ArrayFormat, Block, LinkNodeDisposition, PropertyFormat},
    },
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
            pub fn [<create_ $name _metric>]<'b>(
                &mut self,
                name: impl Into<StringReference<'b>>,
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
            fn [<create_ $name _metric>]<'b>(
                &mut self,
                name: impl Into<StringReference<'b>>,
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
            pub fn [<create_ $name _array>]<'b>(
                &mut self,
                name: impl Into<StringReference<'b>>,
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

macro_rules! arithmetic_array_fns {
    ($name:ident, $type:ident, $value:ident) => {
        paste::paste! {
            pub fn [<create_ $name _array>]<'b>(
                &mut self,
                name: impl Into<StringReference<'b>>,
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
/// `State` writes version 2 of the Inspect Format.
#[derive(Clone, Debug)]
pub struct State {
    /// A reference to the header block in the VMO.
    pub(crate) header: Block<Arc<Mapping>>,

    /// The inner state that actually performs the operations.
    /// This should always be accessed by locking the mutex and then locking the header.
    // TODO(fxbug.dev/51298): have a single locking mechanism implemented on top of the vmo header.
    inner: Arc<Mutex<InnerState>>,
}

impl PartialEq for State {
    fn eq(&self, other: &Self) -> bool {
        Arc::ptr_eq(&self.inner, &other.inner)
    }
}

impl State {
    /// Create a |State| object wrapping the given Heap. This will cause the
    /// heap to be initialized with a header.
    pub fn create(mut heap: Heap, vmo: Arc<zx::Vmo>) -> Result<Self, Error> {
        let mut block = heap.allocate_block(16)?;
        block.become_header()?;
        let inner = Arc::new(Mutex::new(InnerState::new(heap, vmo)));
        Ok(Self { inner, header: block })
    }

    /// Locks the state mutex and inspect vmo. The state will be unlocked on drop.
    /// This can fail when the header is already locked.
    pub fn try_lock<'a>(&'a self) -> Result<LockedStateGuard<'a>, Error> {
        let inner_lock = self.inner.lock();
        LockedStateGuard::new(&self.header, inner_lock)
    }

    /// Locks the state mutex and inspect vmo. The state will be unlocked on drop.
    /// This can fail when the header is already locked.
    pub fn begin_transaction(&self) {
        let mut inner_lock = self.inner.lock();
        if inner_lock.transaction_count == 0 {
            debug_assert!(self.header.check_locked(false).is_ok());
            let res = self.header.lock_header();
            debug_assert!(res.is_ok());
        }
        inner_lock.transaction_count += 1;
    }

    /// Locks the state mutex and inspect vmo. The state will be unlocked on drop.
    /// This can fail when the header is already locked.
    pub fn end_transaction(&self) {
        let mut inner_lock = self.inner.lock();
        inner_lock.transaction_count -= 1;
        if inner_lock.transaction_count == 0 {
            debug_assert!(self.header.check_locked(true).is_ok());
            let res = self.header.unlock_header();
            debug_assert!(res.is_ok());
        }
    }

    /// Copies the bytes in the VMO into the returned vector.
    pub fn copy_vmo_bytes(&self) -> Vec<u8> {
        let state = self.inner.lock();
        state.heap.bytes()
    }
}

/// Statistics about the current inspect state.
#[derive(Debug, Eq, PartialEq)]
pub struct Stats {
    /// Number of lazy links (lazy children and values) that have been added to the state.
    pub total_dynamic_children: usize,

    /// Maximum size of the vmo backing inspect.
    pub maximum_size: usize,

    /// Current size of the vmo backing inspect.
    pub current_size: usize,

    /// Total number of allocated blocks. This includes blocks that might have already been
    /// deallocated. That is, `allocated_blocks` - `deallocated_blocks` = currently allocated.
    pub allocated_blocks: usize,

    /// Total number of deallocated blocks.
    pub deallocated_blocks: usize,

    /// Total number of failed allocations.
    pub failed_allocations: usize,
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
        if inner_lock.transaction_count == 0 {
            header.lock_header()?;
        }
        Ok(Self { header, inner_lock })
    }

    /// Freezes the VMO, does a CoW duplication, thaws the parent, and returns the child.
    pub fn frozen_vmo_copy(&mut self) -> Result<Option<zx::Vmo>, Error> {
        self.inner_lock.frozen_vmo_copy(self.header)
    }

    /// Returns statistics about the current inspect state.
    pub fn stats(&self) -> Stats {
        Stats {
            total_dynamic_children: self.inner_lock.callbacks.len(),
            current_size: self.inner_lock.heap.current_size(),
            maximum_size: self.inner_lock.heap.maximum_size(),
            allocated_blocks: self.inner_lock.heap.total_allocated_blocks(),
            deallocated_blocks: self.inner_lock.heap.total_deallocated_blocks(),
            failed_allocations: self.inner_lock.heap.failed_allocations(),
        }
    }

    /// Returns a reference to the lazy callbacks map.
    pub fn callbacks(&self) -> &HashMap<String, LazyNodeContextFnArc> {
        &self.inner_lock.callbacks
    }

    /// Allocate a NODE block with the given |name| and |parent_index|.
    pub fn create_node<'b>(
        &mut self,
        name: impl Into<StringReference<'b>>,
        parent_index: u32,
    ) -> Result<Block<Arc<Mapping>>, Error> {
        self.inner_lock.create_node(name, parent_index)
    }

    /// Allocate a LINK block with the given |name| and |parent_index| and keep track
    /// of the callback that will fill it.
    pub fn create_lazy_node<'b, F>(
        &mut self,
        name: impl Into<StringReference<'b>>,
        parent_index: u32,
        disposition: LinkNodeDisposition,
        callback: F,
    ) -> Result<Block<Arc<Mapping>>, Error>
    where
        F: Fn() -> BoxFuture<'static, Result<Inspector, anyhow::Error>> + Sync + Send + 'static,
    {
        self.inner_lock.create_lazy_node(name, parent_index, disposition, callback)
    }

    pub fn free_lazy_node(&mut self, index: u32) -> Result<(), Error> {
        self.inner_lock.free_lazy_node(index)
    }

    /// Free a *_VALUE block at the given |index|.
    pub fn free_value(&mut self, index: u32) -> Result<(), Error> {
        self.inner_lock.free_value(index)
    }

    /// Allocate a PROPERTY block with the given |name|, |value| and |parent_index|.
    pub fn create_property<'b>(
        &mut self,
        name: impl Into<StringReference<'b>>,
        value: &[u8],
        format: PropertyFormat,
        parent_index: u32,
    ) -> Result<Block<Arc<Mapping>>, Error> {
        self.inner_lock.create_property(name, value, format, parent_index)
    }

    pub fn reparent(&self, being_reparented: u32, new_parent: u32) -> Result<(), Error> {
        self.inner_lock.reparent(being_reparented, new_parent)
    }

    /// Allocate a STRING_REFERENCE block and necessary EXTENTs.
    #[cfg(test)]
    pub fn get_or_create_string_reference<'b>(
        &mut self,
        value: impl Into<StringReference<'b>>,
    ) -> Result<Block<Arc<Mapping>>, Error> {
        self.inner_lock.get_or_create_string_reference(value)
    }

    /// Free a STRING_REFERENCE block and necessary EXTENTs.
    #[cfg(test)]
    pub fn maybe_free_string_reference(&mut self, block: Block<Arc<Mapping>>) -> Result<(), Error> {
        self.inner_lock.maybe_free_string_reference(block)
    }

    #[cfg(test)]
    pub fn load_string(&mut self, index: u32) -> Result<String, Error> {
        self.inner_lock.load_key_string(index)
    }

    /// Free a PROPERTY block.
    pub fn free_property(&mut self, index: u32) -> Result<(), Error> {
        self.inner_lock.free_property(index)
    }

    /// Set the |value| of a String PROPERTY block.
    pub fn set_property(&mut self, block_index: u32, value: &[u8]) -> Result<(), Error> {
        self.inner_lock.set_property(block_index, value)
    }

    pub fn create_bool<'b>(
        &mut self,
        name: impl Into<StringReference<'b>>,
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

    pub fn create_string_array<'b>(
        &mut self,
        name: impl Into<StringReference<'b>>,
        slots: usize,
        parent_index: u32,
    ) -> Result<Block<Arc<Mapping>>, Error> {
        self.inner_lock.create_string_array(name, slots, parent_index)
    }

    pub fn set_array_string_slot<'b>(
        &mut self,
        block_index: u32,
        slot_index: usize,
        value: impl Into<StringReference<'b>>,
    ) -> Result<(), Error> {
        self.inner_lock.set_array_string_slot(block_index, slot_index, value)
    }

    #[cfg(test)]
    pub fn allocate_link<'b>(
        &mut self,
        name: impl Into<StringReference<'b>>,
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
        if self.inner_lock.transaction_count == 0 {
            self.header.unlock_header().unwrap_or_else(|e| {
                error!(?e, "Failed to unlock header");
            });
        }
    }
}

/// Wraps a heap and implements the Inspect VMO API on top of it at a low level.
#[derive(Derivative)]
#[derivative(Debug)]
struct InnerState {
    heap: Heap,
    vmo: Arc<zx::Vmo>,
    next_unique_link_id: AtomicU64,
    transaction_count: usize,

    // associates a reference ID with it's block index
    string_reference_ids: HashMap<usize, u32>,

    #[derivative(Debug = "ignore")]
    callbacks: HashMap<String, LazyNodeContextFnArc>,
}

impl InnerState {
    /// Creates a new inner state that performs all operations on the heap.
    pub fn new(heap: Heap, vmo: Arc<zx::Vmo>) -> Self {
        Self {
            heap,
            vmo,
            next_unique_link_id: AtomicU64::new(0),
            callbacks: HashMap::new(),
            transaction_count: 0,
            string_reference_ids: HashMap::new(),
        }
    }

    fn frozen_vmo_copy(&self, header: &Block<Arc<Mapping>>) -> Result<Option<zx::Vmo>, Error> {
        let old = header.freeze_header()?;
        let ret = self
            .vmo
            .create_child(
                zx::VmoChildOptions::SNAPSHOT | zx::VmoChildOptions::NO_WRITE,
                0,
                self.vmo.get_size().map_err(|status| Error::VmoSize(status))?,
            )
            .ok();
        header.thaw_header(old)?;
        Ok(ret)
    }

    /// Allocate a NODE block with the given |name| and |parent_index|.
    fn create_node<'b>(
        &mut self,
        name: impl Into<StringReference<'b>>,
        parent_index: u32,
    ) -> Result<Block<Arc<Mapping>>, Error> {
        let (block, name_block) =
            self.allocate_reserved_value(name, parent_index, constants::MIN_ORDER_SIZE)?;
        block.become_node(name_block.index(), parent_index)?;
        Ok(block)
    }

    /// Allocate a LINK block with the given |name| and |parent_index| and keep track
    /// of the callback that will fill it.
    fn create_lazy_node<'b, F>(
        &mut self,
        name: impl Into<StringReference<'b>>,
        parent_index: u32,
        disposition: LinkNodeDisposition,
        callback: F,
    ) -> Result<Block<Arc<Mapping>>, Error>
    where
        F: Fn() -> BoxFuture<'static, Result<Inspector, anyhow::Error>> + Sync + Send + 'static,
    {
        let converted = name.into();
        let content = self.unique_link_name(converted.data());
        let link = self.allocate_link(&converted, &content, disposition, parent_index)?;
        self.callbacks.insert(content, Arc::from(callback));
        Ok(link)
    }

    /// Frees a LINK block at the given |index|.
    fn free_lazy_node(&mut self, index: u32) -> Result<(), Error> {
        let block = self.heap.get_block(index)?;
        let content_block = self.heap.get_block(block.link_content_index()?)?;
        let content = self.load_key_string(content_block.index())?;
        self.delete_value(block)?;
        // Free the name or string reference block used for content.
        match content_block.block_type() {
            BlockType::StringReference => {
                self.release_string_reference(content_block)?;
            }
            _ => {
                self.heap.free_block(content_block).expect("Failed to free block");
            }
        }

        self.callbacks.remove(&content);
        Ok(())
    }

    fn unique_link_name(&mut self, prefix: &str) -> String {
        let id = self.next_unique_link_id.fetch_add(1, Ordering::Relaxed);
        format!("{}-{}", prefix, id)
    }

    pub(in crate) fn allocate_link<'b>(
        &mut self,
        name: impl Into<StringReference<'b>>,
        content: &str,
        disposition: LinkNodeDisposition,
        parent_index: u32,
    ) -> Result<Block<Arc<Mapping>>, Error> {
        let (value_block, name_block) =
            self.allocate_reserved_value(name, parent_index, constants::MIN_ORDER_SIZE)?;
        let result = self.get_or_create_string_reference(content).and_then(|content_block| {
            content_block.increment_string_reference_count()?;
            value_block.become_link(
                name_block.index(),
                parent_index,
                content_block.index(),
                disposition,
            )?;
            Ok(())
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
    fn create_property<'b>(
        &mut self,
        name: impl Into<StringReference<'b>>,
        value: &[u8],
        format: PropertyFormat,
        parent_index: u32,
    ) -> Result<Block<Arc<Mapping>>, Error> {
        let (block, name_block) =
            self.allocate_reserved_value(name, parent_index, constants::MIN_ORDER_SIZE)?;
        block.become_property(name_block.index(), parent_index, format)?;
        if let Err(err) = self.inner_set_property_value(&block, &value) {
            self.heap.free_block(block)?;
            self.release_string_reference(name_block)?;
            return Err(err);
        }
        Ok(block)
    }

    /// Get or allocate a STRING_REFERENCE block with the given |value|.
    /// When a new string reference is created, its reference count is set to zero.
    fn get_or_create_string_reference<'b>(
        &mut self,
        value: impl Into<StringReference<'b>>,
    ) -> Result<Block<Arc<Mapping>>, Error> {
        let string_reference = value.into();
        match self.string_reference_ids.get(&string_reference.id()) {
            None => {
                let block = self.heap.allocate_block(utils::block_size_for_payload(
                    string_reference.data().len() + constants::STRING_REFERENCE_TOTAL_LENGTH_BYTES,
                ))?;
                block.become_string_reference()?;
                self.write_string_reference_payload(&block, string_reference.data())?;
                self.string_reference_ids.insert(string_reference.id(), block.index());
                Ok(block)
            }

            Some(index) => Ok(self.heap.get_block(*index)?),
        }
    }

    /// Given a string, write the canonical value out, allocating as needed.
    fn write_string_reference_payload(
        &mut self,
        block: &Block<Arc<Mapping>>,
        value: &str,
    ) -> Result<(), Error> {
        let head_extent = match self.inline_string_reference(&block, value.as_bytes()) {
            Ok(Some(written)) => self.write_extents(&value.as_bytes()[written..])?,
            Ok(None) => 0,
            Err(e) => return Err(e),
        };

        block.set_extent_next_index(head_extent)?;
        block.set_total_length(value.as_bytes().len().to_u32().unwrap())?;
        Ok(())
    }

    /// Given a string, write the portion that can be inlined to the given block.
    /// If the data overflows, return the number of bytes written.
    fn inline_string_reference(
        &mut self,
        block: &Block<Arc<Mapping>>,
        value: &[u8],
    ) -> Result<Option<usize>, Error> {
        // only returns an error if you call with wrong block type
        let bytes_written = block.write_string_reference_inline(value)?;
        if bytes_written < value.len() {
            Ok(Some(bytes_written))
        } else {
            Ok(None)
        }
    }

    /// Decrement the reference count on the block and free it if the count is 0.
    /// This is the function to call if you want to give up your hold on a StringReference.
    fn release_string_reference(&mut self, block: Block<Arc<Mapping>>) -> Result<(), Error> {
        block.decrement_string_reference_count()?;
        self.maybe_free_string_reference(block)
    }

    /// Free a STRING_REFERENCE if the count is 0. This should not be
    /// directly called outside of tests.
    fn maybe_free_string_reference(&mut self, block: Block<Arc<Mapping>>) -> Result<(), Error> {
        if block.string_reference_count()? != 0 {
            return Ok(());
        }
        let first_extent = block.next_extent()?;
        let block_index = block.index();
        self.heap.free_block(block)?;

        let to_remove =
            self.string_reference_ids.iter().find(|(_, vmo_index)| **vmo_index == block_index);
        if let Some((id, _)) = to_remove {
            let id = *id;
            self.string_reference_ids.remove(&id);
        }

        if first_extent == 0 {
            return Ok(());
        }
        self.free_extents(first_extent)
    }

    fn load_key_string(&mut self, index: u32) -> Result<String, Error> {
        let block = self.heap.get_block(index)?;
        match block.block_type() {
            BlockType::StringReference => self.read_string_reference(block),
            BlockType::Name => block.name_contents().map_err(|_| Error::NameNotUtf8),
            wrong_type => Err(Error::InvalidBlockType(index as usize, wrong_type)),
        }
    }

    /// Read a StringReference
    fn read_string_reference(&mut self, block: Block<Arc<Mapping>>) -> Result<String, Error> {
        let mut content = block.inline_string_reference()?;
        let mut next = block.next_extent()?;
        while next != 0 {
            let next_block = self.heap.get_block(next)?;
            content.append(&mut next_block.extent_contents()?);
            next = next_block.next_extent()?;
        }

        content.truncate(block.total_length()?);
        Ok(String::from_utf8(content).ok().ok_or(Error::NameNotUtf8)?)
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

    fn check_lineage(&self, being_reparented: u32, new_parent: u32) -> Result<(), Error> {
        // you cannot adopt the root node
        if being_reparented == constants::ROOT_INDEX {
            return Err(Error::AdoptAncestor);
        }

        let mut being_checked = new_parent;
        while being_checked != constants::ROOT_INDEX {
            if being_checked == being_reparented {
                return Err(Error::AdoptAncestor);
            }

            being_checked = self.heap.get_block(being_checked)?.parent_index()?;
        }

        Ok(())
    }

    fn reparent(&self, being_reparented: u32, new_parent: u32) -> Result<(), Error> {
        self.check_lineage(being_reparented, new_parent)?;
        let being_reparented_block = self.heap.get_block(being_reparented)?;
        let original_parent_idx = being_reparented_block.parent_index()?;
        if original_parent_idx != constants::ROOT_INDEX {
            let original_parent_block = self.heap.get_block(original_parent_idx)?;
            let child_count = original_parent_block.child_count()? - 1;
            original_parent_block.set_child_count(child_count)?;
        }

        being_reparented_block.set_parent(new_parent)?;

        if new_parent != constants::ROOT_INDEX {
            let new_parent_block = self.heap.get_block(new_parent)?;
            let child_count = new_parent_block.child_count()? + 1;
            new_parent_block.set_child_count(child_count)?;
        }

        Ok(())
    }

    fn create_bool<'b>(
        &mut self,
        name: impl Into<StringReference<'b>>,
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

    arithmetic_array_fns!(int, i64, IntValue);
    arithmetic_array_fns!(uint, u64, UintValue);
    arithmetic_array_fns!(double, f64, DoubleValue);

    fn create_string_array<'b>(
        &mut self,
        name: impl Into<StringReference<'b>>,
        slots: usize,
        parent_index: u32,
    ) -> Result<Block<Arc<Mapping>>, Error> {
        // array_element_size will never fail for BlockType::StringReference.
        let block_size = slots as usize * BlockType::StringReference.array_element_size().unwrap()
            + constants::MIN_ORDER_SIZE;
        if block_size > constants::MAX_ORDER_SIZE {
            return Err(Error::BlockSizeTooBig(block_size));
        }
        let (block, name_block) = self.allocate_reserved_value(name, parent_index, block_size)?;
        block.become_array_value(
            slots,
            ArrayFormat::Default,
            BlockType::StringReference,
            name_block.index(),
            parent_index,
        )?;
        Ok(block)
    }

    fn set_array_string_slot<'b>(
        &mut self,
        block_index: u32,
        slot_index: usize,
        value: impl Into<StringReference<'b>>,
    ) -> Result<(), Error> {
        let block = self.heap.get_block(block_index)?;
        if block.array_slots()? <= slot_index {
            return Err(Error::VmoFormat(FormatError::ArrayIndexOutOfBounds(slot_index)));
        }

        let value = value.into();
        let reference_index = if value.data() != "" {
            let reference = self.get_or_create_string_reference(value)?;
            reference.increment_string_reference_count()?;
            reference.index()
        } else {
            constants::EMPTY_STRING_SLOT_INDEX
        };

        block.array_set_string_slot(slot_index, reference_index)?;
        Ok(())
    }

    /// Sets all slots of the array at the given index to zero.
    /// Does appropriate deallocation on string references in payload.
    fn clear_array(&mut self, block_index: u32, start_slot_index: usize) -> Result<(), Error> {
        let block = self.heap.get_block(block_index)?;
        match block.array_entry_type()? {
            value if value.is_numeric_value() => block.array_clear(start_slot_index)?,
            BlockType::StringReference => {
                for i in start_slot_index..block.array_slots()? {
                    let index = block.array_get_string_index_slot(i)?;
                    if index == constants::EMPTY_STRING_SLOT_INDEX {
                        continue;
                    }

                    block.array_set_string_slot(i, constants::EMPTY_STRING_SLOT_INDEX).unwrap();
                    let to_free = self.heap.get_block(index)?;
                    self.release_string_reference(to_free)?;
                }
            }

            _ => return Err(Error::InvalidArrayType(block.index())),
        }

        Ok(())
    }

    fn allocate_reserved_value<'b>(
        &mut self,
        name: impl Into<StringReference<'b>>,
        parent_index: u32,
        block_size: usize,
    ) -> Result<(Block<Arc<Mapping>>, Block<Arc<Mapping>>), Error> {
        let block = self.heap.allocate_block(block_size)?;
        let name_block = match self.get_or_create_string_reference(name) {
            Ok(b) => {
                b.increment_string_reference_count()?;
                b
            }
            Err(err) => {
                self.heap.free_block(block)?;
                return Err(err);
            }
        };

        let result = self.heap.get_block(parent_index).and_then(|parent_block| match parent_block
            .block_type()
        {
            BlockType::NodeValue | BlockType::Tombstone => {
                parent_block.set_child_count(parent_block.child_count().unwrap() + 1)?;
                Ok(())
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
                self.release_string_reference(name_block)?;
                self.heap.free_block(block)?;
                Err(err)
            }
        }
    }

    fn delete_value(&mut self, block: Block<Arc<Mapping>>) -> Result<(), Error> {
        // Decrement parent child count.
        let parent_index = block.parent_index()?;
        if parent_index != constants::ROOT_INDEX {
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
        match name.block_type() {
            BlockType::StringReference => {
                self.release_string_reference(name)?;
            }
            _ => self.heap.free_block(name).expect("Failed to free block"),
        }

        // If the block is a NODE and has children, make it a TOMBSTONE so that
        // it's freed when the last of its children is freed. Otherwise, free it.
        if block.block_type() == BlockType::NodeValue && block.child_count()? != 0 {
            block.become_tombstone()?;
        } else {
            self.heap.free_block(block)?;
        }
        Ok(())
    }

    fn inner_set_property_value(
        &mut self,
        block: &Block<Arc<Mapping>>,
        value: &[u8],
    ) -> Result<(), Error> {
        self.free_extents(block.property_extent_index()?)?;
        let extent_index = self.write_extents(value)?;
        block.set_total_length(value.len().to_u32().unwrap())?;
        block.set_property_extent_index(extent_index)?;
        Ok(())
    }

    fn free_extents(&mut self, head_extent_index: u32) -> Result<(), Error> {
        let mut index = head_extent_index;
        while index != constants::ROOT_INDEX {
            let block = self.heap.get_block(index).unwrap();
            index = block.next_extent()?;
            self.heap.free_block(block)?;
        }
        Ok(())
    }

    fn write_extents(&mut self, value: &[u8]) -> Result<u32, Error> {
        if value.len() == 0 {
            // Invalid index
            return Ok(constants::ROOT_INDEX);
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
    use crate::{
        assert_data_tree,
        reader::{
            snapshot::{ScannedBlock, Snapshot},
            PartialNodeHierarchy,
        },
        writer::testing_utils::get_state,
        Inspector,
    };
    use futures::prelude::*;
    use std::convert::TryFrom;

    #[fuchsia::test]
    fn test_create() {
        let state = get_state(4096);
        let snapshot = Snapshot::try_from(state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        assert_eq!(blocks.len(), 9);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[fuchsia::test]
    fn test_load_string() {
        let outer = get_state(4096);
        let mut state = outer.try_lock().expect("lock state");
        let block = state.get_or_create_string_reference("a value").unwrap();
        assert_eq!(state.load_string(block.index()).unwrap(), "a value");
    }

    #[fuchsia::test]
    fn test_check_lineage() {
        let core_state = get_state(4096);
        let mut state = core_state.try_lock().expect("lock state");
        let parent = state.create_node("", 0).unwrap();
        let child = state.create_node("", parent.index()).unwrap();
        let uncle = state.create_node("", 0).unwrap();

        state.inner_lock.check_lineage(parent.index(), child.index()).unwrap_err();
        state.inner_lock.check_lineage(0, child.index()).unwrap_err();
        state.inner_lock.check_lineage(child.index(), uncle.index()).unwrap();
    }

    #[fuchsia::test]
    fn test_reparent() {
        let core_state = get_state(4096);
        let mut state = core_state.try_lock().expect("lock state");

        let a = state.create_node("a", 0).unwrap();
        let b = state.create_node("b", 0).unwrap();

        assert_eq!(a.parent_index().unwrap(), 0);
        assert_eq!(b.parent_index().unwrap(), 0);

        assert_eq!(a.child_count().unwrap(), 0);
        assert_eq!(b.child_count().unwrap(), 0);

        state.reparent(b.index(), a.index()).unwrap();

        assert_eq!(a.parent_index().unwrap(), 0);
        assert_eq!(b.parent_index().unwrap(), a.index());

        assert_eq!(a.child_count().unwrap(), 1);
        assert_eq!(b.child_count().unwrap(), 0);

        let c = state.create_node("c", a.index()).unwrap();

        assert_eq!(a.parent_index().unwrap(), 0);
        assert_eq!(b.parent_index().unwrap(), a.index());
        assert_eq!(c.parent_index().unwrap(), a.index());

        assert_eq!(a.child_count().unwrap(), 2);
        assert_eq!(b.child_count().unwrap(), 0);
        assert_eq!(c.child_count().unwrap(), 0);

        state.reparent(c.index(), b.index()).unwrap();

        assert_eq!(a.parent_index().unwrap(), 0);
        assert_eq!(b.parent_index().unwrap(), a.index());
        assert_eq!(c.parent_index().unwrap(), b.index());

        assert_eq!(a.child_count().unwrap(), 1);
        assert_eq!(b.child_count().unwrap(), 1);
        assert_eq!(c.child_count().unwrap(), 0);
    }

    #[fuchsia::test]
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
            assert_eq!(name_block.block_type(), BlockType::StringReference);
            assert_eq!(name_block.total_length().unwrap(), 9);
            assert_eq!(name_block.order(), 1);
            assert_eq!(state.load_string(name_block.index()).unwrap(), "test-node");
            block
        };

        // Verify blocks.
        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        assert_eq!(blocks.len(), 9);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::NodeValue);
        assert_eq!(blocks[2].block_type(), BlockType::StringReference);
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
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[fuchsia::test]
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
            assert_eq!(name_block.block_type(), BlockType::StringReference);
            assert_eq!(name_block.total_length().unwrap(), 4);
            assert_eq!(state.load_string(name_block.index()).unwrap(), "test");
            block
        };

        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        assert_eq!(blocks.len(), 10);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::IntValue);
        assert_eq!(blocks[2].block_type(), BlockType::StringReference);
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
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[fuchsia::test]
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
            assert_eq!(name_block.block_type(), BlockType::StringReference);
            assert_eq!(name_block.total_length().unwrap(), 4);
            assert_eq!(state.load_string(name_block.index()).unwrap(), "test");

            block
        };
        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        assert_eq!(blocks.len(), 10);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::UintValue);
        assert_eq!(blocks[2].block_type(), BlockType::StringReference);
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
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[fuchsia::test]
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
            assert_eq!(name_block.block_type(), BlockType::StringReference);
            assert_eq!(name_block.total_length().unwrap(), 4);
            assert_eq!(state.load_string(name_block.index()).unwrap(), "test");
            block
        };

        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        assert_eq!(blocks.len(), 10);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::DoubleValue);
        assert_eq!(blocks[2].block_type(), BlockType::StringReference);
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
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[fuchsia::test]
    fn test_create_property_cleanup_on_failure() {
        // this implementation detail is important for the test below to be valid
        assert_eq!(constants::MAX_ORDER_SIZE, 2048);

        let core_state = get_state(5121); // large enough to fit to max size blocks plus 1024
        let mut state = core_state.try_lock().expect("lock state");
        // allocate a max size block and one extent
        let name: String = (0..3000).map(|_| " ").collect::<String>();
        // allocate a max size property + at least one extent
        // the extent won't fit into the VMO, causing allocation failure when the property
        // is set
        let payload = [0u8; 4096]; // won't fit into vmo

        // fails because the property is too big, but, allocates the name and should clean it up
        assert!(state.create_property(&name, &payload, PropertyFormat::Bytes, 0).is_err());

        drop(state);

        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();

        // if cleanup happened correctly, the name + extent and property + extent have been freed
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[fuchsia::test]
    fn test_string_reference_allocations() {
        let core_state = get_state(4096); // allocates HEADER
        {
            let mut state = core_state.try_lock().expect("lock state");
            let sf = StringReference::from("a reference-counted canonical name");
            assert_eq!(state.stats().allocated_blocks, 1);

            let mut collected = vec![];
            for _ in 0..100 {
                collected.push(state.create_node(&sf, 0).unwrap());
            }

            assert!(state.inner_lock.string_reference_ids.get(&(&sf).id()).is_some());

            assert_eq!(state.stats().allocated_blocks, 102);
            let sf_block = state.heap().get_block(collected[0].name_index().unwrap()).unwrap();
            assert_eq!(sf_block.string_reference_count().unwrap(), 100);

            collected.iter().for_each(|b| {
                assert!(state.inner_lock.string_reference_ids.get(&(&sf).id()).is_some());
                assert!(state.free_value(b.index()).is_ok())
            });

            assert!(state.inner_lock.string_reference_ids.get(&(&sf).id()).is_none());

            let node = state.create_node(&sf, 0).unwrap();
            assert!(state.inner_lock.string_reference_ids.get(&(&sf).id()).is_some());
            assert!(state.free_value(node.index()).is_ok());
            assert!(state.inner_lock.string_reference_ids.get(&(&sf).id()).is_none());
        }

        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[fuchsia::test]
    fn test_string_reference_data() {
        let core_state = get_state(4096); // allocates HEADER
        let mut state = core_state.try_lock().expect("lock state");

        {
            // 4 bytes (4 ASCII characters in UTF-8) will fit inlined with a minimum block size
            let block = state.get_or_create_string_reference("abcd").unwrap();
            assert_eq!(block.block_type(), BlockType::StringReference);
            assert_eq!(block.order(), 0);
            assert_eq!(state.stats().allocated_blocks, 2);
            assert_eq!(state.stats().deallocated_blocks, 0);
            assert_eq!(block.string_reference_count().unwrap(), 0);
            assert_eq!(block.total_length().unwrap(), 4);
            assert_eq!(block.next_extent().unwrap(), 0);
            assert_eq!(block.order(), 0);
            assert_eq!(state.load_string(block.index()).unwrap(), "abcd");

            state.maybe_free_string_reference(block).unwrap();
            assert_eq!(state.stats().deallocated_blocks, 1);
        }

        {
            let block = state.get_or_create_string_reference("longer").unwrap();
            assert_eq!(block.block_type(), BlockType::StringReference);
            assert_eq!(block.order(), 1);
            assert_eq!(block.string_reference_count().unwrap(), 0);
            assert_eq!(block.total_length().unwrap(), 6);
            assert_eq!(state.stats().allocated_blocks, 3);
            assert_eq!(state.stats().deallocated_blocks, 1);
            assert_eq!(state.load_string(block.index()).unwrap(), "longer");

            let idx = block.next_extent().unwrap();
            assert_eq!(idx, 0);

            state.maybe_free_string_reference(block).unwrap();
            assert_eq!(state.stats().deallocated_blocks, 2);
        }

        {
            let block = state.get_or_create_string_reference("longer").unwrap();
            let index = block.index();
            assert_eq!(block.order(), 1);
            block.increment_string_reference_count().unwrap();
            // not an error to try and free
            assert!(state.maybe_free_string_reference(block).is_ok());

            let block = state.heap().get_block(index).unwrap();
            block.decrement_string_reference_count().unwrap();
            state.maybe_free_string_reference(block).unwrap();
        }
    }

    #[fuchsia::test]
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
            assert_eq!(block.total_length().unwrap(), 13);
            assert_eq!(block.property_format().unwrap(), PropertyFormat::String);

            let name_block = state.heap().get_block(2).unwrap();
            assert_eq!(name_block.block_type(), BlockType::StringReference);
            assert_eq!(name_block.total_length().unwrap(), 4);
            assert_eq!(state.load_string(name_block.index()).unwrap(), "test");

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
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        assert_eq!(blocks.len(), 11);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::BufferValue);
        assert_eq!(blocks[2].block_type(), BlockType::StringReference);
        assert_eq!(blocks[3].block_type(), BlockType::Free);
        assert_eq!(blocks[4].block_type(), BlockType::Extent);
        assert!(blocks[5..].iter().all(|b| b.block_type() == BlockType::Free));

        {
            let mut state = core_state.try_lock().expect("lock state");
            // Free property.
            assert!(state.free_property(block.index()).is_ok());
        }
        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[fuchsia::test]
    fn test_string_arrays() {
        let core_state = get_state(4096);
        {
            let mut state = core_state.try_lock().expect("lock state");
            let array = state.create_string_array("array", 4, 0).unwrap();
            state.set_array_string_slot(array.index(), 0, "0").unwrap();
            state.set_array_string_slot(array.index(), 1, "1").unwrap();
            state.set_array_string_slot(array.index(), 2, "2").unwrap();
            state.set_array_string_slot(array.index(), 3, "3").unwrap();

            // size is 4
            assert!(state.set_array_string_slot(array.index(), 4, "").is_err());
            assert!(state.set_array_string_slot(array.index(), 5, "").is_err());

            for i in 0..4 {
                let idx = array.array_get_string_index_slot(i).unwrap();
                assert_eq!(i.to_string(), state.load_string(idx).unwrap());
            }

            assert!(array.array_get_string_index_slot(4).is_err());
            assert!(array.array_get_string_index_slot(5).is_err());

            state.clear_array(array.index(), 0).unwrap();
            state.free_value(array.index()).unwrap();
        }

        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[fuchsia::test]
    fn test_empty_value_string_arrays() {
        let core_state = get_state(4096);
        let mut state = core_state.try_lock().expect("lock state");
        let array = state.create_string_array("array", 4, 0).unwrap();

        state.set_array_string_slot(array.index(), 0, "").unwrap();
        state.set_array_string_slot(array.index(), 1, "").unwrap();
        state.set_array_string_slot(array.index(), 2, "").unwrap();
        state.set_array_string_slot(array.index(), 3, "").unwrap();

        drop(state);

        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let mut state = core_state.try_lock().expect("lock state");

        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        for b in blocks {
            if b.block_type() == BlockType::StringReference
                && state.load_string(b.index()).unwrap() == "array"
            {
                continue;
            }

            assert!(
                b.block_type() != BlockType::StringReference,
                "Got unexpected StringReference, index: {}, value (wrapped in single quotes): '{}'",
                b.index(),
                b.block_type()
            );
        }
    }

    #[fuchsia::test]
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
            assert_eq!(block.total_length().unwrap(), 13);
            assert_eq!(block.property_extent_index().unwrap(), 4);
            assert_eq!(block.property_format().unwrap(), PropertyFormat::Bytes);

            let name_block = state.heap().get_block(2).unwrap();
            assert_eq!(name_block.block_type(), BlockType::StringReference);
            assert_eq!(name_block.total_length().unwrap(), 4);
            assert_eq!(state.load_string(name_block.index()).unwrap(), "test");

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
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        assert_eq!(blocks.len(), 11);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::BufferValue);
        assert_eq!(blocks[2].block_type(), BlockType::StringReference);
        assert_eq!(blocks[3].block_type(), BlockType::Free);
        assert_eq!(blocks[4].block_type(), BlockType::Extent);
        assert!(blocks[5..].iter().all(|b| b.block_type() == BlockType::Free));

        // Free property.
        {
            let mut state = core_state.try_lock().expect("lock state");
            assert!(state.free_property(block.index()).is_ok());
        }
        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[fuchsia::test]
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
            assert_eq!(name_block.block_type(), BlockType::StringReference);
            assert_eq!(name_block.total_length().unwrap(), 4);
            assert_eq!(state.load_string(name_block.index()).unwrap(), "test");
            block
        };

        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        assert_eq!(blocks.len(), 10);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::BoolValue);
        assert_eq!(blocks[2].block_type(), BlockType::StringReference);
        assert!(blocks[3..].iter().all(|b| b.block_type() == BlockType::Free));

        // Free metric.
        {
            let mut state = core_state.try_lock().expect("lock state");
            assert!(state.free_value(block.index()).is_ok());
        }
        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[fuchsia::test]
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
            assert_eq!(name_block.block_type(), BlockType::StringReference);
            assert_eq!(name_block.total_length().unwrap(), 4);
            assert_eq!(state.load_string(name_block.index()).unwrap(), "test");

            for i in 0..5 {
                state.set_array_int_slot(block.index(), i, 3 * i as i64).unwrap();
            }
            for i in 0..5 {
                assert_eq!(block.array_get_int_slot(i).unwrap(), 3 * i as i64);
            }
            block
        };

        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::StringReference);
        assert_eq!(blocks[2].block_type(), BlockType::Free);
        assert_eq!(blocks[3].block_type(), BlockType::ArrayValue);
        assert!(blocks[4..].iter().all(|b| b.block_type() == BlockType::Free));

        // Free the array.
        {
            let mut state = core_state.try_lock().expect("lock state");
            assert!(state.free_value(block.index()).is_ok());
        }
        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[fuchsia::test]
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
            assert_eq!(block.total_length().unwrap(), 6000);
            assert_eq!(block.property_extent_index().unwrap(), 128);
            assert_eq!(block.property_format().unwrap(), PropertyFormat::String);

            let name_block = state.heap().get_block(2).unwrap();
            assert_eq!(name_block.block_type(), BlockType::StringReference);
            assert_eq!(name_block.total_length().unwrap(), 4);
            assert_eq!(state.load_string(name_block.index()).unwrap(), "test");

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
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        assert_eq!(blocks.len(), 12);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::BufferValue);
        assert_eq!(blocks[2].block_type(), BlockType::StringReference);
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
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[fuchsia::test]
    fn test_freeing_string_references() {
        let core_state = get_state(4096);
        {
            let mut state = core_state.try_lock().expect("lock state");
            assert_eq!(state.stats().allocated_blocks, 1);

            let block0 = state.create_node("abcd123456789", 0).unwrap();
            let block0_name = state.heap().get_block(block0.name_index().unwrap()).unwrap();
            assert_eq!(block0_name.order(), 1);
            assert_eq!(state.stats().allocated_blocks, 3);

            let block1 = state.get_or_create_string_reference("abcd").unwrap();
            assert_eq!(state.stats().allocated_blocks, 4);
            assert_eq!(block1.order(), 0);

            let block2 = state.get_or_create_string_reference("abcd123456789").unwrap();
            assert_eq!(block2.order(), 1);
            assert_eq!(state.stats().allocated_blocks, 5);

            let block3 = state.create_node("abcd123456789", 0).unwrap();
            let block3_name = state.heap().get_block(block3.name_index().unwrap()).unwrap();
            assert_eq!(block3_name.order(), 1);
            assert_eq!(block3.order(), 0);
            assert_eq!(state.stats().allocated_blocks, 7);

            let mut long_name = "".to_string();
            for _ in 0..3000 {
                long_name += " ";
            }

            let block4 = state.create_node(long_name, 0).unwrap();
            let block4_name = state.heap().get_block(block4.name_index().unwrap()).unwrap();
            assert_eq!(block4_name.order(), 7);
            assert!(block4_name.next_extent().unwrap() != 0);
            assert_eq!(state.stats().allocated_blocks, 10);

            assert!(state.maybe_free_string_reference(block1).is_ok());
            assert_eq!(state.stats().deallocated_blocks, 1);
            assert!(state.maybe_free_string_reference(block2).is_ok());
            assert_eq!(state.stats().deallocated_blocks, 2);
            assert!(state.free_value(block3.index()).is_ok());
            assert_eq!(state.stats().deallocated_blocks, 4);
            assert!(state.free_value(block4.index()).is_ok());
            assert_eq!(state.stats().deallocated_blocks, 7);
        }

        // Current expected layout of VMO:
        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();

        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::NodeValue);
        assert_eq!(blocks[2].block_type(), BlockType::StringReference);
        assert!(blocks[3..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[fuchsia::test]
    fn test_tombstone() {
        let core_state = get_state(4096);
        let child_block = {
            let mut state = core_state.try_lock().expect("lock state");

            // Create a node value and verify its fields
            let block = state.create_node("root-node", 0).unwrap();
            let block_name_as_string_ref =
                state.heap().get_block(block.name_index().unwrap()).unwrap();
            assert_eq!(block_name_as_string_ref.order(), 1);
            assert_eq!(state.stats().allocated_blocks, 3);
            assert_eq!(state.stats().deallocated_blocks, 0);

            let child_block = state.create_node("child-node", block.index()).unwrap();
            assert_eq!(state.stats().allocated_blocks, 5);
            assert_eq!(state.stats().deallocated_blocks, 0);

            // Node still has children, so will become a tombstone.
            assert!(state.free_value(block.index()).is_ok());
            assert_eq!(state.stats().allocated_blocks, 5);
            assert_eq!(state.stats().deallocated_blocks, 1);
            child_block
        };

        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();

        // Note that the way Extents get allocated means that they aren't necessarily
        // put in the buffer where it would seem they should based on the literal order of allocation.
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::Tombstone);
        assert_eq!(blocks[2].block_type(), BlockType::Free);
        assert_eq!(blocks[3].block_type(), BlockType::NodeValue);
        assert_eq!(blocks[4].block_type(), BlockType::Free);
        assert_eq!(blocks[5].block_type(), BlockType::StringReference);
        assert!(blocks[6..].iter().all(|b| b.block_type() == BlockType::Free));

        // Freeing the child, causes all blocks to be freed.
        {
            let mut state = core_state.try_lock().expect("lock state");
            assert!(state.free_value(child_block.index()).is_ok());
        }
        let snapshot = Snapshot::try_from(core_state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));
    }

    #[fuchsia::test]
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

    #[fuchsia::test]
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
                    assert_data_tree!(hierarchy, root: {
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
            assert_eq!(name_block.block_type(), BlockType::StringReference);
            assert_eq!(name_block.total_length().unwrap(), 9);
            assert_eq!(state_guard.load_string(name_block.index()).unwrap(), "link-name");

            // Verify link's content block.
            let content_block =
                state_guard.heap().get_block(block.link_content_index().unwrap()).unwrap();
            assert_eq!(content_block.block_type(), BlockType::StringReference);
            assert_eq!(content_block.total_length().unwrap(), 11);
            assert_eq!(state_guard.load_string(content_block.index()).unwrap(), "link-name-0");
            block
        };

        // Verfiy all the VMO blocks.
        let snapshot = Snapshot::try_from(state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        assert_eq!(blocks.len(), 10);
        assert_eq!(blocks[0].block_type(), BlockType::Header);
        assert_eq!(blocks[1].block_type(), BlockType::LinkValue);
        assert_eq!(blocks[2].block_type(), BlockType::StringReference);
        assert_eq!(blocks[3].block_type(), BlockType::StringReference);
        assert_eq!(blocks[4].block_type(), BlockType::Free);
        assert!(blocks[5..].iter().all(|b| b.block_type() == BlockType::Free));

        // Free link
        {
            let mut state_guard = state.try_lock().expect("lock state");
            assert!(state_guard.free_lazy_node(block.index()).is_ok());

            // Verify the callback was cleared on free link.
            assert!(state_guard.callbacks().get("link-name-0").is_none());
        }
        let snapshot = Snapshot::try_from(state.copy_vmo_bytes()).unwrap();
        let blocks: Vec<ScannedBlock<'_>> = snapshot.scan().collect();
        assert!(blocks[1..].iter().all(|b| b.block_type() == BlockType::Free));

        // Verify adding another link generates a different ID regardless of the params.
        let mut state_guard = state.try_lock().expect("lock state");
        state_guard
            .create_lazy_node("link-name", 0, LinkNodeDisposition::Inline, || {
                async move { Ok(Inspector::new()) }.boxed()
            })
            .unwrap();
        let content_block = state_guard.heap().get_block(4).unwrap();
        assert_eq!(state_guard.load_string(content_block.index()).unwrap(), "link-name-1");
    }

    #[fuchsia::test]
    async fn stats() {
        // Intialize state and create a link block.
        let state = get_state(12288);
        let mut state_guard = state.try_lock().expect("lock state");
        let _block1 = state_guard
            .create_lazy_node("link-name", 0, LinkNodeDisposition::Inline, || {
                async move {
                    let inspector = Inspector::new();
                    inspector.root().record_uint("a", 1);
                    Ok(inspector)
                }
                .boxed()
            })
            .unwrap();
        let _block2 = state_guard.create_uint_metric("test", 3, 0).unwrap();
        assert_eq!(
            state_guard.stats(),
            Stats {
                total_dynamic_children: 1,
                maximum_size: 12288,
                current_size: 4096,
                allocated_blocks: 6, /* HEADER, state_guard, _block1 (and content),
                                     // "link-name", _block2, "test" */
                deallocated_blocks: 0,
                failed_allocations: 0,
            }
        )
    }

    #[fuchsia::test]
    fn transaction_locking() {
        let state = get_state(4096);
        // Initial generation count is 0
        assert_eq!(state.header.header_generation_count().unwrap(), 0);

        // Begin a transaction
        state.begin_transaction();
        assert_eq!(state.header.header_generation_count().unwrap(), 1);
        assert!(state.header.check_locked(true).is_ok());

        // Operations on the lock  guard do not change the generation counter.
        let mut lock_guard1 = state.try_lock().expect("lock state");
        assert_eq!(lock_guard1.inner_lock.transaction_count, 1);
        assert_eq!(state.header.header_generation_count().unwrap(), 1);
        assert!(state.header.check_locked(true).is_ok());
        let _ = lock_guard1.create_node("test", 0).unwrap();
        assert_eq!(lock_guard1.inner_lock.transaction_count, 1);
        assert_eq!(state.header.header_generation_count().unwrap(), 1);

        // Dropping the guard releases the mutex lock but the header remains locked.
        drop(lock_guard1);
        assert_eq!(state.header.header_generation_count().unwrap(), 1);
        assert!(state.header.check_locked(true).is_ok());

        // When the transaction finishes, the header is unlocked.
        state.end_transaction();
        assert_eq!(state.header.header_generation_count().unwrap(), 2);
        assert!(state.header.check_locked(false).is_ok());

        // Operations under no transaction work as usual.
        let lock_guard2 = state.try_lock().expect("lock state");
        assert!(state.header.check_locked(true).is_ok());
        assert_eq!(state.header.header_generation_count().unwrap(), 3);
        assert_eq!(lock_guard2.inner_lock.transaction_count, 0);
    }
}
