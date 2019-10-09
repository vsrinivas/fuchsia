// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

///! Serves Client policy services.
///! Note: This implementation is still under development.
///!       Only connect requests will cause the underlying SME to attempt to connect to a given
///!       network.
///!       Unfortunately, there is currently no way to send an Epitaph in Rust. Thus, inbound
///!       controller and listener requests are simply dropped, causing the underlying channel to
///!       get closed.
///!
use {
    crate::known_ess_store::KnownEssStore,
    failure::{format_err, Error},
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_policy as fidl_policy,
    fidl_fuchsia_wlan_sme as fidl_sme, fuchsia_async as fasync,
    futures::{prelude::*, select, stream::FuturesUnordered},
    log::error,
    parking_lot::Mutex,
    std::sync::Arc,
};

pub mod listener;

/// Wrapper around a Client interface, granting access to the Client SME.
/// A Client might not always be available, for example, if no Client interface was created yet.
pub struct Client {
    proxy: Option<fidl_sme::ClientSmeProxy>,
}

impl Client {
    /// Creates a new, empty Client. The returned Client effectively represents the state in which
    /// no client interface is available.
    pub fn new_empty() -> Self {
        Self { proxy: None }
    }

    /// Accesses the Client interface's SME.
    /// Returns None if no Client interface is available.
    fn access_sme(&self) -> Option<&fidl_sme::ClientSmeProxy> {
        self.proxy.as_ref()
    }
}

impl From<fidl_sme::ClientSmeProxy> for Client {
    fn from(proxy: fidl_sme::ClientSmeProxy) -> Self {
        Self { proxy: Some(proxy) }
    }
}

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
const PSK_HEX_STRING_LENGTH: usize = 64;

type ClientRequests = fidl::endpoints::ServerEnd<fidl_policy::ClientControllerMarker>;
type EssStorePtr = Arc<KnownEssStore>;
type ClientPtr = Arc<Mutex<Client>>;

pub fn spawn_provider_server(
    client: ClientPtr,
    update_sender: listener::MessageSender,
    ess_store: EssStorePtr,
    requests: fidl_policy::ClientProviderRequestStream,
) {
    fasync::spawn(serve_provider_requests(client, update_sender, ess_store, requests));
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
    client: ClientPtr,
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
                    let fut = handle_provider_request(
                        Arc::clone(&client),
                        update_sender.clone(),
                        Arc::clone(&ess_store),
                        req
                    );
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
    client: ClientPtr,
    update_sender: listener::MessageSender,
    ess_store: EssStorePtr,
    req: fidl_policy::ClientProviderRequest,
) -> Result<(), fidl::Error> {
    match req {
        fidl_policy::ClientProviderRequest::GetController { requests, updates, .. } => {
            register_listener(update_sender, updates.into_proxy()?);
            handle_client_requests(client, ess_store, requests).await?;
            Ok(())
        }
    }
}

