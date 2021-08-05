// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//! Minimal type-safe definitions of the virtio data structures.
//!
//! Contains definitions and type-safe accessors and manipulators of the virtio data structures.
//! For the leaf data structures like [descriptors](Desc) these definitions are simply the in
//! memory layout as a Rust `struct`.
//!
//! Unfortunately the virtqueues are a variable sized data structure, whose length is not known till
//! run time as the size is determined by the driver. Representing the virtqueue as 'just' a Rust
//! `struct` is therefore not possible.
//!
//! Two structs are used as for the representation as it allows for separating the
//! [`Device`] owned and [`Driver`] owned portions of the virtqueue into
//! separate portions with their correct mutability.
//!
//! Due to the split into the [`Driver`] and [`Device`] structs there is
//! no specifically named `virtqueue` in this module. The [Queue](crate::queue::Queue) builds on the
//! [`Driver`] and [`Device`] to build useful virtqueue functionality.
//!
//! These abstractions are intended to be type-safe, but not enforce correct implementation of the
//! virtio protocols. As such reading the [virtio specification]
//! (https://docs.oasis-open.org/virtio/virtio/v1.1/csprd01/virtio-v1.1-csprd01.html)
//! is required to correctly use this module. Most likely you do not want to use these directly and
//! want to use the higher level [`queue`](crate::queue), and [`chain`](crate::chain) modules that
//! provide easier to use wrappers.

use {
    crate::mem::DeviceRange,
    std::{convert::TryInto, marker::PhantomData, mem, sync::atomic},
};

/// Descriptor has a next field.
pub const VRING_DESC_F_NEXT: u16 = 1 << 0;
/// Descriptor is device write-only (otherwise device read-only).
pub const VRING_DESC_F_WRITE: u16 = 1 << 1;
/// Descriptor contains a list of buffer descriptors.
pub const VRING_DESC_F_INDIRECT: u16 = 1 << 2;

/// Describes descriptor access direction.
///
/// Any given descriptor is either device read only or device write only.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DescAccess {
    DeviceRead,
    DeviceWrite,
}

/// Virtio descriptor data structure
///
/// Represents the in memory format of virtio descriptors and provides some accessors.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct Desc {
    addr: u64,
    len: u32,
    // This is not bitflags! as it may contain additional bits that we do not define
    // and so would violate the bitflags type safety.
    flags: u16,
    next: u16,
}

impl Desc {
    /// Returns whether the [next](VRING_DESC_F_NEXT) bit is set.
    ///
    /// Typically the [next](#next) method is preferred.
    pub fn has_next(&self) -> bool {
        self.flags & VRING_DESC_F_NEXT != 0
    }

    /// Returns whether the [indirect](VRING_DESC_F_INDIRECT) bit is set.
    pub fn is_indirect(&self) -> bool {
        self.flags & VRING_DESC_F_INDIRECT != 0
    }

    /// Returns whether the [write](VRING_DESC_F_WRITE) bit is set.
    ///
    /// This flag should be ignored when [is_indirect](#is_indirect) is true.
    pub fn write_only(&self) -> bool {
        self.flags & VRING_DESC_F_WRITE != 0
    }

    /// Return the descriptor access type.
    ///
    /// This is a convenience wrapper around [write_only](#write_only) to provide a safer type.
    pub fn access_type(&self) -> DescAccess {
        if self.write_only() {
            DescAccess::DeviceWrite
        } else {
            DescAccess::DeviceRead
        }
    }

    /// Returns the next descriptor if there is one, otherwise a `None`.
    pub fn next(&self) -> Option<u16> {
        if self.has_next() {
            Some(self.next)
        } else {
            None
        }
    }

    /// Returns the guest (address, length) pair representing the contents of this descriptor.
    ///
    /// No validation of the address and length is performed. In particular the range could be
    /// invalid or wrap.
    pub fn data(&self) -> (u64, u32) {
        (self.addr, self.len)
    }
}

/// Represents the layout of a virtio header
///
/// Due to the need to access the header fields through raw pointers this struct is never directly
/// used, however we define it so that we can take the `size_of` it, and to make the translation to
/// our manual offsets more obvious.
#[repr(C)]
struct HeaderLayout {
    _flags: u16,
    _idx: u16,
}

impl HeaderLayout {
    // Define the offset of the two fields in the header layout. These offsets will be used to add
    // to u16 pointers.
    const FLAGS_OFFSET: usize = 0;
    const IDX_OFFSET: usize = 1;
}

