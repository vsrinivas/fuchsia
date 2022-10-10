// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Descriptor chain walking.
//!
//! The goal of the [`ReadableChain`] and [`WritableChain`] is to present a byte-wise view of the
//! descriptor chain, and facilitate safe reading and writing to the chain.
//!
//! Although walking these chains feels similar to using an iterator, the chains do not directly
//! implement the [`std::iter::Iterator`] trait as iterator composition works against being able to
//! then convert a [`ReadableChain`] into a [`WritableChain`]. An iterator can be built on top of
//! these interfaces, but it has not been done here yet.
//!
//! In addition to walking byte ranges via the [`next`](ReadableChain::next) or [`next_with_limit`]
//! (ReadableChain::next_with_limit) methods, the [`Read`](std::io::Read) and [`Write`]
//! (std::io::Write) traits are implemented for [`ReadableChain`] and [`WritableChain`]
//! respectively.
//!
//! When using the [`std::io::Write`] interface for the [`WritableChain`] the amount written is
//! tracked, alleviating the need to manually perform [`add_written`](WritableChain::add_written).
//! Although not always appropriate depending on the particular virtio device, the
//! [`Read`](std::io::Read)/[`Write`](std::io::Write) interfaces are therefore the preferred way to
//! manipulate the chains.
//!
//! The requirement from the virtio specification that all readable descriptors occur before all
//! writable descriptors is enforced here, with explicit types that indicate what is being walked.
//! Transitioning from the [`ReadableChain`] to the [`WritableChain`] is an explicit operation that
//! allows for optional checking to ensure all readable descriptors have been consumed. This allows
//! devices to easily check if the driver is violating any protocol assumptions on descriptor
//! layouts.

use {
    crate::{
        mem::{DeviceRange, DriverMem, DriverRange},
        queue::{Desc, DescChain, DescChainIter, DescError, DescType, DriverNotify},
        ring::Desc as RingDesc,
        ring::DescAccess,
    },
    thiserror::Error,
};

/// Errors from walking a descriptor chain.
#[derive(Error, Debug, Clone, PartialEq, Eq)]
pub enum ChainError {
    #[error("Error in descriptor chain: {0}")]
    Desc(#[from] DescError),
    #[error("Found readable descriptor after writable")]
    ReadableAfterWritable,
    #[error("Failed to translate descriptors driver range {0:?} into a device range")]
    TranslateFailed(DriverRange),
    #[error("Nested indirect chain is not supported by the virtio spec")]
    InvalidNestedIndirectChain,
}

impl From<ChainError> for std::io::Error {
    fn from(error: ChainError) -> Self {
        std::io::Error::new(std::io::ErrorKind::Other, error)
    }
}

#[derive(Debug, Clone)]
struct IndirectDescChain<'a> {
    range: DeviceRange<'a>,
    next: Option<u16>,
}

impl<'a> IndirectDescChain<'a> {
    fn new(range: DeviceRange<'a>) -> Self {
        IndirectDescChain { range: range, next: Some(0) }
    }

    pub fn next(&mut self) -> Option<Result<Desc, DescError>> {
        let index = self.next?;
        match self.range.split_at(index as usize * std::mem::size_of::<RingDesc>()) {
            None => Some(Err(DescError::InvalidIndex(index))),
            Some((_, range)) => match range.try_ptr::<RingDesc>() {
                None => Some(Err(DescError::InvalidIndex(index))),
                Some(ptr) => {
                    // * SAFETY
                    // try_ptr guarantees that returned Some(ptr) is valid for read
                    let desc = unsafe { ptr.read_volatile() };
                    self.next = desc.next();
                    Some(desc.try_into())
                }
            },
        }
    }
}

// State for a generic walker that can walk either the readable or writable portions of a
// chain. Ideally `E` would be of type DescAccess to indicate the kind of access this is iterating
// over, but due to current limits in const generics we have to use a bool instead. It gets
// converted to DescAccess in expected_access.
struct State<'a, 'b, N: DriverNotify, M, const E: bool> {
    chain: Option<DescChain<'a, 'b, N>>,
    iter: DescChainIter<'a, 'b, N>,
    current: Option<Desc>,
    mem: &'a M,
    indirect_chain: Option<IndirectDescChain<'a>>,
}

