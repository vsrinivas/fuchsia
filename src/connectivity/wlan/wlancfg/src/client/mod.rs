// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Serves Client policy services.
//! Note: This implementation is still under development.
//!       Only connect requests will cause the underlying SME to attempt to connect to a given
//!       network.
//!       Unfortunately, there is currently no way to send an Epitaph in Rust. Thus, inbound
//!       controller and listener requests are simply dropped, causing the underlying channel to
//!       get closed.
use {
    crate::{
        config_management::{
            Credential, NetworkConfigError, NetworkIdentifier, SaveError, SavedNetworksManager,
        },
        util::fuse_pending::FusePending,
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
    scan::handle_scan,
    std::{convert::TryFrom, sync::Arc},
};

pub mod listener;
mod scan;

/// Max number of network configs that we will send at once through the network config iterator
/// in get_saved_networks. This depends on the maximum size of a FIDL NetworkConfig, so it may
/// need to change if a FIDL NetworkConfig or FIDL Credential changes.
const MAX_CONFIGS_PER_RESPONSE: usize = 100;

/// Wrapper around a Client interface, granting access to the Client SME.
/// A Client might not always be available, for example, if no Client interface was created yet.
#[derive(Debug)]
pub struct Client {
    proxy: Option<fidl_sme::ClientSmeProxy>,
}

impl Client {
    /// Creates a new, empty Client. The returned Client effectively represents the state in which
    /// no client interface is available.
    pub fn new_empty() -> Self {
        Self { proxy: None }
    }

    pub fn set_sme(&mut self, proxy: fidl_sme::ClientSmeProxy) {
        self.proxy = Some(proxy);
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
    /// Sent when a new scan request was issued. Holds the output iterator through which the
    /// scan results will be reported.
    NewPendingScanRequest(fidl::endpoints::ServerEnd<fidl_policy::ScanResultIteratorMarker>),
}
type InternalMsgSink = mpsc::UnboundedSender<InternalMsg>;

// This number was chosen arbitrarily.
const MAX_CONCURRENT_LISTENERS: usize = 1000;

type ClientRequests = fidl::endpoints::ServerEnd<fidl_policy::ClientControllerMarker>;
type SavedNetworksPtr = Arc<SavedNetworksManager>;
pub type ClientPtr = Arc<Mutex<Client>>;

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
    let mut pending_scans = FuturesUnordered::new();
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
                InternalMsg::NewPendingScanRequest(output_iterator) => {
                    pending_scans.push(handle_scan(
                        Arc::clone(&client),
                        output_iterator));
                }
            },
            // Progress scans.
            () = pending_scans.select_next_some() => (),
            // Pending connect request finished.
            resp = pending_con_reqs.select_next_some() => if let (id, cred, Some(Ok(txn))) = resp {
                handle_sme_connect_response(update_sender.clone(), id.into(), cred, txn, Arc::clone(&saved_networks)).await;
            },
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

/// Logs a message for an incoming ClientControllerRequest
fn log_client_request(request: &fidl_policy::ClientControllerRequest) {
    info!(
        "Received policy client request {}",
        match request {
            fidl_policy::ClientControllerRequest::Connect { .. } => "Connect",
            fidl_policy::ClientControllerRequest::StartClientConnections { .. } =>
                "StartClientConnections",
            fidl_policy::ClientControllerRequest::StopClientConnections { .. } =>
                "StopClientConnections",
            fidl_policy::ClientControllerRequest::ScanForNetworks { .. } => "ScanForNetworks",
            fidl_policy::ClientControllerRequest::SaveNetwork { .. } => "SaveNetwork",
            fidl_policy::ClientControllerRequest::RemoveNetwork { .. } => "RemoveNetwork",
            fidl_policy::ClientControllerRequest::GetSavedNetworks { .. } => "GetSavedNetworks",
        }
    );
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
        log_client_request(&request);
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
                if let Err(e) =
                    internal_msg_sink.unbounded_send(InternalMsg::NewPendingScanRequest(iterator))
                {
                    error!("Failed to send internal message: {:?}", e)
                }
            }
            fidl_policy::ClientControllerRequest::SaveNetwork { config, responder } => {
                // If there is an error saving the network, log it and convert to a FIDL value.
                let mut response =
                    handle_client_request_save_network(Arc::clone(&saved_networks), config)
                        .map_err(|e| {
                            error!("Failed to save network: {:?}", e);
                            fidl_policy::NetworkConfigChangeError::from(e)
                        });
                responder.send(&mut response)?;
            }
            fidl_policy::ClientControllerRequest::RemoveNetwork { config, responder } => {
                let mut err =
                    handle_client_request_remove_network(Arc::clone(&saved_networks), config)
                        .map_err(|_| SaveError::GeneralError);
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
    let client_sme = client
        .access_sme()
        .ok_or_else(|| RequestError::new().with_cause(format_err!("no active client interface")))?;

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
    let (local, remote) = fidl::endpoints::create_proxy().map_err(|e| {
        RequestError::new().with_cause(format_err!("failed to create proxy: {:?}", e))
    })?;
    client_sme.connect(&mut request, Some(remote)).map_err(|e| {
        RequestError::new().with_cause(format_err!("failed to connect to sme: {:?}", e))
    })?;

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

/// This function handles requests to save a network by saving the network and sending back to the
/// controller whether or not we successfully saved the network. There could be a FIDL error in
/// sending the response.
fn handle_client_request_save_network(
    saved_networks: SavedNetworksPtr,
    network_config: fidl_policy::NetworkConfig,
) -> Result<(), NetworkConfigError> {
    // The FIDL network config fields are defined as options, and we consider it an error if either
    // field is missing (ie None) here.
    let net_id = network_config.id.ok_or_else(|| NetworkConfigError::ConfigMissingId)?;
    let credential = Credential::try_from(
        network_config.credential.ok_or_else(|| NetworkConfigError::ConfigMissingCredential)?,
    )?;
    saved_networks.store(NetworkIdentifier::from(net_id), credential)?;
    Ok(())
}

/// Will remove the specified network and respond to the remove network request with a network
/// config change error if an error occurs while trying to remove the network.
fn handle_client_request_remove_network(
    mut _saved_networks: SavedNetworksPtr,
    _config: fidl_policy::NetworkConfig,
) -> Result<(), Error> {
    // method is not yet implemented, respond with a general network config error
    Err(format_err!("Remove network is not yet implemented"))
}

/// For now, instead of giving actual results simply give nothing.
async fn handle_client_request_get_networks(
    saved_networks: SavedNetworksPtr,
    iterator: fidl::endpoints::ServerEnd<fidl_policy::NetworkConfigIteratorMarker>,
) -> Result<(), fidl::Error> {
    // make sufficiently small batches of networks to send and convert configs to FIDL values
    let network_configs = saved_networks.get_networks();
    let chunks = network_configs.chunks(MAX_CONFIGS_PER_RESPONSE);
    let fidl_chunks = chunks.into_iter().map(|chunk| {
        chunk
            .iter()
            .map(fidl_policy::NetworkConfig::from)
            .collect::<Vec<fidl_policy::NetworkConfig>>()
    });
    let mut stream = iterator.into_stream()?;
    for chunk in fidl_chunks {
        send_next_chunk(&mut stream, chunk).await?;
    }
    send_next_chunk(&mut stream, vec![]).await
}

/// Send a chunk of saved networks to the specified FIDL iterator
async fn send_next_chunk(
    stream: &mut fidl_policy::NetworkConfigIteratorRequestStream,
    chunk: Vec<fidl_policy::NetworkConfig>,
) -> Result<(), fidl::Error> {
    if let Some(req) = stream.try_next().await? {
        let fidl_policy::NetworkConfigIteratorRequest::GetNext { responder } = req;
        responder.send(&mut chunk.into_iter())
    } else {
        // This will happen if the iterator request stream was closed and we expected to send
        // another response.
        // TODO(45113) Test this error path
        info!("Info: peer closed channel for network config results unexpectedly");
        Ok(())
    }
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
            config_management::{NetworkConfig, SavedNetworksManager, SecurityType, PSK_BYTE_LEN},
            util::logger::set_logger_for_test,
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
            .store(network_id_psk, Credential::Psk(vec![64; PSK_BYTE_LEN].to_vec()))
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
        let client = Arc::new(Mutex::new(Client::new_empty()));
        client.lock().set_sme(client_sme);
        (client, remote.into_stream().expect("failed to create stream"))
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
        set_logger_for_test();
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
                assert_eq!(fidl_sme::Credential::Psk([64; PSK_BYTE_LEN].to_vec()), req.credential);
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
    fn scan_request_sent_to_sme() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup("scan_request_sent_to_sme", &mut exec);
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
        let (iter, server) = fidl::endpoints::create_proxy().expect("failed to create iterator");
        controller.scan_for_networks(server).expect("Failed to call scan for networks");

        // Request a chunk of scan results. Progress until waiting on response from server side of
        // the iterator.
        let mut output_iter_fut = iter.get_next();
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut), Poll::Pending);
        // Progress sever side forward so that it will respond to the iterator get next request.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Check that a scan request was sent to the sme
        assert_variant!(
            exec.run_until_stalled(&mut test_values.sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Scan {
                req, ..
            }))) => {
                assert_eq!(5, req.timeout);
                assert_eq!(fidl_common::ScanType::Passive, req.scan_type);
            }
        );
    }

    #[test]
    fn scan_iterator_never_polled() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup("scan_iterator_never_polled", &mut exec);
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
        let (_iter_not_called, server) =
            fidl::endpoints::create_proxy().expect("failed to create iterator");
        controller.scan_for_networks(server).expect("Failed to call scan for networks");

        // Progress sever side forward without ever calling getNext() on the scan result iterator
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Check that a scan request was sent to the sme and send back some data
        assert_variant!(
            exec.run_until_stalled(&mut test_values.sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Scan {
                txn, ..
            }))) => {
                // Send scan response.
                let (_stream, ctrl) = txn
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl.send_on_result(&mut vec![].into_iter())
                    .expect("failed to send scan data");
            }
        );

        // Progress sever side forward without progressing the scan result iterator
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Issue a second request to scan, to make sure that everything is still
        // moving along even though the first scan result iterator was never progressed.
        let (_iter_not_called2, server2) =
            fidl::endpoints::create_proxy().expect("failed to create iterator");
        controller.scan_for_networks(server2).expect("Failed to call scan for networks");

        // Progress sever side forward without progressing the scan result iterator
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Check that a scan request was sent to the sme and send back some data
        assert_variant!(
            exec.run_until_stalled(&mut test_values.sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Scan {
                txn, ..
            }))) => {
                // Send scan response.
                let (_stream, ctrl) = txn
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl.send_on_result(&mut vec![].into_iter())
                    .expect("failed to send scan data");
            }
        );

        // Progress sever side forward without progressing the scan result iterator
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
    }

    #[test]
    fn scan_error() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup("scan_error", &mut exec);
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
        let (iter, server) = fidl::endpoints::create_proxy().expect("failed to create iterator");
        controller.scan_for_networks(server).expect("Failed to call scan for networks");

        // Request a chunk of scan results. Progress until waiting on response from server side of
        // the iterator.
        let mut output_iter_fut = iter.get_next();
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut), Poll::Pending);
        // Progress sever side forward so that it will respond to the iterator get next request.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Check that a scan request was sent to the sme and send back an error
        assert_variant!(
            exec.run_until_stalled(&mut test_values.sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Scan {
                txn, ..
            }))) => {
                // Send failed scan response.
                let (_stream, ctrl) = txn
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl.send_on_error(&mut fidl_sme::ScanError {
                    code: fidl_sme::ScanErrorCode::InternalError,
                    message: "Failed to scan".to_string()
                })
                    .expect("failed to send scan error");
            }
        );

        // Process SME result.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Verify status was not updated.
        assert_variant!(
            exec.run_until_stalled(&mut test_values.listener_updates.next()),
            Poll::Pending
        );

        // the iterator should have given an error
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut), Poll::Ready(result) => {
            let results = result.expect("Failed to get next scan results");
            assert_eq!(results, Err(fidl_policy::ScanErrorCode::GeneralError));
        });
    }

    // Creates test data for the scan functions.
    fn create_scan_ap_data() -> (Vec<fidl_sme::BssInfo>, Vec<fidl_policy::ScanResult>) {
        let input_aps = vec![
            fidl_sme::BssInfo {
                bssid: [0, 0, 0, 0, 0, 0],
                ssid: "duplicated ssid".as_bytes().to_vec(),
                rx_dbm: 0,
                snr_db: 0,
                channel: 0,
                protection: fidl_sme::Protection::Wpa3Enterprise,
                compatible: true,
            },
            fidl_sme::BssInfo {
                bssid: [1, 2, 3, 4, 5, 6],
                ssid: "unique ssid".as_bytes().to_vec(),
                rx_dbm: 7,
                snr_db: 0,
                channel: 8,
                protection: fidl_sme::Protection::Wpa2Personal,
                compatible: true,
            },
            fidl_sme::BssInfo {
                bssid: [7, 8, 9, 10, 11, 12],
                ssid: "duplicated ssid".as_bytes().to_vec(),
                rx_dbm: 13,
                snr_db: 0,
                channel: 14,
                protection: fidl_sme::Protection::Wpa3Enterprise,
                compatible: false,
            },
        ];
        // input_aps contains some duplicate SSIDs, which should be
        // grouped in the output.
        let output_aps = vec![
            fidl_policy::ScanResult {
                id: Some(fidl_policy::NetworkIdentifier {
                    ssid: "duplicated ssid".as_bytes().to_vec(),
                    type_: fidl_policy::SecurityType::Wpa3,
                }),
                entries: Some(vec![
                    fidl_policy::Bss {
                        bssid: Some([0, 0, 0, 0, 0, 0]),
                        rssi: Some(0),
                        frequency: Some(0),
                        timestamp_nanos: Some(0),
                    },
                    fidl_policy::Bss {
                        bssid: Some([7, 8, 9, 10, 11, 12]),
                        rssi: Some(13),
                        frequency: Some(0),
                        timestamp_nanos: Some(0),
                    },
                ]),
                compatibility: Some(fidl_policy::Compatibility::Supported),
            },
            fidl_policy::ScanResult {
                id: Some(fidl_policy::NetworkIdentifier {
                    ssid: "unique ssid".as_bytes().to_vec(),
                    type_: fidl_policy::SecurityType::Wpa2,
                }),
                entries: Some(vec![fidl_policy::Bss {
                    bssid: Some([1, 2, 3, 4, 5, 6]),
                    rssi: Some(7),
                    frequency: Some(0),
                    timestamp_nanos: Some(0),
                }]),
                compatibility: Some(fidl_policy::Compatibility::Supported),
            },
        ];
        (input_aps, output_aps)
    }

    fn send_sme_scan_result(
        exec: &mut fasync::Executor,
        sme_stream: &mut fidl_sme::ClientSmeRequestStream,
        scan_results: &[fidl_sme::BssInfo],
    ) {
        // Check that the second scan request was sent to the sme and send back results
        assert_variant!(
            exec.run_until_stalled(&mut sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Scan {
                txn, ..
            }))) => {
                // Send all the APs
                let (_stream, ctrl) = txn
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl.send_on_result(&mut scan_results.to_vec().iter_mut())
                    .expect("failed to send scan data");

                // Send the end of data
                ctrl.send_on_finished()
                    .expect("failed to send scan data");
            }
        );
    }

    #[test]
    fn scan_success() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup("scan_success", &mut exec);
        let serve_fut = serve_provider_requests(
            test_values.client,
            test_values.update_sender,
            Arc::clone(&test_values.saved_networks),
            test_values.requests,
        );
        pin_mut!(serve_fut);
        let (input_aps, output_aps) = create_scan_ap_data();

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _update_stream) = request_controller(&test_values.provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut test_values.listener_updates.next()),
            Poll::Ready(_)
        );

        // Create a few sets of endpoints.
        // This set of endpoints will be used to make the initial scan request.
        let (iter0, server0) =
            fidl::endpoints::create_proxy::<fidl_policy::ScanResultIteratorMarker>()
                .expect("failed to create iterator");
        let mut output_iter_fut0 = iter0.get_next();
        // Create a few sets of endpoints.
        // This set of endpoints will be used simultaneously with the first one.
        let (iter1, server1) =
            fidl::endpoints::create_proxy::<fidl_policy::ScanResultIteratorMarker>()
                .expect("failed to create iterator");
        let mut output_iter_fut1 = iter1.get_next();
        // This set of endpoints will be used to make a scan request while the first
        // request is still pending.
        let (iter2, server2) =
            fidl::endpoints::create_proxy::<fidl_policy::ScanResultIteratorMarker>()
                .expect("failed to create iterator");
        let mut output_iter_fut2 = iter2.get_next();
        // This set of endpoints will be used to make a scan request after the
        // first request is complete.
        let (iter3, server3) =
            fidl::endpoints::create_proxy::<fidl_policy::ScanResultIteratorMarker>()
                .expect("failed to create iterator");
        let mut output_iter_fut3 = iter3.get_next();

        // Issue request to scan on the first two iterators.
        controller.scan_for_networks(server0).expect("Failed to call scan for networks");
        controller.scan_for_networks(server1).expect("Failed to call scan for networks");

        // Request a chunk of scan results. Progress until waiting on response from server side of
        // the iterator.
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut0), Poll::Pending);
        // Progress sever side forward so that it will respond to the iterator get next request.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Check that a scan request was sent to the sme and send back results
        assert_variant!(
            exec.run_until_stalled(&mut test_values.sme_stream.next()),
            Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Scan {
                txn, ..
            }))) => {
                // Send the first AP
                let (_stream, ctrl) = txn
                    .into_stream_and_control_handle().expect("error accessing control handle");
                let mut aps = [input_aps[0].clone()];
                ctrl.send_on_result(&mut aps.iter_mut())
                    .expect("failed to send scan data");

                // Process SME result.
                assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

                // The iterator should not have any data yet, until the sme is done
                assert_variant!(exec.run_until_stalled(&mut output_iter_fut0), Poll::Pending);
                assert_variant!(exec.run_until_stalled(&mut output_iter_fut1), Poll::Pending);

                // Make a request on the third iterator.
                controller.scan_for_networks(server2).expect("Failed to call scan for networks");
                assert_variant!(exec.run_until_stalled(&mut output_iter_fut2), Poll::Pending);

                // Send the remaining APs
                let mut aps = input_aps[1..].to_vec();
                ctrl.send_on_result(&mut aps.iter_mut())
                    .expect("failed to send scan data");

                // Process SME result.
                assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

                // The iterator should not have any data yet, until the sme is done
                assert_variant!(exec.run_until_stalled(&mut output_iter_fut0), Poll::Pending);
                assert_variant!(exec.run_until_stalled(&mut output_iter_fut1), Poll::Pending);

                // Send the end of data
                ctrl.send_on_finished()
                    .expect("failed to send scan data");
            }
        );

        // Check that the second scan request was sent to the sme and send back results
        send_sme_scan_result(&mut exec, &mut test_values.sme_stream, &input_aps); // for output_iter_fut1
        send_sme_scan_result(&mut exec, &mut test_values.sme_stream, &input_aps); // for output_iter_fut2

        // Process SME result.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Now, we should have data in the iterator. There should be an item for
        // each pair of ssid+security.
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut0), Poll::Ready(result) => {
            let results = result.expect("Failed to get next scan results").unwrap();
            assert_eq!(results.len(), output_aps.len());
            assert_eq!(results, output_aps);
        });

        // Now, we should also have the same data in the iterator that was called while
        // the first scan was processing in the SME.
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut2), Poll::Ready(result) => {
            let results = result.expect("Failed to get next scan results").unwrap();
            assert_eq!(results.len(), output_aps.len());
            assert_eq!(results, output_aps);
        });

        // Calling either of those two iterators again should return an empty vec
        let mut output_iter_fut0 = iter0.get_next();
        // Progress sever side forward so that it will respond to the iterator get next request.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut0), Poll::Ready(result) => {
            let results = result.expect("Failed to get next scan results").unwrap();
            assert_eq!(results.len(), 0);
        });

        // The iterator that was called simultaneously with the first one should also have the full results.
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut1), Poll::Ready(result) => {
            let results = result.expect("Failed to get next scan results").unwrap();
            assert_eq!(results.len(), output_aps.len());
            assert_eq!(results, output_aps);
        });

        // Start a new transaction using the third iterator.
        controller.scan_for_networks(server3).expect("Failed to call scan for networks");
        // Progress sever side forward so that it will respond to the iterator get next request.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        // Check that a scan request was sent to the sme and send back results for output_iter_fut3
        send_sme_scan_result(&mut exec, &mut test_values.sme_stream, &input_aps);
        // Process SME result.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        // Now, we should have data in the final iterator. There should be an item for
        // each pair of ssid+security.
        assert_variant!(exec.run_until_stalled(&mut output_iter_fut3), Poll::Ready(result) => {
            let results = result.expect("Failed to get next scan results").unwrap();
            assert_eq!(results.len(), output_aps.len());
            assert_eq!(results, output_aps);
        });
    }

    #[test]
    fn save_network() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let stash_id = "save_network";
        let temp_dir = TempDir::new().expect("failed to create temp dir");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks_fut =
            SavedNetworksManager::new_with_stash_or_paths(stash_id, path, tmp_path);
        pin_mut!(saved_networks_fut);
        let saved_networks = Arc::new(
            exec.run_singlethreaded(saved_networks_fut).expect("Failed to create a KnownEssStore"),
        );

        let (provider, requests) = create_proxy::<fidl_policy::ClientProviderMarker>()
            .expect("failed to create ClientProvider proxy");
        let requests = requests.into_stream().expect("failed to create stream");

        let (client, _sme_stream) = create_client();
        let (update_sender, mut listener_updates) = mpsc::unbounded();
        let serve_fut =
            serve_provider_requests(client, update_sender, Arc::clone(&saved_networks), requests);
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _update_stream) = request_controller(&provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut listener_updates.next()), Poll::Ready(_));

        // Save some network
        let network_id = fidl_policy::NetworkIdentifier {
            ssid: b"foo".to_vec(),
            type_: fidl_policy::SecurityType::None,
        };
        let network_config = fidl_policy::NetworkConfig {
            id: Some(network_id.clone()),
            credential: Some(fidl_policy::Credential::None(fidl_policy::Empty)),
        };
        let mut save_fut = controller.save_network(network_config);

        // Run server_provider forward so that it will process the save network request
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Check that the the response says we succeeded.
        assert_variant!(exec.run_until_stalled(&mut save_fut), Poll::Ready(result) => {
            let save_result = result.expect("Failed to get save network response");
            assert_eq!(save_result, Ok(()));
        });

        // Check that the value was actually saved in the saved networks manager.
        let target_id = NetworkIdentifier::from(network_id);
        let target_config = NetworkConfig::new(target_id.clone(), Credential::None, false, false)
            .expect("Failed to create network config");
        assert_eq!(saved_networks.lookup(target_id), vec![target_config]);
    }

    #[test]
    fn save_bad_network_should_fail() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let stash_id = "save_bad_network_should_fail";
        let temp_dir = TempDir::new().expect("failed to create temp dir");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");

        // Need to create this here so that the temp files will be in scope here.
        let saved_networks_fut =
            SavedNetworksManager::new_with_stash_or_paths(stash_id, path, tmp_path);
        pin_mut!(saved_networks_fut);
        let _saved_networks = Arc::new(
            exec.run_singlethreaded(&mut saved_networks_fut)
                .expect("Failed to create a KnownEssStore"),
        );
        let saved_networks = exec.run_singlethreaded(create_network_store(stash_id));

        let (provider, requests) = create_proxy::<fidl_policy::ClientProviderMarker>()
            .expect("failed to create ClientProvider proxy");
        let requests = requests.into_stream().expect("failed to create stream");

        let (client, _sme_stream) = create_client();
        let (update_sender, mut listener_updates) = mpsc::unbounded();
        let serve_fut =
            serve_provider_requests(client, update_sender, Arc::clone(&saved_networks), requests);
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Request a new controller.
        let (controller, _update_stream) = request_controller(&provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut listener_updates.next()), Poll::Ready(_));

        // Create a network config whose password is too short. FIDL network config does not
        // require valid fields unlike our crate define config. We should not be able to
        // successfully save this network through the API.
        let bad_network_id = fidl_policy::NetworkIdentifier {
            ssid: b"foo".to_vec(),
            type_: fidl_policy::SecurityType::Wpa2,
        };
        let network_config = fidl_policy::NetworkConfig {
            id: Some(bad_network_id.clone()),
            credential: Some(fidl_policy::Credential::Password(b"bar".to_vec())),
        };
        // Attempt to save the config
        let mut save_fut = controller.save_network(network_config);

        // Run server_provider forward so that it will process the save network request
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Check that the the response says we failed to save the network.
        assert_variant!(exec.run_until_stalled(&mut save_fut), Poll::Ready(result) => {
            let error = result.expect("Failed to get save network response");
            assert_eq!(error, Err(SaveError::GeneralError));
        });

        // Check that the value was was not saved in saved networks manager.
        let target_id = NetworkIdentifier::from(bad_network_id);
        assert_eq!(saved_networks.lookup(target_id), vec![]);
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
            assert_eq!(error, Err(SaveError::GeneralError));
        });
    }

    #[test]
    fn get_saved_networks_empty() {
        let saved_networks = vec![];
        let expected_configs = vec![];
        let expected_num_sends = 0;
        test_get_saved_networks(
            "get_saved_networks_empty",
            saved_networks,
            expected_configs,
            expected_num_sends,
        );
    }

    #[test]
    fn get_saved_network() {
        // save a network
        let network_id = NetworkIdentifier::new(b"foobar".to_vec(), SecurityType::Wpa2);
        let credential = Credential::Password(b"password".to_vec());
        let saved_networks = vec![(network_id.clone(), credential.clone())];

        let expected_id = network_id.into();
        let expected_credential = credential.into();
        let expected_configs = vec![fidl_policy::NetworkConfig {
            id: Some(expected_id),
            credential: Some(expected_credential),
        }];

        let expected_num_sends = 1;
        test_get_saved_networks(
            "get_saved_network",
            saved_networks,
            expected_configs,
            expected_num_sends,
        );
    }

    #[test]
    fn get_saved_networks_multiple_chunks() {
        // Save MAX_CONFIGS_PER_RESPONSE + 1 configs so that get_saved_networks should respond with
        // 2 chunks of responses plus one response with an empty vector.
        let mut saved_networks = vec![];
        let mut expected_configs = vec![];
        for index in 0..MAX_CONFIGS_PER_RESPONSE + 1 {
            // Create unique network config to be saved.
            let ssid = format!("some_config{}", index).into_bytes();
            let net_id = NetworkIdentifier::new(ssid.clone(), SecurityType::None);
            saved_networks.push((net_id, Credential::None));

            // Create corresponding FIDL value and add to list of expected configs/
            let ssid = format!("some_config{}", index).into_bytes();
            let net_id = fidl_policy::NetworkIdentifier {
                ssid: ssid,
                type_: fidl_policy::SecurityType::None,
            };
            let credential = fidl_policy::Credential::None(fidl_policy::Empty);
            let network_config =
                fidl_policy::NetworkConfig { id: Some(net_id), credential: Some(credential) };
            expected_configs.push(network_config);
        }

        let expected_num_sends = 2;
        test_get_saved_networks(
            "get_saved_networks_multiple_chunks",
            saved_networks,
            expected_configs,
            expected_num_sends,
        );
    }

    /// Test that get saved networks with the given saved networks
    /// test_id: the name of the test to create a unique persistent store for each test
    /// saved_configs: list of NetworkIdentifier and Credential pairs that are to be stored to the
    ///     SavedNetworksManager in the test.
    /// expected_configs: list of FIDL NetworkConfigs that we expect to get from get_saved_networks
    /// expected_num_sends: number of chunks of results we expect to get from get_saved_networks.
    ///     This is not counting the empty vector that signifies no more results.
    fn test_get_saved_networks(
        test_id: impl AsRef<str>,
        saved_configs: Vec<(NetworkIdentifier, Credential)>,
        expected_configs: Vec<fidl_policy::NetworkConfig>,
        expected_num_sends: usize,
    ) {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = TempDir::new().expect("failed to create temp dir");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks = Arc::new(
            exec.run_singlethreaded(SavedNetworksManager::new_with_stash_or_paths(
                test_id, path, tmp_path,
            ))
            .expect("Failed to create a KnownEssStore"),
        );

        let (provider, requests) = create_proxy::<fidl_policy::ClientProviderMarker>()
            .expect("failed to create ClientProvider proxy");
        let requests = requests.into_stream().expect("failed to create stream");

        let (client, _sme_stream) = create_client();
        let (update_sender, _listener_updates) = mpsc::unbounded();

        let serve_fut =
            serve_provider_requests(client, update_sender, Arc::clone(&saved_networks), requests);
        pin_mut!(serve_fut);

        // No request has been sent yet. Future should be idle.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Save the networks specified for this test.
        for (net_id, credential) in saved_configs {
            saved_networks.store(net_id, credential).expect("failed to store network");
        }

        // Request a new controller.
        let (controller, _update_stream) = request_controller(&provider);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Issue request to get the list of saved networks.
        let (iter, server) =
            fidl::endpoints::create_proxy::<fidl_policy::NetworkConfigIteratorMarker>()
                .expect("failed to create iterator");
        controller.get_saved_networks(server).expect("Failed to call scan for networks");

        // Get responses from iterator. Expect to see the specified number of responses with
        // results plus one response of an empty vector indicating the end of results.
        let mut saved_networks_results = vec![];
        for i in 0..expected_num_sends {
            let mut get_saved_fut = iter.get_next();
            assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
            let results = exec
                .run_singlethreaded(&mut get_saved_fut)
                .expect("Failed to get next chunk of saved networks results");
            // the size of received chunk should either be max chunk size or whatever is left
            // to receive in the last chunk
            if i < expected_num_sends - 1 {
                assert_eq!(results.len(), MAX_CONFIGS_PER_RESPONSE);
            } else {
                assert_eq!(results.len(), expected_configs.len() % MAX_CONFIGS_PER_RESPONSE);
            }
            saved_networks_results.extend(results);
        }
        let mut get_saved_end_fut = iter.get_next();
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        let results = exec
            .run_singlethreaded(&mut get_saved_end_fut)
            .expect("Failed to get next chunk of saved networks results");
        assert!(results.is_empty());

        // check whether each network we saved is in the results and that nothing else is there.
        for network_config in &expected_configs {
            assert!(saved_networks_results.contains(&network_config));
        }
        assert_eq!(expected_configs.len(), saved_networks_results.len());
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
