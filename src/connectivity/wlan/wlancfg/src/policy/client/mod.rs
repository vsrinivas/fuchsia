// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

///! Serves Client policy services.
///! Note: This implementation is still under development.
///!       No request is being processed and responded to yet.
///!       Unfortunately, there is currently no way to send an Epitaph in Rust. Thus, inbound
///!       controller and listener requests are simply dropped, causing the underlying channel to get
///!       closed.
///!
use {
    fidl_fuchsia_wlan_policy as fidl_policy, fuchsia_async as fasync,
    futures::{prelude::*, select, stream::FuturesUnordered},
    log::error,
};

// This number was chosen arbitrarily.
const MAX_CONCURRENT_LISTENERS: usize = 1000;

pub fn spawn_provider_server(requests: fidl_policy::ClientProviderRequestStream) {
    fasync::spawn(serve_provider_requests(requests));
}

pub fn spawn_listener_server(requests: fidl_policy::ClientListenerRequestStream) {
    fasync::spawn(serve_listener_requests(requests));
}

/// Serves the ClientProvider protocol.
/// Only one ClientController can be active. Additional requests to register ClientControllers
/// will result in their channel being immediately closed.
async fn serve_provider_requests(mut requests: fidl_policy::ClientProviderRequestStream) {
    let mut controller_reqs = FuturesUnordered::new();

    loop {
        select! {
            req = requests.next() => if let Some(Ok(req)) = req {
                // If there is an active controller - reject new requests.
                // Rust cannot yet send Epitaphs when closing a channel, thus, simply drop the
                // request.
                if controller_reqs.is_empty() {
                    controller_reqs.push(handle_provider_request(req));
                }
            },
            // Progress controller requests.
            _ = controller_reqs.next() => (),
        }
    }
}

/// Serves the ClientListener protocol.
async fn serve_listener_requests(requests: fidl_policy::ClientListenerRequestStream) {
    let _ignored = requests
        .try_for_each_concurrent(MAX_CONCURRENT_LISTENERS, |req| handle_listener_request(req))
        .unwrap_or_else(|e| error!("error serving Client Listener API: {}", e))
        .await;
}

/// Handle inbound requests to acquire a new ClientController.
async fn handle_provider_request(
    req: fidl_policy::ClientProviderRequest,
) -> Result<(), fidl::Error> {
    match req {
        fidl_policy::ClientProviderRequest::GetController { .. } => Ok(()),
    }
}

/// Handle inbound requests to register an additional ClientStateUpdates listener.
async fn handle_listener_request(
    req: fidl_policy::ClientListenerRequest,
) -> Result<(), fidl::Error> {
    match req {
        fidl_policy::ClientListenerRequest::GetListener { .. } => Ok(()),
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::{
            endpoints::{create_proxy, create_request_stream},
            Error,
        },
        fuchsia_zircon as zx,
        futures::task::Poll,
        pin_utils::pin_mut,
        wlan_common::assert_variant,
    };

    fn request_controller(
        provider: &fidl_policy::ClientProviderProxy,
    ) -> (fidl_policy::ClientControllerProxy, fidl_policy::ClientStateUpdatesRequestStream) {
        let (controller, requests) = create_proxy::<fidl_policy::ClientControllerMarker>()
            .expect("failed to create ClientController proxy");
        let (update_sink, update_stream) =
            create_request_stream::<fidl_policy::ClientStateUpdatesMarker>()
                .expect("failed to create ClientStateUpdates proxy");
        provider.get_controller(requests, update_sink).expect("error getting controller");
        (controller, update_stream)
    }

    #[test]
    fn get_controller() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let (provider, requests) = create_proxy::<fidl_policy::ClientProviderMarker>()
            .expect("failed to create ClientProvider proxy");
        let requests = requests.into_stream().expect("failed to create stream");

        let serve_fut = serve_provider_requests(requests);
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, mut update_stream) = request_controller(&provider);

        // Process request.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Issue connect request. Request should fail as current implementation immediately drops
        // the request.
        let connect_fut = controller.connect(&mut fidl_policy::NetworkIdentifier {
            ssid: b"foobar".to_vec(),
            type_: fidl_policy::SecurityType::None,
        });
        pin_mut!(connect_fut);
        assert_variant!(
            exec.run_until_stalled(&mut connect_fut),
            Poll::Ready(Err(Error::ClientWrite(zx::Status::PEER_CLOSED)))
        );

        // Attempt to read an update from the stream. There shouldn't be an updated provided yet.
        assert_variant!(exec.run_until_stalled(&mut update_stream.next()), Poll::Ready(None));
    }

    #[test]
    fn get_listener() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let (listener, requests) = create_proxy::<fidl_policy::ClientListenerMarker>()
            .expect("failed to create ClientProvider proxy");
        let requests = requests.into_stream().expect("failed to create stream");

        let serve_fut = serve_listener_requests(requests);
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Register listener.
        let (update_sink, mut update_stream) =
            create_request_stream::<fidl_policy::ClientStateUpdatesMarker>()
                .expect("failed to create ClientStateUpdates proxy");
        listener.get_listener(update_sink).expect("error getting listener");

        // Process request.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Attempt to read an update from the stream. There shouldn't be an updated provided yet.
        assert_variant!(exec.run_until_stalled(&mut update_stream.next()), Poll::Ready(None));
    }
}
