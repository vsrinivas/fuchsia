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
    crate::known_ess_store::KnownEssStore,
    failure::{format_err, Error},
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_policy as fidl_policy,
    fuchsia_async as fasync,
    futures::{prelude::*, select, stream::FuturesUnordered},
    log::error,
    std::sync::Arc,
};

pub mod listener;

#[derive(Debug)]
struct RequestError {
    cause: Error,
    status: fidl_common::RequestStatus,
}

impl RequestError {
    /// Produces a new `RequestError` for internal errors.
    fn new() -> Self {
        RequestError {
            cause: format_err!("internal error"),
            status: fidl_common::RequestStatus::RejectedNotSupported,
        }
    }

    fn with_cause(self, cause: Error) -> Self {
        RequestError { cause, ..self }
    }

    fn with_status(self, status: fidl_common::RequestStatus) -> Self {
        RequestError { status, ..self }
    }
}

impl From<fidl::Error> for RequestError {
    fn from(e: fidl::Error) -> RequestError {
        RequestError::new()
            .with_cause(format_err!("FIDL error: {}", e))
            .with_status(fidl_common::RequestStatus::RejectedNotSupported)
    }
}

// This number was chosen arbitrarily.
const MAX_CONCURRENT_LISTENERS: usize = 1000;

type ClientRequests = fidl::endpoints::ServerEnd<fidl_policy::ClientControllerMarker>;
type EssStorePtr = Arc<KnownEssStore>;

pub fn spawn_provider_server(
    update_sender: listener::MessageSender,
    ess_store: EssStorePtr,
    requests: fidl_policy::ClientProviderRequestStream,
) {
    fasync::spawn(serve_provider_requests(update_sender, ess_store, requests));
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
    ess_store: EssStorePtr,
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
                    let fut = handle_provider_request(update_sender.clone(),
                                                      Arc::clone(&ess_store), req);
                    controller_reqs.push(fut);
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
    ess_store: EssStorePtr,
    req: fidl_policy::ClientProviderRequest,
) -> Result<(), fidl::Error> {
    match req {
        fidl_policy::ClientProviderRequest::GetController { requests, updates, .. } => {
            register_listener(update_sender, updates.into_proxy()?);
            handle_client_requests(ess_store, requests).await?;
            Ok(())
        }
    }
}

/// Handles all incoming requests from a ClientController.
async fn handle_client_requests(
    ess_store: EssStorePtr,
    requests: ClientRequests,
) -> Result<(), fidl::Error> {
    let mut request_stream = requests.into_stream()?;
    while let Some(request) = request_stream.try_next().await? {
        match request {
            fidl_policy::ClientControllerRequest::Connect { id, responder, .. } => {
                match handle_client_request_connect(Arc::clone(&ess_store), &id).await {
                    Ok(()) => {
                        responder.send(fidl_common::RequestStatus::Acknowledged)?;
                    }
                    Err(error) => {
                        error!("error while connection attempt: {}", error.cause);
                        responder.send(error.status)?;
                    }
                }
            }
            unsupported => error!("unsupported request: {:?}", unsupported),
        }
    }
    Ok(())
}

/// Attempts to issue a new connect request to the currently active Client.
/// The network's configuration must have been stored before issuing a connect request.
async fn handle_client_request_connect(
    ess_store: EssStorePtr,
    network: &fidl_policy::NetworkIdentifier,
) -> Result<(), RequestError> {
    let _network_config = ess_store.lookup(&network.ssid[..]).ok_or_else(|| {
        RequestError::new().with_cause(format_err!(
            "error network not found: {}",
            String::from_utf8_lossy(&network.ssid)
        ))
    })?;

    // TODO(hahnr): Verify if client STA is available.
    // TODO(hahnr): Send connect request to SME.
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
        crate::known_ess_store::KnownEss,
        fidl::endpoints::{create_proxy, create_request_stream},
        futures::{channel::mpsc, task::Poll},
        pin_utils::pin_mut,
        std::path::Path,
        wlan_common::assert_variant,
    };

    /// Creates an ESS Store holding entries for protected and unprotected networks.
    fn create_ess_store(path: &Path) -> EssStorePtr {
        let ess_store = Arc::new(
            KnownEssStore::new_with_paths(path.join("store.json"), path.join("store.json.tmp"))
                .expect("Failed to create a KnownEssStore"),
        );
        ess_store
            .store(b"foobar".to_vec(), KnownEss { password: vec![] })
            .expect("error saving network");
        ess_store
            .store(b"foobar-protected".to_vec(), KnownEss { password: b"supersecure".to_vec() })
            .expect("error saving network");
        ess_store
    }

    /// Requests a new ClientController from the given ClientProvider.
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
    fn connect_request_unknown_network() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());

        let (provider, requests) = create_proxy::<fidl_policy::ClientProviderMarker>()
            .expect("failed to create ClientProvider proxy");
        let requests = requests.into_stream().expect("failed to create stream");

        let (update_sender, listener_updates) = mpsc::unbounded();
        let serve_listeners = listener::serve(listener_updates);
        pin_mut!(serve_listeners);
        let serve_fut = serve_provider_requests(update_sender, ess_store, requests);
        pin_mut!(serve_fut);
        assert_variant!(exec.run_until_stalled(&mut serve_listeners), Poll::Pending);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _) = request_controller(&provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Issue connect request.
        let connect_fut = controller.connect(&mut fidl_policy::NetworkIdentifier {
            ssid: b"foobar-unknown".to_vec(),
            type_: fidl_policy::SecurityType::None,
        });
        pin_mut!(connect_fut);

        // Process connect request and verify connect response.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut connect_fut),
            Poll::Ready(Ok(fidl_common::RequestStatus::RejectedNotSupported))
        );
    }

    #[test]
    fn connect_request_open_network() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());

        let (provider, requests) = create_proxy::<fidl_policy::ClientProviderMarker>()
            .expect("failed to create ClientProvider proxy");
        let requests = requests.into_stream().expect("failed to create stream");

        let (update_sender, _listener_updates) = mpsc::unbounded();
        let serve_fut = serve_provider_requests(update_sender, ess_store, requests);
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _update_stream) = request_controller(&provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Issue connect request.
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
    fn register_update_listener() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());

        let (provider, requests) = create_proxy::<fidl_policy::ClientProviderMarker>()
            .expect("failed to create ClientProvider proxy");
        let requests = requests.into_stream().expect("failed to create stream");

        let (update_sender, mut listener_updates) = mpsc::unbounded();
        let serve_fut = serve_provider_requests(update_sender, ess_store, requests);
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (_controller, _update_stream) = request_controller(&provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut listener_updates.next()),
            Poll::Ready(Some(listener::Message::NewListener(_)))
        );
    }

    #[test]
    fn get_listener() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let (listener, requests) = create_proxy::<fidl_policy::ClientListenerMarker>()
            .expect("failed to create ClientProvider proxy");
        let requests = requests.into_stream().expect("failed to create stream");

        let (update_sender, mut listener_updates) = mpsc::unbounded();
        let serve_fut = serve_listener_requests(update_sender, requests);
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Register listener.
        let (update_sink, _update_stream) =
            create_request_stream::<fidl_policy::ClientStateUpdatesMarker>()
                .expect("failed to create ClientStateUpdates proxy");
        listener.get_listener(update_sink).expect("error getting listener");

        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut listener_updates.next()),
            Poll::Ready(Some(listener::Message::NewListener(_)))
        );
    }
}