/// Wrapper around accessing a virtio header
///
/// For safety the members of the virtio header must be individually read and written using volatile
/// accesses through a raw pointer, and we cannot construct a regular `&HeaderLayout`. Therefore
/// this object wraps a raw pointer and provides safe accesses to the header fields.
//
// # Safety
//
// `base` must always be a non-null pointer that points to an array of two u16 values (i.e. it
// must point to a HeaderLayout), that can be read and written from. This pointer must be known to
// be valid for the lifetime 'a, making it valid for at least the lifetime of this object.
#[derive(Clone)]
struct Header<'a> {
    base: *mut u16,
    lifetime: PhantomData<&'a ()>,
}

impl<'a> Header<'a> {
    /// Construct a [`Header`] wrapping the given [`HeaderLayout`]
    ///
    /// # Safety
    ///
    /// Behavior is undefined if:
    /// - `layout` is not valid for reads or writes
    /// - `layout` is not correctly aligned
    /// - `layout` does not point to an object that lives for at least the lifetime `'a`
    unsafe fn from_layout(layout: *mut HeaderLayout) -> Self {
        // If layout is a valid pointer to a HeaderLayout, then it is also a valid pointer to an
        // array of two u16 values, which is why we can do this cast and perform the offsetting that
        // we do in `flags()` and `idx()`
        Header { base: layout.cast(), lifetime: PhantomData }
    }

    // The returned pointer is guaranteed to be correctly aligned and valid for reads and writes.
    fn flags(&self) -> *mut u16 {
        // From the requirements in from_layout, base is a valid pointer to a HeaderLayout, and so
        // offsetting it to the flags field must result in a valid pointer.
        unsafe { self.base.add(HeaderLayout::FLAGS_OFFSET) }
    }

    // The returned pointer is guaranteed to be correctly aligned and valid for reads and writes.
    fn idx(&self) -> *mut u16 {
        // From the requirements in from_layout, base is a valid pointer to a HeaderLayout, and so
        // offsetting it to the idx field must result in a valid pointer.
        unsafe { self.base.add(HeaderLayout::IDX_OFFSET) }
    }

    fn are_notifications_suppressed(&self) -> bool {
        // flags() is guaranteed to return a pointer that is aligned and valid for reading
        unsafe { self.flags().read_volatile() == 1 }
    }

    fn load_idx(&self) -> u16 {
        // idx() is guaranteed to return a pointer that is aligned and valid for reading
        let result = unsafe { self.idx().read_volatile() };
        atomic::fence(atomic::Ordering::Acquire);
        result
    }

    fn store_idx(&self, idx: u16) {
        atomic::fence(atomic::Ordering::Release);
        // idx() is guaranteed to return a pointer that is aligned and valid for writing
        unsafe { self.idx().write_volatile(idx) };
    }

    /// Changes flags to suppress notifications.
    ///
    /// Not permitted if VIRTIO_F_EVENT_IDX feature was negotiated.
    /// This is not yet exposed for use.
    #[allow(dead_code)]
    fn suppress_notifications(&self) {
        // flags() is guaranteed to return a pointer that is aligned and valid for writing
        unsafe { self.flags().write_volatile(1) };
    }

    /// Change flags to enable notifications.
    fn enable_notifications(&self) {
        // flags() is guaranteed to return a pointer that is aligned and valid for writing
        unsafe { self.flags().write_volatile(0) };
    }
}

/// Representation of driver owned data.
///
/// Provides methods for safely querying, using appropriate memory barriers, items published by the
/// driver.
///
/// Contents of this `struct` are not expected to be being modified in parallel by a driver in a
/// guest, but as there is no way to guarantee guest behavior it is designed under the assumption of
/// parallel modifications by a malicious guest.
//
// # Safety
//
// The pointers `desc` and `avail` are created and validated in [`new`](#new) to point to ranges of
// memory that have at least `queue_size` valid objects in them, and are otherwise correctly aligned
// and are valid to read from. `used_event_index` must be an aligned pointer that can be read from.
//
// All of the objects pointed to by `desc`, `avail` and `used_event_index` must remain valid for the
// lifetime `'a`. It is the job of [`new`](#new) to take a [`DeviceRange`] and construct valid
// pointers, and they, along with `queue_size`, should never be changed.
//
// The pointers are marked mutable so as to allow the `as_driver::Driver` to be implemented,
// although the regular device implementation does not expose any way to perform writes.
// `as_driver::Driver` has its own safety discussion.
pub struct Driver<'a> {
    desc: *mut Desc,
    queue_size: u16,
    avail_header: Header<'a>,
    avail: *mut u16,
    used_event_index: *mut u16,
}

