// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use anyhow::Error;
use core::fmt::Debug;
use spinel_pack::TryOwnedUnpack;
use std::io;

/// Combinator used with several commands for interpreting
/// the response into a concrete return type.
#[derive(Copy, Clone)]
pub struct PropReturning<RD: RequestDesc, T: TryOwnedUnpack> {
    inner: RD,
    phantom: std::marker::PhantomData<fn() -> *const T>,
}

impl<RD: RequestDesc + Debug, T: TryOwnedUnpack> std::fmt::Debug for PropReturning<RD, T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("PropReturning").field("inner", &self.inner).finish()
    }
}

impl<RD: RequestDesc, T: TryOwnedUnpack> PropReturning<RD, T> {
    pub(super) fn new(inner: RD) -> PropReturning<RD, T> {
        PropReturning { inner, phantom: std::marker::PhantomData }
    }
}

impl<RD, T> RequestDesc for PropReturning<RD, T>
where
    RD: RequestDesc,
    T: TryOwnedUnpack,
{
    type Result = T::Unpacked;

    fn write_request(&self, buffer: &mut dyn io::Write) -> io::Result<()> {
        self.inner.write_request(buffer)
    }

    fn on_response(
        self,
        response: Result<SpinelFrameRef<'_>, Canceled>,
    ) -> Result<Self::Result, Error> {
        #[spinel_packed("iD")]
        #[derive(Debug)]
        struct Response<'a>(Prop, &'a [u8]);

        self.inner.on_response(response.clone())?;

        T::try_owned_unpack_from_slice(Response::try_unpack_from_slice(response?.payload)?.1)
    }
}
