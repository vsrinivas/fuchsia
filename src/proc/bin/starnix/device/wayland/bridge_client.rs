// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::ClientEnd;
use fidl_fuchsia_ui_app as fuiapp;
use futures::channel::mpsc::UnboundedSender;
use wayland_bridge::display::LocalViewProducerClient;

use crate::logging::log_warn;

/// `WaylandClient` exists to implement `LocalViewProducerClient`.
///
/// The wayland bridge library will notify the `WaylandClient` implementation of any new view
/// providers that are created. When the `WaylandClient` receives a `ViewProvider` it will send it
/// via `view_provider_sender`.
///
/// The `UnboundedReceiver` that is connected to the `WaylandClient`'s `view_provider_sender` can
/// thus be used to wait for the wayland bridge to create a `ViewProvider`.
pub struct WaylandClient {
    /// Sends `ViewProviderProxy`'s received via `LocalViewProducerClient.new_view`.
    view_provider_sender: UnboundedSender<fuiapp::ViewProviderProxy>,
}

impl LocalViewProducerClient for WaylandClient {
    fn new_view(&mut self, view_provider: ClientEnd<fuiapp::ViewProviderMarker>, _view_id: u32) {
        self.view_provider_sender.unbounded_send(view_provider.into_proxy().expect("")).expect("");
    }

    fn shutdown_view(&mut self, view_id: u32) {
        log_warn!("Wayland bridge is attempting to shut down view_id: {:?}.", view_id);
    }
}

impl WaylandClient {
    pub fn new_view_producer_client(
        view_provider_sender: UnboundedSender<fuiapp::ViewProviderProxy>,
    ) -> Box<dyn LocalViewProducerClient> {
        Box::new(WaylandClient { view_provider_sender })
    }
}