impl<'a, 'b, N: DriverNotify, M: DriverMem, const E: bool> State<'a, 'b, N, M, E> {
    // Hack for const generics limitation to convert bool->DescAccess.
    fn expected_access() -> DescAccess {
        if E {
            DescAccess::DeviceWrite
        } else {
            DescAccess::DeviceRead
        }
    }

    fn next_desc(&mut self) -> Option<Result<Desc, ChainError>> {
        fn into_desc(desc: Result<Desc, DescError>) -> Option<Result<Desc, ChainError>> {
            match desc {
                Ok(desc) => Some(Ok(desc)),
                Err(e) => Some(Err(e.into())),
            }
        }

        match self.current.take() {
            None => {
                // Nothing in the current, time to read a new descriptor
                // Let's see if we have an active indirect chain
                if let Some(indirect_chain) = &mut self.indirect_chain {
                    // Keep processing the indirect chain
                    match indirect_chain.next() {
                        None => {
                            // Indirect chain has been fully processed
                            self.indirect_chain = None;
                            // Read from the normal chain
                            into_desc(self.iter.next()?)
                        }
                        // Read from the indirect chain
                        Some(desc_res) => into_desc(desc_res),
                    }
                } else {
                    // Read from the normal chain
                    into_desc(self.iter.next()?)
                }
            }
            // Read the remains of the self.current
            Some(desc) => Some(Ok(desc)),
        }
    }

    fn next_into_indirect(
        &mut self,
        range: DriverRange,
        limit: usize,
    ) -> Option<Result<DeviceRange<'a>, ChainError>> {
        assert!(self.current.is_none());
        if self.indirect_chain.is_some() {
            // Supplying the nested indirect chain violates the virtio spec
            // Either our processing is wrong or guest driver has a bug
            return Some(Err(ChainError::InvalidNestedIndirectChain));
        }

        match self.mem.translate(range.clone()) {
            Some(range) => {
                self.indirect_chain = Some(IndirectDescChain::new(range));
                self.next_with_limit(limit)
            }
            None => Some(Err(ChainError::TranslateFailed(range))),
        }
    }

    fn into_device_range(
        &mut self,
        access: DescAccess,
        range: DriverRange,
        limit: usize,
    ) -> Option<Result<DeviceRange<'a>, ChainError>> {
        match (Self::expected_access(), access) {
            // If descriptor we found matches what we expected then we return as much as we can
            // based on the requested limit.
            (DescAccess::DeviceWrite, DescAccess::DeviceWrite)
            | (DescAccess::DeviceRead, DescAccess::DeviceRead) => {
                let range = if let Some((range, rest)) = range.split_at(limit) {
                    // If we could split the range, and there is non-zero remaining, then stash the
                    // remaining portion for later and return the range that was split.
                    if rest.len() > 0 {
                        self.current = Some(Desc(DescType::Direct(access), rest));
                    }
                    range
                } else {
                    // Split failed, meaning we have less than was requested so we just return all
                    // of it.
                    range
                };
                Some(self.mem.translate(range.clone()).ok_or(ChainError::TranslateFailed(range)))
            }
            // This is a readable descriptor, while we are expecting a writable one.
            // This indicates a corrupt descriptor chain, so return an error.
            (DescAccess::DeviceWrite, DescAccess::DeviceRead) => {
                // Consume the rest of the iterator to ensure any future calls to next_with_limit
                // fail.
                self.iter.complete();
                Some(Err(ChainError::ReadableAfterWritable))
            }
            (DescAccess::DeviceRead, DescAccess::DeviceWrite) => {
                // Put the descriptor back as we might want to walk the writable section later.
                self.current = Some(Desc(DescType::Direct(access), range));
                None
            }
        }
    }

    fn next_with_limit(&mut self, limit: usize) -> Option<Result<DeviceRange<'a>, ChainError>> {
        match self.next_desc()? {
            Ok(Desc(desc_type, range)) => match desc_type {
                DescType::Direct(access) => self.into_device_range(access, range, limit),
                DescType::Indirect => self.next_into_indirect(range, limit),
            },
            Err(e) => Some(Err(e.into())),
        }
    }

    fn remaining(&self) -> Result<usize, ChainError> {
        let mut state = State::<N, M, E> {
            chain: None,
            mem: self.mem,
            iter: self.iter.clone(),
            current: self.current.clone(),
            indirect_chain: self.indirect_chain.clone(),
        };
        let mut total = 0;
        while let Some(v) = state.next_with_limit(usize::MAX) {
            total += v?.len();
        }
        Ok(total)
    }
}

