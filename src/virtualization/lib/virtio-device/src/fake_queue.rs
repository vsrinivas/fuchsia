// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Fake queue for simulating driver interactions in unittests
//!
//! To facilitate writing unittests it is useful to manipulate the queues from the perspective of a
//! a driver. This module contains helpers that allow for:
//!  * Building simple descriptor chains
//!  * Publishing the build descriptors in the available ring
//!  * Receiving written descriptors from the used ring
//! This functionality centers around the [`FakeQueue`] implementation.
//!
//! For simplicity of writing tests the [`TestQueue`] struct packages together all the pieces
//! commonly needed to write a test.
//!
//! This module is available as, in addition to be used for writing the unittests for this library,
//! it can also be used for writing unittests for actual virtio device drivers without needing a
//! guest environment.

use {
    crate::{
        mem::{DeviceRange, DriverRange},
        queue::{Queue, QueueMemory},
        ring::{self, DescAccess, VRING_DESC_F_INDIRECT},
        util::NotificationCounter,
    },
    parking_lot::Mutex,
    std::{
        alloc::{self, GlobalAlloc},
        collections::HashMap,
    },
};

// Helper struct that just holds an allocation and returns it to the system allocator on drop.
struct IdentityAlloc {
    // alloc must not be null and be the return result from having passed layout to System.alloc,
    // such that it is safe to pass alloc and layout to System.dealloc.
    alloc: *mut u8,
    layout: alloc::Layout,
}

impl IdentityAlloc {
    fn new(layout: alloc::Layout) -> Option<Self> {
        if layout.size() == 0 {
            return None;
        }
        // The safety requirement on alloc_zeroed is that layout not be for a zero size, which we
        // validated just above.
        let alloc = unsafe { alloc::System.alloc_zeroed(layout) };
        if alloc.is_null() {
            return None;
        }
        Some(IdentityAlloc { alloc, layout })
    }
    fn base(&self) -> usize {
        self.alloc as usize
    }
}

impl Drop for IdentityAlloc {
    fn drop(&mut self) {
        // It is an invariant on alloc and layout that alloc is not null and that these are valid to
        // pass to System.dealloc.
        unsafe { alloc::System.dealloc(self.alloc, self.layout) };
    }
}

/// Implementation of [`crate::mem::DriverMem`] assuming the identity translation.
///
/// Can be used to allocate valid [`DeviceRange`] using the [`range_with_layout`] or [`new_range`]
/// methods. This then implements the identity transformation in [`translate`] meaning that:
///
/// ```
/// let range = identity_driver_mem.new_range(64)?;
/// assert_eq!(identity_driver_mem.translate(range.get().into()), Some(range)
/// ```
///
/// There is no mechanism to free or deallocate any constructed ranges, this is neccessary to ensure
/// they remain valid their provided lifetimes. Allocations will be freed once the
/// [`IdentityDriverMem`] is dropped.
pub struct IdentityDriverMem {
    // Allocations are stored using a DriverRange as the key, instead of a DeviceRange, to avoid
    // needing to invent a fake lifetime annotation. It is a requirement that these allocations only
    // be added to over the lifetime of the IdentityDriverMem.
    allocations: Mutex<Vec<(DriverRange, IdentityAlloc)>>,
}

impl crate::mem::DriverMem for IdentityDriverMem {
    fn translate<'a>(&'a self, driver: DriverRange) -> Option<DeviceRange<'a>> {
        if driver.len() == 0 {
            return None;
        }
        // See `driver` is contained in any of the ranges we have.
        let range = self
            .allocations
            .lock()
            .iter()
            .map(|x| x.0.clone())
            .find(|x| x.0.contains(&driver.0.start) && x.0.contains(&(driver.0.end - 1)))?
            .0;
        // We know (see `register`) that the DriverRange in allocations is a valid DeviceRange. As
        // the backing memory will not be free'd until IdentityDriverMem is dropped, we can safely
        // provide the lifetime `'a` on the range.
        let device = unsafe { DeviceRange::new(range) };
        // Now trim device down to the potential sub range that was requested.
        let device = device.split_at(driver.0.start - device.get().start)?.1;
        Some(device.split_at(driver.len())?.0)
    }
}

impl IdentityDriverMem {
    /// Construct a new [`IdentityDriverMem`]
    pub fn new() -> IdentityDriverMem {
        IdentityDriverMem { allocations: Mutex::new(Vec::new()) }
    }

