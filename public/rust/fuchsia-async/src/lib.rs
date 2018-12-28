// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A futures-rs executor design specifically for Fuchsia OS.

#![feature(async_await, await_macro, futures_api)]
#![deny(warnings)]
#![deny(missing_docs)]

// Set the system allocator for anything using this crate
extern crate fuchsia_system_alloc;

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
pub use self::executor::{spawn, spawn_local, EHandle, Executor};
mod fifo;
pub use self::fifo::{Fifo, FifoEntry, FifoReadable, FifoWritable, ReadEntry, WriteEntry};
pub mod net;

// Rexport pin_mut as its used by the async proc macros
pub use pin_utils::pin_mut;

pub use fuchsia_async_macro::{run, run_singlethreaded, run_until_stalled};

// TODO(cramertj) remove once async/awaitification has occurred
pub mod temp;

// Reexport futures for use in macros;
#[doc(hidden)]
pub mod futures {
    pub use futures::*;
}

/// Safety: manual `Drop` or `Unpin` impls are not allowed on the resulting type
#[macro_export]
macro_rules! unsafe_many_futures {
    ($future:ident, [$first:ident, $($subfuture:ident $(,)*)*]) => {

        enum $future<$first, $($subfuture,)*> {
            $first($first),
            $(
                $subfuture($subfuture),
            )*
        }

        impl<$first, $($subfuture,)*> $crate::futures::Future for $future<$first, $($subfuture,)*>
        where
            $first: $crate::futures::Future,
            $(
                $subfuture: $crate::futures::Future<Output = $first::Output>,
            )*
        {
            type Output = $first::Output;
            fn poll(self: ::std::pin::Pin<&mut Self>,
                    lw: &$crate::futures::task::LocalWaker,
            ) -> $crate::futures::Poll<Self::Output> {
                unsafe {
                    match ::std::pin::Pin::get_unchecked_mut(self) {
                        $future::$first(x) =>
                            $crate::futures::Future::poll(
                                ::std::pin::Pin::new_unchecked(x), lw),
                        $(
                            $future::$subfuture(x) =>
                                $crate::futures::Future::poll(
                                    ::std::pin::Pin::new_unchecked(x), lw),
                        )*
                    }
                }
            }
        }
    }
}

#[cfg(test)]
mod unsafe_many_futures_test {
    #![allow(unused)]
    unsafe_many_futures!(Compiles, [First, Second, Third]);
}
