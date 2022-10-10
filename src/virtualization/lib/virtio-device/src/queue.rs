// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Virtqueue management wrappers.
//!
//! This is a slightly opinionated wrapper that presents the underlying [`Device`](ring::Device) and
//! [`Driver`](ring::Driver) rings as a single 'virtqueue' where descriptor chains can be retrieved,
//! walked or iterated, and then returned.
//!
//! The primary opinionated decision taken by this wrapper is that a descriptor chain is considered
//! a formal object that can automatically returns itself to the queue when dropped. This return is
//! then required to have a mechanism to potentially signal the guest driver, via the
//! [`DriverNotify`] trait.

use {
    crate::{
        mem::{DeviceRange, DriverRange},
        ring,
    },
    parking_lot::Mutex,
    std::{
        convert::{TryFrom, TryInto},
        sync::atomic,
    },
    thiserror::Error,
};

/// Informs device that driver needs a notification.
///
/// When returning descriptor chains to the queue it may be required, due to the virtio
/// specification, that the driver is supposed to be notified. As descriptor chains can be returned
/// out-of-band during a `drop`, with no opportunity to report via a return code that a notification
/// is needed, this trait exists instead. Further, since the queue has no understanding of the
/// transport protocol, or how the guest should be notified in general, it must appeal to the higher
/// level device code using the [`Queue`].
pub trait DriverNotify {
    /// Driver requires a notification.
    ///
    /// Indicates the device must notify the driver for correct continuation of the virtio protocol.
    /// The notification does not need to happen synchronously during this call, it can be stored
    /// and performed at some later point, but the driver may not make progress until it is
    /// notified.
    fn notify(&self);
}

/// Mutable state of a virtqueue.
///
/// Includes both the reference to the [`Device`](ring::Device), which is the memory shared with the
/// guest that we actually need to manipulate, as well as additional state needed for us to
/// correctly implement the virtio protocol. Captured here in a separate struct so that it can be
/// wrapped in a [`Mutex`] in the [`Queue`].
struct State<'a> {
    device: ring::Device<'a>,
    // Next index in avail that we expect to be come available
    next: u16,
    // Next index in used that we will publish at.
    next_used: u16,
}

impl<'a> State<'a> {
    /// Return a descriptor chain.
    ///
    /// `written` is the number of bytes that were written by the device to the beginning of the
    /// buffer.
    ///
    /// Returns to the driver, by writing it to the device owned ring, and returns the index that
    /// it was published at. This index is intended to be used when determining whether the driver
    /// needs a notification.
    fn return_chain(&mut self, used: ring::Used) -> u16 {
        let submit = self.next_used;
        self.device.insert_used(used, submit);
        self.next_used = submit.wrapping_add(1);
        self.device.publish_used(self.next_used);
        // Return the index that we just published.
        submit
    }
}

/// Describes the memory ranges for a queue.
///
/// Collects the three different memory ranges that combined make up the [`Driver`](ring::Driver)
/// and [`Device`](ring::Device) portions of a queue. This exists as a way to conveniently name the
/// members for passing to [`Queue::new`]
#[derive(Debug, Clone)]
pub struct QueueMemory<'a> {
    pub desc: DeviceRange<'a>,
    pub avail: DeviceRange<'a>,
    pub used: DeviceRange<'a>,
}

/// Representation of a virtqueue.
///
/// Aside from construction of the queue the only provided method is to retrieve the [`next_chain`]
/// (Queue::next_chain) if one has been published by the driver. The [`DescChain`], if one is
/// returned, implements a custom `drop` to return the chain to the queue, and hence to the driver.
// Currently event_idx feature is hard set to false. Support exists though for the device
// requirements of handling suppression from the driver, however no interface is yet exposed here
// for the device to tell the driver any event requirements.
pub struct Queue<'a, N> {
    driver: ring::Driver<'a>,
    state: Mutex<State<'a>>,
    notify: N,
    // Whether or not the EVENT_IDX feature was negotiated. This is stored here as we need it to
    // correctly determine when we should signal a notify to the driver.
    feature_event_idx: bool,
}

impl<'a, N> Queue<'a, N> {
    /// Constructs a new [`Queue`] from memory descriptions.
    ///
    /// Takes a [`QueueMemory`], which is just a list of memory regions, for which to create a
    /// queue out of. This internally creates a [`Driver`](ring::Driver) and [`Device`]
    /// (ring::Device) from the provided regions.
    ///
    /// The rings are assumed to not have yet been used and so the used and avail indices will
    /// start at 0. There is presently no way to construct a [`Queue`] around rings that have
    /// already been in use.
    pub fn new(mem: QueueMemory<'a>, notify: N) -> Option<Self> {
        Self::new_from_rings(
            ring::Driver::new(mem.desc, mem.avail)?,
            ring::Device::new(mem.used)?,
            notify,
        )
    }