    // Helper method for localizing the reasoning on the allocations map.
    // # Safety
    //
    // Require that `alloc` be a valid `IdentityAlloc` and that range.start == alloc.base and
    // range.len() <= alloc.layout.size(). This ensures range can be safely re-interpreted as a
    // DeviceRange.
    unsafe fn register(&self, range: DriverRange, alloc: IdentityAlloc) {
        self.allocations.lock().push((range, alloc));
    }

    fn range_with_layout_size<'a>(
        &'a self,
        size_bytes: usize,
        layout: alloc::Layout,
    ) -> Option<DeviceRange<'a>> {
        // Validate that our desired length fits inside the amount we are going to allocate
        if size_bytes > layout.size() {
            return None;
        }
        let alloc = IdentityAlloc::new(layout)?;
        let base = alloc.base();
        // The memory we provide to DeviceRange is valid as it just came from the allocator. And it
        // will remain valid until the [`IdentityAlloc`] is dropped and the memory freed, which does
        // not happen till this object is destroyed, and we have a borrow against this object of the
        // same lifetime as we provide in the DeviceRange.
        let range = unsafe { DeviceRange::new(base..(base + size_bytes)) };
        // register is safe to call since we provide a range that is pulled out of a valid
        // DeviceRange and range was is from the provided allocation.
        unsafe { self.register(DriverRange(range.get()), alloc) };
        Some(range)
    }

    /// Allocate with a specific [`alloc::Layout`]
    ///
    /// Specifying a specific [`alloc::Layout`] for the range is to allow for alignments to be
    /// specified so that [underlying](#get) [`DeviceRange`] can be accessed directly as a desired
    /// object using [`DeviceRange::try_ptr`].
    ///
    /// The allocated range will be zeroed.
    pub fn range_with_layout<'a>(&'a self, layout: alloc::Layout) -> Option<DeviceRange<'a>> {
        self.range_with_layout_size(layout.size(), layout)
    }

    /// Allocate a range to hold `size_bytes`
    ///
    /// The backing allocation will be aligned to match a [`u64`], but the [`DeviceRange`] reported
    /// by [`get`](#get) will be exactly `size_bytes` long.
    ///
    /// The allocated range will be zeroed.
    pub fn new_range<'a>(&'a self, size_bytes: usize) -> Option<DeviceRange<'a>> {
        if size_bytes == 0 {
            return None;
        }
        let layout = alloc::Layout::from_size_align(size_bytes, std::mem::align_of::<u64>())
            .ok()?
            .pad_to_align();
        self.range_with_layout_size(size_bytes, layout)
    }

    /// Allocates ranges to fill and return a [`QueueMemory`]
    pub fn alloc_queue_memory<'a>(&'a self, queue_size: u16) -> Option<QueueMemory<'a>> {
        let desc = self.new_range(std::mem::size_of::<ring::Desc>() * queue_size as usize)?;
        let avail = self.new_range(ring::Driver::avail_len_for_queue_size(queue_size))?;
        let used = self.new_range(ring::Device::used_len_for_queue_size(queue_size))?;
        Some(QueueMemory { desc, avail, used })
    }
}

/// Simulates driver side interactions of a queue for facilitating tests.
///
/// This is termed a `FakeQueue` as, whilst it can correctly function as a driver side queue
/// manager, its methods and internal book keeping are aimed at writing tests and not being
/// efficient.
pub struct FakeQueue<'a> {
    device: ring::as_driver::Device<'a>,
    driver: ring::as_driver::Driver<'a>,
    queue_size: u16,
    next_desc: u16,
    next_avail: u16,
    next_used: u16,
    chains: HashMap<u16, Chain>,
}

impl<'a> FakeQueue<'a> {
    /// Construct a [`FakeQueue`] to act as driver for provided rings.
    ///
    /// Takes a [`ring::Driver`] and [`ring::Device`] and constructs a view to the same memory to
    /// act as the driver.
    ///
    /// This assumes the rings have been correctly initialized, which by the virtio
    /// specification means they have been zeroed.
    pub fn new(driver: &ring::Driver<'a>, device: &ring::Device<'a>) -> Option<Self> {
        let queue_size = driver.queue_size();
        if queue_size != device.queue_size() {
            return None;
        }
        let driver = ring::as_driver::Driver::new(driver);
        let device = ring::as_driver::Device::new(device);
        Some(FakeQueue {
            device,
            driver,
            queue_size,
            next_desc: 0,
            next_avail: 0,
            next_used: 0,
            chains: HashMap::new(),
        })
    }

