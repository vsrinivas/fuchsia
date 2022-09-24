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
use std::sync::atomic::AtomicBool;
use std::sync::mpsc;
use std::time::Duration;

mod alarm;
mod infra_if;
mod radio;
mod reset;
mod trel;
mod udp;

pub(crate) use alarm::*;
pub(crate) use infra_if::InfraIfInstance;
use openthread::ot::NetifIdentifier;
pub(crate) use reset::PlatformResetRequested;
pub(crate) use udp::*;

pub(super) struct PlatformBacking {
    pub(super) ot_to_rcp_sender: RefCell<mpsc::Sender<Vec<u8>>>,
    pub(super) rcp_to_ot_receiver: RefCell<mpsc::Receiver<Vec<u8>>>,
    pub(super) alarm: AlarmInstance,
    pub(super) netif_index_thread: Option<ot::NetifIndex>,
    pub(super) netif_index_backbone: Option<ot::NetifIndex>,
    pub(super) trel: RefCell<Option<trel::TrelInstance>>,
    pub(super) infra_if: Option<InfraIfInstance>,
    pub(super) is_platform_reset_requested: AtomicBool,
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
    pub(super) unsafe fn as_ref() -> &'static PlatformBacking {
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

impl PlatformBacking {
    fn lookup_netif_index(&self, id: ot::NetifIdentifier) -> Option<ot::NetifIndex> {
        match id {
            NetifIdentifier::Backbone => self.netif_index_backbone,
            NetifIdentifier::Thread => self.netif_index_thread,
            NetifIdentifier::Unspecified => Some(ot::NETIF_INDEX_UNSPECIFIED),
        }
    }
}
