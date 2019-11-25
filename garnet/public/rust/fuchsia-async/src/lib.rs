// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A futures-rs executor design specifically for Fuchsia OS.

#![deny(missing_docs)]

/// A future which can be used by multiple threads at once.
pub mod atomic_future;

mod channel;
pub use self::channel::{Channel, RecvMsg};
mod on_signals;
pub use self::on_signals::OnSignals;
mod rwhandle;
pub use self::rwhandle::RWHandle;
mod socket;
pub use self::socket::Socket;
mod timer;
pub use self::timer::{Interval, OnTimeout, TimeoutExt, Timer};
mod executor;
pub use self::executor::{
    spawn, spawn_local, DurationExt, EHandle, Executor, PacketReceiver, ReceiverRegistration, Time,
    WaitState,
};
mod fifo;
pub use self::fifo::{Fifo, FifoEntry, FifoReadable, FifoWritable, ReadEntry, WriteEntry};
pub mod net;

// Re-export pin_mut as its used by the async proc macros
pub use pin_utils::pin_mut;

pub use fuchsia_async_macro::{run, run_singlethreaded, run_until_stalled};

// TODO(cramertj) remove once async/awaitification has occurred
pub mod temp;

// Reexport futures for use in macros;
#[doc(hidden)]
pub mod futures {
    pub use futures::*;
}