    pub fn publish_indirect(
        &mut self,
        chain: Chain,
        mem: &IdentityDriverMem,
    ) -> Option<(u16, u16)> {
        if chain.descriptors.len() == 0 {
            return None;
        }

        let indirect_range =
            mem.new_range(chain.descriptors.len() * std::mem::size_of::<ring::Desc>())?;

        let mut iter = chain.descriptors.iter().enumerate().peekable();
        while let Some((index, desc)) = iter.next() {
            let has_next = iter.peek().is_some();

            let write_flags =
                if desc.access == DescAccess::DeviceWrite { ring::VRING_DESC_F_WRITE } else { 0 };
            let next_flags = if has_next { ring::VRING_DESC_F_NEXT } else { 0 };
            let next_desc = if has_next { index as u16 + 1 } else { 0 };
            let ring_desc = ring::as_driver::make_desc(
                desc.data_addr,
                desc.data_len,
                write_flags | next_flags,
                next_desc,
            );

            let ptr = indirect_range.try_mut_ptr::<ring::Desc>()?;
            unsafe {
                std::ptr::copy_nonoverlapping::<ring::Desc>(
                    &ring_desc as *const ring::Desc,
                    ptr.add(index as usize),
                    1,
                )
            };
        }
        self.driver.write_desc(
            self.next_desc,
            ring::as_driver::make_desc(
                indirect_range.get().start as u64,
                indirect_range.len() as u32,
                VRING_DESC_F_INDIRECT,
                0,
            ),
        );
        self.update_index(chain, 1)
    }

    /// Attempt to publish a [`Chain`] into the ring.
    ///
    /// This returns a `None` if the chain is of zero length, or there are not enough available
    /// descriptors in the ring for the chain. Otherwise it returns the current available index, and
    /// the index of the first descriptor in the chain.
    pub fn publish(&mut self, chain: Chain) -> Option<(u16, u16)> {
        if chain.descriptors.len() == 0 {
            return None;
        }
        // Need to validate that enough descriptors are available. We know next_desc is either the
        // start of a chain, or is free, as such we just need to walk forward and check if any
        // chains start in our desired descriptor range.
        let desc_count = chain.descriptors.len() as u16;
        // Use a loop to check so that we can wrap indexes in a clearly readable way.
        for offset in 0..desc_count {
            let index = self.next_desc.wrapping_add(offset) % self.queue_size;
            if self.chains.get(&index).is_some() {
                return None;
            }
        }
        // Write descriptors
        let mut iter = chain.descriptors.iter().enumerate().peekable();
        while let Some((index, desc)) = iter.next() {
            let has_next = iter.peek().is_some();
            let ring_index = self.next_desc.wrapping_add(index as u16) % self.queue_size;
            let write_flags =
                if desc.access == DescAccess::DeviceWrite { ring::VRING_DESC_F_WRITE } else { 0 };
            let next_flags = if has_next { ring::VRING_DESC_F_NEXT } else { 0 };
            // If a specific next descriptor was supplied by the chain then use it, otherwise
            // calculate the actual next index we will insert.
            let next_desc = if has_next {
                desc.next.unwrap_or(ring_index.wrapping_add(1) % self.queue_size)
            } else {
                0
            };
            self.driver.write_desc(
                ring_index,
                ring::as_driver::make_desc(
                    desc.data_addr,
                    desc.data_len,
                    write_flags | next_flags,
                    next_desc,
                ),
            );
        }
        self.update_index(chain, desc_count)
    }

    fn update_index(&mut self, chain: Chain, desc_count: u16) -> Option<(u16, u16)> {
        // Mark the start of the descriptor chain.
        let first_desc = self.next_desc % self.queue_size;
        let avail_index = self.next_avail;
        self.driver.write_avail(avail_index, first_desc);
        // Available index is monotonic increasing and does not wrap at queue_size.
        self.next_avail = self.next_avail.wrapping_add(1);
        // Signal it as existing.
        self.driver.write_idx(self.next_avail);
        // Record the index we should start allocating descriptors from next time. This range may
        // or may not be free right now.
        self.next_desc = self.next_desc.wrapping_add(desc_count) % self.queue_size;
        // Store the descriptor in our map so we can return it in next_used.
        self.chains.insert(first_desc, chain);
        // Return the avail index we used and where the descriptor chain starts.
        Some((avail_index, first_desc))
    }

    /// Retrieve the next returned chain, if any.
    ///
    /// If a chain has been returned by the device return a [`UsedChain`], otherwise a `None.
    pub fn next_used(&mut self) -> Option<UsedChain> {
        // Check if the device has returned anything.
        if self.device.read_idx() == self.next_used {
            return None;
        }
        // Read out the chain that was returned.
        let (id, written) =
            ring::as_driver::deconstruct_used(self.device.read_used(self.next_used));
        // Expect something in the next slot next time.
        self.next_used = self.next_used.wrapping_add(1);
        // Remove the chain from our internal map and return it.
        self.chains.remove(&(id as u16)).map(|chain| UsedChain { written, chain })
    }
}

