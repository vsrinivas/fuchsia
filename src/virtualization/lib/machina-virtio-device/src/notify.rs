// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_virtualization_hardware::{
        EVENT_SET_CONFIG, EVENT_SET_INTERRUPT, EVENT_SET_QUEUE,
    },
    fuchsia_zircon::{self as zx, AsHandleRef},
    virtio_device::queue::DriverNotify,
};

const USER_SIGNALS: [zx::Signals; 8] = [
    zx::Signals::USER_0,
    zx::Signals::USER_1,
    zx::Signals::USER_2,
    zx::Signals::USER_3,
    zx::Signals::USER_4,
    zx::Signals::USER_5,
    zx::Signals::USER_6,
    zx::Signals::USER_7,
];

/// Wraps a [`zx::Event`] and implements [`DriverNotify`]
///
/// Implements [`DriverNotify`] by setting the appropriate signals on a [`zx::Event`] to notify the
/// machina VMM that the guest needs an interrupt.
///
/// The appropriate [`zx::Event`] to use for this can be found in the `StartInfo` provided as the
/// first message to a device.
#[derive(Clone)]
pub struct NotifyEvent(std::sync::Arc<zx::Event>);

impl NotifyEvent {
    /// Construct a new [`NotifyEvent`]
    pub fn new(event: zx::Event) -> NotifyEvent {
        NotifyEvent(std::sync::Arc::new(event))
    }
}

impl NotifyEvent {
    fn signal(&self, event: usize) -> Result<(), zx::Status> {
        self.0.as_handle_ref().signal(
            zx::Signals::empty(),
            USER_SIGNALS[event] | USER_SIGNALS[EVENT_SET_INTERRUPT as usize],
        )
    }

    pub fn signal_queue(&self) -> Result<(), zx::Status> {
        self.signal(EVENT_SET_QUEUE as usize)
    }

    pub fn signal_config(&self) -> Result<(), zx::Status> {
        self.signal(EVENT_SET_CONFIG as usize)
    }
}

impl DriverNotify for NotifyEvent {
    fn notify(&self) {
        // Signaling the event will only fail if the VMM has shutdown unexpectedly, without
        // first explicitly shutting us down, which is not expected to happen. As we have no way
        // to forward any error here anyway, just unwrap and panic.
        self.signal_queue().unwrap();
    }
}