/// Handles all incoming requests from a ClientController.
async fn handle_client_requests(
    client: ClientPtr,
    ess_store: EssStorePtr,
    requests: ClientRequests,
) -> Result<(), fidl::Error> {
    let mut request_stream = requests.into_stream()?;
    while let Some(request) = request_stream.try_next().await? {
        match request {
            fidl_policy::ClientControllerRequest::Connect { id, responder, .. } => {
                match handle_client_request_connect(
                    Arc::clone(&client),
                    Arc::clone(&ess_store),
                    &id,
                )
                .await
                {
                    Ok(_txn) => {
                        // TODO(hahnr): Await connection result.
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
    client: ClientPtr,
    ess_store: EssStorePtr,
    network: &fidl_policy::NetworkIdentifier,
) -> Result<fidl_sme::ConnectTransactionProxy, RequestError> {
    let network_config = ess_store.lookup(&network.ssid[..]).ok_or_else(|| {
        RequestError::new().with_cause(format_err!(
            "error network not found: {}",
            String::from_utf8_lossy(&network.ssid)
        ))
    })?;

    // TODO(hahnr): Discuss whether every request should verify the existence of a Client, or
    // whether that should be handled by either, closing the currently active controller if a
    // client interface is brought down and not supporting controller requests if no client
    // interface is active.
    let client = client.lock();
    let client_sme = client.access_sme().ok_or_else(|| {
        RequestError::new().with_cause(format_err!("error no active client interface"))
    })?;

    // TODO(hahnr): The credential type from the given NetworkIdentifier is currently ignored.
    // Instead the credentials are derived from the saved |network_config| which is looked-up.
    // There has to be a decision how the case of two different credential types should be handled.

    let credential = credential_from_bytes(network_config.password);
    let mut request = fidl_sme::ConnectRequest {
        ssid: network.ssid.to_vec(),
        credential,
        radio_cfg: fidl_sme::RadioConfig {
            override_phy: false,
            phy: fidl_common::Phy::Vht,
            override_cbw: false,
            cbw: fidl_common::Cbw::Cbw80,
            override_primary_chan: false,
            primary_chan: 0,
        },
        scan_type: fidl_common::ScanType::Passive,
    };
    let (local, remote) = fidl::endpoints::create_proxy()?;
    client_sme.connect(&mut request, Some(remote))?;

    Ok(local)
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

/// Returns:
/// - an Open-Credential instance iff `bytes` is empty,
/// - a PSK-Credential instance iff `bytes` holds exactly 64 bytes,
/// - a Password-Credential in all other cases.
/// In the PSK case, the provided bytes must represent the PSK in hex format.
/// Note: This function is of temporary nature until Wlancfg's ESS-Store supports richer
/// credential types beyond plain passwords.
fn credential_from_bytes(bytes: Vec<u8>) -> fidl_sme::Credential {
    match bytes.len() {
        0 => fidl_sme::Credential::None(fidl_sme::Empty),
        PSK_HEX_STRING_LENGTH => fidl_sme::Credential::Psk(bytes),
        _ => fidl_sme::Credential::Password(bytes),
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::known_ess_store::KnownEss,
        fidl::{
            endpoints::{create_proxy, create_request_stream},
            Error,
        },
        fuchsia_zircon as zx,
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

    /// Creates a Client wrapper.
    fn create_client() -> (ClientPtr, fidl_sme::ClientSmeRequestStream) {
        let (client_sme, remote) =
            create_proxy::<fidl_sme::ClientSmeMarker>().expect("error creating proxy");
        (
            Arc::new(Mutex::new(client_sme.into())),
            remote.into_stream().expect("failed to create stream"),
        )
    }

    #[test]
    fn connect_request_unknown_network() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());

        let (provider, requests) = create_proxy::<fidl_policy::ClientProviderMarker>()
            .expect("failed to create ClientProvider proxy");
        let requests = requests.into_stream().expect("failed to create stream");

        let (client, _sme_stream) = create_client();
        let (update_sender, _listener_updates) = mpsc::unbounded();
        let serve_fut = serve_provider_requests(client, update_sender, ess_store, requests);
        pin_mut!(serve_fut);

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

        let (client, mut sme_stream) = create_client();
        let (update_sender, _listener_updates) = mpsc::unbounded();
        let serve_fut = serve_provider_requests(client, update_sender, ess_store, requests);
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

        assert_variant!(
            exec.run_until_stalled(&mut sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Connect {
                req, ..
            }))) => {
                assert_eq!(b"foobar", &req.ssid[..]);
                assert_eq!(fidl_sme::Credential::None(fidl_sme::Empty), req.credential);
                // TODO(hahnr): Send connection response.
            }
        );
    }

    #[test]
    fn connect_request_protected_network() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());

        let (provider, requests) = create_proxy::<fidl_policy::ClientProviderMarker>()
            .expect("failed to create ClientProvider proxy");
        let requests = requests.into_stream().expect("failed to create stream");

        let (update_sender, _listener_updates) = mpsc::unbounded();
        let (client, mut sme_stream) = create_client();
        let serve_fut = serve_provider_requests(client, update_sender, ess_store, requests);
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _update_stream) = request_controller(&provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Issue connect request.
        let connect_fut = controller.connect(&mut fidl_policy::NetworkIdentifier {
            ssid: b"foobar-protected".to_vec(),
            type_: fidl_policy::SecurityType::None,
        });
        pin_mut!(connect_fut);

        // Process connect request and verify connect response.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut connect_fut),
            Poll::Ready(Ok(fidl_common::RequestStatus::Acknowledged))
        );

        assert_variant!(
            exec.run_until_stalled(&mut sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Connect {
                req, ..
            }))) => {
                assert_eq!(b"foobar-protected", &req.ssid[..]);
                assert_eq!(fidl_sme::Credential::Password(b"supersecure".to_vec()), req.credential);
                // TODO(hahnr): Send connection response.
            }
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

        let (client, _sme_stream) = create_client();
        let (update_sender, mut listener_updates) = mpsc::unbounded();
        let serve_fut = serve_provider_requests(client, update_sender, ess_store, requests);
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

    #[test]
    fn multiple_controllers() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());

        let (provider, requests) = create_proxy::<fidl_policy::ClientProviderMarker>()
            .expect("failed to create ClientProvider proxy");
        let requests = requests.into_stream().expect("failed to create stream");

        let (client, _sme_stream) = create_client();
        let (update_sender, _listener_updates) = mpsc::unbounded();
        let serve_fut = serve_provider_requests(client, update_sender, ess_store, requests);
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a controller.
        let (controller1, _) = request_controller(&provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request another controller.
        let (controller2, _) = request_controller(&provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Ensure first controller is operable. Issue connect request.
        let connect_fut = controller1.connect(&mut fidl_policy::NetworkIdentifier {
            ssid: b"foobar".to_vec(),
            type_: fidl_policy::SecurityType::None,
        });
        pin_mut!(connect_fut);

        // Process connect request from first controller. Verify success.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut connect_fut),
            Poll::Ready(Ok(fidl_common::RequestStatus::Acknowledged))
        );

        // Ensure second controller is not operable. Issue connect request.
        let connect_fut = controller2.connect(&mut fidl_policy::NetworkIdentifier {
            ssid: b"foobar".to_vec(),
            type_: fidl_policy::SecurityType::None,
        });
        pin_mut!(connect_fut);

        // Process connect request from second controller. Verify failure.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut connect_fut),
            Poll::Ready(Err(Error::ClientWrite(zx::Status::PEER_CLOSED)))
        );

        // Drop first controller. A new controller can now take control.
        drop(controller1);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request another controller.
        let (controller3, _) = request_controller(&provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Ensure third controller is operable. Issue connect request.
        let connect_fut = controller3.connect(&mut fidl_policy::NetworkIdentifier {
            ssid: b"foobar".to_vec(),
            type_: fidl_policy::SecurityType::None,
        });
        pin_mut!(connect_fut);

        // Process connect request from third controller. Verify success.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut connect_fut),
            Poll::Ready(Ok(fidl_common::RequestStatus::Acknowledged))
        );
    }

    #[test]
    fn no_client_interface() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());

        let (provider, requests) = create_proxy::<fidl_policy::ClientProviderMarker>()
            .expect("failed to create ClientProvider proxy");
        let requests = requests.into_stream().expect("failed to create stream");

        let client = Arc::new(Mutex::new(Client::new_empty()));
        let (update_sender, _listener_updates) = mpsc::unbounded();
        let serve_fut = serve_provider_requests(client, update_sender, ess_store, requests);
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a controller.
        let (controller, _) = request_controller(&provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Issue connect request.
        let connect_fut = controller.connect(&mut fidl_policy::NetworkIdentifier {
            ssid: b"foobar".to_vec(),
            type_: fidl_policy::SecurityType::None,
        });
        pin_mut!(connect_fut);

        // Process connect request from first controller. Verify success.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut connect_fut),
            Poll::Ready(Ok(fidl_common::RequestStatus::RejectedNotSupported))
        );
    }

    #[test]
    fn test_credential_from_bytes() {
        assert_eq!(credential_from_bytes(vec![1]), fidl_sme::Credential::Password(vec![1]));
        assert_eq!(credential_from_bytes(vec![2; 63]), fidl_sme::Credential::Password(vec![2; 63]));
        assert_eq!(credential_from_bytes(vec![2; 64]), fidl_sme::Credential::Psk(vec![2; 64]));
        assert_eq!(credential_from_bytes(vec![]), fidl_sme::Credential::None(fidl_sme::Empty));
    }
}