/// Represents a chain returned by a device.
pub struct UsedChain {
    written: u32,
    chain: Chain,
}

impl UsedChain {
    /// Get the amount of data written to the chain.
    ///
    /// This is the amount of data the device claimed it wrote to the chain and could be incorrect,
    /// for example some devices do not zero this field when they return a read only chain.
    pub fn written(&self) -> u32 {
        self.written
    }

    /// Iterate over any written portions.
    ///
    /// Iterates over the writable portion of the descriptor chain, up to the amount that was
    /// claimed to be [`written`](#written). The iterator produces
    /// `(driver_addr as u64, length as u32)` tuples and it is the responsibility of the caller to
    /// know if this range is valid and how to access it.
    pub fn data_iter<'a>(&'a self) -> ChainDataIter<'a> {
        ChainDataIter { next: Some(0), remaining: self.written, chain: &self.chain }
    }
}

/// Iterator for the data in a [`UsedChain`]
pub struct ChainDataIter<'a> {
    next: Option<usize>,
    remaining: u32,
    chain: &'a Chain,
}

impl<'a> Iterator for ChainDataIter<'a> {
    type Item = (u64, u32);

    fn next(&mut self) -> Option<Self::Item> {
        if self.remaining == 0 {
            return None;
        }
        let next = self.next.take()?;
        // Walk the descriptors till we find a writable one.
        let (index, desc) = self
            .chain
            .descriptors
            .iter()
            .enumerate()
            .skip(next)
            .find(|(_, desc)| desc.access == DescAccess::DeviceWrite)?;
        self.next = Some(index + 1);
        let size = std::cmp::min(self.remaining, desc.data_len);
        self.remaining = self.remaining - size;
        Some((desc.data_addr, size))
    }
}

struct DescriptorInfo {
    access: DescAccess,
    data_addr: u64,
    data_len: u32,
    next: Option<u16>,
}

/// Descriptor chain that can be published in a [`FakeQueue`]
pub struct Chain {
    descriptors: Vec<DescriptorInfo>,
}

impl Chain {
    /// Build a descriptor chain with zeroed readable and writable descriptors.
    ///
    /// For every value in the `readable` and `writable` slice provided, allocates a descriptor of
    /// that many bytes in the descriptor chain.
    pub fn with_lengths(readable: &[u32], writable: &[u32], mem: &IdentityDriverMem) -> Self {
        let builder = readable
            .iter()
            .cloned()
            .fold(ChainBuilder::new(), |build, range| build.readable_zeroed(range, mem));
        writable.iter().cloned().fold(builder, |build, range| build.writable(range, mem)).build()
    }

    /// Build a descriptor chain providing data for readable descriptors.
    ///
    /// Similar to [`with_lengths`](#with_lengths) except the readable descriptors are populated
    /// with a copy of the provided data slice instead.
    pub fn with_data<T: Copy>(
        readable: &[&[T]],
        writable: &[u32],
        mem: &IdentityDriverMem,
    ) -> Self {
        let builder = readable
            .iter()
            .cloned()
            .fold(ChainBuilder::new(), |build, range| build.readable(range, mem));
        writable.iter().cloned().fold(builder, |build, range| build.writable(range, mem)).build()
    }

    /// Build a descriptor chain with raw data references.
    ///
    /// Does not allocate data for any descriptors, instead puts the provided address and length
    /// directly into the final descriptor. This is intentionally designed to allow to you to build
    /// corrupt and invalid descriptor chains for the purposes of testing.
    pub fn with_exact_data(chain: &[(ring::DescAccess, u64, u32)]) -> Self {
        chain
            .iter()
            .fold(ChainBuilder::new(), |builder, (writable, addr, len)| {
                builder.reference(*writable, *addr, *len)
            })
            .build()
    }
}

/// Builder for a [`Chain`]
pub struct ChainBuilder(Chain);

impl ChainBuilder {
    /// Construct a new [`ChainBuilder`]
    pub fn new() -> Self {
        ChainBuilder(Chain { descriptors: Vec::new() })
    }

