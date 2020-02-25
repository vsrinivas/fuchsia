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
    crate::{
        config_manager::SavedNetworksManager,
        fuse_pending::FusePending,
        network_config::{Credential, NetworkIdentifier},
    },
    anyhow::{format_err, Error},
    fidl::epitaph::ChannelEpitaphExt,
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_policy as fidl_policy,
    fidl_fuchsia_wlan_sme as fidl_sme, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{
        channel::mpsc,
        prelude::*,
        select,
        stream::{FuturesOrdered, FuturesUnordered},
    },
    log::{error, info},
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

#[derive(Debug)]
enum InternalMsg {
    /// Sent when a new connection request was issued. Holds the NetworkIdentifier, credential
    /// used to connect, and Transaction which the connection result will be reported through.
    NewPendingConnectRequest(
        fidl_policy::NetworkIdentifier,
        Credential,
        fidl_sme::ConnectTransactionProxy,
    ),
}
type InternalMsgSink = mpsc::UnboundedSender<InternalMsg>;

// This number was chosen arbitrarily.
const MAX_CONCURRENT_LISTENERS: usize = 1000;

type ClientRequests = fidl::endpoints::ServerEnd<fidl_policy::ClientControllerMarker>;
type SavedNetworksPtr = Arc<SavedNetworksManager>;
type ClientPtr = Arc<Mutex<Client>>;