impl<'a> Driver<'a> {
    /// How many bytes the avail ring should be for the given `queue_size`.
    ///
    /// Provides an easy way to calculate the correct size of the range for passing to [`new`](#new)
    pub const fn avail_len_for_queue_size(queue_size: u16) -> usize {
        mem::size_of::<HeaderLayout>() + mem::size_of::<u16>() * (queue_size as usize + 1)
    }

    /// Construct a [`Driver`] using the provided memory for descriptor and available rings.
    ///
    /// Provided ranges must be correctly sized and aligned to represent the same power of two
    /// queue size, otherwise a `None` is returned.
    pub fn new<'b: 'a, 'c: 'a>(desc: DeviceRange<'b>, avail: DeviceRange<'c>) -> Option<Self> {
        let queue_size = desc.len() / std::mem::size_of::<Desc>();
        if !queue_size.is_power_of_two() {
            return None;
        }
        let queue_size16: u16 = queue_size.try_into().ok()?;
        // Here we calculated queue_size based on the length of desc, so we know that desc points to
        // at least queue_size valid objects.
        let desc = desc.try_mut_ptr()?;

        let (avail_header, rest) = avail.split_at(mem::size_of::<HeaderLayout>())?;
        // from_layout requires that the pointer we give it is correctly aligned, sized and lives
        // long enough. try_mut_ptr will only return a Some() if avail_header is aligned and at
        // least large enough for there to be a HeaderLayout. We also know that avail_header is
        // valid for at least our lifetime of `'a`.
        let avail_header = unsafe { Header::from_layout(avail_header.try_mut_ptr()?) };

        // Reinterpret the rest as a [u16], with the last one being the used_event_index
        if rest.len() != mem::size_of::<u16>() * (queue_size + 1) {
            return None;
        }

        let avail: *mut u16 = rest.try_mut_ptr()?;
        // We know that avail is an aligned pointer, as otherwise rest.try_mut_ptr() would have
        // returned a none and the size of avail was just validated above to hold queue_size+1 items
        let used_event_index = unsafe { avail.add(queue_size) };
        // Building the final struct we know that our pointers; desc, avail and used_event_index,
        // all point to sufficiently large objects for our queue_size16 that are aligned. As they
        // were derived from DeviceRanges that have a lifetime of at least `'a`, we have fulfilled
        // all the invariants defined on the struct.
        Some(Self { desc, queue_size: queue_size16, avail_header, avail, used_event_index })
    }

    /// Query if a descriptor chain has been published with the given index.
    ///
    /// If a chain has been published by the driver then returns the index of the first descriptor
    /// in the chain. Otherwise returns a `None`.
    pub fn get_avail(&self, next_index: u16) -> Option<u16> {
        if next_index != self.avail_header.load_idx() {
            // The struct level invariant on `avail` and `queue_size` guarantee that this offset
            // produces a readable value.
            Some(unsafe { self.avail.add((next_index % self.queue_size).into()).read_volatile() })
        } else {
            None
        }
    }

    /// Request a descriptor by index.
    ///
    /// Returns a none if the requested index is not within the range of the ring. Beyond this check
    /// this method has no way to validate if the requested descriptor is valid and it is the
    /// responsibility of the caller to know this.
    pub fn get_desc(&self, index: u16) -> Option<Desc> {
        if index < self.queue_size {
            // The struct level invariant on `desc` and `queue_size` guarantee that this offset
            // produces a readable value.
            Some(unsafe { self.desc.add(index.into()).read_volatile() })
        } else {
            None
        }
    }

    /// Determines if the driver has requested a notification for the given descriptor submission.
    ///
    /// Queries the information published by the driver to determine whether or not it would like a
    /// notification for the given `submitted` descriptor by the [`Device`]. As the [`Driver`] holds
    /// no host state whether the `VIRTIO_F_EVENT_IDX` feature was negotiated must be passed in.
    pub fn needs_notification(&self, feature_event_idx: bool, submitted: u16) -> bool {
        if feature_event_idx {
            // The struct level invariant on `used_event_index` guarantee this this is readable.
            submitted == unsafe { self.used_event_index.read_volatile() }
        } else {
            !self.avail_header.are_notifications_suppressed()
        }
    }