    /// Amend the last descriptor added with a specific next value.
    ///
    /// By default the next field in the published [`ring::Desc`] will be set automatically by the
    /// [`FakeQueue`] when publishing the chain, since the [`FakeQueue`] is the one allocating the
    /// actual descriptor ring slots.
    ///
    /// For testing this can be used to override the next field that [`FakeQueue::publish`] will
    /// generate and is intended for creating broken descriptor chains. It is not intended that this
    /// can be used and result in a properly functioning chain and queue.
    ///
    /// # Panics
    ///
    /// Will panic if no descriptor has yet been added to the chain.
    pub fn amend_next(mut self, next: u16) -> Self {
        self.0.descriptors.last_mut().unwrap().next = Some(next);
        self
    }

    /// Append new readable descriptor with a copy of `data`
    ///
    /// # Panics
    ///
    /// Will panic if there is not enough memory to allocate a buffer to hold `data`.
    pub fn readable<T: Copy>(mut self, data: &[T], mem: &IdentityDriverMem) -> Self {
        let layout = alloc::Layout::for_value(data);
        let mem = mem.range_with_layout(layout).unwrap();
        // This usage of copy_nonoverlapping is valid since
        //  * src region is from a slice reference and can assumed to be valid and correctly aligned
        //  * dst region produced from [`DeviceRange`] is defined to be valid as long as
        //    the DeviceRange is held alive, which it is over the duration of the unsafe block.
        //  * dst region is known to be correctly aligned as it was constructed aligned, and
        //    try_mut_ptr only returns validly aligned pointers.
        //  * src and dst do not overlap as the [`DeviceRange`] is valid, and valid device ranges do
        //    not overlap with other rust objects.
        unsafe {
            std::ptr::copy_nonoverlapping::<T>(
                data.as_ptr(),
                // unwrap cannot fail since we allocated with alignment of T.
                mem.try_mut_ptr::<T>().unwrap(),
                data.len(),
            )
        };
        self.0.descriptors.push(DescriptorInfo {
            access: DescAccess::DeviceRead,
            data_addr: mem.get().start as u64,
            data_len: layout.size() as u32,
            next: None,
        });
        self
    }

    /// Append an empty descriptor of the specified type and length
    ///
    /// # Panics
    ///
    /// Will panic if there is not enough memory to allocate a buffer of len `data_len`.
    pub fn zeroed(mut self, access: DescAccess, data_len: u32, mem: &IdentityDriverMem) -> Self {
        let mem = mem.new_range(data_len as usize).unwrap();
        self.0.descriptors.push(DescriptorInfo {
            access,
            data_addr: mem.get().start as u64,
            data_len,
            next: None,
        });
        self
    }

    /// Append a descriptor with raw data.
    ///
    /// This does not perform any allocations and will pass through the exact `data_addr` and
    /// `data_len` provided.
    pub fn reference(mut self, access: DescAccess, data_addr: u64, data_len: u32) -> Self {
        self.0.descriptors.push(DescriptorInfo { access, data_addr, data_len, next: None });
        self
    }

    pub fn readable_zeroed(self, len: u32, mem: &IdentityDriverMem) -> Self {
        self.zeroed(DescAccess::DeviceRead, len, mem)
    }
    pub fn readable_reference(self, addr: u64, len: u32) -> Self {
        self.reference(DescAccess::DeviceRead, addr, len)
    }
    pub fn writable(self, len: u32, mem: &IdentityDriverMem) -> Self {
        self.zeroed(DescAccess::DeviceWrite, len, mem)
    }
    pub fn writable_reference(self, addr: u64, len: u32) -> Self {
        self.reference(DescAccess::DeviceWrite, addr, len)
    }

    /// Consume the builder and produce a [`Chain`].
    pub fn build(self) -> Chain {
        self.0
    }
}

/// Wraps common state needed for writing test code with a [`FakeQueue`].
pub struct TestQueue<'a> {
    pub queue: Queue<'a, NotificationCounter>,
    pub notify: NotificationCounter,
    pub fake_queue: FakeQueue<'a>,
}

impl<'a> TestQueue<'a> {
    /// Allocates a [`Queue`] and [`FakeQueue`] for unit tests.
    pub fn new(size: usize, mem: &'a IdentityDriverMem) -> Self {
        let mem = mem.alloc_queue_memory(size as u16).unwrap();
        let notify = NotificationCounter::new();
        let driver = ring::Driver::new(mem.desc.clone(), mem.avail.clone()).unwrap();
        let device = ring::Device::new(mem.used.clone()).unwrap();

        let fake_queue = FakeQueue::new(&driver, &device).unwrap();
        let queue = Queue::new_from_rings(driver, device, notify.clone()).unwrap();
        TestQueue { queue, notify, fake_queue }
    }
}