// Allow easily transforming a read chain into a write.
impl<'a, 'b, N: DriverNotify, M> From<State<'a, 'b, N, M, false>> for State<'a, 'b, N, M, true> {
    fn from(state: State<'a, 'b, N, M, false>) -> State<'a, 'b, N, M, true> {
        State {
            chain: state.chain,
            iter: state.iter,
            current: state.current,
            mem: state.mem,
            indirect_chain: state.indirect_chain,
        }
    }
}

/// Errors resulting from completing a chain.
///
/// These errors are from the optional interfaces for completing and converting chains.
#[derive(Error, Debug, Clone, PartialEq, Eq)]
pub enum ChainCompleteError {
    #[error("Unexpected readable descriptor found")]
    ReadableRemaining,
    #[error("Unexpected writable descriptor found")]
    WritableRemaining,
    #[error("Chain walk error {0} when checking for descriptors")]
    Chain(#[from] ChainError),
}

/// Access the readable portion of a descriptor chain.
///
/// Provides access to the read-only portion of a descriptor chain. Can be [constructed directly]
/// (ReadableChain::new) from a [`DescChain`] and once finished with can either be dropped or
/// converted to a [`WritableChain`] if there are writable portions as well.
///
/// As the [`ReadableChain`] takes ownership of the [`DescChain`] dropping the [`ReadableChain`]
/// will automatically return the [`DescChain`] to the [`Queue`](crate::queue::Queue).
///
/// For devices and protocols where it is useful, the chain can also be explicitly returned via the
/// [`return_complete`](#return_complete) method to validate full consumption of the chain.
pub struct ReadableChain<'a, 'b, N: DriverNotify, M: DriverMem> {
    state: State<'a, 'b, N, M, false>,
}

impl<'a, 'b, N: DriverNotify, M: DriverMem> ReadableChain<'a, 'b, N, M> {
    /// Construct a [`ReadableChain`] from a [`DescChain`].
    ///
    /// Requires a reference to a [`DriverMem`] in order to perform translation into
    /// [`DeviceRange`].
    pub fn new(chain: DescChain<'a, 'b, N>, mem: &'a M) -> Self {
        let iter = chain.iter();
        ReadableChain {
            state: State { chain: Some(chain), mem, iter, current: None, indirect_chain: None },
        }
    }

    /// Immediately return a fully consumed chain.
    ///
    /// This both drops the chain, thus returning the underlying [`DescChain`] to the [`Queue`]
    /// (crate::queue::Queue), and also checks if it was fully walked, generating an error if not.
    /// Fully walked here means that there are no readable or writable sections that had not been
    /// iterated over.
    ///
    /// For virtio queues where the device is expected to fully consume what it is sent, and there
    /// is not expected to be anything to write, this provides a way to both check for correct
    /// device and driver functionality.
    pub fn return_complete(self) -> Result<(), ChainCompleteError> {
        WritableChain::from_readable(self)?.return_complete()
    }

    /// Request the next range of readable bytes, up to a limit.
    ///
    /// As the [`DeviceRange`] returned here represents a contiguous range this may return a smaller
    /// range than requested by `limit`, even if there is more readable descriptor(s) remaining. In
    /// this way the caller is directly exposed to size of the underlying descriptors in the chain
    /// as queued by the driver.
    ///
    /// A return value of `None` indicates there are no more readable descriptors, however there
    /// may still be readable descriptors.
    ///
    /// Should this ever return a `Some(Err(_))` it will always yield a `None` in future calls as
    /// the chain will be deemed corrupt. If walking and attempting to recover from corrupt chains
    /// is desirable, beyond just reporting an error, then you must use the [`DescChain`] directly
    /// and not this interface.
    pub fn next_with_limit(&mut self, limit: usize) -> Option<Result<DeviceRange<'a>, ChainError>> {
        self.state.next_with_limit(limit)
    }

