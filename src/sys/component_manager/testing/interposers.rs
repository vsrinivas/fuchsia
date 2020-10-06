// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        events::{CapabilityRouted, Event, EventSource, EventStreamError, RoutingProtocol},
        matcher::EventMatcher,
    },
    anyhow::Error,
    async_trait::async_trait,
    fidl::endpoints::{create_request_stream, ClientEnd, ServerEnd, ServiceMarker},
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    futures::{
        future::{AbortHandle, Abortable, TryFutureExt},
        StreamExt,
    },
    log::warn,
    std::sync::Arc,
};

/// A ProtocolInterposer allows a test to sit between a service and a client
/// and mutate or silently observe messages being passed between them.
///
/// Client <---> ProtocolInterposer <---> Server
#[async_trait]
pub trait ProtocolInterposer: 'static + Send + Sync {
    type Marker: ServiceMarker;

    async fn interpose(
        self: Arc<Self>,
        event_source: &EventSource,
        matcher: EventMatcher,
    ) -> AbortHandle {
        let matcher = matcher.capability_id(Self::Marker::NAME);
        let mut event_stream = event_source
            .subscribe(vec![CapabilityRouted::NAME])
            .await
            .expect("Could not create event stream");
        let (abort_handle, abort_registration) = AbortHandle::new_pair();

        // Spawn a new thread to listen to CapabilityRoutedEvents.
        // We use a new thread here because running this on the main thread may
        // not work if a test writer needs to do blocking operations.
        fasync::Task::blocking(
            Abortable::new(
                async move {
                    loop {
                        // Wait for a capability routed event that matches
                        let event =
                            match matcher.clone().wait::<CapabilityRouted>(&mut event_stream).await
                            {
                                Ok(e) => e,
                                Err(e) => match e.downcast::<EventStreamError>() {
                                    Ok(EventStreamError::StreamClosed) => return,
                                    Err(e) => panic!("Unknown error! {:?}", e),
                                },
                            };

                        // An event was found! Inject the route.
                        if event.result.is_ok() {
                            let (provider_client_end, server_end) = self.clone().route();
                            event
                                .protocol_proxy()
                                .expect("Event does not have routing protocol")
                                .replace_and_open(provider_client_end, server_end)
                                .await
                                .expect("Could not set provider for CapabilityRouted event");
                        }
                    }
                },
                abort_registration,
            )
            .unwrap_or_else(|_| ()),
        )
        .detach();

        abort_handle
    }

    fn route(self: Arc<Self>) -> (ClientEnd<fsys::CapabilityProviderMarker>, fidl::Channel) {
        // Create the Interposer <---> Server channel
        let (client_end, server_end) = fidl::Channel::create().expect("could not create channel");

        // Create the CapabilityProvider channel
        let (provider_client_end, mut provider_capability_stream) =
            create_request_stream::<fsys::CapabilityProviderMarker>()
                .expect("Could not create request stream for CapabilityProvider");

        // Spawn a task to handle this new route
        fasync::Task::spawn(async move {
            if let Some(Ok(fsys::CapabilityProviderRequest::Open {
                server_end,
                flags,
                mode,
                path,
                responder,
            })) = provider_capability_stream.next().await
            {
                // Unblock component manager
                responder.send().expect("Failed to respond to CapabilityProvider Open");

                if !path.is_empty() {
                    warn!(
                        "Interposed service {} was provided a non-empty path: {}",
                        Self::Marker::NAME,
                        path
                    );
                }

                if flags != (fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE) {
                    warn!(
                        "Interposed service {} was provided unexpected flags: 0x{:x}",
                        Self::Marker::NAME,
                        flags
                    );
                }

                let mode_type = mode & fio::MODE_TYPE_MASK;
                if mode_type != fio::MODE_TYPE_SERVICE {
                    warn!(
                        "Interposed service {} was provided unexpected mode type: 0x{:x}",
                        Self::Marker::NAME,
                        mode_type
                    );
                }

                // Create the proxy for the Interposer <---> Server connection
                let proxy = ClientEnd::<Self::Marker>::new(client_end)
                    .into_proxy()
                    .expect("could not convert into proxy");

                // Create the stream for the Client <---> Interposer connection
                let stream = ServerEnd::<Self::Marker>::new(server_end)
                    .into_stream()
                    .expect("could not convert channel into stream");

                // Start interposing!
                self.serve(stream, proxy).await.expect("Interposition failed");
            }
        })
        .detach();

        (provider_client_end, server_end)
    }

    /// This function will be run asynchronously when a client attempts
    /// to connect to the service being interposed. `from_client` is a stream of
    /// requests coming in from the client and `to_server` is a proxy to the
    /// real server.
    async fn serve(
        self: Arc<Self>,
        mut from_client: <<Self as ProtocolInterposer>::Marker as ServiceMarker>::RequestStream,
        to_server: <<Self as ProtocolInterposer>::Marker as ServiceMarker>::Proxy,
    ) -> Result<(), Error>;
}
