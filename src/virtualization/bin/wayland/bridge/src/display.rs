// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::Client,
    crate::object::{NewObjectExt, ObjectRef, RequestReceiver},
    crate::registry::Registry,
    anyhow::{format_err, Error},
    fidl::endpoints::{ClientEnd, RequestStream},
    fidl_fuchsia_ui_app::ViewProviderMarker,
    fidl_fuchsia_ui_scenic::{ScenicMarker, ScenicProxy},
    fidl_fuchsia_wayland::ViewProducerRequestStream,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon as zx,
    futures::channel::mpsc,
    parking_lot::Mutex,
    std::sync::Arc,
    wayland::*,
};

#[cfg(not(feature = "flatland"))]
use {
    crate::scenic::ScenicSession, fidl::endpoints::create_proxy,
    fidl_fuchsia_ui_scenic::SessionListenerMarker, fuchsia_scenic as scenic,
};

/// When the connection is created it is initialized with a 'wl_display' object
/// that the client can immediately interact with.
pub const DISPLAY_SINGLETON_OBJECT_ID: u32 = 1;

pub trait LocalViewProducerClient: Send + Sync {
    /// Notifes the view producer client that a new view has been created.
    ///
    /// # Parameters
    /// - `view_provider`: The view provider associated with the new view.
    /// - `view_id`: The identifier for the view that was created.
    fn new_view(&mut self, view_provider: ClientEnd<ViewProviderMarker>, view_id: u32);

    /// Notifies the ViewProducer client that the view with `view_id` is being shut down.
    fn shutdown_view(&mut self, view_id: u32);
}

#[derive(Clone)]
enum ViewProducerClient {
    Local(Arc<Mutex<Box<dyn LocalViewProducerClient>>>),
    Remote(Arc<Mutex<Option<ViewProducerRequestStream>>>),
}

/// |Display| is the global object used to manage a wayland server.
///
/// The |Display| has a |Registry| that will hold the set of global
/// interfaces that will be advertised to clients.
#[derive(Clone)]
pub struct Display {
    registry: Arc<Mutex<Registry>>,
    /// A connection to the 'Scenic' service.
    scenic: Arc<ScenicProxy>,
    /// A binding to a public `ViewProducer` service. This is used to publish
    /// new views to the consumer.
    ///
    /// This must be bound before any views are created.
    view_producer_client: ViewProducerClient,
}

impl Display {
    pub fn new(registry: Registry) -> Result<Self, Error> {
        let scenic = connect_to_protocol::<ScenicMarker>().unwrap();
        Ok(Display {
            registry: Arc::new(Mutex::new(registry)),
            scenic: Arc::new(scenic),
            view_producer_client: ViewProducerClient::Remote(Arc::new(Mutex::new(None))),
        })
    }

    pub fn new_local(
        registry: Registry,
        client: Arc<Mutex<Box<dyn LocalViewProducerClient>>>,
    ) -> Result<Self, Error> {
        let scenic = connect_to_protocol::<ScenicMarker>().unwrap();
        Ok(Display {
            registry: Arc::new(Mutex::new(registry)),
            scenic: Arc::new(scenic),
            view_producer_client: ViewProducerClient::Local(client),
        })
    }

    /// Creates a `Display` without a valid scenic connection. This is intended
    /// for unit testing purposes only.
    #[cfg(test)]
    pub fn new_no_scenic(registry: Registry) -> Result<Self, Error> {
        let (c1, _c2) = zx::Channel::create()?;
        let scenic = ScenicProxy::new(fasync::Channel::from_channel(c1)?);
        Ok(Display {
            registry: Arc::new(Mutex::new(registry)),
            scenic: Arc::new(scenic),
            view_producer_client: ViewProducerClient::Remote(Arc::new(Mutex::new(None))),
        })
    }

    /// Provides access to the Scenic connection for this display.
    pub fn scenic(&self) -> &Arc<ScenicProxy> {
        &self.scenic
    }

    /// Provides access to the global registry for this display.
    pub fn registry(&self) -> Arc<Mutex<Registry>> {
        self.registry.clone()
    }

    /// Publish a new view back to the client for presentation.
    pub fn new_view_provider(&self, view_provider: ClientEnd<ViewProviderMarker>, view_id: u32) {
        match &self.view_producer_client {
            ViewProducerClient::Local(view_producer_client) => {
                view_producer_client.lock().new_view(view_provider, view_id);
            }
            ViewProducerClient::Remote(view_producer_binding) => {
                view_producer_binding
                    .lock()
                    .as_ref()
                    .expect(
                        "\
                 A new view has been created without a ViewProducer connection. \
                 The ViewProducer must be bound before issuing any new channels \
                 into the bridge.",
                    )
                    .control_handle()
                    .send_on_new_view(view_provider, view_id)
                    .expect("Failed to emit OnNewView event");
            }
        }
    }

