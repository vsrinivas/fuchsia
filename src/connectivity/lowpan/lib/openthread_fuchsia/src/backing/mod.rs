// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::prelude::*;
use openthread::prelude::*;
use spinel_pack::prelude::*;

use fuchsia_async as fasync;
use futures::channel::mpsc as fmpsc;
use log::*;
use lowpan_driver_common::spinel::*;
use std::cell::{Cell, RefCell};
use std::sync::mpsc;
use std::time::Duration;

mod alarm;
mod radio;
mod reset;
mod udp;

pub(crate) use udp::*;

pub(super) struct PlatformBacking {
    pub(super) ot_to_rcp_sender: RefCell<mpsc::Sender<Vec<u8>>>,
    pub(super) rcp_to_ot_receiver: RefCell<mpsc::Receiver<Vec<u8>>>,
    pub(super) task_alarm: Cell<Option<fasync::Task<()>>>,
    pub(super) timer_sender: fmpsc::Sender<usize>,
}

impl PlatformBacking {
    // SAFETY: Unsafe because the type system cannot enforce thread safety on globals.
    //         Caller should ensure that no other calls in this section are being
    //         simultaneously made on other threads.
    unsafe fn glob() -> &'static mut Option<PlatformBacking> {
        static mut SINGLETON_BACKING: Option<PlatformBacking> = None;
        &mut SINGLETON_BACKING
    }

    // SAFETY: Unsafe because the type system cannot enforce thread safety on globals.
    //         Caller should ensure that no other calls in this section are being
    //         simultaneously made on other threads.
    unsafe fn as_ref() -> &'static PlatformBacking {
        Self::glob().as_ref().expect("Platform is uninitialized")
    }

    // SAFETY: Unsafe because the type system cannot enforce thread safety on globals.
    //         Caller should ensure that no other calls in this section are being
    //         simultaneously made on other threads.
    pub(super) unsafe fn set_singleton(backing: PlatformBacking) {
        assert!(Self::glob().replace(backing).is_none(), "Tried to make two Platform instances");
    }

    // SAFETY: Must only be called from Drop.
    pub(super) unsafe fn drop_singleton() {
        // SAFETY: When we are dropped, we can safely assume no other simultaneous calls are
        //         being made on other threads.
        assert!(Self::glob().take().is_some(), "Tried to drop singleton that was never allocated");
    }
}