    /// Construct a new [`Queue`] from provided rings.
    ///
    /// Consumes a [`Driver`](ring::Driver) and [`Device`](ring::Device) to create a `Queue`.
    /// It is expected that [`new`](#new) will typically be more useful to automate the ring
    /// construction.
    ///
    /// Has the same initial ring state assumptions as [`new`](#new).
    pub fn new_from_rings(
        driver: ring::Driver<'a>,
        device: ring::Device<'a>,
        notify: N,
    ) -> Option<Self> {
        if driver.queue_size() != device.queue_size() {
            return None;
        }
        Some(Queue {
            driver,
            state: Mutex::new(State { device, next: 0, next_used: 0 }),
            notify,
            feature_event_idx: false,
        })
    }

    fn take_avail(&self) -> Option<u16> {
        let mut state = self.state.lock();
        let ret = self.driver.get_avail(state.next);
        if ret.is_some() {
            state.next = state.next.wrapping_add(1);
        }
        ret
    }
}

impl<'a, N: DriverNotify> Queue<'a, N> {
    /// Return any available [`DescChain`].
    ///
    /// Polls the available ring for any queued descriptor chain, and if found returns a
    /// [`DescChain`] abstraction around it.
    ///
    /// It is the responsibility of the device to know, presumably via the transport level queue
    /// notifications, when a descriptor chain might be available and to call this polling function.
    ///
    /// The [`DescChain`] is automatically returned the driver, via the used ring, when it is
    /// dropped.
    pub fn next_chain<'b>(&'b self) -> Option<DescChain<'a, 'b, N>> {
        if let Some(desc_index) = self.take_avail() {
            Some(DescChain { queue: self, first_desc: desc_index })
        } else {
            None
        }
    }
    fn return_chain(&self, used: ring::Used) {
        let submitted = self.state.lock().return_chain(used);
        // Must ensure the read of flags or used_event occurs *after* we have returned the chain
        // and published the index. We also need to ensure that in the event we do send an
        // interrupt that any state and idx updates have been written. In this case acquire/release
        // is not sufficient since the 'acquire' will prevent future loads re-ordering earlier, and
        // the release will prevent past writes from re-ordering later, but we need a past write and
        // a future load to not be re-ordered. Therefore we require sequentially consistent
        // semantics.
        atomic::fence(atomic::Ordering::SeqCst);
        if self.driver.needs_notification(self.feature_event_idx, submitted) {
            self.notify.notify();
        }
    }
}

/// Descriptor type
///
/// May be an indirect descriptor type ( see virtio spec 2.7.5.3 ) or a regular aka
///  direct descriptor type.
/// Regular descriptor wraps up [`ring::DescAccess`]
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum DescType {
    Direct(ring::DescAccess),
    Indirect,
}
/// Reference to descriptor data.
///
/// Provides a higher level representation of a descriptors payload, compared to what
/// [`ring::Desc::data`] reports. The conversion of a [`ring::Desc`] into a `Desc` necessitates some
/// error checking and can fail with a [`DescError::BadRange`].
///
/// Is provided as a [`DriverRange`] as the [`DescChain`] and its [iterator](DescChainIter) have no
/// way to translate a [`DriverRange`] and this responsibility is offloaded to the caller.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Desc(pub DescType, pub DriverRange);

impl TryFrom<ring::Desc> for Desc {
    type Error = DescError;

    fn try_from(desc: ring::Desc) -> Result<Self, Self::Error> {
        let range = desc.data();
        TryInto::<DriverRange>::try_into(range)
            .map_err(|()| DescError::BadRange(range.0, range.1))
            .map(|range| {
                if desc.is_indirect() {
                    Desc(DescType::Indirect, range)
                } else {
                    Desc(DescType::Direct(desc.access_type()), range)
                }
            })
    }
}

/// Errors that occur when walking descriptor chains via [`DescChainIter`].
#[derive(Error, Debug, Clone, PartialEq, Eq)]
pub enum DescError {
    #[error("Descriptor {0} is not in the range of the ring")]
    InvalidIndex(u16),
    #[error("Descriptor data range addr: {0} len: {1} is not a valid driver range")]
    BadRange(u64, u32),
}

