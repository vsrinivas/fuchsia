// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// A internal implementation of `From`.
pub trait FromExt<T> {
    /// Performs the conversion.
    fn from_ext(f: T) -> Self;
}

/// A internal implementation of `Into`.
///
/// A blanket implementation is provided for implementers of `FromExt<T>`.
pub trait IntoExt<T> {
    /// Performs the conversion.
    fn into_ext(self) -> T;
}

impl<T, U> IntoExt<U> for T
where
    U: FromExt<T>,
{
    fn into_ext(self) -> U {
        U::from_ext(self)
    }
}
