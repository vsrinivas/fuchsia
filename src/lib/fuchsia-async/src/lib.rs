// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A futures-rs executor design specifically for Fuchsia OS.

#![deny(missing_docs)]

/// A future which can be used by multiple threads at once.
pub mod atomic_future;

#[cfg(target_os = "fuchsia")]
mod channel;
#[cfg(target_os = "fuchsia")]
pub use self::channel::{Channel, RecvMsg};
#[cfg(target_os = "fuchsia")]
mod on_signals;
#[cfg(target_os = "fuchsia")]
pub use self::on_signals::OnSignals;
#[cfg(target_os = "fuchsia")]
mod rwhandle;
#[cfg(target_os = "fuchsia")]
pub use self::rwhandle::RWHandle;
#[cfg(target_os = "fuchsia")]
mod socket;
#[cfg(target_os = "fuchsia")]
pub use self::socket::Socket;
#[cfg(target_os = "fuchsia")]
mod timer;
#[cfg(target_os = "fuchsia")]
pub use self::timer::{Interval, OnTimeout, TimeoutExt, Timer, WakeupTime};
#[cfg(target_os = "fuchsia")]
mod executor;
#[cfg(target_os = "fuchsia")]
pub use self::executor::{
    spawn, spawn_local, DurationExt, EHandle, Executor, PacketReceiver, ReceiverRegistration, Time,
    WaitState,
};
#[cfg(target_os = "fuchsia")]
mod fifo;
#[cfg(target_os = "fuchsia")]
pub use self::fifo::{Fifo, FifoEntry, FifoReadable, FifoWritable, ReadEntry, WriteEntry};
#[cfg(target_os = "fuchsia")]
pub mod net;
#[cfg(target_os = "fuchsia")]
mod task;
#[cfg(target_os = "fuchsia")]
pub use self::task::Task;

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