pub fn spawn_provider_server(
    client: ClientPtr,
    update_sender: listener::MessageSender,
    saved_networks: SavedNetworksPtr,
    requests: fidl_policy::ClientProviderRequestStream,
) {
    fasync::spawn(serve_provider_requests(client, update_sender, saved_networks, requests));
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
    saved_networks: SavedNetworksPtr,
    mut requests: fidl_policy::ClientProviderRequestStream,
) {
    let (internal_messages_sink, mut internal_messages_stream) = mpsc::unbounded();
    let mut controller_reqs = FuturesUnordered::new();
    let mut pending_con_reqs = FusePending(FuturesOrdered::new());

    loop {
        select! {
            // Progress controller requests.
            _ = controller_reqs.select_next_some() => (),
            // Process provider requests.
            req = requests.select_next_some() => if let Ok(req) = req {
                // If there is an active controller - reject new requests.
                // Rust cannot yet send Epitaphs when closing a channel, thus, simply drop the
                // request.
                if controller_reqs.is_empty() {
                    let fut = handle_provider_request(
                        Arc::clone(&client),
                        internal_messages_sink.clone(),
                        update_sender.clone(),
                        Arc::clone(&saved_networks),
                        req
                    );
                    controller_reqs.push(fut);
                } else {
                    if let Err(e) = reject_provider_request(req) {
                        error!("error sending rejection epitaph");
                    }
                }
            },
            // Progress internal messages.
            msg = internal_messages_stream.select_next_some() => match msg {
                InternalMsg::NewPendingConnectRequest(id, cred, txn) => {
                    let connect_result_fut = txn.take_event_stream().into_future()
                        .map(|(first, _next)| (id, cred, first));
                    pending_con_reqs.push(connect_result_fut);
                }
            },
            // Pending connect request finished.
            resp = pending_con_reqs.select_next_some() => if let (id, cred, Some(Ok(txn))) = resp {
                handle_sme_connect_response(update_sender.clone(), id.into(), cred, txn, Arc::clone(&saved_networks)).await;
            }
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
    internal_msg_sink: InternalMsgSink,
    update_sender: listener::MessageSender,
    saved_networks: SavedNetworksPtr,
    req: fidl_policy::ClientProviderRequest,
) -> Result<(), fidl::Error> {
    match req {
        fidl_policy::ClientProviderRequest::GetController { requests, updates, .. } => {
            register_listener(update_sender, updates.into_proxy()?);
            handle_client_requests(client, internal_msg_sink, saved_networks, requests).await?;
            Ok(())
        }
    }
}

/// Handles all incoming requests from a ClientController.
async fn handle_client_requests(
    client: ClientPtr,
    internal_msg_sink: InternalMsgSink,
    saved_networks: SavedNetworksPtr,
    requests: ClientRequests,
) -> Result<(), fidl::Error> {
    let mut request_stream = requests.into_stream()?;
    while let Some(request) = request_stream.try_next().await? {
        match request {
            fidl_policy::ClientControllerRequest::Connect { id, responder, .. } => {
                match handle_client_request_connect(
                    Arc::clone(&client),
                    Arc::clone(&saved_networks),
                    &id,
                )
                .await
                {
                    Ok((cred, txn)) => {
                        responder.send(fidl_common::RequestStatus::Acknowledged)?;
                        // TODO(hahnr): Send connecting update.
                        let _ignored = internal_msg_sink
                            .unbounded_send(InternalMsg::NewPendingConnectRequest(id, cred, txn));
                    }
                    Err(error) => {
                        error!("error while connection attempt: {}", error.cause);
                        responder.send(error.status)?;
                    }
                }
            }
            fidl_policy::ClientControllerRequest::StartClientConnections { responder } => {
                let status = handle_client_request_start_connections();
                responder.send(status)?;
            }
            fidl_policy::ClientControllerRequest::StopClientConnections { responder } => {
                let status = handle_client_request_stop_connections();
                responder.send(status)?;
            }
            fidl_policy::ClientControllerRequest::ScanForNetworks { iterator, .. } => {
                handle_client_request_scan(iterator).await?;
            }
            fidl_policy::ClientControllerRequest::SaveNetwork { config, responder } => {
                let mut err =
                    handle_client_request_save_network(Arc::clone(&saved_networks), config);
                responder.send(&mut err)?;
            }
            fidl_policy::ClientControllerRequest::RemoveNetwork { config, responder } => {
                let mut err =
                    handle_client_request_remove_network(Arc::clone(&saved_networks), config);
                responder.send(&mut err)?;
            }
            fidl_policy::ClientControllerRequest::GetSavedNetworks { iterator, .. } => {
                handle_client_request_get_networks(Arc::clone(&saved_networks), iterator).await?;
            }
        }
    }
    Ok(())
}

async fn handle_sme_connect_response(
    update_sender: listener::MessageSender,
    id: NetworkIdentifier,
    credential: Credential,
    txn_event: fidl_sme::ConnectTransactionEvent,
    saved_networks: SavedNetworksPtr,
) {
    match txn_event {
        fidl_sme::ConnectTransactionEvent::OnFinished { code } => match code {
            fidl_sme::ConnectResultCode::Success => {
                info!("connection request successful to: {:?}", String::from_utf8_lossy(&id.ssid));
                saved_networks.record_connect_success(id.clone(), &credential);
                let update = fidl_policy::ClientStateSummary {
                    state: None,
                    networks: Some(vec![fidl_policy::NetworkState {
                        id: Some(id.into()),
                        state: Some(fidl_policy::ConnectionState::Connected),
                        status: None,
                    }]),
                };
                let _ignored =
                    update_sender.unbounded_send(listener::Message::NotifyListeners(update));
            }
            // No-op. Connect request was replaced.
            fidl_sme::ConnectResultCode::Canceled => (),
            error_code => {
                error!(
                    "connection request failed to: {:?} - {:?}",
                    String::from_utf8_lossy(&id.ssid),
                    error_code
                );
                // TODO(hahnr): Send failure update.
            }
        },
    }
}

/// Attempts to issue a new connect request to the currently active Client.
/// The network's configuration must have been stored before issuing a connect request.
async fn handle_client_request_connect(
    client: ClientPtr,
    saved_networks: SavedNetworksPtr,
    network: &fidl_policy::NetworkIdentifier,
) -> Result<(Credential, fidl_sme::ConnectTransactionProxy), RequestError> {
    let network_config = saved_networks
        .lookup(NetworkIdentifier::new(network.ssid.clone(), network.type_.into()))
        .pop()
        .ok_or_else(|| {
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

    let credential = sme_credential_from_policy(&network_config.credential);
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

    Ok((network_config.credential, local))
}

/// This is not yet implemented and just returns that request is not supported
fn handle_client_request_start_connections() -> fidl_common::RequestStatus {
    fidl_common::RequestStatus::RejectedNotSupported
}

/// This is not yet implemented and just returns that the request is not supported
fn handle_client_request_stop_connections() -> fidl_common::RequestStatus {
    fidl_common::RequestStatus::RejectedNotSupported
}

/// Respond to a scan request, returning results through the given iterator.
/// This function is not yet implemented and just sends back that there was an error.
async fn handle_client_request_scan(
    iterator: fidl::endpoints::ServerEnd<fidl_policy::ScanResultIteratorMarker>,
) -> Result<(), fidl::Error> {
    // for now instead of getting scan results, just return a general error
    // TODO(44878) implement this method: perform a scan and return the results (networks or error)
    let scan_results = Err(fidl_policy::ScanErrorCode::GeneralError);

    // wait to get a request for a chunk of scan results, so we can respond to it with an error
    let mut stream = iterator.into_stream()?;

    if let Some(req) = stream.try_next().await? {
        let fidl_policy::ScanResultIteratorRequest::GetNext { responder } = req;
        let mut next_result = scan_results;
        responder.send(&mut next_result)?;
    } else {
        // This will happen if the iterator request stream was closed and we expected to send
        // another response.
        // TODO(45113) Test this error path
        info!("Info: peer closed channel for scan results unexpectedly");
    }
    Ok(())
}

/// This function handles requests to save a network by saving the network and sending back to the
/// controller whether or not we successfully saved the network. There could be a fidl error in
/// sending the response.
/// Currently this just tries to respond with a general error, it is not yet implemented.
fn handle_client_request_save_network(
    mut _saved_networks: SavedNetworksPtr,
    _network_config: fidl_policy::NetworkConfig,
) -> Result<(), fidl_policy::NetworkConfigChangeError> {
    Err(fidl_policy::NetworkConfigChangeError::GeneralError)
}

/// Will remove the specified network and respond to the remove network request with a network
/// config change error if an error occurs while trying to remove the network.
fn handle_client_request_remove_network(
    mut _saved_networks: SavedNetworksPtr,
    _config: fidl_policy::NetworkConfig,
) -> Result<(), fidl_policy::NetworkConfigChangeError> {
    // method is not yet implemented, respond with a general network config error
    Err(fidl_policy::NetworkConfigChangeError::GeneralError)
}

/// For now, instead of giving actual results simply give nothing.
async fn handle_client_request_get_networks(
    _saved_networks: SavedNetworksPtr,
    iterator: fidl::endpoints::ServerEnd<fidl_policy::NetworkConfigIteratorMarker>,
) -> Result<(), fidl::Error> {
    let mut stream = iterator.into_stream()?;
    if let Some(req) = stream.try_next().await? {
        let fidl_policy::NetworkConfigIteratorRequest::GetNext { responder } = req;
        let networks: Vec<fidl_policy::NetworkConfig> = vec![];
        responder.send(&mut networks.into_iter())?;
    } else {
        // This will happen if the iterator request stream was closed and we expected to send
        // another response.
        // TODO(45113) Test this error path
        info!("Info: peer closed channel for get saved networks results unexpectedly");
    }
    Ok(())
}

/// convert from policy fidl Credential to sme fidl Credential
pub fn sme_credential_from_policy(cred: &Credential) -> fidl_sme::Credential {
    match cred {
        Credential::Password(pwd) => fidl_sme::Credential::Password(pwd.clone()),
        Credential::Psk(psk) => fidl_sme::Credential::Psk(psk.clone()),
        Credential::None => fidl_sme::Credential::None(fidl_sme::Empty {}),
    }
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

/// Rejects a ClientProvider request by sending a corresponding Epitaph via the |requests| and
/// |updates| channels.
fn reject_provider_request(req: fidl_policy::ClientProviderRequest) -> Result<(), fidl::Error> {
    match req {
        fidl_policy::ClientProviderRequest::GetController { requests, updates, .. } => {
            requests.into_channel().close_with_epitaph(zx::Status::ALREADY_BOUND)?;
            updates.into_channel().close_with_epitaph(zx::Status::ALREADY_BOUND)?;
            Ok(())
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            config_manager::SavedNetworksManager,
            network_config::{NetworkConfig, SecurityType},
        },
        fidl::{
            endpoints::{create_proxy, create_request_stream},
            Error,
        },
        futures::{channel::mpsc, task::Poll},
        pin_utils::pin_mut,
        tempfile::TempDir,
        wlan_common::assert_variant,
    };

    /// Creates an ESS Store holding entries for protected and unprotected networks.
    async fn create_network_store(stash_id: impl AsRef<str>) -> SavedNetworksPtr {
        let temp_dir = TempDir::new().expect("failed to create temp dir");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks = Arc::new(
            SavedNetworksManager::new_with_stash_or_paths(stash_id, path, tmp_path)
                .await
                .expect("Failed to create a KnownEssStore"),
        );
        let network_id_none = NetworkIdentifier::new(b"foobar".to_vec(), SecurityType::None);
        let network_id_password =
            NetworkIdentifier::new(b"foobar-protected".to_vec(), SecurityType::Wpa2);
        let network_id_psk = NetworkIdentifier::new(b"foobar-psk".to_vec(), SecurityType::Wpa2);

        saved_networks.store(network_id_none, Credential::None).expect("error saving network");
        saved_networks
            .store(network_id_password, Credential::Password(b"supersecure".to_vec()))
            .expect("error saving network");
        saved_networks
            .store(network_id_psk, Credential::Psk(vec![64; 64].to_vec()))
            .expect("error saving network foobar-psk");

        saved_networks
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

    struct TestValues {
        saved_networks: SavedNetworksPtr,
        provider: fidl_policy::ClientProviderProxy,
        requests: fidl_policy::ClientProviderRequestStream,
        client: ClientPtr,
        sme_stream: fidl_sme::ClientSmeRequestStream,
        update_sender: mpsc::UnboundedSender<listener::Message>,
        listener_updates: mpsc::UnboundedReceiver<listener::Message>,
    }

    // setup channels and proxies needed for the tests to use use the Client Provider and
    // Client Controller APIs in tests. The stash id should be the test name so that each
    // test will have a unique persistent store behind it.
    fn test_setup(stash_id: impl AsRef<str>, exec: &mut fasync::Executor) -> TestValues {
        let saved_networks = exec.run_singlethreaded(create_network_store(stash_id));

        let (provider, requests) = create_proxy::<fidl_policy::ClientProviderMarker>()
            .expect("failed to create ClientProvider proxy");
        let requests = requests.into_stream().expect("failed to create stream");

        let (client, sme_stream) = create_client();
        let (update_sender, listener_updates) = mpsc::unbounded();
        TestValues {
            saved_networks,
            provider,
            requests,
            client,
            sme_stream,
            update_sender,
            listener_updates,
        }
    }

    #[test]
    fn connect_request_unknown_network() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup("connect_request_unknown_network", &mut exec);
        let serve_fut = serve_provider_requests(
            test_values.client,
            test_values.update_sender,
            Arc::clone(&test_values.saved_networks),
            test_values.requests,
        );
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _) = request_controller(&test_values.provider);
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

        // unknown network should not have been saved by saved networks manager
        // since we did not successfully connect
        assert!(test_values
            .saved_networks
            .lookup(NetworkIdentifier::new(b"foobar-unknown".to_vec(), SecurityType::None))
            .is_empty());
    }

    #[test]
    fn connect_request_open_network() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let mut test_values = test_setup("connect_request_open_network", &mut exec);
        let serve_fut = serve_provider_requests(
            test_values.client,
            test_values.update_sender,
            Arc::clone(&test_values.saved_networks),
            test_values.requests,
        );
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _update_stream) = request_controller(&test_values.provider);
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
            exec.run_until_stalled(&mut test_values.sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Connect {
                req, ..
            }))) => {
                assert_eq!(b"foobar", &req.ssid[..]);
                assert_eq!(fidl_sme::Credential::None(fidl_sme::Empty), req.credential);
            }
        );
    }

    #[test]
    fn connect_request_protected_network() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup("connect_request_protected_network", &mut exec);
        let serve_fut = serve_provider_requests(
            test_values.client,
            test_values.update_sender,
            Arc::clone(&test_values.saved_networks),
            test_values.requests,
        );
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _update_stream) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Issue connect request.
        let connect_fut = controller.connect(&mut fidl_policy::NetworkIdentifier {
            ssid: b"foobar-protected".to_vec(),
            type_: fidl_policy::SecurityType::Wpa2,
        });
        pin_mut!(connect_fut);

        // Process connect request and verify connect response.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut connect_fut),
            Poll::Ready(Ok(fidl_common::RequestStatus::Acknowledged))
        );

        assert_variant!(
            exec.run_until_stalled(&mut test_values.sme_stream.next()),
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
    fn connect_request_protected_psk_network() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup("connect_request_protected_psk_network", &mut exec);
        let serve_fut = serve_provider_requests(
            test_values.client,
            test_values.update_sender,
            Arc::clone(&test_values.saved_networks),
            test_values.requests,
        );
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _update_stream) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Issue connect request.
        let connect_fut = controller.connect(&mut fidl_policy::NetworkIdentifier {
            ssid: b"foobar-psk".to_vec(),
            type_: fidl_policy::SecurityType::Wpa2,
        });
        pin_mut!(connect_fut);

        // Process connect request and verify connect response.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut connect_fut),
            Poll::Ready(Ok(fidl_common::RequestStatus::Acknowledged))
        );

        assert_variant!(
            exec.run_until_stalled(&mut test_values.sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Connect {
                req, ..
            }))) => {
                assert_eq!(b"foobar-psk", &req.ssid[..]);
                assert_eq!(fidl_sme::Credential::Psk([64;64].to_vec()), req.credential);
                // TODO(hahnr): Send connection response.
            }
        );
    }

    #[test]
    fn connect_request_success() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup("connect_request_success", &mut exec);
        let serve_fut = serve_provider_requests(
            test_values.client,
            test_values.update_sender,
            Arc::clone(&test_values.saved_networks),
            test_values.requests,
        );
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _update_stream) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut test_values.listener_updates.next()),
            Poll::Ready(_)
        );

        // Issue connect request.
        let connect_fut = controller.connect(&mut fidl_policy::NetworkIdentifier {
            ssid: b"foobar".to_vec(),
            type_: fidl_policy::SecurityType::None,
        });
        pin_mut!(connect_fut);

        // Process connect request and verify connect response.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut connect_fut), Poll::Ready(Ok(_)));

        assert_variant!(
            exec.run_until_stalled(&mut test_values.sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Connect {
                txn, ..
            }))) => {
                // Send connection response.
                let (_stream, ctrl) = txn.expect("connect txn unused")
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl.send_on_finished(fidl_sme::ConnectResultCode::Success)
                    .expect("failed to send connection completion");
            }
        );

        // Process SME result.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Verify status update.
        let summary = assert_variant!(
            exec.run_until_stalled(&mut test_values.listener_updates.next()),
            Poll::Ready(Some(listener::Message::NotifyListeners(summary))) => summary
        );
        let expected_summary = fidl_policy::ClientStateSummary {
            state: None,
            networks: Some(vec![fidl_policy::NetworkState {
                id: Some(fidl_policy::NetworkIdentifier {
                    ssid: b"foobar".to_vec(),
                    type_: fidl_policy::SecurityType::None,
                }),
                state: Some(fidl_policy::ConnectionState::Connected),
                status: None,
            }]),
        };
        assert_eq!(summary, expected_summary);

        // saved network config should reflect that it has connected successfully
        let cfg = get_config(
            Arc::clone(&test_values.saved_networks),
            NetworkIdentifier::new(b"foobar".to_vec(), SecurityType::None),
            Credential::None,
        );
        assert_variant!(cfg, Some(cfg) => {
            assert!(cfg.has_ever_connected)
        });
    }

    #[test]
    fn connect_request_failure() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup("connect_request_failure", &mut exec);
        let serve_fut = serve_provider_requests(
            test_values.client,
            test_values.update_sender,
            Arc::clone(&test_values.saved_networks),
            test_values.requests,
        );
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _update_stream) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut test_values.listener_updates.next()),
            Poll::Ready(_)
        );

        // Issue connect request.
        let connect_fut = controller.connect(&mut fidl_policy::NetworkIdentifier {
            ssid: b"foobar".to_vec(),
            type_: fidl_policy::SecurityType::None,
        });
        pin_mut!(connect_fut);

        // Process connect request and verify connect response.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut connect_fut), Poll::Ready(Ok(_)));

        assert_variant!(
            exec.run_until_stalled(&mut test_values.sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Connect {
                txn, ..
            }))) => {
                // Send failed connection response.
                let (_stream, ctrl) = txn.expect("connect txn unused")
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl.send_on_finished(fidl_sme::ConnectResultCode::Failed)
                    .expect("failed to send connection completion");
            }
        );

        // Process SME result.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Verify status was not updated.
        assert_variant!(
            exec.run_until_stalled(&mut test_values.listener_updates.next()),
            Poll::Pending
        );

        // Verify network config reflects that we still have not connected successfully
        let cfg = get_config(
            test_values.saved_networks,
            NetworkIdentifier::new(b"foobar".to_vec(), SecurityType::None),
            Credential::None,
        );
        assert_variant!(cfg, Some(cfg) => {
            assert_eq!(false, cfg.has_ever_connected);
        });
    }

    #[test]
    fn start_and_stop_client_connections_should_fail() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup("start_and_stop_client_connections_should_fail", &mut exec);
        let serve_fut = serve_provider_requests(
            test_values.client,
            test_values.update_sender,
            Arc::clone(&test_values.saved_networks),
            test_values.requests,
        );
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should now be waiting for request.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _update_stream) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Issue request to start client connections.
        let start_fut = controller.start_client_connections();
        pin_mut!(start_fut);

        // Request should be rejected.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut start_fut),
            Poll::Ready(Ok(fidl_common::RequestStatus::RejectedNotSupported))
        );

        // Issue request to stop client connections.
        let stop_fut = controller.stop_client_connections();
        pin_mut!(stop_fut);

        // Request should be rejected.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut stop_fut),
            Poll::Ready(Ok(fidl_common::RequestStatus::RejectedNotSupported))
        );
    }

    #[test]
    fn scan_for_networks_should_fail() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup("scan_for_networks_should_fail", &mut exec);
        let serve_fut = serve_provider_requests(
            test_values.client,
            test_values.update_sender,
            Arc::clone(&test_values.saved_networks),
            test_values.requests,
        );
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _update_stream) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut test_values.listener_updates.next()),
            Poll::Ready(_)
        );

        // Issue request to scan.
        let (iter, server) =
            fidl::endpoints::create_proxy::<fidl_policy::ScanResultIteratorMarker>()
                .expect("failed to create iterator");
        controller.scan_for_networks(server).expect("Failed to call scan for networks");

        // Request a chunk of scan results. Progress until waiting on response from server side of
        // the iterator.
        let mut scan_fut = iter.get_next();
        assert_variant!(exec.run_until_stalled(&mut scan_fut), Poll::Pending);
        // Progress sever side forward so that it will respond to the iterator get next request.
        // It will be waiting on the next client provider request because scan simply returns an
        // error for now.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // the iterator should have given an error
        assert_variant!(exec.run_until_stalled(&mut scan_fut), Poll::Ready(result) => {
            let results = result.expect("Failed to get next scan results");
            assert_eq!(results, Err(fidl_policy::ScanErrorCode::GeneralError));
        });
    }

    #[test]
    fn save_network_should_fail() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup("save_network_should_fail", &mut exec);
        let serve_fut = serve_provider_requests(
            test_values.client,
            test_values.update_sender,
            Arc::clone(&test_values.saved_networks),
            test_values.requests,
        );
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _update_stream) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut test_values.listener_updates.next()),
            Poll::Ready(_)
        );

        // Save some network
        let network_id = fidl_policy::NetworkIdentifier {
            ssid: b"foo".to_vec(),
            type_: fidl_policy::SecurityType::None,
        };
        let network_config = fidl_policy::NetworkConfig {
            id: Some(network_id),
            credential: Some(fidl_policy::Credential::None(fidl_policy::Empty)),
        };
        let mut save_fut = controller.save_network(network_config);

        // Run server_provider forward so that it will process the save network request
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        assert_variant!(exec.run_until_stalled(&mut save_fut), Poll::Ready(result) => {
            let error = result.expect("Failed to get save network response");
            assert_eq!(error, Err(fidl_policy::NetworkConfigChangeError::GeneralError));
        });
    }

    #[test]
    fn remove_network_should_fail() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup("remove_network_should_fail", &mut exec);
        let serve_fut = serve_provider_requests(
            test_values.client,
            test_values.update_sender,
            Arc::clone(&test_values.saved_networks),
            test_values.requests,
        );
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _update_stream) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut test_values.listener_updates.next()),
            Poll::Ready(_)
        );

        // Request to remove some network
        let network_id = fidl_policy::NetworkIdentifier {
            ssid: b"foo".to_vec(),
            type_: fidl_policy::SecurityType::None,
        };
        let network_config = fidl_policy::NetworkConfig {
            id: Some(network_id),
            credential: Some(fidl_policy::Credential::None(fidl_policy::Empty)),
        };
        let mut remove_fut = controller.remove_network(network_config);

        // Run server_provider forward so that it will process the save network request
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        assert_variant!(exec.run_until_stalled(&mut remove_fut), Poll::Ready(result) => {
            let error = result.expect("Failed to get save network response");
            assert_eq!(error, Err(fidl_policy::NetworkConfigChangeError::GeneralError));
        });
    }

    #[test]
    fn get_saved_networks_should_fail() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup("get_saved_networks_should_fail", &mut exec);
        let serve_fut = serve_provider_requests(
            test_values.client,
            test_values.update_sender,
            Arc::clone(&test_values.saved_networks),
            test_values.requests,
        );
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _update_stream) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut test_values.listener_updates.next()),
            Poll::Ready(_)
        );

        // Issue request to get the list of saved networks.
        let (iter, server) =
            fidl::endpoints::create_proxy::<fidl_policy::NetworkConfigIteratorMarker>()
                .expect("failed to create iterator");
        controller.get_saved_networks(server).expect("Failed to call scan for networks");

        // Request a chunk of results. Will progress until waiting on response from server side of
        // the iterator.
        let mut get_saved_fut = iter.get_next();
        assert_variant!(exec.run_until_stalled(&mut get_saved_fut), Poll::Pending);
        // Progress sever side forward so that it will respond to the iterator get next request.
        // After, it will be waiting on the next client provider request because get saved networks
        // simply returns an empty list for now
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // the iterator should have given an empty list
        assert_variant!(exec.run_until_stalled(&mut get_saved_fut), Poll::Ready(result) => {
            let results = result.expect("Failed to get next get-saved-networks results");
            assert!(results.is_empty());
        });
    }

    #[test]
    fn register_update_listener() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup("register_update_listener", &mut exec);
        let serve_fut = serve_provider_requests(
            test_values.client,
            test_values.update_sender,
            test_values.saved_networks,
            test_values.requests,
        );
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (_controller, _update_stream) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut test_values.listener_updates.next()),
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
    fn multiple_controllers_write_attempt() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup("multiple_controllers_write_attempt", &mut exec);
        let serve_fut = serve_provider_requests(
            test_values.client,
            test_values.update_sender,
            test_values.saved_networks,
            test_values.requests,
        );
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a controller.
        let (controller1, _) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request another controller.
        let (controller2, _) = request_controller(&test_values.provider);
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
        let (controller3, _) = request_controller(&test_values.provider);
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
    fn multiple_controllers_epitaph() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup("multiple_controllers_epitaph", &mut exec);
        let serve_fut = serve_provider_requests(
            test_values.client,
            test_values.update_sender,
            test_values.saved_networks,
            test_values.requests,
        );
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a controller.
        let (_controller1, _) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request another controller.
        let (controller2, _) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        let chan = controller2.into_channel().expect("error turning proxy into channel");
        let mut buffer = zx::MessageBuf::new();
        let epitaph_fut = chan.recv_msg(&mut buffer);
        pin_mut!(epitaph_fut);
        assert_variant!(exec.run_until_stalled(&mut epitaph_fut), Poll::Ready(Ok(_)));

        // Verify Epitaph was received.
        use fidl::encoding::{decode_transaction_header, Decodable, Decoder, EpitaphBody};
        let (header, tail) =
            decode_transaction_header(buffer.bytes()).expect("failed decoding header");
        let mut msg = Decodable::new_empty();
        Decoder::decode_into::<EpitaphBody>(&header, tail, &mut [], &mut msg)
            .expect("failed decoding body");
        assert_eq!(msg.error, zx::Status::ALREADY_BOUND);
        assert!(chan.is_closed());
    }

    #[test]
    fn no_client_interface() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let stash_id = "no_client_interface";
        let saved_networks = exec.run_singlethreaded(create_network_store(stash_id));

        let (provider, requests) = create_proxy::<fidl_policy::ClientProviderMarker>()
            .expect("failed to create ClientProvider proxy");
        let requests = requests.into_stream().expect("failed to create stream");

        let client = Arc::new(Mutex::new(Client::new_empty()));
        let (update_sender, _listener_updates) = mpsc::unbounded();
        let serve_fut = serve_provider_requests(client, update_sender, saved_networks, requests);
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

    // Gets a saved network config with a particular SSID, security type, and credential.
    // If there are more than one configs saved for the same SSID, security type, and credential,
    // the function will panic.
    fn get_config(
        saved_networks: Arc<SavedNetworksManager>,
        id: NetworkIdentifier,
        cred: Credential,
    ) -> Option<NetworkConfig> {
        let mut cfgs = saved_networks
            .lookup(id)
            .into_iter()
            .filter(|cfg| cfg.credential == cred)
            .collect::<Vec<_>>();
        // there should not be multiple configs with the same SSID, security type, and credential.
        assert!(cfgs.len() <= 1);
        cfgs.pop()
    }

    #[fasync::run_singlethreaded(test)]
    async fn get_correct_config() {
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let stash_id = "get_correct_config";
        let saved_networks = Arc::new(
            SavedNetworksManager::new_with_stash_or_paths(stash_id, path, tmp_path)
                .await
                .expect("Failed to create SavedNetworksManager"),
        );
        let network_id = NetworkIdentifier::new(b"foo".to_vec(), SecurityType::Wpa2);
        let cfg = NetworkConfig::new(
            network_id.clone(),
            Credential::Password(b"password".to_vec()),
            false,
            false,
        )
        .expect("Failed to create network config");

        saved_networks
            .store(network_id.clone(), Credential::Password(b"password".to_vec()))
            .expect("Failed to store network config");

        assert_eq!(
            Some(cfg),
            get_config(
                Arc::clone(&saved_networks),
                network_id,
                Credential::Password(b"password".to_vec())
            )
        );
        assert_eq!(
            None,
            get_config(
                Arc::clone(&saved_networks),
                NetworkIdentifier::new(b"foo".to_vec(), SecurityType::Wpa2),
                Credential::Password(b"not-saved".to_vec())
            )
        );
    }
}
