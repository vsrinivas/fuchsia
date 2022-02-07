// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use std::marker::PhantomData;
use std::task::{Context, Waker};

/// OpenThread instance.
///
/// This type cannot be instantiated directly: owned instances are
/// passed around in an [`ot::Box<ot::Instance>`](crate::ot::Box), or
/// [`OtInstanceBox`](crate::OtInstanceBox) for short.
#[repr(transparent)]
pub struct Instance(otInstance, PhantomData<*mut otMessage>);

// SAFETY: Instances of `ot::Instance` may be safely moved across threads.
//         OpenThread does not use thread-local storage.
unsafe impl Send for Instance {}

// SAFETY: `Instance` transparently wraps around an opaque type and is
//         never used by value nor passed by value.
unsafe impl ot::Boxable for Instance {
    type OtType = otInstance;
    unsafe fn finalize(&mut self) {
        trace!("Instance::finalize(): Finalizing otInstance");
        otInstanceFinalize(self.as_ot_ptr());
        InstanceBacking::drop_singleton();
    }
}

impl InstanceInterface for Instance {}

impl std::fmt::Debug for Instance {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_tuple("otInstance").field(&self.as_ot_ptr()).finish()
    }
}

impl Instance {
    /// Initializes and returns a new boxed singleton OpenThread instance.
    ///
    /// If called twice without dropping the first instance, this method will panic.
    pub fn new<T: Platform + 'static>(platform: T) -> ot::Box<Instance> {
        unsafe {
            // OpenThread is inherently thread-unsafe, just as `set_singleton()` is below.
            // For singleton instances it only makes sense to call this method once.
            InstanceBacking::set_singleton(InstanceBacking::new(platform));

            // The ot::Box will take ownership of the instance
            // pointer and manage its lifetime.
            ot::Box::from_ot_ptr(otInstanceInitSingle()).unwrap()
        }
    }
}

impl Instance {
    /// Returns a reference to the backing structure.
    pub(crate) fn borrow_backing(&self) -> &InstanceBacking {
        unsafe {
            // SAFETY: We are making a call to one unsafe methods here,
            //         `InstanceBacking::as_ref()`. This method must
            //         only be called from the same thread that the OpenThread
            //         instance is being used on. Since the OpenThread instance
            //         does not implement `Sync`, the self reference is neither
            //         `Send` nor `Sync`, so it is is safe to call here.
            InstanceBacking::as_ref()
        }
    }

    pub(crate) fn platform_poll(self: &Self, cx: &mut Context<'_>) {
        unsafe {
            // SAFETY: The underlying unsafe call, process_poll(), must
            //         only be called from the same thread that the OpenThread
            //         instance is being used on. Since the OpenThread instance
            //         does not implement `Sync`, the self reference is neither
            //         `Send` nor `Sync`, so it is is safe to call here.
            self.borrow_backing().platform.borrow_mut().process_poll(self, cx);
        }
    }
}

impl ot::Tasklets for Instance {
    fn set_waker(&self, waker: Waker) {
        self.borrow_backing().waker.set(waker);
    }

    fn wake_waker(&self) {
        self.borrow_backing().waker.replace(futures::task::noop_waker()).wake()
    }

    fn process(&self) {
        unsafe { otTaskletsProcess(self.as_ot_ptr()) }
    }

    fn has_pending(&self) -> bool {
        unsafe { otTaskletsArePending(self.as_ot_ptr()) }
    }
}
