// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A futures-rs executor design specifically for Fuchsia OS.

#![deny(missing_docs)]

#[cfg(target_os = "fuchsia")]
mod executor;
#[cfg(target_os = "fuchsia")]
pub use self::executor::{
    spawn, spawn_local, DurationExt, EHandle, Executor, PacketReceiver, ReceiverRegistration, Time,
    WaitState,
};
#[cfg(target_os = "fuchsia")]
pub mod net;
#[cfg(target_os = "fuchsia")]
mod task;
#[cfg(target_os = "fuchsia")]
pub use self::task::Task;
#[cfg(target_os = "fuchsia")]
mod zircon_handle;

#[cfg(target_os = "fuchsia")]
mod fuchsia {
    pub(crate) use super::zircon_handle as platform_handle;
    pub use super::zircon_handle::fifo::{
        Fifo, FifoEntry, FifoReadable, FifoWritable, ReadEntry, WriteEntry,
    };
    pub use super::zircon_handle::on_signals::OnSignals;
    pub use super::zircon_handle::rwhandle::RWHandle;
}
#[cfg(target_os = "fuchsia")]
pub use fuchsia::*;

#[cfg(not(target_os = "fuchsia"))]
pub mod emulated_handle;

#[cfg(not(target_os = "fuchsia"))]
mod not_fuchsia {
    pub(crate) use super::emulated_handle as platform_handle;
}
#[cfg(not(target_os = "fuchsia"))]
pub(crate) use not_fuchsia::*;

pub use platform_handle::channel::{Channel, RecvMsg};
pub use platform_handle::socket::Socket;

#[cfg(target_os = "fuchsia")]
mod timer;
#[cfg(target_os = "fuchsia")]
pub use self::timer::{Interval, OnTimeout, TimeoutExt, Timer, WakeupTime};

/// A future which can be used by multiple threads at once.
pub mod atomic_future;

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
