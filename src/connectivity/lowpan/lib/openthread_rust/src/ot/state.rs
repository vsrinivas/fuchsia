// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// for now, we allow this for this module because we can't apply it
// specifically to the type `ChangedFlags`, due to a bug in `bitflags!`.
#![allow(missing_docs)]

use crate::prelude_internal::*;

use bitflags::bitflags;
use core::pin::Pin;
use core::task::{Context, Poll};
use parking_lot::Mutex;
use std::sync::Arc;
use std::task::Waker;

bitflags! {
#[repr(C)]
#[derive(Default)]
pub struct ChangedFlags : ::std::os::raw::c_uint {
    const IP6_ADDRESS_ADDED = OT_CHANGED_IP6_ADDRESS_ADDED;
    const IP6_ADDRESS_REMOVED = OT_CHANGED_IP6_ADDRESS_REMOVED;
    const THREAD_ROLE = OT_CHANGED_THREAD_ROLE;
    const THREAD_LL_ADDR = OT_CHANGED_THREAD_LL_ADDR;
    const THREAD_ML_ADDR = OT_CHANGED_THREAD_ML_ADDR;
    const THREAD_RLOC_ADDED = OT_CHANGED_THREAD_RLOC_ADDED;
    const THREAD_RLOC_REMOVED = OT_CHANGED_THREAD_RLOC_REMOVED;
    const THREAD_PARTITION_ID = OT_CHANGED_THREAD_PARTITION_ID;
    const THREAD_KEY_SEQUENCE_COUNTER = OT_CHANGED_THREAD_KEY_SEQUENCE_COUNTER;
    const THREAD_NETDATA = OT_CHANGED_THREAD_NETDATA;
    const THREAD_CHILD_ADDED = OT_CHANGED_THREAD_CHILD_ADDED;
    const THREAD_CHILD_REMOVED = OT_CHANGED_THREAD_CHILD_REMOVED;
    const IP6_MULTICAST_SUBSCRIBED = OT_CHANGED_IP6_MULTICAST_SUBSCRIBED;
    const IP6_MULTICAST_UNSUBSCRIBED = OT_CHANGED_IP6_MULTICAST_UNSUBSCRIBED;
    const THREAD_CHANNEL = OT_CHANGED_THREAD_CHANNEL;
    const THREAD_PANID = OT_CHANGED_THREAD_PANID;
    const THREAD_NETWORK_NAME = OT_CHANGED_THREAD_NETWORK_NAME;
    const THREAD_EXT_PANID = OT_CHANGED_THREAD_EXT_PANID;
    const NETWORK_KEY = OT_CHANGED_NETWORK_KEY;
    const PSKC = OT_CHANGED_PSKC;
    const SECURITY_POLICY = OT_CHANGED_SECURITY_POLICY;
    const CHANNEL_MANAGER_NEW_CHANNEL = OT_CHANGED_CHANNEL_MANAGER_NEW_CHANNEL;
    const SUPPORTED_CHANNEL_MASK = OT_CHANGED_SUPPORTED_CHANNEL_MASK;
    const COMMISSIONER_STATE = OT_CHANGED_COMMISSIONER_STATE;
    const THREAD_NETIF_STATE = OT_CHANGED_THREAD_NETIF_STATE;
    const THREAD_BACKBONE_ROUTER_STATE = OT_CHANGED_THREAD_BACKBONE_ROUTER_STATE;
    const THREAD_BACKBONE_ROUTER_LOCAL = OT_CHANGED_THREAD_BACKBONE_ROUTER_LOCAL;
    const JOINER_STATE = OT_CHANGED_JOINER_STATE;
    const ACTIVE_DATASET = OT_CHANGED_ACTIVE_DATASET;
    const PENDING_DATASET = OT_CHANGED_PENDING_DATASET;
}
}

/// State-change-related methods from the
/// [OpenThread "Instance" Module](https://openthread.io/reference/group/api-instance).
pub trait State {
    /// Functional equivalent to
    /// [`otsys::otSetStateChangedCallback`](crate::otsys::otSetStateChangedCallback).
    fn set_state_changed_fn<F>(&self, f: Option<F>)
    where
        F: FnMut(ChangedFlags) + 'static;

    /// Returns an asynchronous stream for state-change events.
    fn state_changed_stream(&self) -> StateChangedStream;
}

impl<T: State + Boxable> State for ot::Box<T> {
    fn set_state_changed_fn<F>(&self, f: Option<F>)
    where
        F: FnMut(ChangedFlags) + 'static,
    {
        self.as_ref().set_state_changed_fn(f);
    }

    fn state_changed_stream(&self) -> StateChangedStream {
        self.as_ref().state_changed_stream()
    }
}

/// Stream for getting state changed events.
#[derive(Debug, Clone)]
pub struct StateChangedStream(Arc<Mutex<(ChangedFlags, Waker)>>);

impl Stream for StateChangedStream {
    type Item = ChangedFlags;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let mut state = self.0.lock();

        state.1 = cx.waker().clone();

        if state.0 != ChangedFlags::empty() {
            let flags = state.0;
            state.0 = ChangedFlags::empty();
            Poll::Ready(Some(flags))
        } else {
            Poll::Pending
        }
    }
}

impl State for Instance {
    fn set_state_changed_fn<F>(&self, f: Option<F>)
    where
        F: FnMut(ChangedFlags) + 'static,
    {
        unsafe extern "C" fn _ot_state_changed_callback<F: FnMut(ChangedFlags)>(
            flags: otChangedFlags,
            context: *mut ::std::os::raw::c_void,
        ) {
            // Reconstitute a reference to our closure.
            let sender = &mut *(context as *mut F);

            sender(ChangedFlags::from_bits_truncate(flags))
        }

        let (fn_ptr, fn_box, cb): (_, _, otStateChangedCallback) = if let Some(f) = f {
            let mut x = Box::new(f);

            (
                x.as_mut() as *mut F as *mut ::std::os::raw::c_void,
                Some(x as Box<dyn FnMut(ChangedFlags)>),
                Some(_ot_state_changed_callback::<F>),
            )
        } else {
            (std::ptr::null_mut() as *mut ::std::os::raw::c_void, None, None)
        };

        unsafe {
            otSetStateChangedCallback(self.as_ot_ptr(), cb, fn_ptr);
        }

        // Make sure our object eventually gets cleaned up.
        self.borrow_backing().state_change_fn.set(fn_box);
    }

    fn state_changed_stream(&self) -> StateChangedStream {
        let state = Arc::new(Mutex::new((ChangedFlags::empty(), futures::task::noop_waker())));

        let state_copy = state.clone();

        self.set_state_changed_fn(Some(move |flags: ChangedFlags| {
            let mut borrowed = state_copy.lock();
            borrowed.0 |= flags;
            borrowed.1.clone().wake();
        }));

        StateChangedStream(state)
    }
}
