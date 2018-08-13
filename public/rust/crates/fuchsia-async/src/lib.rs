// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A futures-rs executor design specifically for Fuchsia OS.

#![feature(async_await, await_macro, futures_api, pin, arbitrary_self_types)]
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
pub use self::timer::{Interval, Timer, TimeoutExt, OnTimeout};
mod executor;
pub use self::executor::{Executor, EHandle, spawn, spawn_local};
mod fifo;
pub use self::fifo::{Fifo, FifoEntry, FifoReadable, FifoWritable, ReadEntry, WriteEntry};
pub mod net;

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
            fn poll(self: ::std::mem::PinMut<Self>,
                    cx: &mut $crate::futures::task::Context,
            ) -> $crate::futures::Poll<Self::Output> {
                unsafe {
                    match ::std::mem::PinMut::get_mut_unchecked(self) {
                        $future::$first(x) =>
                            $crate::futures::Future::poll(
                                ::std::mem::PinMut::new_unchecked(x), cx),
                        $(
                            $future::$subfuture(x) =>
                                $crate::futures::Future::poll(
                                    ::std::mem::PinMut::new_unchecked(x), cx),
                        )*
                    }
                }
            }
        }
    }
}
