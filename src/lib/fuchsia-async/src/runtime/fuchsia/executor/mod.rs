// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::future::{FutureObj, LocalFutureObj};
use std::future::Future;

mod common;
mod instrumentation;
mod local;
mod packets;
mod send;
mod time;

pub use common::EHandle;
pub use local::{LocalExecutor, TestExecutor, WaitState};
pub use packets::{need_signal, schedule_packet, PacketReceiver, ReceiverRegistration};
pub use send::SendExecutor;
pub use time::{Duration, Time};

use common::Inner;

/// Spawn a new task to be run on the global executor.
///
/// Tasks spawned using this method must be threadsafe (implement the `Send` trait),
/// as they may be run on either a singlethreaded or multithreaded executor.
#[cfg_attr(trace_level_logging, track_caller)]
pub(crate) fn spawn<F>(future: F)
where
    F: Future<Output = ()> + Send + 'static,
{
    Inner::spawn(&EHandle::local().inner, FutureObj::new(Box::new(future)));
}

/// Spawn a new task to be run on the global executor.
///
/// This is similar to the `spawn` function, but tasks spawned using this method
/// do not have to be threadsafe (implement the `Send` trait). In return, this method
/// requires that the current executor never be run in a multithreaded mode-- only
/// `run_singlethreaded` can be used.
#[cfg_attr(trace_level_logging, track_caller)]
pub(crate) fn spawn_local<F>(future: F)
where
    F: Future<Output = ()> + 'static,
{
    Inner::spawn_local(&EHandle::local().inner, LocalFutureObj::new(Box::new(future)));
}

/// Spawn a new task to be run on the given executor.
///
/// Tasks spawned using this method must be threadsafe (implement the `Send` trait),
/// as they may be run on either a singlethreaded or multithreaded executor.
#[cfg_attr(trace_level_logging, track_caller)]
pub(crate) fn spawn_on<F>(executor: &EHandle, future: F)
where
    F: Future<Output = ()> + Send + 'static,
{
    Inner::spawn(&executor.inner, FutureObj::new(Box::new(future)));
}