    /// Returns the size of the descriptor and available rings.
    ///
    /// The descriptor and available rings are, by definition, the same size. This is just returning
    /// the size that was calculated during [`new`](#new)
    pub fn queue_size(&self) -> u16 {
        self.queue_size
    }
}

/// Representation of an entry in the used ring.
///
/// The only purpose [`Used`] has is to be passed to [insert_used](Device::insert_used) to be
/// copied into the used ring. As a result the only provided method is [new](Used::new) and there
/// are no accessors, as the driver is the one who will be accessing it.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct Used {
    /// Index of start of used descriptor chain.
    ///
    /// For padding reasons the spec makes `id` in this structure is 32-bits, although it will never
    /// exceed an actual 16-bit descriptor index.
    id: u32,

    /// Total length of the descriptor chain which was used (written to), in bytes.
    len: u32,
}

impl Used {
    /// Constructs a new used entry.
    ///
    /// `id` is the index of the first descriptor in the chain being returned and `len` is the
    /// total number of bytes written to any writable descriptors in the chain.
    pub fn new(id: u16, len: u32) -> Used {
        Used { id: id.into(), len }
    }
}

/// Represents the device owned data.
///
/// Contents of this struct are expected to be modified by the device and so are mutable. Provided
/// methods allow for safely publishing data to the driver using appropriate memory barriers.
///
/// Although only the device is supposed to be modifying this data it is designed to account for a
/// malicious guest performing modifications in parallel.
//
// # Safety
//
// The pointer `used` is created and validated in [`new`](#new) to point to ranges of memory that
// have at least `queue_size` valid objects in them, and are otherwise correctly aligned and are
// valid to write to. `avail_event_index` must be an aligned pointer that can be written.
//
// All of the objects pointed to by `used`, and `avail_event_index` must remain valid for the
// lifetime `'a`. It is the job of [`new`](#new) to take a [`DeviceRange`] and construct valid
// pointers, and they, along with `queue_size`, should never be changed.
pub struct Device<'a> {
    queue_size: u16,
    used_header: Header<'a>,
    used: *mut Used,

    // Notification suppression is not yet exposed for use.
    #[allow(dead_code)]
    avail_event_index: *mut u16,
}

impl<'a> Device<'a> {
    /// How many bytes the avail ring should be for the given `queue_size`.
    ///
    /// Provides an easy way to calculate the correct size of the slice for passing to [`new`](#new)
    pub const fn used_len_for_queue_size(queue_size: u16) -> usize {
        mem::size_of::<HeaderLayout>()
            + mem::size_of::<Used>() * queue_size as usize
            + mem::size_of::<u16>()
    }

    /// Construct a [`Device`] using the provided memory for the used ring.
    ///
    /// Provided range must be correctly sized and aligned to represent a power of two queue size,
    /// otherwise a `None` is returned.
    pub fn new<'b: 'a>(used: DeviceRange<'b>) -> Option<Self> {
        let (used_header, rest) = used.split_at(mem::size_of::<HeaderLayout>())?;
        // from_layout requires that the pointer we give it is correctly aligned, sized and lives
        // long enough. try_mut_ptr will only return a Some() if avail_header is aligned and at
        // least large enough for there to be a HeaderLayout. We also know that avail_header is
        // valid for at least our lifetime of `'a`.
        let used_header = unsafe { Header::from_layout(used_header.try_mut_ptr()?) };

        // Take the last u16 from what is remaining as avail_event_index
        if rest.len() < mem::size_of::<u16>() {
            return None;
        }

        let queue_size = (rest.len() - mem::size_of::<u16>()) / mem::size_of::<Used>();
        if !queue_size.is_power_of_two() {
            return None;
        }
        let queue_size16: u16 = queue_size.try_into().ok()?;

        let used: *mut Used = rest.try_mut_ptr()?;

        // We know that used is an aligned pointer, as otherwise rest.try_mut_ptr() would have
        // returned a none and the size of used was just validated above to hold queue_size+1 items
        let avail_event_index = unsafe { used.add(queue_size).cast() };

        // Start with notifications from the driver enabled by default.
        used_header.enable_notifications();

        // Building the final struct we know that our pointers; used and avail_event_index, all
        // point to sufficiently large objects for our queue_size16 that are aligned. As they
        // were derived from DeviceRanges that have a lifetime of at least `'a`, we have fulfilled
        // all the invariants defined on the struct.
        Some(Self { queue_size: queue_size16, used_header, used, avail_event_index })
    }

