// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Magenta waitset objects.

use {HandleBase, Handle, HandleRef, Signals, Status, Time};
use {sys, into_result, size_to_u32_sat};

// This is the lowest level interface, strictly in terms of cookies.

/// An object representing a Magenta
/// [waitset](https://fuchsia.googlesource.com/magenta/+/master/docs/objects/waitset.md).
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
pub struct WaitSet(Handle);

impl HandleBase for WaitSet {
    fn get_ref(&self) -> HandleRef {
        self.0.get_ref()
    }

    fn from_handle(handle: Handle) -> Self {
        WaitSet(handle)
    }
}

impl WaitSet {
    /// Create a wait set.
    ///
    /// Wraps the
    /// [mx_waitset_create](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/waitset_create.md)
    /// sycall.
    pub fn create(options: WaitSetOpts) -> Result<WaitSet, Status> {
        let mut handle = 0;
        let status = unsafe { sys::mx_waitset_create(options as u32, &mut handle) };
        into_result(status, ||
            WaitSet::from_handle(Handle(handle)))
    }

    /// Add an entry to a wait set.
    ///
    /// Wraps the
    /// [mx_waitset_add](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/waitset_add.md)
    /// sycall.
    pub fn add<H>(&self, handle: &H, cookie: u64, signals: Signals) -> Result<(), Status>
        where H: HandleBase
    {
        let status = unsafe {
            sys::mx_waitset_add(self.raw_handle(), cookie, handle.raw_handle(), signals)
        };
        into_result(status, || ())
    }

    /// Remove an entry from a wait set.
    ///
    /// Wraps the
    /// [mx_waitset_remove](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/waitset_remove.md)
    /// sycall.
    pub fn remove(&self, cookie: u64) -> Result<(), Status> {
        let status = unsafe { sys::mx_waitset_remove(self.raw_handle(), cookie) };
        into_result(status, || ())
    }

    /// Wait for one or more entires to be signalled.
    ///
    /// Wraps the
    /// [mx_waitset_wait](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/waitset_wait.md)
    /// sycall.
    ///
    /// The caller must make sure `results` has enough capacity. If the length is
    /// equal to the capacity on return, that may be interpreted as a sign that
    /// the capacity should be expanded.
    pub fn wait(&self, timeout: Time, results: &mut Vec<WaitSetResult>)
        -> Result<(), Status>
    {
        unsafe {
            let mut count = size_to_u32_sat(results.capacity());
            let status = sys::mx_waitset_wait(self.raw_handle(), timeout,
                results.as_mut_ptr() as *mut sys::mx_waitset_result_t,
                &mut count);
            if status != sys::NO_ERROR {
                results.clear();
                return Err(Status::from_raw(status));
            }
            results.set_len(count as usize);
            Ok(())
        }
    }
}

/// An element of the result of `WaitSet::wait`. See
/// [waitset_wait](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/waitset_wait.md)
/// for more information about the underlying structure.
pub struct WaitSetResult(sys::mx_waitset_result_t);

impl WaitSetResult {
    /// The cookie used to identify the wait, same as was given in `WaitSet::add`.
    pub fn cookie(&self) -> u64 {
        self.0.cookie
    }

    /// The status. NoError if the signals are satisfied, ErrBadState if the signals
    /// became unsatisfiable, or ErrHandleClosed if the handle was dropped.
    pub fn status(&self) -> Status {
        Status::from_raw(self.0.status)
    }

    /// The observed signals at some point shortly before `WaitSet::wait` returned.
    pub fn observed(&self) -> Signals {
        self.0.observed
    }
}

/// Options for creating wait sets. None supported yet.
#[repr(u32)]
pub enum WaitSetOpts {
    /// Default options.
    Default = 0,
}

impl Default for WaitSetOpts {
    fn default() -> Self {
        WaitSetOpts::Default
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {Event, EventOpts, MX_SIGNAL_NONE, MX_USER_SIGNAL_0, MX_USER_SIGNAL_1, Time};

    #[test]
    fn waitset() {
        let ten_ms: Time = 10_000_000;
        let cookie1 = 1;
        let cookie2 = 2;
        let e1 = Event::create(EventOpts::Default).unwrap();
        let e2 = Event::create(EventOpts::Default).unwrap();

        let waitset = WaitSet::create(WaitSetOpts::Default).unwrap();
        assert!(waitset.add(&e1, cookie1, MX_USER_SIGNAL_0).is_ok());
        // Adding another handle with the same cookie should fail
        assert_eq!(waitset.add(&e2, cookie1, MX_USER_SIGNAL_0), Err(Status::ErrAlreadyExists));
        assert!(waitset.add(&e2, cookie2, MX_USER_SIGNAL_1).is_ok());

        // Waiting on the waitset now should time out.
        let mut results = Vec::with_capacity(2);
        assert_eq!(waitset.wait(ten_ms, &mut results), Err(Status::ErrTimedOut));
        assert_eq!(results.len(), 0);

        // Signal one object and it should return success.
        assert!(e1.signal(MX_SIGNAL_NONE, MX_USER_SIGNAL_0).is_ok());
        assert!(waitset.wait(ten_ms, &mut results).is_ok());
        assert_eq!(results.len(), 1);
        assert_eq!(results[0].cookie(), cookie1);
        assert_eq!(results[0].status(), Status::NoError);
        assert_eq!(results[0].observed(), MX_USER_SIGNAL_0);

        // Signal the other and it should return both.
        assert!(e2.signal(MX_SIGNAL_NONE, MX_USER_SIGNAL_1).is_ok());
        assert!(waitset.wait(ten_ms, &mut results).is_ok());
        assert_eq!(results.len(), 2);
        assert_eq!(results[0].cookie(), cookie1);
        assert_eq!(results[0].status(), Status::NoError);
        assert_eq!(results[0].observed(), MX_USER_SIGNAL_0);
        assert_eq!(results[1].cookie(), cookie2);
        assert_eq!(results[1].status(), Status::NoError);
        assert_eq!(results[1].observed(), MX_USER_SIGNAL_1);

        // Remove one and clear signals on the other; now it should time out again.
        assert!(waitset.remove(cookie1).is_ok());
        assert!(e2.signal(MX_USER_SIGNAL_1, MX_SIGNAL_NONE).is_ok());
        assert_eq!(waitset.wait(ten_ms, &mut results), Err(Status::ErrTimedOut));
        assert_eq!(results.len(), 0);
    }
}