/// Iterates over a [`DescChain`].
///
/// The iterator provides a [`Desc`] representing each [virtio descriptor](ring::Desc) in the chain.
/// Walking this chain may generate errors due to a faulty or malicious guest providing corrupt
/// descriptors.
///
/// Only the most minimal validation is done to yield valid [`Desc`], with no virtio protocol
/// validation being performed. In particular, although the virtio specification says that all
/// readable descriptors must appear before writable ones, this is not enforced or checked for by
/// this iterator.
///
/// A lifetime is associated with the underlying [`Queue`], but not the [`DescChain`] this is
/// iterating. This makes it possible, albeit not advised, to hold an iterator after having returned
/// a chain to the guest. Doing so will almost certainly result in violating the virtio protocol and
/// will confuse the guest, but there are no safety concerns. Restricting the iterator to the
/// lifetime of the chain makes them cumbersome and you should almost always be using the
/// abstractions provided by the [`chain`](crate::chain) module instead of these iterators directly.
pub struct DescChainIter<'a, 'b, N: DriverNotify> {
    queue: &'b Queue<'a, N>,
    desc: Option<u16>,
}

impl<'a, 'b, N: DriverNotify> DescChainIter<'a, 'b, N> {
    /// Cause the iterator to complete.
    ///
    /// Places the iterator in a state where it will always produce None.
    pub fn complete(&mut self) {
        self.desc = None;
    }
}

impl<'a, 'b, N: DriverNotify> Iterator for DescChainIter<'a, 'b, N> {
    type Item = Result<Desc, DescError>;
    fn next(&mut self) -> Option<Self::Item> {
        self.desc.map(|ret| {
            match self.queue.driver.get_desc(ret.into()).ok_or(DescError::InvalidIndex(ret)) {
                Ok(desc) => {
                    // If we were able to lookup the descriptor then we can always retrieve the
                    // next one, even if this one reports a bad range.
                    self.desc = desc.next();
                    desc.try_into()
                }
                Err(e) => {
                    // Not able to find the next descriptor, so we must terminate the iteration
                    // after reporting this error.
                    self.desc = None;
                    Err(e)
                }
            }
        })
    }
}

impl<'a, 'b, N: DriverNotify> Clone for DescChainIter<'a, 'b, N> {
    fn clone(&self) -> DescChainIter<'a, 'b, N> {
        DescChainIter { queue: self.queue, desc: self.desc }
    }
}

/// Represents a chain of descriptors in the available ring.
///
/// The `DescChain` is a thin representation over a virtio descriptor chain. It can either be walked
/// using its [iterator](#iter), yielding the readable and writable portions, or it can be returned
/// to the ring.
///
/// Although returning happens automatically when dropped, if data was written into the descriptors
/// the chain needs to be explicitly returned with [`set_written`](#set_written) to propagate the
/// portion that was written.
pub struct DescChain<'a, 'b, N: DriverNotify> {
    queue: &'b Queue<'a, N>,
    first_desc: u16,
}

impl<'a, 'b, N: DriverNotify> DescChain<'a, 'b, N> {
    /// Iterate over the descriptor chain
    ///
    /// See [`DescChainIter`].
    pub fn iter(&self) -> DescChainIter<'a, 'b, N> {
        DescChainIter { queue: self.queue, desc: Some(self.first_desc) }
    }

