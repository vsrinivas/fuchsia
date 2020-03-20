// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The never type.
//!
//! This crate defines [`Never`], which is a type that can never be constructed
//! (in type theory parlance, it is "uninhabited"). It is a stable version of
//! the currently-unstable [`!`] type from the standard library.
//!
//! By default, this crate links against `std`. This is enabled via the `std`
//! feature, which is on by default. To make this crate `no_std`, disable
//! default features.
//!
//! [`!`]: https://doc.rust-lang.org/std/primitive.never.html

#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "std")]
extern crate core;

use core::fmt::{self, Display, Formatter};
#[cfg(feature = "std")]
use std::{
    error::Error,
    io::{BufRead, Read, Seek, SeekFrom, Write},
};

/// A type that can never be constructed.
///
/// `Never` can never be constructed (in type theory parlance, it is
/// "uninhabited"). It represents any computation which never resolves to a
/// particular value (because it runs forever, panics, aborts the process, etc).
/// Because the `Never` type can never be constructed, the existence of a
/// `Never` proves that a piece of code can never be reached.
///
/// For example, we could write a function like:
///
/// ```rust
/// # use never::Never;
/// fn result_into_ok<T>(res: Result<T, Never>) -> T {
///     match res {
///         Ok(t) => t,
///         // This branch can never be taken, and so the
///         // compiler is happy to treat it as evaluating
///         // to whatever type we wish - in this case, `T`.
///         Err(never) => match never {},
///     }
/// }
/// ```
///
/// Generalizing, it is always valid to convert a `Never` into a value of any
/// other type. We provide the [`into_any`] and [`to_any`] methods for this
/// purpose.
///
/// `Never` is a stable version of the currently-unstable [`!`] type from the
/// standard library.
///
/// [`into_any`]: crate::Never::into_any
/// [`to_any`]: crate::Never::to_any
/// [`!`]: https://doc.rust-lang.org/std/primitive.never.html
#[derive(Copy, Clone, Hash, Eq, PartialEq, Ord, PartialOrd, Debug)]
pub enum Never {}

impl Never {
    /// Convert this `Never` into a value of a different type.
    ///
    /// Since a `Never` can never be constructed, this is valid for any `Sized`
    /// type.
    pub fn into_any<T>(self) -> T {
        match self {}
    }

    /// Convert this `Never` into a value of a different type.
    ///
    /// Since a `Never` can never be constructed, this is valid for any `Sized`
    /// type.
    pub fn to_any<T>(&self) -> T {
        match *self {}
    }
}

impl<T: ?Sized> AsRef<T> for Never {
    fn as_ref(&self) -> &T {
        self.to_any()
    }
}

impl<T: ?Sized> AsMut<T> for Never {
    fn as_mut(&mut self) -> &mut T {
        self.to_any()
    }
}

impl Display for Never {
    fn fmt(&self, _: &mut Formatter<'_>) -> fmt::Result {
        self.to_any()
    }
}

#[cfg(feature = "std")]
impl Error for Never {}

#[cfg(feature = "std")]
impl Read for Never {
    fn read(&mut self, _buf: &mut [u8]) -> std::io::Result<usize> {
        self.to_any()
    }
}

#[cfg(feature = "std")]
impl BufRead for Never {
    fn fill_buf(&mut self) -> std::io::Result<&[u8]> {
        self.to_any()
    }

    fn consume(&mut self, _amt: usize) {
        self.to_any()
    }
}

#[cfg(feature = "std")]
impl Seek for Never {
    fn seek(&mut self, _pos: SeekFrom) -> std::io::Result<u64> {
        self.to_any()
    }
}

#[cfg(feature = "std")]
impl Write for Never {
    fn write(&mut self, _buf: &[u8]) -> std::io::Result<usize> {
        self.to_any()
    }

    fn flush(&mut self) -> std::io::Result<()> {
        self.to_any()
    }
}