    /// Request the next range of readable bytes.
    ///
    /// Similar to [`next_with_limit`](#next_with_limit) except limit is implicitly `usize::MAX`.
    /// This will therefore walk the descriptors in the structure as they were provided by the
    /// driver.
    pub fn next(&mut self) -> Option<Result<DeviceRange<'a>, ChainError>> {
        self.next_with_limit(usize::MAX)
    }

    /// Query readable bytes remaining.
    ///
    /// Returns the number of readable bytes remaining in the chain. This does not imply that
    /// calling [`next_with_limit`](#next_with_limit) with the result will return that much, see
    /// [`next_with_limit`](#next_with_limit) for more details.
    pub fn remaining(&self) -> Result<usize, ChainError> {
        self.state.remaining()
    }
}

impl<'a, 'b, N: DriverNotify, M: DriverMem> std::io::Read for ReadableChain<'a, 'b, N, M> {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        match self.next_with_limit(buf.len()) {
            None => Ok(0),
            Some(Err(e)) => Err(e.into()),
            Some(Ok(range)) => {
                let len = range.len();
                assert!(len <= buf.len());
                // This unwrap is safe as we are requesting a u8 pointer that has no alignment
                // constraints.
                let ptr = range.try_ptr().unwrap();
                // In the implementation of std::io::Write for WritableChain we use libc::memmove in
                // an attempt to ensure our copy cannot be elided. Here in the read path we do not
                // need to make guarantees as this not MMIO memory and reading has no side effects.
                // As such if the compiler can determine that the read data is not used, we would
                // very much like it to elide the copy.
                // We meet the safety requirements of copy_nonoverlapping since:
                // * buf is a reference to a slice and assumed to be valid
                // * ptr comes from `range`, which is a DeviceRange and is defined to be valid.
                unsafe { std::ptr::copy_nonoverlapping(ptr, buf.as_mut_ptr(), len) };
                Ok(len)
            }
        }
    }
}

/// Access the writable portion of a descriptor chain.
///
/// Provides access to the write-only portion of a descriptor chain. If no readable portion a
/// [`WritableChain`] can be constructed directly from a [`DescChain`], either [generating errors]
/// (WritiableChain::new) if there are readable portions, or [ignoring them]
/// (WritableChain::new_ignore_readable). Otherwise [`Readable`] chain can be [converted]
/// (WritableChain::from_readable) into a [`WritableChain`], with a similar option to
/// [ignore any remaining readable](WritableChain::from_incomplete_readable).
///
/// As the [`Writable`] takes ownership of the [`DescChain`] dropping the [`WritableChain`]
/// will automatically return the [`DescChain`] to the [`Queue`](crate::queue::Queue). To report
/// how much was written the [`WritableChain`] has an internal counter of how much you have claimed
/// to have written via [`add_written`](WritableChain::add_written). Walking the chain via
/// [`next`](WritableChain::next) or [`next_with_limit`](WritableChain::next_with_limit) does not
/// automatically increment the written counter as the [`WritableChain`] cannot assume how much of
/// the returned range was written to.
///
/// Writing to the chain via the [`std::io::Write`] trait will automatically increment the written
/// counter.
///
/// For devices and protocols where it is useful, the chain can also be explicitly returned via the
/// [`return_complete`](#return_complete) method to validate the full chain was written to.
pub struct WritableChain<'a, 'b, N: DriverNotify, M: DriverMem> {
    state: State<'a, 'b, N, M, true>,
    written: u32,
}

impl<'a, 'b, N: DriverNotify, M: DriverMem> WritableChain<'a, 'b, N, M> {
    /// Construct a [`WritableChain`] from a [`DescChain`].
    ///
    /// Requires a reference to a [`DriverMem`] in order to perform translation into
    /// [`DeviceRange`]. Generates an error if there are any readable portions.
    pub fn new(chain: DescChain<'a, 'b, N>, mem: &'a M) -> Result<Self, ChainCompleteError> {
        WritableChain::from_readable(ReadableChain::new(chain, mem))
    }