    /// Explicitly return a written to chain.
    ///
    /// Returns the chain to the used ring, as if the chain was dropped, but also forwards how much
    /// of the chain was written to. No validation or manipulation is performed on written amount
    /// and it is faithfully passed through. In particular you can claim to have written more bytes
    /// than were made available for writing by the chain.
    pub fn return_written(self, written: u32) {
        self.queue.return_chain(ring::Used::new(self.first_desc, written));
        // Don't call drop so that we avoid returning the chain a second time.
        std::mem::forget(self)
    }
}
impl<'a, 'b, N: DriverNotify> Drop for DescChain<'a, 'b, N> {
    fn drop(&mut self) {
        // By default return the chain with a write of 0, since as far as we know nothing was
        // written.
        self.queue.return_chain(ring::Used::new(self.first_desc, 0));
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            fake_queue::{Chain, ChainBuilder, IdentityDriverMem, TestQueue},
            ring::DescAccess,
            util::NotificationCounter,
        },
    };

    #[test]
    fn test_create() {
        let driver_mem = IdentityDriverMem::new();
        // Should fail for non pow-2 ring
        let mem = driver_mem.alloc_queue_memory(3).unwrap();
        let notify = NotificationCounter::new();
        assert!(Queue::new(mem, notify.clone()).is_none());
        // Also fail for not same sized rings.
        let mem = driver_mem.alloc_queue_memory(4).unwrap();
        let mem2 = driver_mem.alloc_queue_memory(8).unwrap();
        assert!(Queue::new(
            QueueMemory { desc: mem.desc, avail: mem.avail, used: mem2.used },
            notify.clone(),
        )
        .is_none());
        // Correctly sized rings should work
        let mem = driver_mem.alloc_queue_memory(4).unwrap();
        assert!(Queue::new(mem, notify.clone()).is_some());
    }

    #[test]
    fn test_notify_and_return() {
        let driver_mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &driver_mem);
        // Should be nothing notified or queued yet.
        assert_eq!(state.notify.get(), 0);
        assert!(state.queue.next_chain().is_none());
        // Publish a chain
        assert!(state.fake_queue.publish(Chain::with_lengths(&[32], &[], &driver_mem)).is_some());
        let chain = state.queue.next_chain().unwrap();
        assert_eq!(state.notify.get(), 0);
        // If we drop the chain it should get returned and trigger a notification.
        std::mem::drop(chain);
        assert_eq!(state.notify.get(), 1);
        // And there should be something on the driver side.
        assert!(state.fake_queue.next_used().is_some());
        // Should also be able to explicitly return a written amount and see it in the driver.
        assert!(state.fake_queue.publish(Chain::with_lengths(&[], &[32], &driver_mem)).is_some());
        let chain = state.queue.next_chain().unwrap();
        chain.return_written(16);
        let used = state.fake_queue.next_used().unwrap();
        assert_eq!(used.written(), 16);
    }

    #[test]
    fn test_good_chain_iter() {
        let driver_mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &driver_mem);
        // Build and insert a variety of chains.
        let chains: [&[(DescAccess, u64, u32)]; 4] = [
            &[(DescAccess::DeviceRead, 100, 42)],
            &[(DescAccess::DeviceWrite, 200, 64)],
            &[(DescAccess::DeviceRead, 1000, 20), (DescAccess::DeviceRead, 300, 40)],
            &[
                (DescAccess::DeviceRead, 4000, 40),
                (DescAccess::DeviceWrite, 400, 64),
                (DescAccess::DeviceWrite, 8000, 80),
            ],
        ];
        for chain in chains {
            assert!(state.fake_queue.publish(Chain::with_exact_data(chain)).is_some());
        }
        // Now read them all out and walk the iterators to ensure a match.
        for chain in chains {
            assert!(state
                .queue
                .next_chain()
                .unwrap()
                .iter()
                .map(|desc| match desc {
                    Ok(Desc(DescType::Direct(access), range)) =>
                        (access, range.0.start as u64, range.0.len() as u32),
                    Ok(Desc(DescType::Indirect, _)) => (DescAccess::DeviceRead, 0, 0),
                    Err(_) => (DescAccess::DeviceRead, 0, 0),
                })
                .eq(chain.iter().cloned()));
        }
    }

    #[test]
    fn test_bad_range_iter() {
        let driver_mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &driver_mem);
        // Build a chain with some invalid ranges, we should still be able to iterate it.
        assert!(state
            .fake_queue
            .publish(Chain::with_exact_data(&[
                (DescAccess::DeviceRead, 100, 42),
                (DescAccess::DeviceRead, u64::MAX - 10, 20),
                (DescAccess::DeviceRead, u64::MAX - 20, 5)
            ]))
            .is_some());
        let chain = state.queue.next_chain().unwrap();
        let mut iter = chain.iter();
        assert_eq!(
            iter.next().unwrap(),
            Ok(Desc(DescType::Direct(DescAccess::DeviceRead), (100, 42).try_into().unwrap()))
        );
        assert_eq!(iter.next().unwrap(), Err(DescError::BadRange(u64::MAX - 10, 20)));
        assert_eq!(
            iter.next().unwrap(),
            Ok(Desc(
                DescType::Direct(DescAccess::DeviceRead),
                (u64::MAX - 20, 5).try_into().unwrap()
            ))
        );
        assert_eq!(iter.next(), None);
    }

    #[test]
    fn test_bad_index_iter() {
        let driver_mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &driver_mem);
        // Build a chain with an invalid descriptor index in the middle.
        let chain = ChainBuilder::new()
            .readable_reference(100, 42)
            .amend_next(33)
            .readable_zeroed(30, &driver_mem)
            .build();
        assert!(state.fake_queue.publish(chain).is_some());
        let chain = state.queue.next_chain().unwrap();
        let mut iter = chain.iter();
        assert_eq!(
            iter.next().unwrap(),
            Ok(Desc(DescType::Direct(DescAccess::DeviceRead), (100, 42).try_into().unwrap()))
        );
        assert_eq!(iter.next().unwrap(), Err(DescError::InvalidIndex(33)));
        assert_eq!(iter.next(), None);
    }
}
