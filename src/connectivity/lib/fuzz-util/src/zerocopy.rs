// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provides [`ArbitraryFromBytes`] to support generation of arbitrary instances
//! of [`zerocopy`]-friendly types.

use arbitrary::{Result, Unstructured};
use zerocopy::FromBytes;

/// Extension trait that allows construction of arbitrary values via
/// [`zerocopy::FromBytes`].
///
/// [`ArbitraryFromBytes`] has a blanket implementation for all types that
/// implement `zerocopy::FromBytes`.
pub trait ArbitraryFromBytes<'a>: FromBytes + Sized {
    /// Constructs an arbitrary instance of `Self` from the provided
    /// unstructured data.
    fn arbitrary_from_bytes(u: &mut Unstructured<'a>) -> Result<Self>;
}

impl<'a, A> ArbitraryFromBytes<'a> for A
where
    A: FromBytes + 'a,
{
    fn arbitrary_from_bytes(u: &mut Unstructured<'a>) -> Result<Self> {
        let mut bytes = vec![0u8; std::mem::size_of::<A>()];
        u.fill_buffer(&mut bytes)?;
        Ok(Self::read_from(&*bytes).unwrap())
    }
}
