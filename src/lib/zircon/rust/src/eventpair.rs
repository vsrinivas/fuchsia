// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon event pairs.

use crate::ok;
use crate::{AsHandleRef, Handle, HandleBased, HandleRef, Peered, Status};
use fuchsia_zircon_sys as sys;

/// An object representing a Zircon
/// [event_pair](https://fuchsia.dev/fuchsia-src/concepts/kernel/concepts#events_event_pairs)
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct EventPair(Handle);
impl_handle_based!(EventPair);
impl Peered for EventPair {}

impl EventPair {
    /// Create an event pair, a pair of objects which can signal each other. Wraps the
    /// [zx_eventpair_create](https://fuchsia.dev/fuchsia-src/reference/syscalls/eventpair_create.md)
    /// syscall.
    pub fn create() -> Result<(EventPair, EventPair), Status> {
        let mut out0 = 0;
        let mut out1 = 0;
        let options = 0;
        let status = unsafe { sys::zx_eventpair_create(options, &mut out0, &mut out1) };
        ok(status)?;
        unsafe { Ok((Self::from(Handle::from_raw(out0)), Self::from(Handle::from_raw(out1)))) }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{DurationNum, Signals, Time};

    #[test]
    fn wait_and_signal_peer() {
        let (p1, p2) = EventPair::create().unwrap();
        let eighty_ms = 80.millis();

        // Waiting on one without setting any signal should time out.
        assert_eq!(p2.wait_handle(Signals::USER_0, Time::after(eighty_ms)), Err(Status::TIMED_OUT));

        // If we set a signal, we should be able to wait for it.
        assert!(p1.signal_peer(Signals::NONE, Signals::USER_0).is_ok());
        assert_eq!(
            p2.wait_handle(Signals::USER_0, Time::after(eighty_ms)).unwrap(),
            Signals::USER_0
        );

        // Should still work, signals aren't automatically cleared.
        assert_eq!(
            p2.wait_handle(Signals::USER_0, Time::after(eighty_ms)).unwrap(),
            Signals::USER_0
        );

        // Now clear it, and waiting should time out again.
        assert!(p1.signal_peer(Signals::USER_0, Signals::NONE).is_ok());
        assert_eq!(p2.wait_handle(Signals::USER_0, Time::after(eighty_ms)), Err(Status::TIMED_OUT));
    }
}