    /// Returns the size of the used ring.
    pub fn queue_size(&self) -> u16 {
        self.queue_size
    }

    /// Add a descriptor chain to the used ring.
    ///
    /// After calling this the descriptor is not yet visible to the driver. To make it visible
    /// [`publish_used`](#publish_used) must be called. Chains are not implicitly published to allow
    /// for batching the return of chains.
    ///
    /// To allow for passing the same `index` between this and [`publish_used`](#publish_used),
    /// `index` here will automatically be wrapped to the queue length.
    pub fn insert_used(&mut self, used: Used, index: u16) {
        // The struct level invariant on `used` and `queue_size` guarantee that this offset
        // produces a writable value.
        unsafe { self.used.add((index % self.queue_size).into()).write_volatile(used) };
    }

    /// Publish the avail ring up to the provided `index` to the driver.
    ///
    /// This updates the driver visible index and performs appropriate memory barriers for the
    /// driver to see any returned descriptors. It does not perform any kind of asynchronous
    /// notification, such as an interrupt injection, to the guest or driver as that is a virtio
    /// transport specific detail and is the responsibility of the caller to know how to do.
    ///
    /// Note that indices should not be wrapped by the caller to the queue length as they are
    /// supposed to be free running and only wrap at the `u16` limit.
    pub fn publish_used(&mut self, index: u16) {
        self.used_header.store_idx(index);
    }
}

/// Driver side access to rings for writing tests
///
/// This module provides helpers to access rings from the side of the driver, and not the device,
/// which inverts the expectations on reading and writing. Provided here to reuse the [`Driver`]
/// and [`Device`] definitions, and is only intended for consumption by the [`fake_queue`]
/// (crate::fake_queue).
///
/// The helpers provided here are extremely minimal and low-level, and aim to the be the bare
/// minimum to simulate the driver side of ring interactions for the purpose of writing unit-tests.
pub(crate) mod as_driver {
    use std::sync::atomic;

