// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

use std::pin::Pin;
use std::task::{Context, Poll, Waker};

/// Methods from the
/// [OpenThread "Tasklets" Module](https://openthread.io/reference/group/api-tasklets).
///
/// This trait has additional methods to allow for efficient use in asynchronous rust.
pub trait Tasklets: Unpin {
    /// Sets the waker to be used to wake up the tasklet future.
    fn set_waker(&self, waker: Waker);

    /// Wakes the waker previously passed to [`set_waker`].
    fn wake_waker(&self);

    /// Functional equivalent to [`otsys::otTaskletsProcess`](crate::otsys::otTaskletsProcess).
    fn process(&self);

    /// Functional equivalent to
    /// [`otsys::otTaskletsHasPending`](crate::otsys::otTaskletsHasPending).
    fn has_pending(&self) -> bool;
}

impl<T: Tasklets + ot::Boxable> Tasklets for ot::Box<T> {
    fn set_waker(&self, waker: Waker) {
        self.as_ref().set_waker(waker)
    }

    fn wake_waker(&self) {
        self.as_ref().wake_waker()
    }

    fn process(&self) {
        self.as_ref().process()
    }

    fn has_pending(&self) -> bool {
        self.as_ref().has_pending()
    }
}

/// Trait that provides the [`process_poll()`] method.
pub trait ProcessPollAsync {
    /// Processes all tasks that need to be handled for this instance,
    /// including those from the platform implementation.
    fn process_poll(self: &Self, cx: &mut Context<'_>) -> std::task::Poll<Option<()>>;
}

impl ProcessPollAsync for ot::Instance {
    fn process_poll(self: &Self, cx: &mut Context<'_>) -> std::task::Poll<Option<()>> {
        self.platform_poll(cx);
        self.set_waker(cx.waker().clone());
        if self.has_pending() {
            std::task::Poll::Ready(Some(self.process()))
        } else {
            std::task::Poll::Pending
        }
    }
}

/// Hook for signal from OpenThread that there are tasklets that need processing.
#[no_mangle]
unsafe extern "C" fn otTaskletsSignalPending(instance: *mut otInstance) {
    trace!("otTaskletsSignalPending");
    Instance::ref_from_ot_ptr(instance).unwrap().wake_waker();
}

/// Provides an asynchronous interface to [`ot::Tasklets`](Tasklets).
///
/// Created by [`TaskletsStreamExt::tasklets_stream()`].
#[derive(Debug)]
pub struct TaskletsStream<'a, T: ?Sized>(&'a T);
impl<'a, T: TaskletsStreamExt + ?Sized> Stream for TaskletsStream<'a, T> {
    type Item = ();
    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        self.0.tasklets_poll(cx)
    }
}

/// Trait used to wrap around a mutex that polls internally without needing to externally
/// unlock the mutex.
pub trait TaskletsStreamExt {
    /// Method which polls the tasklets for processing updates.
    fn tasklets_poll(&self, cx: &mut Context<'_>) -> Poll<Option<()>>;

    /// Returns an asynchronous stream which can be used for processing tasklets.
    fn tasklets_stream(&self) -> TaskletsStream<'_, Self> {
        TaskletsStream(self)
    }
}

impl<T: AsRef<ot::Instance>> TaskletsStreamExt for parking_lot::Mutex<T> {
    fn tasklets_poll(&self, cx: &mut std::task::Context<'_>) -> std::task::Poll<Option<()>> {
        use std::ops::Deref;
        let guard = self.lock();
        guard.deref().as_ref().process_poll(cx)
    }
}

impl<T: AsRef<ot::Instance>> TaskletsStreamExt for std::sync::Mutex<T> {
    fn tasklets_poll(&self, cx: &mut std::task::Context<'_>) -> std::task::Poll<Option<()>> {
        use std::ops::Deref;
        let guard = self.lock().expect("Lock is poisoned");
        guard.deref().as_ref().process_poll(cx)
    }
}
