// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities which allow code to be more robust to changes in dependencies.
//!
//! The utilities in this crate allow code to depend on details of its
//! dependencies which would not normally be captured by code using the
//! canonical Rust style. See [this document][rust-patterns] for a discussion of
//! when and why this might be desirable.
//!
//! [rust-patterns]: https://fuchsia.dev/fuchsia-src/contribute/contributing-to-netstack/rust-patterns

#![no_std]
#![deny(missing_docs)]

/// An extension trait adding functionality to [`Result`].
pub trait ResultExt<T, E> {
    /// Like [`Result::ok`], but the caller must provide the error type being
    /// discarded.
    ///
    /// This allows code to be written which will stop compiling if a result's
    /// error type changes in the future.
    fn ok_checked<EE: sealed::EqType<E>>(self) -> Option<T>;
}

impl<T, E> ResultExt<T, E> for Result<T, E> {
    fn ok_checked<EE: sealed::EqType<E>>(self) -> Option<T> {
        Result::ok(self)
    }
}

mod sealed {
    /// `EqType<T>` indicates that the implementer is equal to `T`.
    ///
    /// For all `T`, `T: EqType<T>`. For all distinct `T` and `U`, `T:
    /// !EqType<U>`.
    pub trait EqType<T> {}

    impl<T> EqType<T> for T {}
}
