// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A support library to allow creating arbitrary instances of certain types by
//! providing impls of [`arbitrary::Arbitrary`].

#![deny(missing_docs)]

mod packet_formats;
pub mod zerocopy;

/// A wrapper type that can be used to generate arbitrary `A`s for fuzzing by
/// implementing [`arbitrary::Arbitrary`] for specific types.
///
/// This makes it possible to generate arbitrary instances of a type even if
/// that type doesn't impl `arbitrary::Arbitrary`.
#[derive(Copy, Clone, Debug)]
pub struct Fuzzed<A>(A);

impl<A> Fuzzed<A> {
    /// Produces the object created from arbitrary input.
    pub fn into(self: Fuzzed<A>) -> A {
        let Self(a) = self;
        a
    }
}
