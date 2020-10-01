// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth_snoop::{SnoopControlHandle, SnoopPacket},
    log::{trace, warn},
    std::{collections::HashMap, iter},
};

use crate::{ClientId, DeviceId};

/// `SubscriptionManager` tracks the client subscriptions for hci devices. It allows clients to be
/// registered and degregistered as subscribers, clean up all clients registered to
/// a specific device, and notify clients when packets are received on a device.
///
/// It optimizes for the `notify` usecase. Notification happens every time a new packet is logged
/// from a snoop channel. This can be very frequent. It intentionally does *not* optimize for
/// tracking state changes of subscribers and devices. This is because client and device state is
/// not expected to change frequently.
pub(crate) struct SubscriptionManager {
    global: Vec<(ClientId, SnoopControlHandle)>,
    by_device: HashMap<DeviceId, Vec<(ClientId, SnoopControlHandle)>>,
}

impl SubscriptionManager {
    pub fn new() -> SubscriptionManager {
        SubscriptionManager { global: vec![], by_device: HashMap::new() }
    }

    /// Register a client as a subscriber. If `device` is provided, the client is registered to
    /// receive packets from a single device. If `device` is not provided, the client is registered
    /// to receive packets from all devices.
    ///
    /// Returns an error if the client is already registered as a subscriber.
    pub fn register(
        &mut self,
        id: ClientId,
        handle: SnoopControlHandle,
        device: Option<DeviceId>,
    ) -> Result<(), Error> {
        if self.is_registered(&id) {
            return Err(format_err!("Client already registered."));
        }
        match device {
            Some(device) => {
                self.by_device.entry(device).or_insert_with(Vec::new).push((id, handle));
            }
            None => self.global.push((id, handle)),
        }
        Ok(())
    }

    /// Remove client subscriptions by id if the client is currently registered.
    /// Set the client to shutdown.
    pub fn deregister(&mut self, id: &ClientId) {
        let global_subs = iter::once(&mut self.global);
        for subscribers in self.by_device.values_mut().chain(global_subs) {
            let index = subscribers.iter().position(|(id_, _)| id_ == id);
            if let Some(i) = index {
                let (_, handle) = subscribers.swap_remove(i);
                handle.shutdown();
            }
        }
    }

    /// Close connections to all clients that are registered as subscribers to the given device.
    /// Clients with global subscriptions are not affected.
    pub fn remove_device(&mut self, device_id: &DeviceId) {
        if let Some(clients) = self.by_device.remove(device_id) {
            for (_, handle) in clients {
                handle.shutdown();
            }
        }
    }

    /// Is the client registered as a subscriber.
    pub fn is_registered(&mut self, id: &ClientId) -> bool {
        self.by_device
            .values()
            .flat_map(|v| v.iter())
            .chain(self.global.iter())
            .any(|(id_, _)| id_ == id)
    }

    /// Send `packet` to all clients that are subscribed to receive it.
    /// If sending the packet to a client results in an error, the client is deregistered and the
    /// error is logged.
    pub fn notify(&mut self, device: &DeviceId, packet: &mut SnoopPacket) {
        let mut success_count = 0;
        let mut to_cleanup = vec![];

        // Look up clients that are subscribed only to this device.
        let subscribers = self.by_device.get(device);
        // Chain on clients that are subscribed globally.
        let subscribers = subscribers.iter().flat_map(|subs| subs.iter()).chain(self.global.iter());

        // Send events to all clients that have registered interest in this device
        for (id, handle) in subscribers {
            if let Err(e) = handle.send_on_packet(device, packet) {
                warn!("Subscriber {} failed with {}. Removing.", id, e);
                to_cleanup.push(*id);
            } else {
                success_count += 1;
            }
        }

        // Clean up any client handles that have returned an error when sent an event.
        for id in to_cleanup {
            self.deregister(&id);
        }
        trace!("Notified {} clients.", success_count);
    }

    #[allow(dead_code)] // Used in test assertions.
    pub fn number_of_subscribers(&self) -> usize {
        self.by_device.values().fold(self.global.len(), |acc, vec| acc + vec.len())
    }
}
