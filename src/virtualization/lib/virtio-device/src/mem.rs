// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Structures for talking about memory shared between virtio devices and drivers
//!
//! Drivers, located in the guest, and devices, located in the host, operate with shared memory, but
//! may use different addresses to access it. All addresses published into the virtio data
//! structures are published by the driver and so refer to memory addresses as understood by the
//! driver.
//!
//! It is the responsibility of the host to know how to turn an address published by the driver,
//! a [`DriverRange`], into memory that can be ultimately be dereferenced and read/written to by
//! this device code, i.e. a [`DeviceRange`]. The [`DriverMem`] trait defines an interface for
//! performing this translation, although it is the responsibility of the application code to
//! provide and pass in implementations of this trait.
//!
//! [`DeviceRange`] is intended to describe valid ranges of memory that can be turned into pointers
//! through  [`try_ptr`](DeviceRange::try_ptr) and [`try_mut_ptr`](DeviceRange::try_mut_ptr) to
//! actually read and write.

use std::{
    convert::{From, TryFrom},
    marker::PhantomData,
    mem,
    ops::Range,
};

/// Represents a range of memory as seen from the driver.
///
/// These ranges are only published by the driver and should never otherwise need to be created. The
/// The only meaningful thing that can be done with them is to use the [`DriverMem::translate`]
/// method to attempt to turn it into a [`DeviceRange`].
#[derive(Debug, Clone, Eq, PartialEq)]
pub struct DriverRange(pub Range<usize>);

impl DriverRange {
    /// Split the range at `offset` producing two new ranges.
    ///
    /// Returns `None` if `offset` is not in the range, otherwise produces the two ranges
    /// `[start.. start + offset)` and `[start + offset ..end)`.
    pub fn split_at(&self, offset: usize) -> Option<(Self, Self)> {
        if self.0.len() < offset {
            None
        } else {
            let mid = self.0.start + offset;
            Some((Self(self.0.start..mid), Self(mid..self.0.end)))
        }
    }

    pub fn len(&self) -> usize {
        self.0.len()
    }
}

impl TryFrom<(u64, u32)> for DriverRange {
    type Error = ();

    fn try_from(range: (u64, u32)) -> Result<Self, Self::Error> {
        let (start, len) = range;
        let start = start as usize;
        let end = start.checked_add(len as usize).ok_or(())?;
        Ok(DriverRange(start..end))
    }
}

impl From<Range<usize>> for DriverRange {
    fn from(range: Range<usize>) -> Self {
        Self(range)
    }
}

/// Represents a range of memory as seen from the device.
///
/// A [`DeviceRange`] can be accessed through the [`try_ptr`](#try_ptr) and
/// [`try_mut_ptr`](#try_mut_ptr) methods. Although these functions are guaranteed to point to
/// valid memory, due to the requirements on [`new`](#new), raw pointers are still returned as the
/// memory may always be being modified in parallel by the guest and so references cannot be safely
/// created. The onus therefore is on the caller to access this memory in a way that is safe under
/// concurrent modifications.
///
/// Although there may be concurrent modifications, these are only from the guest, and it can be
/// assumed that a [`DeviceRange`] does not alias any other Rust objects from the heap, stack,
/// globals etc.
///
/// With the requirements on [`new`](#new) users of a [`DeviceRange`] can assume that pointers
/// returned from [`try_ptr`](#try_ptr) and [`try_mut_ptr`](#try_mut_ptr) are valid for reads and
/// writes and are correctly aligned. Further, it can be assumed that `ptr.offset(N)` is valid for
/// any `N < len() / size_of::<T>()`. These pointer are only valid as long as the original
/// [`DeviceRange`] is still alive.
///
/// The expected way to get [`DeviceRange`] is through [`DriverMem::translate`], and it is only
/// implementations of that trait that are expected to use [`new`](#new) and actually construct
/// a [`DeviceRange`].
#[derive(Debug, Clone, Eq, PartialEq)]
pub struct DeviceRange<'a>(Range<usize>, PhantomData<&'a ()>);

