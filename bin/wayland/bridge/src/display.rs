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
        client
            .objects()
            .add_object(WlDisplay, DISPLAY_SINGLETON_OBJECT_ID, DisplayReceiver)
            .unwrap();

        // Start polling the channel for messages.
        client.start();
    }
}

/// An implementation of wl_display.
struct DisplayReceiver;

impl wl::RequestReceiver<WlDisplay> for DisplayReceiver {
    fn receive(
        this: wl::ObjectRef<Self>, request: WlDisplayRequest, client: &mut wl::Client,
    ) -> Result<(), Error> {
        match request {
            WlDisplayRequest::GetRegistry { registry } => {
                RegistryReceiver::report_globals(registry, client)?;
                client
                    .objects()
                    .add_object(WlRegistry, registry, RegistryReceiver)?;
                Ok(())
            }
            WlDisplayRequest::Sync { callback } => {
                client.post(callback, WlCallbackEvent::Done { callback_data: 0 })?;
                client.post(this.id(), WlDisplayEvent::DeleteId { id: callback })?;
                Ok(())
            }
        }
    }
}

/// An implementation of wl_registry.
struct RegistryReceiver;

impl RegistryReceiver {
    /// Sends a wl_registry::global event for each entry in the |wl::Registry|.
    pub fn report_globals(this: wl::ObjectId, client: &wl::Client) -> Result<(), Error> {
        let registry = client.registry();
        for (name, global) in registry.lock().globals().iter().enumerate() {
            client.post(
                this,
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
        _this: wl::ObjectRef<Self>, request: WlRegistryRequest, client: &mut wl::Client,
    ) -> Result<(), Error> {
        let WlRegistryRequest::Bind { name, id, .. } = request;
        let registry = client.registry();
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