    pub struct Device<'a>(super::Device<'a>);

    impl<'a> Device<'a> {
        pub fn new<'b: 'a>(device: &super::Device<'b>) -> Self {
            // In constructing a new super::Device we have not broken any invariants on the original
            // as we do not change any of the pointers or sizes, and ensure the original has at
            // least as long a lifetime.
            Self(super::Device {
                queue_size: device.queue_size,
                used_header: device.used_header.clone(),
                used: device.used,
                avail_event_index: device.avail_event_index,
            })
        }
        pub fn read_idx(&self) -> u16 {
            // Header::idx() is defined to always produce a pointer that may be read.
            let result = unsafe { self.0.used_header.idx().read_volatile() };
            atomic::fence(atomic::Ordering::Acquire);
            result
        }
        pub fn read_used(&self, index: u16) -> super::Used {
            // The struct invariant on super::Device guarantee this offset is valid and readable.
            unsafe { self.0.used.add((index % self.0.queue_size).into()).read_volatile() }
        }
    }

    pub struct Driver<'a>(super::Driver<'a>);

    impl<'a> Driver<'a> {
        pub fn new<'b: 'a>(driver: &super::Driver<'b>) -> Self {
            // In constructing a new super::Driver we have not broken any invariants on the original
            // as we do not change any of the pointers or sizes, and ensure the original has at
            // least as long a lifetime.
            Self(super::Driver {
                desc: driver.desc,
                queue_size: driver.queue_size,
                avail_header: driver.avail_header.clone(),
                avail: driver.avail,
                used_event_index: driver.used_event_index,
            })
        }
        pub fn write_desc(&mut self, index: u16, desc: super::Desc) {
            // The struct invariant on super::Driver guarantee this offset is valid and writable.
            unsafe { self.0.desc.add((index % self.0.queue_size).into()).write_volatile(desc) };
        }
        pub fn write_avail(&mut self, index: u16, val: u16) {
            // The struct invariant on super::Driver guarantee this offset is valid and writable.
            unsafe { self.0.avail.add((index % self.0.queue_size).into()).write_volatile(val) };
        }
        #[allow(unused)]
        pub fn write_flags(&mut self, flags: u16) {
            atomic::fence(atomic::Ordering::Release);
            // Header::flags() is defined to always produce a pointer that may be written.
            unsafe { self.0.avail_header.flags().write_volatile(flags) };
        }
        pub fn write_idx(&mut self, idx: u16) {
            atomic::fence(atomic::Ordering::Release);
            // Header::idx() is defined to always produce a pointer that may be written.
            unsafe { self.0.avail_header.idx().write_volatile(idx) };
        }
        #[allow(unused)]
        pub fn write_used_event_index(&mut self, index: u16) {
            atomic::fence(atomic::Ordering::Release);
            // The struct invariant on super::Driver guarantee this pointer is valid and writable.
            unsafe { self.0.used_event_index.write_volatile(index) };
        }
    }

    pub fn make_desc(addr: u64, len: u32, flags: u16, next: u16) -> super::Desc {
        super::Desc { addr, len, flags, next }
    }
    pub fn deconstruct_used(used: super::Used) -> (u32, u32) {
        (used.id, used.len)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::fake_queue::{Chain, FakeQueue, IdentityDriverMem},
    };

    #[test]
    fn test_size() {
        let driver_mem = IdentityDriverMem::new();
        // Declare memory for queue size of 3, which is not a power of two.
        let mem = driver_mem.alloc_queue_memory(3).unwrap();
        assert!(Driver::new(mem.desc, mem.avail).is_none());
        assert!(Device::new(mem.used).is_none());
        // Differing, but otherwise valid, sizes for the two rings.
        let mem = driver_mem.alloc_queue_memory(4).unwrap();
        let mem2 = driver_mem.alloc_queue_memory(8).unwrap();
        assert!(Driver::new(mem.desc, mem2.avail).is_none());
        // Declare memory for queues with a queue size of 8, which is good.
        let mem = driver_mem.alloc_queue_memory(8).unwrap();
        assert!(Driver::new(mem.desc, mem.avail).is_some());
        assert!(Device::new(mem.used).is_some());
    }

    #[test]
    fn test_descriptor() {
        let driver_mem = IdentityDriverMem::new();
        let mem = driver_mem.alloc_queue_memory(128).unwrap();
        let driver = Driver::new(mem.desc, mem.avail).unwrap();
        let mut device = Device::new(mem.used).unwrap();
        let mut fake_queue = FakeQueue::new(&driver, &device).unwrap();
        // Check initial state.
        assert!(driver.get_avail(0).is_none());
        // Ask the fake driver to publish a couple of descriptor chains. We know where in the
        // available list they must be placed, but not what descriptor index they will get.
        let (avail0, first_desc0) =
            fake_queue.publish(Chain::with_lengths(&[64, 64], &[], &driver_mem)).unwrap();
        assert_eq!(avail0, 0);
        assert_eq!(driver.get_avail(0), Some(first_desc0));
        let (avail1, first_desc1) =
            fake_queue.publish(Chain::with_lengths(&[32], &[48], &driver_mem)).unwrap();
        assert_eq!(avail1, 1);
        assert_eq!(driver.get_avail(0), Some(first_desc0));
        assert_eq!(driver.get_avail(1), Some(first_desc1));
        // Validate the two chains are what we expect them to be.
        let desc = driver.get_desc(first_desc0).unwrap();
        assert!(desc.has_next());
        assert!(!desc.write_only());
        assert_eq!(desc.data().1, 64);
        let desc = driver.get_desc(desc.next().unwrap()).unwrap();
        assert!(!desc.has_next());
        assert!(!desc.write_only());
        assert_eq!(desc.data().1, 64);

        let desc = driver.get_desc(first_desc1).unwrap();
        assert!(desc.has_next());
        assert!(!desc.write_only());
        assert_eq!(desc.data().1, 32);
        let desc = driver.get_desc(desc.next().unwrap()).unwrap();
        assert!(!desc.has_next());
        assert!(desc.write_only());
        assert_eq!(desc.data().1, 48);
        // Return the chains in reverse order. Claim we wrote 16 bytes to the writable portion.
        device.insert_used(Used::new(first_desc1, 16), 0);
        device.insert_used(Used::new(first_desc0, 0), 1);
        assert!(fake_queue.next_used().is_none());

        // Publish at once.
        device.publish_used(2);

        // Should now be able to receive the descriptors back.
        let chain = fake_queue.next_used().unwrap();
        assert_eq!(chain.written(), 16);
        let mut iter = chain.data_iter();
        assert_eq!(iter.next().map(|(_, len)| len), Some(16));
        assert!(iter.next().is_none());
        let chain = fake_queue.next_used().unwrap();
        assert_eq!(chain.written(), 0);
        assert!(chain.data_iter().next().is_none());

        // Should be nothing left.
        assert!(fake_queue.next_used().is_none());
    }
}
