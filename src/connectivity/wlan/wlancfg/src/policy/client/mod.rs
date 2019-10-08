// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

///! Serves Client policy services.
///! Note: This implementation is still under development.
///!       No request is being processed and responded to yet.
///!       Unfortunately, there is currently no way to send an Epitaph in Rust. Thus, inbound
///!       controller and listener requests are simply dropped, causing the underlying channel to
///!       get closed.
///!
use {
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_policy as fidl_policy,
    fuchsia_async as fasync,
    futures::{prelude::*, select, stream::FuturesUnordered},
    log::error,
};

pub mod listener;

// This number was chosen arbitrarily.
const MAX_CONCURRENT_LISTENERS: usize = 1000;

type ClientRequests = fidl::endpoints::ServerEnd<fidl_policy::ClientControllerMarker>;

pub fn spawn_provider_server(
    update_sender: listener::MessageSender,
    requests: fidl_policy::ClientProviderRequestStream,
) {
    fasync::spawn(serve_provider_requests(update_sender, requests));
}

pub fn spawn_listener_server(
    update_sender: listener::MessageSender,
    requests: fidl_policy::ClientListenerRequestStream,
) {
    fasync::spawn(serve_listener_requests(update_sender, requests));
}

/// Serves the ClientProvider protocol.
/// Only one ClientController can be active. Additional requests to register ClientControllers
/// will result in their channel being immediately closed.
async fn serve_provider_requests(
    update_sender: listener::MessageSender,
    mut requests: fidl_policy::ClientProviderRequestStream,
) {
    let mut controller_reqs = FuturesUnordered::new();

    loop {
        select! {
            req = requests.select_next_some() => if let Ok(req) = req {
                // If there is an active controller - reject new requests.
                // Rust cannot yet send Epitaphs when closing a channel, thus, simply drop the
                // request.
                if controller_reqs.is_empty() {
                    controller_reqs.push(handle_provider_request(update_sender.clone(), req));
                }
            },
            // Progress controller requests.
            _ = controller_reqs.select_next_some() => (),
        }
    }
}

/// Serves the ClientListener protocol.
async fn serve_listener_requests(
    update_sender: listener::MessageSender,
    requests: fidl_policy::ClientListenerRequestStream,
) {
    let serve_fut = requests
        .try_for_each_concurrent(MAX_CONCURRENT_LISTENERS, |req| {
            handle_listener_request(update_sender.clone(), req)
        })
        .unwrap_or_else(|e| error!("error serving Client Listener API: {}", e));
    let _ignored = serve_fut.await;
}

/// Handle inbound requests to acquire a new ClientController.
async fn handle_provider_request(
    update_sender: listener::MessageSender,
    req: fidl_policy::ClientProviderRequest,
) -> Result<(), fidl::Error> {
    match req {
        fidl_policy::ClientProviderRequest::GetController { requests, updates, .. } => {
            register_listener(update_sender, updates.into_proxy()?);
            handle_client_requests(requests).await?;
            Ok(())
        }
    }
}

/// Handles all incoming requests from a ClientController.
async fn handle_client_requests(requests: ClientRequests) -> Result<(), fidl::Error> {
    let mut request_stream = requests.into_stream()?;
    while let Some(request) = request_stream.try_next().await? {
        match request {
            fidl_policy::ClientControllerRequest::Connect { responder, .. } => {
                // TODO(hahnr): Verify whether the requested network is known.
                // TODO(hahnr): Send connect request to SME.
                responder.send(fidl_common::RequestStatus::Acknowledged)?;
            }
            unsupported => error!("unsupported request: {:?}", unsupported),
        }
    }
    Ok(())
}

/// Handle inbound requests to register an additional ClientStateUpdates listener.
async fn handle_listener_request(
    update_sender: listener::MessageSender,
    req: fidl_policy::ClientListenerRequest,
) -> Result<(), fidl::Error> {
    match req {
        fidl_policy::ClientListenerRequest::GetListener { updates, .. } => {
            register_listener(update_sender, updates.into_proxy()?);
            Ok(())
        }
    }
}

/// Registers a new update listener.
/// The client's current state will be send to the newly added listener immediately.
fn register_listener(
    update_sender: listener::MessageSender,
    listener: fidl_policy::ClientStateUpdatesProxy,
) {
    let _ignored = update_sender.unbounded_send(listener::Message::NewListener(listener));
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::{create_proxy, create_request_stream},
        futures::{channel::mpsc, task::Poll},
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

        let (update_sender, listener_updates) = mpsc::unbounded();
        let serve_listeners = listener::serve(listener_updates);
        pin_mut!(serve_listeners);
        let serve_fut = serve_provider_requests(update_sender, requests);
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, mut update_stream) = request_controller(&provider);

        // Process request & verify listener was registered.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut serve_listeners), Poll::Pending);

        // Send initial status update to newly registered listener.
        let summary = assert_variant!(
            exec.run_until_stalled(&mut update_stream.next()),
            Poll::Ready(Some(Ok(fidl_policy::ClientStateUpdatesRequest::OnClientStateUpdate {
                summary, responder
            }))) => {
                // Ack update.
                responder.send().expect("error acking update");
                summary
            }
        );
        assert_eq!(summary, fidl_policy::ClientStateSummary { state: None, networks: None });
        // Verify exactly one update was sent.
        assert_variant!(exec.run_until_stalled(&mut update_stream.next()), Poll::Pending);

        // Issue connect request. Request should fail as current implementation immediately drops
        // the request.
        let connect_fut = controller.connect(&mut fidl_policy::NetworkIdentifier {
            ssid: b"foobar".to_vec(),
            type_: fidl_policy::SecurityType::None,
        });
        pin_mut!(connect_fut);

        // Process connect request and verify connect response.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut connect_fut),
            Poll::Ready(Ok(fidl_common::RequestStatus::Acknowledged))
        );
    }

    #[test]
    fn get_listener() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let (listener, requests) = create_proxy::<fidl_policy::ClientListenerMarker>()
            .expect("failed to create ClientProvider proxy");
        let requests = requests.into_stream().expect("failed to create stream");

        let (update_sender, listener_updates) = mpsc::unbounded();
        let serve_listeners = listener::serve(listener_updates);
        pin_mut!(serve_listeners);
        let serve_fut = serve_listener_requests(update_sender, requests);
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Register listener.
        let (update_sink, mut update_stream) =
            create_request_stream::<fidl_policy::ClientStateUpdatesMarker>()
                .expect("failed to create ClientStateUpdates proxy");
        listener.get_listener(update_sink).expect("error getting listener");

        // Process request & verify listener was registered.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut serve_listeners), Poll::Pending);

        // Read initial state update from the stream.
        let summary = assert_variant!(
            exec.run_until_stalled(&mut update_stream.next()),
            Poll::Ready(Some(Ok(fidl_policy::ClientStateUpdatesRequest::OnClientStateUpdate {
                summary, responder
            }))) => {
                // Ack update.
                responder.send().expect("error acking update");
                summary
            }
        );
        assert_eq!(summary, fidl_policy::ClientStateSummary { state: None, networks: None });
        // Verify exactly one update was sent.
        assert_variant!(exec.run_until_stalled(&mut update_stream.next()), Poll::Pending);
    }
}
