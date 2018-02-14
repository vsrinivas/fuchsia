// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A futures-rs executor design specifically for Fuchsia OS.

#![deny(warnings)]
#![deny(missing_docs)]

#[macro_use]
extern crate futures;
extern crate fuchsia_zircon as zx;
extern crate parking_lot;
extern crate slab;
extern crate smallvec;
extern crate tokio_io;

macro_rules! try_nb {
    ($e:expr) => (match $e {
        Ok(t) => t,
        Err(::zx::Status::SHOULD_WAIT) => {
            return Ok(::futures::Async::NotReady)
        }
        Err(e) => return Err(e.into()),
    })
}

/// A future which can be used by multiple threads at once.
pub mod atomic_future;

mod channel;
pub use channel::Channel;
mod on_signals;
pub use on_signals::OnSignals;
mod rwhandle;
pub use rwhandle::RWHandle;
mod socket;
pub use socket::Socket;
mod timer;
pub use timer::{Interval, Timeout};
mod executor;
pub use executor::{Executor, EHandle};