impl<'a> DeviceRange<'a> {
    /// Split the range at `offset` producing two new ranges.
    ///
    /// Returns `None` if `offset` is not in the range, otherwise produces the two ranges
    /// `[start.. start + offset)` and `[start + offset ..end)`.
    pub fn split_at(&self, offset: usize) -> Option<(Self, Self)> {
        if self.0.len() < offset {
            None
        } else {
            let mid = self.0.start + offset;
            // The returned ranges do not exceed the original range of self, and we return them for
            // the same lifetime `'a` as self. Therefore, as long as the original range was valid,
            // our produced ranges are too.
            unsafe { Some((Self::new(self.0.start..mid), Self::new(mid..self.0.end))) }
        }
    }

    pub fn len(&self) -> usize {
        self.0.len()
    }
    /// Construct a new [`DeviceRange`].
    ///
    /// # Safety
    ///
    /// The provided range must be:
    /// - Valid memory that can be read or written to if it were cast to a pointer.
    /// - Not alias any Rust objects from the heap, stack, globals etc.
    /// - Remain valid for the lifetime `'a`.
    pub unsafe fn new(range: Range<usize>) -> Self {
        Self(range, PhantomData)
    }

    /// Attempt to get a pointer to a mutable `T` at the start of the range.
    ///
    /// Returns a `None` if the range is too small to represent a `T`, or if the start of the range
    /// has the wrong alignment. Although there ways to safely perform accesses to unaligned
    /// pointers, as virtio requires all objects to be placed with correct alignment any
    /// misalignment represents a configuration issue.
    ///
    /// The caller may assume that if a pointer is returned that it is valid for reads and writes of
    /// an object of size and alignment of T, however no guarantee is made on T being a copy-able
    /// object that can be safely read or written. Further, the returned pointer is valid only as
    /// long as the underlying [`DeviceRange`] is alive.
    pub fn try_mut_ptr<T>(&self) -> Option<*mut T> {
        if self.len() < mem::size_of::<T>() {
            return None;
        }
        let byte_ptr = self.0.start as *mut u8;
        if byte_ptr.align_offset(mem::align_of::<T>()) != 0 {
            return None;
        }
        Some(byte_ptr.cast())
    }

    /// Attempt to get a pointer to a `T` at the start of the range.
    ///
    /// See `try_mut_ptr`.
    pub fn try_ptr<T>(&self) -> Option<*const T> {
        self.try_mut_ptr().map(|x| x as *const T)
    }

    /// Retrieve the underlying range.
    pub fn get(&self) -> Range<usize> {
        self.0.clone()
    }
}

/// Provides interface for converting from a [`DriverRange`] to a [`DeviceRange`].
pub trait DriverMem {
    /// Attempt to turn a [`DriverRange`] into a [`DeviceRange`].
    ///
    /// May return `None` if [`DriverRange`] does not represent valid driver memory, otherwise should
    /// return the corresponding `DeviceRange`. [`DriverMem`] is borrowed for lifetime of the
    /// returned [`DeviceRange`] ensuring that any returned ranges do not outlive the backing
    /// memory.
    fn translate<'a>(&'a self, driver: DriverRange) -> Option<DeviceRange<'a>>;
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn test_split_at() {
        let range = DriverRange(10..20);
        assert_eq!(range.split_at(4), Some((DriverRange(10..14), DriverRange(14..20))));
        assert_eq!(range.split_at(9), Some((DriverRange(10..19), DriverRange(19..20))));
        let (r1, r2) = range.split_at(10).unwrap();
        assert_eq!(r1, DriverRange(10..20));
        assert_eq!(r2.len(), 0);
        let (r1, r2) = range.split_at(0).unwrap();
        assert_eq!(r1.len(), 0);
        assert_eq!(r2, DriverRange(10..20));
    }
    #[test]
    fn test_ptr() {
        // We build some invalid DeviceRanges here, but we know not to dereference any pointers from
        // them so this safe.
        unsafe {
            assert!(mem::align_of::<u64>() > 1);
            let range = DeviceRange::new(65..128);
            assert!(range.try_ptr::<u64>().is_none());
            let range = DeviceRange::new(64..128);
            assert!(range.try_ptr::<u64>().is_some());
            let range = DeviceRange::new(64..(64 + mem::size_of::<u64>() - 1));
            assert!(range.try_ptr::<u64>().is_none());
        }
    }
}
