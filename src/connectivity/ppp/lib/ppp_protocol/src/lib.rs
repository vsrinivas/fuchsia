// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for implementing PPP control protocols.
//!
//! Provides a generic implementation of an LCP-like protocol state machine, and implementations of
//! LCP, IPCP and IPV6CP using this generic implementation.

#![deny(missing_docs)]

pub mod ppp;

pub mod ipv4;
pub mod ipv6;
pub mod link;

mod test;

use packet::{Buf, Either, EmptyBuf};

/// Flatten an `Either<EmptyBuf, Buf<Vec<u8>>>` into a `Buf<Vec<u8>>`.
///
/// `flatten_either` either unwraps the `B` variant or, if the `A` variant is
/// present, returns `Buf::new(Vec::new())`.
fn flatten_either(buf: Either<EmptyBuf, Buf<Vec<u8>>>) -> Buf<Vec<u8>> {
    match buf {
        Either::A(_) => Buf::new(Vec::new(), ..),
        Either::B(buf) => buf,
    }
}