    /// Construct a [`WritableChain`] from a [`DescChain`], ignoring some errors.
    ///
    /// Same as [`new`](#new) but ignores any readable descriptors. It may still generate an error
    /// as a corrupt chain may be noticed when it is walked to skip any readable descriptors.
    pub fn new_ignore_readable(
        chain: DescChain<'a, 'b, N>,
        mem: &'a M,
    ) -> Result<Self, ChainError> {
        WritableChain::from_incomplete_readable(ReadableChain::new(chain, mem))
    }

    /// Convert a [`ReadableChain`] to a [`WritableChain`]
    ///
    /// Generates an error if there are still readable portions of the chain left.
    pub fn from_readable(
        mut readable: ReadableChain<'a, 'b, N, M>,
    ) -> Result<Self, ChainCompleteError> {
        match readable.next() {
            None => Ok(()),
            Some(Ok(_)) => Err(ChainCompleteError::ReadableRemaining),
            Some(Err(e)) => Err(e.into()),
        }?;
        Ok(WritableChain { state: readable.state.into(), written: 0 })
    }

    /// Convert a [`ReadableChain`] to a [`WritableChain`]
    ///
    /// Skips any remaining readable descriptors to construct a [`WritableChain`]. May still
    /// generate an error if there was a problem walking the chain.
    pub fn from_incomplete_readable(
        mut readable: ReadableChain<'a, 'b, N, M>,
    ) -> Result<Self, ChainError> {
        // Walk the readable iterator to the end, returning an error if one is found
        while let Some(_) = readable.next().transpose()? {}
        Ok(WritableChain { state: readable.state.into(), written: 0 })
    }

    /// Immediately return a fully consumed chain.
    ///
    /// Similar to [`ReadableChain::return_complete`].
    pub fn return_complete(mut self) -> Result<(), ChainCompleteError> {
        match self.next() {
            None => Ok(()),
            Some(Ok(_)) => Err(ChainCompleteError::WritableRemaining),
            Some(Err(e)) => Err(e.into()),
        }
    }

    /// Request the next range of readable bytes, up to a limit.
    ///
    /// Similar to [`ReadableChain::next_with_limit`]
    pub fn next_with_limit(&mut self, limit: usize) -> Option<Result<DeviceRange<'a>, ChainError>> {
        self.state.next_with_limit(limit)
    }

    /// Request the next range of readable bytes.
    ///
    /// Similar to [`ReadableChain::next`]
    pub fn next(&mut self) -> Option<Result<DeviceRange<'a>, ChainError>> {
        self.next_with_limit(usize::MAX)
    }

    /// Query writable bytes remaining.
    ///
    /// Similar to [`ReadableChain::remaining`]
    pub fn remaining(&self) -> Result<usize, ChainError> {
        self.state.remaining()
    }

    /// Increments the written bytes counter.
    ///
    /// If descriptor ranges returned from [`next`](#next) and [`next_with_limit`](#next_with_limit)
    /// are actually written to then the amount that is written needs to be added by calling this
    /// method, as the [`WritableChain`] itself does not know if, or how much, might have been
    /// returned to the returned ranges.
    ///
    /// Note if using the [`std::io::Write`] trait implementation to write to the chain this method
    /// does not need to be called, as the trait implementation will call it for you. You only need
    /// to call this if actually directly calling [`next`](#next) or [`next_with_limit`]
    /// (#next_with_limit).
    ///
    /// `add_written` is cumulative and can be called multiple times. No checking of this value is
    /// performed and it is up to the caller to choose to honor the virtio specification.
    pub fn add_written(&mut self, written: u32) {
        self.written += written;
    }
}

impl<'a, 'b, N: DriverNotify, M: DriverMem> Drop for WritableChain<'a, 'b, N, M> {
    fn drop(&mut self) {
        self.state.chain.take().unwrap().return_written(self.written);
    }
}

