// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use failure::{format_err, Error};
use fuchsia_async as fasync;
use fuchsia_wayland_core as wl;
use parking_lot::Mutex;
use wayland::*;

/// When the connection is created it is initialized with a 'wl_display' object
/// that the client can immediately interact with.
const DISPLAY_SINGLETON_OBJECT_ID: u32 = 1;

/// |Display| is the global object used to manage a wayland server.
///
/// The |Display| has a |wl::Registry| that will hold the set of global
/// interfaces that will be advertised to clients.
#[derive(Clone)]
pub struct Display {
    registry: Arc<Mutex<wl::Registry>>,
}

impl Display {
    pub fn new(registry: wl::Registry) -> Self {
        Display {
            registry: Arc::new(Mutex::new(registry)),
        }
    }

    pub fn spawn_new_client(&self, chan: fasync::Channel) {
        let mut client = wl::Client::new(chan, self.registry.clone());

        // Add the global wl_display object. We unwrap here since the object map
        // is empty so failure should not be possible.
        let display_recevier = DisplayReceiver {
            registry: client.registry(),
            event_sender: EventSender {
                chan: client.chan(),
            },
        };
        client
            .objects()
            .add_object(WlDisplay, DISPLAY_SINGLETON_OBJECT_ID, display_recevier)
            .unwrap();

        // Start polling the channel for messages.
        client.start();
    }
}

/// A simple type for writing interface events to a channel.
///
/// TODO(tjdetwiler): We'll want to make this a bit more sophisticated,
/// including not requiring the caller to pass in the object id and we'll most
/// likely want some form of batching.
#[derive(Clone)]
pub struct EventSender {
    chan: Arc<fasync::Channel>,
}

impl EventSender {
    /// Serialize |event| into a buffer and write it to the zircon channel.
    pub fn send<E: wl::IntoMessage>(&self, sender: u32, event: E) -> Result<(), Error> {
        let mut message = event.into_message(sender)?;
        let (bytes, mut handles) = message.take();
        self.chan.write(&bytes, &mut handles)?;
        Ok(())
    }
}

/// An implementation of wl_display.
struct DisplayReceiver {
    registry: Arc<Mutex<wl::Registry>>,
    event_sender: EventSender,
}

impl wl::RequestReceiver<WlDisplay> for DisplayReceiver {
    fn receive(
        display: wl::ObjectRef<Self>, request: WlDisplayRequest, client: &mut wl::Client,
    ) -> Result<(), Error> {
        let map = client.objects();
        let this = display.get(map)?;
        match request {
            WlDisplayRequest::GetRegistry { registry } => {
                let receiver = RegistryReceiver {
                    id: registry,
                    registry: this.registry.clone(),
                    event_sender: this.event_sender.clone(),
                };
                receiver.report_globals()?;
                map.add_object(WlRegistry, registry, receiver)?;
                Ok(())
            }
            WlDisplayRequest::Sync { callback } => {
                this.event_sender
                    .send(callback, WlCallbackEvent::Done { callback_data: 0 })?;
                this.event_sender
                    .send(display.id(), WlDisplayEvent::DeleteId { id: callback })?;
                Ok(())
            }
        }
    }
}

/// An implementation of wl_registry.
struct RegistryReceiver {
    registry: Arc<Mutex<wl::Registry>>,

    // TODO(tjdetwiler): We'll need a more scalable way to handle these.
    id: wl::ObjectId,
    event_sender: EventSender,
}

impl RegistryReceiver {
    /// Sends a wl_registry::global event for each entry in the |wl::Registry|.
    pub fn report_globals(&self) -> Result<(), Error> {
        for (name, global) in self.registry.lock().globals().iter().enumerate() {
            self.event_sender.send(
                self.id,
                WlRegistryEvent::Global {
                    name: name as u32,
                    interface: global.interface().into(),
                    version: global.version(),
                },
            )?;
        }
        Ok(())
    }
}

impl wl::RequestReceiver<WlRegistry> for RegistryReceiver {
    fn receive(
        this: wl::ObjectRef<Self>, request: WlRegistryRequest, client: &mut wl::Client,
    ) -> Result<(), Error> {
        let WlRegistryRequest::Bind { name, id, .. } = request;
        let registry = this.get(client.objects())?.registry.clone();
        let (spec, receiver) = {
            let mut lock = registry.lock();
            if let Some(global) = lock.globals().get_mut(name as usize) {
                (global.request_spec(), global.bind(id, client)?)
            } else {
                return Err(format_err!("Invalid global name {}", name));
            }
        };
        client.objects().add_object_raw(id, receiver, spec)?;
        Ok(())
    }
}
