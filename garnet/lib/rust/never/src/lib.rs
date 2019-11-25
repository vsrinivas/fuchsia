// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The never type.
//!
//! This crate defines [`Never`], which is a type that can never be constructed.
//! It is a stable version of the currently-unstable `!` type from the standard
//! library.

use std::error::Error;
use std::fmt::{self, Display, Formatter};

/// A type that can never be constructed.
///
/// `Never` is a stable version of the currently-unstable `!` type from the
/// standard library.
#[derive(Copy, Clone, Hash, Eq, PartialEq, Debug)]
pub enum Never {}

impl Never {
    /// Convert this `Never` into a value of a different type.
    ///
    /// Since a `Never` can never be constructed, this is valid for any `Sized`
    /// type.
    pub fn into_any<T>(self) -> T {
        match self {}
    }
}

impl<T: ?Sized> AsRef<T> for Never {
    fn as_ref(&self) -> &T {
        match *self {}
    }
}

impl<T: ?Sized> AsMut<T> for Never {
    fn as_mut(&mut self) -> &mut T {
        match *self {}
    }
}

impl Display for Never {
    fn fmt(&self, _: &mut Formatter<'_>) -> fmt::Result {
        match *self {}
    }
}

impl Error for Never {}