    /// Notify client that presentation of view should stop.
    pub fn delete_view_provider(&self, view_id: u32) {
        match &self.view_producer_client {
            ViewProducerClient::Local(view_producer_client) => {
                view_producer_client.lock().shutdown_view(view_id);
            }
            ViewProducerClient::Remote(view_producer_binding) => {
                if let Some(view_producer_ref) = view_producer_binding.lock().as_ref() {
                    let _ = view_producer_ref.control_handle().send_on_shutdown_view(view_id);
                }
            }
        }
    }

    pub fn bind_view_producer(&self, stream: ViewProducerRequestStream) {
        match &self.view_producer_client {
            ViewProducerClient::Local(_view_producer_client) => {
                panic!("Attempting to bind remote view producer when display has local view producer client.");
            }
            ViewProducerClient::Remote(view_producer_binding) => {
                *view_producer_binding.lock() = Some(stream);
            }
        }
    }

    pub fn bind_local_view_producer(&self, view_producer_client: Box<dyn LocalViewProducerClient>) {
        match &self.view_producer_client {
            ViewProducerClient::Local(client) => {
                *client.lock() = view_producer_client;
            }
            ViewProducerClient::Remote(_view_producer_binding) => {
                panic!("Attempting to bind local view producer when display has remote view producer client.");
            }
        }
    }

    /// Create a new client and begin polling `chan` for requests.
    pub fn spawn_new_client(self, chan: fasync::Channel, protocol_logging: bool) {
        Display::spawn_client(Client::new(chan, self), protocol_logging);
    }

    pub fn spawn_new_local_client(
        self,
        sender: mpsc::UnboundedSender<zx::MessageBuf>,
        receiver: mpsc::UnboundedReceiver<zx::MessageBuf>,
    ) {
        Display::spawn_client(Client::new_local(sender, receiver, self), false);
    }

    fn spawn_client(mut client: Client, protocol_logging: bool) {
        client.set_protocol_logging(protocol_logging);

        // Add the global wl_display object. We unwrap here since the object map
        // is empty so failure should not be possible.
        client.add_object(DISPLAY_SINGLETON_OBJECT_ID, DisplayReceiver).unwrap();

        // Start polling the channel for messages.
        client.start();
    }
}

#[cfg(not(feature = "flatland"))]
impl Display {
    pub fn create_session(
        &self,
        listener: Option<ClientEnd<SessionListenerMarker>>,
    ) -> Result<ScenicSession, Error> {
        let (session_proxy, session_request) = create_proxy().unwrap();
        self.scenic.create_session(session_request, listener).unwrap();
        Ok(ScenicSession::new(scenic::Session::new(session_proxy)))
    }
}

/// An implementation of wl_display.
struct DisplayReceiver;

impl RequestReceiver<WlDisplay> for DisplayReceiver {
    fn receive(
        this: ObjectRef<Self>,
        request: WlDisplayRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            WlDisplayRequest::GetRegistry { registry } => {
                let registry = registry.implement(client, RegistryReceiver)?;
                RegistryReceiver::report_globals(registry, client)?;
                Ok(())
            }
            WlDisplayRequest::Sync { callback } => {
                // Since we callback immediately we'll skip actually adding this
                // object, but we need to send the wl_display::delete_id event
                // explicitly, which is otherwise done for us in
                // Client:delete_id.
                client
                    .event_queue()
                    .post(callback.id(), WlCallbackEvent::Done { callback_data: 0 })?;
                client
                    .event_queue()
                    .post(this.id(), WlDisplayEvent::DeleteId { id: callback.id() })?;
                Ok(())
            }
        }
    }
}

/// An implementation of wl_registry.
struct RegistryReceiver;

impl RegistryReceiver {
    /// Sends a wl_registry::global event for each entry in the |Registry|.
    pub fn report_globals(this: ObjectRef<Self>, client: &Client) -> Result<(), Error> {
        let registry = client.display().registry();
        for (name, global) in registry.lock().globals().iter().enumerate() {
            client.event_queue().post(
                this.id(),
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

impl RequestReceiver<WlRegistry> for RegistryReceiver {
    fn receive(
        _this: ObjectRef<Self>,
        request: WlRegistryRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        let WlRegistryRequest::Bind { name, id, id_interface_version: version, .. } = request;
        let registry = client.display().registry();
        let (spec, receiver) = {
            let mut lock = registry.lock();
            if let Some(global) = lock.globals().get_mut(name as usize) {
                (global.request_spec(), global.bind(id, version, client)?)
            } else {
                return Err(format_err!("Invalid global name {}", name));
            }
        };
        client.add_object_raw(id, receiver, spec)?;
        Ok(())
    }
}

/// An implementation of wl_callback.
pub struct Callback;

impl Callback {
    /// Posts the `done` event for this callback.
    pub fn done(
        this: ObjectRef<Self>,
        client: &mut Client,
        callback_data: u32,
    ) -> Result<(), Error> {
        client.event_queue().post(this.id(), WlCallbackEvent::Done { callback_data })
    }
}

impl RequestReceiver<WlCallback> for Callback {
    fn receive(
        _this: ObjectRef<Self>,
        request: WlCallbackRequest,
        _client: &mut Client,
    ) -> Result<(), Error> {
        match request {}
    }
}