impl<'a, 'b, N: DriverNotify, M: DriverMem> std::io::Write for WritableChain<'a, 'b, N, M> {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        match self.next_with_limit(buf.len()) {
            None => Ok(0),
            Some(Err(e)) => Err(e.into()),
            Some(Ok(range)) => {
                let len = range.len();
                assert!(len <= buf.len());
                // This unwrap is safe as we are requesting a u8 pointer that has no alignment
                // constraints.
                let ptr = range.try_mut_ptr().unwrap();
                // We use libc::memmove over ptr::copy_nonoverlapping as ptr::copy_nonoverlapping
                // does not provide a strong guarantee that the copy cannot be elided. Ideally we
                // would perform a volatile copy, however volatile_copy_nonoverlapping_memory
                // intrinsic has no stable interface, and manually writing a loop of
                // ptr::write_volatile cannot be optimized equivalently. As such, performing an ffi
                // call to something we know cannot elide our operation, we can thus guarantee our
                // copy happens.
                // The safety requirements need to satisfy for memmove are the same as
                // ptr::copy_nonoverlapping and we this is safe since:
                // * buf is a reference to a slice and assumed to be valid
                // * ptr comes from `range`, which is a DeviceRange, and is defined to be valid
                // * len is checked for both of these ranges, and so the pointers are valid for the
                //   full range of bytes.
                unsafe { libc::memmove(ptr, buf.as_ptr() as *const libc::c_void, len) };
                self.add_written(len as u32);
                Ok(len)
            }
        }
    }
    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            fake_queue::{Chain, IdentityDriverMem, TestQueue},
            ring::DescAccess,
        },
        std::io::{Read, Write},
    };

    fn check_read<'a>(result: Option<Result<DeviceRange<'a>, ChainError>>, expected: &[u8]) {
        let range = result.unwrap().unwrap();
        assert_eq!(range.len(), expected.len());
        assert_eq!(
            // Calling slice::from_raw_parts is valid since
            // * This memory was allocated from a single TestDeviceRange block to become a
            //   descriptor.
            // * No references are hold elsewhere, mutable or otherwise. Other pointers exist, but
            //   they will not be dereferenced for the duration we hold this as a slice.
            // * fake_queue::ChainBuilder initialized the memory, not that types of 'u8' need any
            //   initialization.
            unsafe { std::slice::from_raw_parts::<u8>(range.try_ptr().unwrap(), range.len()) },
            expected
        );
    }

    fn check_returned(result: Option<(u64, u32)>, expected: &[u8]) {
        let (data, len) = result.unwrap();
        assert_eq!(len as usize, expected.len());
        assert_eq!(
            // See check_read for safety argument.
            unsafe { std::slice::from_raw_parts::<u8>(data as usize as *const u8, len as usize) },
            expected
        );
    }

    fn test_write<'a>(result: Option<Result<DeviceRange<'a>, ChainError>>, expected: u32) {
        let range = result.unwrap().unwrap();
        assert_eq!(range.len(), expected as usize);
    }

    fn test_write_data<'a>(result: Option<Result<DeviceRange<'a>, ChainError>>, data: &[u8]) {
        let range = result.unwrap().unwrap();
        assert_eq!(range.len(), data.len());
        // See check_read for safety argument.
        unsafe { std::slice::from_raw_parts_mut::<u8>(range.try_mut_ptr().unwrap(), range.len()) }
            .copy_from_slice(data);
    }

    fn test_smoke_test_body<'a>(state: &mut TestQueue<'a>, driver_mem: &'a IdentityDriverMem) {
        {
            let mut readable = ReadableChain::new(state.queue.next_chain().unwrap(), driver_mem);
            assert_eq!(readable.remaining(), Ok(12));
            check_read(readable.next(), &[1, 2, 3, 4]);
            assert_eq!(readable.remaining(), Ok(8));
            check_read(readable.next_with_limit(2), &[5, 6]);
            assert_eq!(readable.remaining(), Ok(6));
            check_read(readable.next_with_limit(200), &[7, 8]);
            assert_eq!(readable.remaining(), Ok(4));
            check_read(readable.next_with_limit(4), &[9, 10, 11, 12]);
            assert_eq!(readable.remaining(), Ok(0));
            assert!(readable.next().is_none());

            let mut writable = WritableChain::from_readable(readable).unwrap();
            test_write_data(writable.next_with_limit(3), &[1, 2, 3]);
            test_write_data(writable.next(), &[4]);
            test_write(writable.next(), 4);
            assert!(writable.next().is_none());

            writable.add_written(4);
        }

        let returned = state.fake_queue.next_used().unwrap();
        assert_eq!(returned.written(), 4);
        let mut iter = returned.data_iter();
        check_returned(iter.next(), &[1, 2, 3, 4]);
        assert!(iter.next().is_none());
    }

    #[test]
    fn test_smoke_test() {
        let driver_mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &driver_mem);
        assert!(state
            .fake_queue
            .publish(Chain::with_data::<u8>(
                &[&[1, 2, 3, 4], &[5, 6, 7, 8], &[9, 10, 11, 12]],
                &[4, 4],
                &driver_mem
            ))
            .is_some());
        test_smoke_test_body(&mut state, &driver_mem);
    }

    #[test]
    fn test_smoke_test_indirect_chain() {
        let driver_mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &driver_mem);
        assert!(state
            .fake_queue
            .publish_indirect(
                Chain::with_data::<u8>(
                    &[&[1, 2, 3, 4], &[5, 6, 7, 8], &[9, 10, 11, 12]],
                    &[4, 4],
                    &driver_mem
                ),
                &driver_mem
            )
            .is_some());

        test_smoke_test_body(&mut state, &driver_mem)
    }

    fn test_io_body<'a>(state: &mut TestQueue<'a>, driver_mem: &'a IdentityDriverMem) {
        {
            let mut readable = ReadableChain::new(state.queue.next_chain().unwrap(), driver_mem);
            let mut buffer: [u8; 2] = [0; 2];
            assert!(readable.read_exact(&mut buffer).is_ok());
            assert_eq!(&buffer, &[1, 2]);
            check_read(readable.next_with_limit(1), &[3]);
            let mut buffer: [u8; 5] = [0; 5];
            assert!(readable.read_exact(&mut buffer).is_ok());
            assert_eq!(&buffer, &[4, 5, 6, 7, 8]);
            let mut buffer = Vec::new();
            assert!(readable.read_to_end(&mut buffer).is_ok());
            assert_eq!(buffer, vec![9, 10, 11, 12]);

            let mut writable = WritableChain::from_readable(readable).unwrap();
            assert!(writable.write_all(&[1, 2, 3, 4, 5]).is_ok());
            assert!(writable.write_all(&[6, 7, 8]).is_ok());
            assert!(writable.write_all(&[9]).is_err());
            assert!(writable.flush().is_ok());
        }
        let returned = state.fake_queue.next_used().unwrap();
        assert_eq!(returned.written(), 8);
        let mut iter = returned.data_iter();
        check_returned(iter.next(), &[1, 2, 3, 4]);
        check_returned(iter.next(), &[5, 6, 7, 8]);
        assert!(iter.next().is_none());
    }

    #[test]
    fn test_io() {
        let driver_mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &driver_mem);
        assert!(state
            .fake_queue
            .publish(Chain::with_data::<u8>(
                &[&[1, 2, 3, 4], &[5, 6, 7, 8], &[9, 10, 11, 12]],
                &[4, 4],
                &driver_mem
            ))
            .is_some());
        test_io_body(&mut state, &driver_mem)
    }

    #[test]
    fn test_io_indirect_chain() {
        let driver_mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &driver_mem);
        assert!(state
            .fake_queue
            .publish_indirect(
                Chain::with_data::<u8>(
                    &[&[1, 2, 3, 4], &[5, 6, 7, 8], &[9, 10, 11, 12]],
                    &[4, 4],
                    &driver_mem
                ),
                &driver_mem
            )
            .is_some());
        test_io_body(&mut state, &driver_mem)
    }

    #[test]
    fn test_readable_completed() {
        let driver_mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &driver_mem);

        let mut test_return = |read, write, limit, expected| {
            assert!(state
                .fake_queue
                .publish(Chain::with_lengths(read, write, &driver_mem))
                .is_some());
            let mut readable = ReadableChain::new(state.queue.next_chain().unwrap(), &driver_mem);
            if limit == 0 {
                assert!(readable.next().unwrap().is_ok());
            } else {
                assert!(readable.next_with_limit(limit).unwrap().is_ok());
            }
            assert_eq!(readable.return_complete(), expected);
            assert!(state.fake_queue.next_used().is_some());
        };

        test_return(&[4], &[], 0, Ok(()));
        test_return(&[4], &[], 4, Ok(()));
        test_return(&[4, 2], &[], 0, Err(ChainCompleteError::ReadableRemaining));
        test_return(&[4], &[], 2, Err(ChainCompleteError::ReadableRemaining));
        test_return(&[4], &[4], 2, Err(ChainCompleteError::ReadableRemaining));
        test_return(&[4], &[4], 0, Err(ChainCompleteError::WritableRemaining));
        test_return(&[4], &[4], 4, Err(ChainCompleteError::WritableRemaining));
    }

    #[test]
    fn test_make_writable() {
        let driver_mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &driver_mem);

        assert!(state.fake_queue.publish(Chain::with_lengths(&[], &[4], &driver_mem)).is_some());
        assert!(WritableChain::new(state.queue.next_chain().unwrap(), &driver_mem).is_ok());
        assert!(state.fake_queue.next_used().is_some());

        assert!(state.fake_queue.publish(Chain::with_lengths(&[4], &[4], &driver_mem)).is_some());
        assert_eq!(
            WritableChain::new(state.queue.next_chain().unwrap(), &driver_mem).err().unwrap(),
            ChainCompleteError::ReadableRemaining
        );
        assert!(state.fake_queue.next_used().is_some());

        assert!(state.fake_queue.publish(Chain::with_lengths(&[4], &[4], &driver_mem)).is_some());
        assert!(WritableChain::new_ignore_readable(state.queue.next_chain().unwrap(), &driver_mem)
            .is_ok());
        assert!(state.fake_queue.next_used().is_some());
    }

    #[test]
    fn test_writable_completed() {
        let driver_mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &driver_mem);

        let mut test_return = |read, write, limit, expected| {
            assert!(state
                .fake_queue
                .publish(Chain::with_lengths(read, write, &driver_mem))
                .is_some());
            let mut writable =
                WritableChain::new(state.queue.next_chain().unwrap(), &driver_mem).unwrap();
            if limit == 0 {
                assert!(writable.next().unwrap().is_ok());
            } else {
                assert!(writable.next_with_limit(limit).unwrap().is_ok());
            }
            assert_eq!(writable.return_complete(), expected);
            assert!(state.fake_queue.next_used().is_some());
        };

        test_return(&[], &[4], 0, Ok(()));
        test_return(&[], &[4], 4, Ok(()));
        test_return(&[], &[4, 2], 0, Err(ChainCompleteError::WritableRemaining));
        test_return(&[], &[4], 2, Err(ChainCompleteError::WritableRemaining));
    }

    #[test]
    fn test_bad_chain() {
        let driver_mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &driver_mem);

        // Get memory for two descriptors so we can build our custom chain.
        let desc1 = driver_mem.new_range(10).unwrap();
        let desc2 = driver_mem.new_range(20).unwrap();

        assert!(state
            .fake_queue
            .publish(Chain::with_exact_data(&[
                (DescAccess::DeviceWrite, desc1.get().start as u64, desc1.len() as u32),
                (DescAccess::DeviceRead, desc2.get().start as u64, desc2.len() as u32)
            ]))
            .is_some());

        {
            let mut writable =
                WritableChain::new_ignore_readable(state.queue.next_chain().unwrap(), &driver_mem)
                    .unwrap();
            assert!(writable.next().unwrap().is_ok());
            assert_eq!(writable.next().unwrap().err().unwrap(), ChainError::ReadableAfterWritable);
        }
        assert!(state.fake_queue.next_used().is_some());
    }
}
