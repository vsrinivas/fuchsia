// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A wrapper to expose Zircon kernel objects for use in tokio.

#[macro_use]
extern crate futures;
#[macro_use]
extern crate tokio_core;
extern crate mio;
extern crate fuchsia_zircon as zircon;

mod channel;

pub use channel::Channel;

use std::io;

#[inline(always)]
fn would_block() -> io::Error {
    io::Error::from(io::ErrorKind::WouldBlock)
}
