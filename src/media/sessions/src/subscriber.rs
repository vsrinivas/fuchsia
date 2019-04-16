// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::MAX_EVENTS_SENT_WITHOUT_ACK;
use fidl_fuchsia_mediasession::{ActiveSession, RegistryControlHandle, SessionsChange};

pub trait SubscriberEvent: Sized {
    fn send(self, handle: &RegistryControlHandle) -> bool;
}

impl SubscriberEvent for SessionsChange {
    fn send(mut self, handle: &RegistryControlHandle) -> bool {
        handle.send_on_sessions_changed(&mut self).is_ok()
    }
}

impl SubscriberEvent for ActiveSession {
    fn send(self, handle: &RegistryControlHandle) -> bool {
        handle.send_on_active_session_changed(self).is_ok()
    }
}

/// A subscriber is a client of `fuchsia.mediasession.Registry`.
pub struct Subscriber {
    control_handle: RegistryControlHandle,
    events_sent_without_ack: usize,
}

impl Subscriber {
    pub fn new(control_handle: RegistryControlHandle) -> Self {
        Self { control_handle, events_sent_without_ack: 0 }
    }

    pub fn ack(&mut self) {
        self.events_sent_without_ack = 0;
    }

    pub fn should_wait_to_send_more(&self) -> bool {
        self.events_sent_without_ack >= MAX_EVENTS_SENT_WITHOUT_ACK
    }

    /// Sends an event to a subscriber without increasing their
    /// events-without-ack count and returns whether sending to that subscriber
    /// succeeded.
    pub fn send_no_ack_count(&self, event: impl SubscriberEvent) -> bool {
        event.send(&self.control_handle)
    }

    /// Sends an event to a subscriber and returns whether sending to that
    /// subscriber succeeded.
    pub fn send(&mut self, event: impl SubscriberEvent) -> bool {
        self.events_sent_without_ack += 1;
        event.send(&self.control_handle)
    }
}
