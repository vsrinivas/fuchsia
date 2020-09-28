// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        client::{sme_credential_from_policy, types},
        config_management::{Credential, SavedNetworksManager},
        util::{
            listener::{
                ClientListenerMessageSender, ClientNetworkState, ClientStateUpdate,
                Message::NotifyListeners,
            },
            state_machine::{self, ExitReason, IntoStateExt},
        },
    },
    anyhow::format_err,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_wlan_sme as fidl_sme, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{
        channel::{mpsc, oneshot},
        future::FutureExt,
        select,
        stream::{self, FuturesUnordered, StreamExt, TryStreamExt},
    },
    log::{debug, error, info},
    pin_utils::pin_mut,
    std::sync::Arc,
    void::ResultVoidErrExt,
    wlan_common::RadioConfig,
};

// TODO(fxbug.dev/53513): add Cobalt metrics

const SME_STATUS_INTERVAL_SEC: i64 = 1; // this poll is very cheap, so we can do it frequently
const MAX_CONNECTION_ATTEMPTS: u8 = 4; // arbitrarily chosen until we have some data

type State = state_machine::State<ExitReason>;
type ReqStream = stream::Fuse<mpsc::Receiver<ManualRequest>>;

pub trait ClientApi {
    fn connect(
        &mut self,
        request: ConnectRequest,
        responder: oneshot::Sender<()>,
    ) -> Result<(), anyhow::Error>;
    fn disconnect(&mut self, responder: oneshot::Sender<()>) -> Result<(), anyhow::Error>;
    fn exit(&mut self, responder: oneshot::Sender<()>) -> Result<(), anyhow::Error>;
}

pub struct Client {
    req_sender: mpsc::Sender<ManualRequest>,
}

impl Client {
    pub fn new(req_sender: mpsc::Sender<ManualRequest>) -> Self {
        Self { req_sender }
    }
}

impl ClientApi for Client {
    fn connect(
        &mut self,
        request: ConnectRequest,
        responder: oneshot::Sender<()>,
    ) -> Result<(), anyhow::Error> {
        self.req_sender
            .try_send(ManualRequest::Connect((request, responder)))
            .map_err(|e| format_err!("failed to send connect request: {:?}", e))
    }

    fn disconnect(&mut self, responder: oneshot::Sender<()>) -> Result<(), anyhow::Error> {
        self.req_sender
            .try_send(ManualRequest::Disconnect(responder))
            .map_err(|e| format_err!("failed to send disconnect request: {:?}", e))
    }

    fn exit(&mut self, responder: oneshot::Sender<()>) -> Result<(), anyhow::Error> {
        self.req_sender
            .try_send(ManualRequest::Exit(responder))
            .map_err(|e| format_err!("failed to send exit request: {:?}", e))
    }
}

pub enum ManualRequest {
    Connect((ConnectRequest, oneshot::Sender<()>)),
    Disconnect(oneshot::Sender<()>),
    Exit(oneshot::Sender<()>),
}
#[derive(PartialEq, Debug)]
pub enum ClientStateMachineNotification {
    Idle { iface_id: u16 },
}

fn send_listener_state_update(
    sender: &ClientListenerMessageSender,
    network_update: ClientNetworkState,
) {
    let updates = ClientStateUpdate { state: None, networks: [network_update].to_vec() };
    match sender.clone().unbounded_send(NotifyListeners(updates)) {
        Ok(_) => (),
        Err(e) => error!("failed to send state update: {:?}", e),
    };
}

#[derive(Clone, Debug, PartialEq)]
pub struct ConnectRequest {
    pub network: types::NetworkIdentifier,
    pub credential: Credential,
}

pub async fn serve(
    iface_id: u16,
    proxy: fidl_sme::ClientSmeProxy,
    sme_event_stream: fidl_sme::ClientSmeEventStream,
    req_stream: mpsc::Receiver<ManualRequest>,
    update_sender: ClientListenerMessageSender,
    iface_manager_sender: mpsc::Sender<ClientStateMachineNotification>,
    saved_networks_manager: Arc<SavedNetworksManager>,
) {
    let disconnect_options = DisconnectingOptions {
        disconnect_responder: None,
        previous_network: None,
        next_network: None,
    };
    let common_options = CommonStateOptions {
        proxy: proxy,
        req_stream: req_stream.fuse(),
        update_sender: update_sender,
        iface_id: iface_id,
        iface_manager_sender: iface_manager_sender,
        saved_networks_manager: saved_networks_manager,
    };
    let state_machine =
        disconnecting_state(common_options, disconnect_options).into_state_machine();
    let removal_watcher = sme_event_stream.map_ok(|_| ()).try_collect::<()>();
    select! {
        state_machine = state_machine.fuse() => {
            match state_machine.void_unwrap_err() {
                ExitReason(Err(e)) => info!("Client state machine for iface #{} terminated with an error: {:?}",
                    iface_id, e),
                ExitReason(Ok(_)) => info!("Client state machine for iface #{} exited gracefully",
                    iface_id,),
            }
        }
        removal_watcher = removal_watcher.fuse() => if let Err(e) = removal_watcher {
            info!("Error reading from Client SME channel of iface #{}: {:?}",
                iface_id, e);
        },
    }
}

/// Common parameters passed to all states
struct CommonStateOptions {
    proxy: fidl_sme::ClientSmeProxy,
    req_stream: ReqStream,
    update_sender: ClientListenerMessageSender,
    iface_id: u16,
    iface_manager_sender: mpsc::Sender<ClientStateMachineNotification>,
    saved_networks_manager: Arc<SavedNetworksManager>,
}

async fn handle_exit_or_none_request(req: Option<ManualRequest>) -> Result<State, ExitReason> {
    match req {
        Some(ManualRequest::Exit(responder)) => {
            responder.send(()).unwrap_or_else(|_| ());
            Err(ExitReason(Ok(())))
        }
        None => {
            return Err(ExitReason(Err(format_err!(
                "The stream of user requests ended unexpectedly"
            ))))
        }
        Some(ManualRequest::Disconnect(_)) => {
            return Err(ExitReason(Err(format_err!(
                "Unexpected disconnect request passed in to exit handler"
            ))))
        }
        Some(ManualRequest::Connect(_)) => {
            return Err(ExitReason(Err(format_err!(
                "Unexpected connect request passed in to exit handler"
            ))))
        }
    }
}

// These functions were introduced to resolve the following error:
// ```
// error[E0391]: cycle detected when evaluating trait selection obligation
// `impl core::future::future::Future: std::marker::Send`
// ```
// which occurs when two functions that return an `impl Trait` call each other
// in a cycle. (e.g. this case `connecting_state` calling `disconnecting_state`,
// which calls `connecting_state`)
fn to_disconnecting_state(
    common_options: CommonStateOptions,
    disconnecting_options: DisconnectingOptions,
) -> State {
    disconnecting_state(common_options, disconnecting_options).into_state()
}
fn to_connecting_state(
    common_options: CommonStateOptions,
    connecting_options: ConnectingOptions,
) -> State {
    connecting_state(common_options, connecting_options).into_state()
}

struct DisconnectingOptions {
    disconnect_responder: Option<oneshot::Sender<()>>,
    /// Information about the previously connected network, if there was one. Used to send out
    /// listener updates.
    previous_network: Option<(types::NetworkIdentifier, types::DisconnectStatus)>,
    /// Configuration for the next network to connect to, after the disconnect is complete. If not
    /// present, the state machine will proceed to IDLE.
    next_network: Option<ConnectingOptions>,
}
/// The DISCONNECTING state requests an SME disconnect, then transitions to either:
/// - the CONNECTING state if options.next_network is present
/// - the IDLE state otherwise
async fn disconnecting_state(
    common_options: CommonStateOptions,
    options: DisconnectingOptions,
) -> Result<State, ExitReason> {
    debug!("Entering disconnecting state");

    // TODO(fxbug.dev/53505): either make this fire-and-forget in the SME, or spawn a thread for this,
    // so we don't block on it
    common_options.proxy.disconnect().await.map_err(|e| {
        ExitReason(Err(format_err!("Failed to send command to wlanstack: {:?}", e)))
    })?;

    // Notify the caller that disconnect was sent to the SME
    match options.disconnect_responder {
        Some(responder) => responder.send(()).unwrap_or_else(|_| ()),
        None => (),
    }
    // Notify listeners of disconnection
    match options.previous_network {
        Some((network_identifier, status)) => send_listener_state_update(
            &common_options.update_sender,
            ClientNetworkState {
                id: network_identifier,
                state: types::ConnectionState::Disconnected,
                status: Some(status),
            },
        ),
        None => (),
    }

    // Transition to next state
    match options.next_network {
        Some(next_network) => Ok(to_connecting_state(common_options, next_network)),
        None => Ok(idle_state(common_options).into_state()),
    }
}

/// The IDLE state notifies the iface manager that we are idle, then awaits
/// further ManualRequests.
async fn idle_state(mut common_options: CommonStateOptions) -> Result<State, ExitReason> {
    debug!("Entering idle state");

    // Inform the iface manager that we are idle
    common_options
        .iface_manager_sender
        .try_send(ClientStateMachineNotification::Idle { iface_id: common_options.iface_id })
        .map_err(|e| {
            ExitReason(Err(format_err!("Failed to send notice to ifaceManager: {:?}", e)))
        })?;

    // Wait for the next request from the caller
    loop {
        match common_options.req_stream.next().await {
            // Immediately reply to disconnect requests indicating that the client is already stopped
            Some(ManualRequest::Disconnect(responder)) => {
                responder.send(()).unwrap_or_else(|_| ());
            }
            Some(ManualRequest::Connect((connect_request, responder))) => {
                let connecting_options = ConnectingOptions {
                    connect_responder: Some(responder),
                    connect_request: connect_request,
                    attempt_counter: 0,
                };
                return Ok(to_connecting_state(common_options, connecting_options));
            }
            exit_or_none => return handle_exit_or_none_request(exit_or_none).await,
        }
    }
}

async fn wait_until_connected(
    txn: fidl_sme::ConnectTransactionProxy,
) -> Result<fidl_sme::ConnectResultCode, anyhow::Error> {
    let mut stream = txn.take_event_stream();
    while let Some(event) = stream.try_next().await? {
        match event {
            fidl_sme::ConnectTransactionEvent::OnFinished { code } => return Ok(code),
        }
    }
    Err(format_err!("Server closed the ConnectTransaction channel before sending a response"))
}

struct ConnectingOptions {
    connect_responder: Option<oneshot::Sender<()>>,
    connect_request: ConnectRequest,
    /// Count of previous consecutive failed connection attempts to this same network.
    attempt_counter: u8,
}

/// The CONNECTING state requests an SME connect. It handles the SME connect response:
/// - for a successful connection, transition to CONNECTED state
/// - for a failed connection, retry connection by passing a next_network to the
///       DISCONNECTING state, as long as there haven't been too many connection attempts
/// During this time, incoming ManualRequests are also monitored for:
/// - duplicate connect requests are deduped
/// - different connect requests are serviced by passing a next_network to the DISCONNECTING state
/// - disconnect requests cause a transition to DISCONNECTING state
async fn connecting_state(
    mut common_options: CommonStateOptions,
    options: ConnectingOptions,
) -> Result<State, ExitReason> {
    debug!("Entering connecting state");
    if options.attempt_counter > 0 {
        info!(
            "Retrying connection, {} attempts remaining",
            MAX_CONNECTION_ATTEMPTS - options.attempt_counter
        );
    }

    // Send a connect request to the SME
    let (connect_txn, remote) = create_proxy()
        .map_err(|e| ExitReason(Err(format_err!("Failed to create proxy: {:?}", e))))?;
    let mut sme_connect_request = fidl_sme::ConnectRequest {
        ssid: options.connect_request.network.ssid.clone(),
        credential: sme_credential_from_policy(&options.connect_request.credential),
        radio_cfg: RadioConfig { phy: None, cbw: None, primary_chan: None }.to_fidl(),
        deprecated_scan_type: fidl_fuchsia_wlan_common::ScanType::Active,
    };
    common_options.proxy.connect(&mut sme_connect_request, Some(remote)).map_err(|e| {
        ExitReason(Err(format_err!("Failed to send command to wlanstack: {:?}", e)))
    })?;
    let pending_connect_request = wait_until_connected(connect_txn).fuse();
    pin_mut!(pending_connect_request);

    // Send a "Connecting" update to listeners, unless this is a retry
    if options.attempt_counter == 0 {
        send_listener_state_update(
            &common_options.update_sender,
            ClientNetworkState {
                id: options.connect_request.network.clone(),
                state: types::ConnectionState::Connecting,
                status: None,
            },
        );
    };

    // Let the responder know we've successfully started this connection attempt
    match options.connect_responder {
        Some(responder) => responder.send(()).unwrap_or_else(|_| ()),
        None => {}
    }

    loop {
        select! {
            // Monitor the SME connection attempt
            connected = pending_connect_request => {
                let code = connected.map_err({
                    |e| ExitReason(Err(format_err!("failed to send connect to sme: {:?}", e)))
                })?;
                // Notify the saved networks manager
                common_options.saved_networks_manager.record_connect_result(
                    options.connect_request.network.clone().into(),
                    &options.connect_request.credential,
                    code
                ).await;
                match code {
                    fidl_sme::ConnectResultCode::Success => {
                        info!("Successfully connected to network");
                        send_listener_state_update(
                            &common_options.update_sender,
                            ClientNetworkState {
                                id: options.connect_request.network.clone(),
                                state: types::ConnectionState::Connected,
                                status: None
                            },
                        );
                        return Ok(
                            connected_state(common_options, options.connect_request).into_state()
                        );
                    },
                    fidl_sme::ConnectResultCode::CredentialRejected | fidl_sme::ConnectResultCode::WrongCredentialType => {
                        info!("Failed to connect. Will not retry because of credential error: {:?}", code);
                        send_listener_state_update(
                            &common_options.update_sender,
                            ClientNetworkState {
                                id: options.connect_request.network,
                                state: types::ConnectionState::Failed,
                                status: Some(types::DisconnectStatus::CredentialsFailed),
                            },
                        );
                        return Ok(idle_state(common_options).into_state());
                    },
                    other => {
                        info!("Failed to connect: {:?}", other);
                        // Check if the limit for connection attempts to this network has been
                        // exceeded.
                        let new_attempt_count = options.attempt_counter + 1;
                        if new_attempt_count >= MAX_CONNECTION_ATTEMPTS {
                            info!("Exceeded maximum connection attempts, will not retry");
                            send_listener_state_update(
                                &common_options.update_sender,
                                ClientNetworkState {
                                    id: options.connect_request.network,
                                    state: types::ConnectionState::Failed,
                                    status: Some(types::DisconnectStatus::ConnectionFailed)
                                },
                            );
                            return Ok(idle_state(common_options).into_state());
                        } else {
                            // Limit not exceeded, retry.
                            let next_connecting_options = ConnectingOptions {
                                connect_responder: None,
                                connect_request: options.connect_request,
                                attempt_counter: new_attempt_count,
                            };
                            let disconnecting_options = DisconnectingOptions {
                                disconnect_responder: None,
                                previous_network: None,
                                next_network: Some(next_connecting_options)
                            };
                            return Ok(to_disconnecting_state(common_options, disconnecting_options));
                        }
                    }
                };
            },
            // Monitor incoming ManualRequests
            new_req = common_options.req_stream.next() => {
                match new_req {
                    Some(ManualRequest::Disconnect(responder)) => {
                        info!("Cancelling pending connect due to disconnect request");
                        send_listener_state_update(
                            &common_options.update_sender,
                            ClientNetworkState {
                                id: options.connect_request.network,
                                state: types::ConnectionState::Disconnected,
                                status: Some(types::DisconnectStatus::ConnectionStopped)
                            },
                        );
                        let options = DisconnectingOptions {
                            disconnect_responder: Some(responder),
                            previous_network: None,
                            next_network: None,
                        };
                        return Ok(to_disconnecting_state(common_options, options));
                    }
                    Some(ManualRequest::Connect((new_connect_request, new_responder))) => {
                        // Check if it's the same network as we're currently connected to.
                        // If yes, dedupe the request by adding the responder to our list of
                        // responders.
                        if (new_connect_request.network == options.connect_request.network) {
                            info!("Received duplicate connection request, deduping");
                            new_responder.send(()).unwrap_or_else(|_| ());
                        } else {
                            info!("Cancelling pending connect due to new connection request");
                            send_listener_state_update(
                                &common_options.update_sender,
                                ClientNetworkState {
                                    id: options.connect_request.network,
                                    state: types::ConnectionState::Disconnected,
                                    status: Some(types::DisconnectStatus::ConnectionStopped)
                                },
                            );
                            let next_connecting_options = ConnectingOptions {
                                connect_responder: Some(new_responder),
                                connect_request: new_connect_request,
                                attempt_counter: 0,
                            };
                            let disconnecting_options = DisconnectingOptions {
                                disconnect_responder: None,
                                previous_network: None,
                                next_network: Some(next_connecting_options),
                            };
                            return Ok(to_disconnecting_state(common_options, disconnecting_options));
                        }
                    }
                    exit_or_none => return handle_exit_or_none_request(exit_or_none).await,
                };
            },
        }
    }
}

/// The CONNECTED state monitors the SME status. It handles the SME status response:
/// - if still connected to the correct network, no action
/// - if disconnected, retry connection by passing a next_network to the
///       DISCONNECTING state
/// During this time, incoming ManualRequests are also monitored for:
/// - duplicate connect requests are deduped
/// - different connect requests are serviced by passing a next_network to the DISCONNECTING state
/// - disconnect requests cause a transition to DISCONNECTING state
async fn connected_state(
    mut common_options: CommonStateOptions,
    current_network: ConnectRequest,
) -> Result<State, ExitReason> {
    debug!("Entering connected state");

    // TODO(fxbug.dev/57237): replace this poll with a notification from wlanstack in the ConnectTxn
    // Holds a pending SME status request.  Request status immediately upon entering the started state.
    let mut pending_status_req = FuturesUnordered::new();
    pending_status_req.push(common_options.proxy.status());

    let mut status_timer =
        fasync::Interval::new(zx::Duration::from_seconds(SME_STATUS_INTERVAL_SEC));

    loop {
        select! {
            status_response = pending_status_req.select_next_some() => {
                let status_response = status_response.map_err(|e| {
                    ExitReason(Err(format_err!("failed to get sme status: {:?}", e)))
                })?;
                match status_response.connected_to {
                    Some(bss_info) => {
                        // TODO(fxbug.dev/53545): send some stats to the saved network manager
                        if (bss_info.ssid != current_network.network.ssid) {
                            error!("Currently connected SSID changed unexpectedly");
                            return Err(ExitReason(Err(format_err!("Currently connected SSID changed unexpectedly"))));
                        }
                    }
                    None => {
                        let next_connecting_options = ConnectingOptions {
                            connect_responder: None,
                            connect_request: current_network.clone(),
                            attempt_counter: 0,
                        };
                        let options = DisconnectingOptions {
                            disconnect_responder: None,
                            previous_network: Some((current_network.clone().network, types::DisconnectStatus::ConnectionFailed)),
                            next_network: Some(next_connecting_options)
                        };
                        info!("Detected disconnection from network");
                        return Ok(disconnecting_state(common_options, options).into_state());
                    }
                }
            },
            _ = status_timer.select_next_some() => {
                if pending_status_req.is_empty() {
                    pending_status_req.push(common_options.proxy.clone().status());
                }
            },
            req = common_options.req_stream.next() => {
                match req {
                    Some(ManualRequest::Disconnect(responder)) => {
                        debug!("Disconnect requested");
                        let options = DisconnectingOptions {
                            disconnect_responder: Some(responder),
                            previous_network: Some((current_network.network, types::DisconnectStatus::ConnectionStopped)),
                            next_network: None
                        };
                        return Ok(disconnecting_state(common_options, options).into_state());
                    }
                    Some(ManualRequest::Connect((new_connect_request, new_responder))) => {
                        // Check if it's the same network as we're currently connected to. If yes, reply immediately
                        if (new_connect_request.network == current_network.network) {
                            info!("Connection requested for current network, deduping request");
                            new_responder.send(()).unwrap_or_else(|_| ());
                        } else {
                            let next_connecting_options = ConnectingOptions {
                                connect_responder: Some(new_responder),
                                connect_request: new_connect_request,
                                attempt_counter: 0,
                            };
                            let options = DisconnectingOptions {
                                disconnect_responder: None,
                                previous_network: Some((current_network.network, types::DisconnectStatus::ConnectionStopped)),
                                next_network: Some(next_connecting_options)
                            };
                            info!("Connection to new network requested, disconnecting from current network");
                            return Ok(disconnecting_state(common_options,options).into_state())
                        }
                    }
                    exit_or_none => return handle_exit_or_none_request(exit_or_none).await,
                };
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            config_management::network_config::{self, FailureReason},
            util::{cobalt::create_mock_cobalt_sender, listener, logger::set_logger_for_test},
        },
        fidl_fuchsia_stash as fidl_stash, fidl_fuchsia_wlan_policy as fidl_policy, fuchsia_zircon,
        futures::{stream::StreamFuture, task::Poll, Future},
        rand::{distributions::Alphanumeric, thread_rng, Rng},
        std::time::SystemTime,
        wlan_common::assert_variant,
    };

    struct TestValues {
        common_options: CommonStateOptions,
        sme_req_stream: fidl_sme::ClientSmeRequestStream,
        client_req_sender: mpsc::Sender<ManualRequest>,
        update_receiver: mpsc::UnboundedReceiver<listener::ClientListenerMessage>,
        iface_manager_stream: mpsc::Receiver<ClientStateMachineNotification>,
    }

    async fn test_setup() -> TestValues {
        set_logger_for_test();
        let (client_req_sender, client_req_stream) = mpsc::channel(1);
        let (iface_manager_sender, iface_manager_stream) = mpsc::channel(1);
        let (update_sender, update_receiver) = mpsc::unbounded();
        let (sme_proxy, sme_server) =
            create_proxy::<fidl_sme::ClientSmeMarker>().expect("failed to create an sme channel");
        let sme_req_stream = sme_server.into_stream().expect("could not create SME request stream");
        let saved_networks_manager = Arc::new(
            SavedNetworksManager::new_for_test()
                .await
                .expect("Failed to create saved networks manager"),
        );

        TestValues {
            common_options: CommonStateOptions {
                iface_id: 20,
                iface_manager_sender: iface_manager_sender,
                proxy: sme_proxy,
                req_stream: client_req_stream.fuse(),
                update_sender: update_sender,
                saved_networks_manager: saved_networks_manager,
            },
            sme_req_stream,
            client_req_sender,
            update_receiver,
            iface_manager_stream,
        }
    }

    fn poll_sme_req(
        exec: &mut fasync::Executor,
        next_sme_req: &mut StreamFuture<fidl_sme::ClientSmeRequestStream>,
    ) -> Poll<fidl_sme::ClientSmeRequest> {
        exec.run_until_stalled(next_sme_req).map(|(req, stream)| {
            *next_sme_req = stream.into_future();
            req.expect("did not expect the SME request stream to end")
                .expect("error polling SME request stream")
        })
    }

    async fn run_state_machine(
        fut: impl Future<Output = Result<State, ExitReason>> + Send + 'static,
    ) {
        let state_machine = fut.into_state_machine();
        select! {
            state_machine = state_machine.fuse() => return,
        }
    }

    /// Move stash requests forward so that a save request can progress.
    fn process_stash_write(
        exec: &mut fasync::Executor,
        stash_server: &mut fidl_stash::StoreAccessorRequestStream,
    ) {
        assert_variant!(
            exec.run_until_stalled(&mut stash_server.try_next()),
            Poll::Ready(Ok(Some(fidl_stash::StoreAccessorRequest::SetValue{..})))
        );
        assert_variant!(
            exec.run_until_stalled(&mut stash_server.try_next()),
            Poll::Ready(Ok(Some(fidl_stash::StoreAccessorRequest::Flush{responder}))) => {
                responder.send(&mut Ok(())).expect("failed to send stash response");
            }
        );
    }

    #[test]
    fn idle_state_gets_exit_request() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = exec.run_singlethreaded(test_setup());

        let initial_state = idle_state(test_values.common_options);
        let fut = run_state_machine(initial_state);
        pin_mut!(fut);
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Send an exit request
        let mut client = Client::new(test_values.client_req_sender);
        let (sender, mut receiver) = oneshot::channel();
        client.exit(sender).expect("failed to make request");

        // Ensure the state machine has no further actions and is exited
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));

        // Expect the responder to be acknowledged
        assert_variant!(exec.run_until_stalled(&mut receiver), Poll::Ready(Ok(())));
    }

    #[test]
    fn idle_state_notifies_iface_manager() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = exec.run_singlethreaded(test_setup());
        let test_iface_id = test_values.common_options.iface_id;

        let initial_state = idle_state(test_values.common_options);
        let fut = run_state_machine(initial_state);
        pin_mut!(fut);
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Ensure the iface manager got a notification
        assert_variant!(test_values.iface_manager_stream.try_next(), Ok(Some(message)) => {
            assert_eq!(message, ClientStateMachineNotification::Idle {
                iface_id: test_iface_id
            })
        });
    }

    #[test]
    fn idle_state_gets_connect_request() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = exec.run_singlethreaded(test_setup());

        let initial_state = idle_state(test_values.common_options);
        let fut = run_state_machine(initial_state);
        pin_mut!(fut);
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Send a connect request
        let mut client = Client::new(test_values.client_req_sender);
        let (sender, _receiver) = oneshot::channel();
        let next_network_ssid = "bar";
        let connect_request = ConnectRequest {
            network: types::NetworkIdentifier {
                ssid: next_network_ssid.as_bytes().to_vec(),
                type_: types::SecurityType::Wpa2,
            },
            credential: Credential::None,
        };
        client.connect(connect_request.clone(), sender).expect("failed to make request");

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Ensure a connect request is sent to the SME
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Connect{ req, txn, control_handle: _ }) => {
                assert_eq!(req.ssid, next_network_ssid.as_bytes().to_vec());
                assert_eq!(req.credential, sme_credential_from_policy(&connect_request.credential));
                assert_eq!(req.radio_cfg, RadioConfig { phy: None, cbw: None, primary_chan: None }.to_fidl());
                assert_eq!(req.deprecated_scan_type, fidl_fuchsia_wlan_common::ScanType::Active);
                 // Send connection response.
                let (_stream, ctrl) = txn.expect("connect txn unused")
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl.send_on_finished(fidl_sme::ConnectResultCode::Success)
                    .expect("failed to send connection completion");
            }
        );
    }

    #[test]
    fn idle_state_gets_disconnect_request() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = exec.run_singlethreaded(test_setup());

        let initial_state = idle_state(test_values.common_options);
        let fut = run_state_machine(initial_state);
        pin_mut!(fut);
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Send a disconnect request
        let mut client = Client::new(test_values.client_req_sender);
        let (sender, _receiver) = oneshot::channel();
        client.disconnect(sender).expect("failed to make request");

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Ensure nothing was sent to the SME, since we shouldn't have
        // transitioned to the disconnect state at all
        assert_variant!(poll_sme_req(&mut exec, &mut sme_fut), Poll::Pending);
    }

    #[test]
    fn connecting_state_gets_exit_request() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = exec.run_singlethreaded(test_setup());

        let connect_request = ConnectRequest {
            network: types::NetworkIdentifier {
                ssid: "test".as_bytes().to_vec(),
                type_: types::SecurityType::Wpa2,
            },
            credential: Credential::None,
        };
        let connecting_options = ConnectingOptions {
            connect_responder: None,
            connect_request: connect_request,
            attempt_counter: 0,
        };
        let initial_state = connecting_state(test_values.common_options, connecting_options);
        let fut = run_state_machine(initial_state);
        pin_mut!(fut);
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Send an exit request
        let mut client = Client::new(test_values.client_req_sender);
        let (sender, mut receiver) = oneshot::channel();
        client.exit(sender).expect("failed to make request");

        // Ensure the state machine has no further actions and is exited
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));

        // Expect the responder to be acknowledged
        assert_variant!(exec.run_until_stalled(&mut receiver), Poll::Ready(Ok(())));
    }

    fn rand_string() -> String {
        thread_rng().sample_iter(&Alphanumeric).take(20).collect()
    }

    #[test]
    fn connecting_state_successfully_connects() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        // Do test set up manually to get stash server
        set_logger_for_test();
        let (_client_req_sender, client_req_stream) = mpsc::channel(1);
        let (iface_manager_sender, _iface_manager_stream) = mpsc::channel(1);
        let (update_sender, mut update_receiver) = mpsc::unbounded();
        let (sme_proxy, sme_server) =
            create_proxy::<fidl_sme::ClientSmeMarker>().expect("failed to create an sme channel");
        let sme_req_stream = sme_server.into_stream().expect("could not create SME request stream");
        let temp_dir = tempfile::TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join(rand_string());
        let tmp_path = temp_dir.path().join(rand_string());
        let (saved_networks, mut stash_server) =
            exec.run_singlethreaded(SavedNetworksManager::new_and_stash_server(path, tmp_path));
        let saved_networks_manager = Arc::new(saved_networks);
        let next_network_ssid = "bar";
        let connect_request = ConnectRequest {
            network: types::NetworkIdentifier {
                ssid: next_network_ssid.as_bytes().to_vec(),
                type_: types::SecurityType::Wep,
            },
            credential: Credential::Password("Anything".as_bytes().to_vec()),
        };

        // Store the network in the saved_networks_manager, so we can record connection success
        let save_fut = saved_networks_manager
            .store(connect_request.network.clone().into(), connect_request.credential.clone());
        pin_mut!(save_fut);
        assert_variant!(exec.run_until_stalled(&mut save_fut), Poll::Pending);
        process_stash_write(&mut exec, &mut stash_server);
        assert_variant!(exec.run_until_stalled(&mut save_fut), Poll::Ready(Ok(())));

        let (connect_sender, mut connect_receiver) = oneshot::channel();
        let connecting_options = ConnectingOptions {
            connect_responder: Some(connect_sender),
            connect_request: connect_request.clone(),
            attempt_counter: 0,
        };
        let common_options = CommonStateOptions {
            iface_id: 20,
            iface_manager_sender: iface_manager_sender,
            proxy: sme_proxy,
            req_stream: client_req_stream.fuse(),
            update_sender: update_sender,
            saved_networks_manager: saved_networks_manager.clone(),
        };
        let initial_state = connecting_state(common_options, connecting_options);
        let fut = run_state_machine(initial_state);
        pin_mut!(fut);
        let sme_fut = sme_req_stream.into_future();
        pin_mut!(sme_fut);

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Ensure a connect request is sent to the SME
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Connect{ req, txn, control_handle: _ }) => {
                assert_eq!(req.ssid, next_network_ssid.as_bytes().to_vec());
                assert_eq!(req.credential, sme_credential_from_policy(&connect_request.credential));
                assert_eq!(req.radio_cfg, RadioConfig { phy: None, cbw: None, primary_chan: None }.to_fidl());
                assert_eq!(req.deprecated_scan_type, fidl_fuchsia_wlan_common::ScanType::Active);
                 // Send connection response.
                let (_stream, ctrl) = txn.expect("connect txn unused")
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl.send_on_finished(fidl_sme::ConnectResultCode::Success)
                    .expect("failed to send connection completion");
            }
        );

        // Check for a connecting update
        let client_state_update = ClientStateUpdate {
            state: None,
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from(next_network_ssid).into_bytes(),
                    type_: fidl_policy::SecurityType::Wep,
                },
                state: fidl_policy::ConnectionState::Connecting,
                status: None,
            }],
        };
        assert_variant!(
            update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert_eq!(updates, client_state_update);
        });

        // Progress the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        process_stash_write(&mut exec, &mut stash_server);
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Check the responder was acknowledged
        assert_variant!(exec.run_until_stalled(&mut connect_receiver), Poll::Ready(Ok(())));

        // Check for a connect update
        let client_state_update = ClientStateUpdate {
            state: None,
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from(next_network_ssid).into_bytes(),
                    type_: fidl_policy::SecurityType::Wep,
                },
                state: fidl_policy::ConnectionState::Connected,
                status: None,
            }],
        };
        assert_variant!(
            update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert_eq!(updates, client_state_update);
        });

        // Check that the saved networks manager has the connection recorded
        let saved_networks = exec.run_singlethreaded(
            saved_networks_manager.lookup(connect_request.network.clone().into()),
        );
        assert_eq!(true, saved_networks[0].has_ever_connected);

        // Progress the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Ensure no further updates were sent to listeners
        assert_variant!(exec.run_until_stalled(&mut update_receiver.into_future()), Poll::Pending);
    }

    #[test]
    fn connecting_state_fails_to_connect_and_retries() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = exec.run_singlethreaded(test_setup());

        let next_network_ssid = "bar";
        let connect_request = ConnectRequest {
            network: types::NetworkIdentifier {
                ssid: next_network_ssid.as_bytes().to_vec(),
                type_: types::SecurityType::Wpa2,
            },
            credential: Credential::None,
        };
        let (connect_sender, mut connect_receiver) = oneshot::channel();
        let connecting_options = ConnectingOptions {
            connect_responder: Some(connect_sender),
            connect_request: connect_request.clone(),
            attempt_counter: 0,
        };
        let initial_state = connecting_state(test_values.common_options, connecting_options);
        let fut = run_state_machine(initial_state);
        pin_mut!(fut);
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Check the responder was acknowledged
        assert_variant!(exec.run_until_stalled(&mut connect_receiver), Poll::Ready(Ok(())));

        // Ensure a connect request is sent to the SME
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Connect{ req, txn, control_handle: _ }) => {
                assert_eq!(req.ssid, next_network_ssid.as_bytes().to_vec());
                 // Send connection response.
                let (_stream, ctrl) = txn.expect("connect txn unused")
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl.send_on_finished(fidl_sme::ConnectResultCode::Failed)
                    .expect("failed to send connection completion");
            }
        );

        // Check for a connecting update
        let client_state_update = ClientStateUpdate {
            state: None,
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from(next_network_ssid).into_bytes(),
                    type_: fidl_policy::SecurityType::Wpa2,
                },
                state: fidl_policy::ConnectionState::Connecting,
                status: None,
            }],
        };
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert_eq!(updates, client_state_update);
        });

        // Progress the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Ensure a disconnect request is sent to the SME
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Disconnect{ responder }) => {
                responder.send().expect("could not send sme response");
            }
        );

        // Progress the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Ensure a connect request is sent to the SME
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Connect{ req, txn, control_handle: _ }) => {
                assert_eq!(req.ssid, next_network_ssid.as_bytes().to_vec());
                 // Send connection response.
                let (_stream, ctrl) = txn.expect("connect txn unused")
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl.send_on_finished(fidl_sme::ConnectResultCode::Success)
                    .expect("failed to send connection completion");
            }
        );

        // Progress the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Check for a connected update
        let client_state_update = ClientStateUpdate {
            state: None,
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from(next_network_ssid).into_bytes(),
                    type_: fidl_policy::SecurityType::Wpa2,
                },
                state: fidl_policy::ConnectionState::Connected,
                status: None,
            }],
        };
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert_eq!(updates, client_state_update);
        });

        // Progress the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Ensure no further updates were sent to listeners
        assert_variant!(
            exec.run_until_stalled(&mut test_values.update_receiver.into_future()),
            Poll::Pending
        );
    }

    #[test]
    fn connecting_state_fails_to_connect_at_max_retries() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        // Don't use test_values() because of issue with KnownEssStore
        set_logger_for_test();
        let (update_sender, mut update_receiver) = mpsc::unbounded();
        let (sme_proxy, sme_server) =
            create_proxy::<fidl_sme::ClientSmeMarker>().expect("failed to create an sme channel");
        let sme_req_stream = sme_server.into_stream().expect("could not create SME request stream");
        let stash_id = "connecting_state_fails_to_connect_at_max_retries";
        let temp_dir = tempfile::TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks_manager = Arc::new(
            exec.run_singlethreaded(SavedNetworksManager::new_with_stash_or_paths(
                stash_id,
                path,
                tmp_path,
                create_mock_cobalt_sender(),
            ))
            .expect("Failed to create saved networks manager"),
        );
        let (_client_req_sender, client_req_stream) = mpsc::channel(1);
        let (iface_manager_sender, _iface_manager_stream) = mpsc::channel(1);
        let common_options = CommonStateOptions {
            iface_id: 20,
            iface_manager_sender: iface_manager_sender,
            proxy: sme_proxy,
            req_stream: client_req_stream.fuse(),
            update_sender: update_sender,
            saved_networks_manager: saved_networks_manager.clone(),
        };

        let next_network_ssid = "bar";
        let next_security_type = types::SecurityType::None;
        let next_credential = Credential::None;
        let next_network_identifier = types::NetworkIdentifier {
            ssid: next_network_ssid.as_bytes().to_vec(),
            type_: next_security_type,
        };
        let config_net_id =
            network_config::NetworkIdentifier::from(next_network_identifier.clone());
        // save network to check that failed connect is recorded
        exec.run_singlethreaded(
            saved_networks_manager.store(config_net_id.clone(), next_credential.clone()),
        )
        .expect("Failed to save network");
        let before_recording = SystemTime::now();

        let connect_request =
            ConnectRequest { network: next_network_identifier, credential: next_credential };
        let (connect_sender, mut connect_receiver) = oneshot::channel();
        let connecting_options = ConnectingOptions {
            connect_responder: Some(connect_sender),
            connect_request: connect_request.clone(),
            attempt_counter: MAX_CONNECTION_ATTEMPTS - 1,
        };
        let initial_state = connecting_state(common_options, connecting_options);
        let fut = run_state_machine(initial_state);
        pin_mut!(fut);
        let sme_fut = sme_req_stream.into_future();
        pin_mut!(sme_fut);

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Check the responder was acknowledged
        assert_variant!(exec.run_until_stalled(&mut connect_receiver), Poll::Ready(Ok(())));

        // Ensure a connect request is sent to the SME
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Connect{ req, txn, control_handle: _ }) => {
                assert_eq!(req.ssid, next_network_ssid.as_bytes().to_vec());
                assert_eq!(req.credential, sme_credential_from_policy(&connect_request.credential));
                assert_eq!(req.radio_cfg, RadioConfig { phy: None, cbw: None, primary_chan: None }.to_fidl());
                assert_eq!(req.deprecated_scan_type, fidl_fuchsia_wlan_common::ScanType::Active);
                 // Send connection response.
                let (_stream, ctrl) = txn.expect("connect txn unused")
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl.send_on_finished(fidl_sme::ConnectResultCode::Failed)
                    .expect("failed to send connection completion");
            }
        );

        // Progress the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Check for a connect update
        let client_state_update = ClientStateUpdate {
            state: None,
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from(next_network_ssid).into_bytes(),
                    type_: next_security_type,
                },
                state: fidl_policy::ConnectionState::Failed,
                status: Some(fidl_policy::DisconnectStatus::ConnectionFailed),
            }],
        };
        assert_variant!(
            update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert_eq!(updates, client_state_update);
        });

        // Progress the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Ensure no further updates were sent to listeners
        assert_variant!(exec.run_until_stalled(&mut update_receiver.into_future()), Poll::Pending);

        // Ensure no further requests were sent to the SME
        assert_variant!(poll_sme_req(&mut exec, &mut sme_fut), Poll::Pending);

        // Check that failure was recorded in SavedNetworksManager
        let mut configs = exec.run_singlethreaded(saved_networks_manager.lookup(config_net_id));
        let network_config = configs.pop().expect("Failed to get saved network");
        let mut failures = network_config.perf_stats.failure_list.get_recent(before_recording);
        let connect_failure = failures.pop().expect("Saved network is missing failure reason");
        assert_eq!(connect_failure.reason, FailureReason::GeneralFailure);
    }

    #[test]
    fn connecting_state_fails_to_connect_with_bad_password() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        // Don't use test_values() because of issue with KnownEssStore
        set_logger_for_test();
        let (update_sender, mut update_receiver) = mpsc::unbounded();
        let (sme_proxy, sme_server) =
            create_proxy::<fidl_sme::ClientSmeMarker>().expect("failed to create an sme channel");
        let sme_req_stream = sme_server.into_stream().expect("could not create SME request stream");
        let stash_id = "connecting_state_fails_to_connect_with_bad_password";
        let temp_dir = tempfile::TempDir::new().expect("failed to create temporary directory");
        let path = temp_dir.path().join("networks.json");
        let tmp_path = temp_dir.path().join("tmp.json");
        let saved_networks_manager = Arc::new(
            exec.run_singlethreaded(SavedNetworksManager::new_with_stash_or_paths(
                stash_id,
                path,
                tmp_path,
                create_mock_cobalt_sender(),
            ))
            .expect("Failed to create saved networks manager"),
        );
        let (_client_req_sender, client_req_stream) = mpsc::channel(1);
        let (iface_manager_sender, _iface_manager_stream) = mpsc::channel(1);
        let common_options = CommonStateOptions {
            iface_id: 20,
            iface_manager_sender: iface_manager_sender,
            proxy: sme_proxy,
            req_stream: client_req_stream.fuse(),
            update_sender: update_sender,
            saved_networks_manager: saved_networks_manager.clone(),
        };

        let next_network_ssid = "bar";
        let next_network_identifier = types::NetworkIdentifier {
            ssid: next_network_ssid.as_bytes().to_vec(),
            type_: types::SecurityType::Wpa2,
        };
        let config_net_id =
            network_config::NetworkIdentifier::from(next_network_identifier.clone());
        let next_credential = Credential::Password("password".as_bytes().to_vec());
        // save network to check that failed connect is recorded
        let saved_networks_manager = common_options.saved_networks_manager.clone();
        exec.run_singlethreaded(
            saved_networks_manager.store(config_net_id.clone(), next_credential.clone()),
        )
        .expect("Failed to save network");
        let before_recording = SystemTime::now();

        let connect_request =
            ConnectRequest { network: next_network_identifier, credential: next_credential };
        let (connect_sender, mut connect_receiver) = oneshot::channel();
        let connecting_options = ConnectingOptions {
            connect_responder: Some(connect_sender),
            connect_request: connect_request.clone(),
            attempt_counter: MAX_CONNECTION_ATTEMPTS - 1,
        };
        let initial_state = connecting_state(common_options, connecting_options);
        let fut = run_state_machine(initial_state);
        pin_mut!(fut);
        let sme_fut = sme_req_stream.into_future();
        pin_mut!(sme_fut);

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Check the responder was acknowledged
        assert_variant!(exec.run_until_stalled(&mut connect_receiver), Poll::Ready(Ok(())));

        // Ensure a connect request is sent to the SME
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Connect{ req, txn, control_handle: _ }) => {
                assert_eq!(req.ssid, next_network_ssid.as_bytes().to_vec());
                assert_eq!(req.credential, sme_credential_from_policy(&connect_request.credential));
                assert_eq!(req.radio_cfg, RadioConfig { phy: None, cbw: None, primary_chan: None }.to_fidl());
                assert_eq!(req.deprecated_scan_type, fidl_fuchsia_wlan_common::ScanType::Active);
                 // Send connection response.
                let (_stream, ctrl) = txn.expect("connect txn unused")
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl.send_on_finished(fidl_sme::ConnectResultCode::CredentialRejected)
                    .expect("failed to send connection completion");
            }
        );

        // Progress the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Check for a connect update
        let client_state_update = ClientStateUpdate {
            state: None,
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from(next_network_ssid).into_bytes(),
                    type_: fidl_policy::SecurityType::Wpa2,
                },
                state: fidl_policy::ConnectionState::Failed,
                status: Some(fidl_policy::DisconnectStatus::CredentialsFailed),
            }],
        };
        assert_variant!(
            update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert_eq!(updates, client_state_update);
        });

        // Progress the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Ensure no further updates were sent to listeners
        assert_variant!(exec.run_until_stalled(&mut update_receiver.into_future()), Poll::Pending);

        // Ensure no further requests were sent to the SME
        assert_variant!(poll_sme_req(&mut exec, &mut sme_fut), Poll::Pending);

        // Check that failure was recorded in SavedNetworksManager
        let mut configs = exec.run_singlethreaded(saved_networks_manager.lookup(config_net_id));
        let network_config = configs.pop().expect("Failed to get saved network");
        let mut failures = network_config.perf_stats.failure_list.get_recent(before_recording);
        let connect_failure = failures.pop().expect("Saved network is missing failure reason");
        assert_eq!(connect_failure.reason, FailureReason::CredentialRejected);
    }

    #[test]
    fn connecting_state_gets_duplicate_connect_request() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = exec.run_singlethreaded(test_setup());

        let next_network_ssid = "bar";
        let connect_request = ConnectRequest {
            network: types::NetworkIdentifier {
                ssid: next_network_ssid.as_bytes().to_vec(),
                type_: types::SecurityType::Wpa2,
            },
            credential: Credential::None,
        };
        let (connect_sender, mut connect_receiver) = oneshot::channel();
        let connecting_options = ConnectingOptions {
            connect_responder: Some(connect_sender),
            connect_request: connect_request.clone(),
            attempt_counter: 0,
        };
        let initial_state = connecting_state(test_values.common_options, connecting_options);
        let fut = run_state_machine(initial_state);
        pin_mut!(fut);
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Check the responder was acknowledged
        assert_variant!(exec.run_until_stalled(&mut connect_receiver), Poll::Ready(Ok(())));

        // Check for a connecting update
        let client_state_update = ClientStateUpdate {
            state: None,
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from(next_network_ssid).into_bytes(),
                    type_: fidl_policy::SecurityType::Wpa2,
                },
                state: fidl_policy::ConnectionState::Connecting,
                status: None,
            }],
        };
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert_eq!(updates, client_state_update);
        });

        // Send a duplicate connect request
        let mut client = Client::new(test_values.client_req_sender);
        let (connect_sender2, mut connect_receiver2) = oneshot::channel();
        client.connect(connect_request.clone(), connect_sender2).expect("failed to make request");

        // Progress the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Check the responder was acknowledged
        assert_variant!(exec.run_until_stalled(&mut connect_receiver2), Poll::Ready(Ok(())));

        // Ensure a connect request is sent to the SME
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Connect{ req, txn, control_handle: _ }) => {
                assert_eq!(req.ssid, next_network_ssid.as_bytes().to_vec());
                assert_eq!(req.credential, sme_credential_from_policy(&connect_request.clone().credential));
                assert_eq!(req.radio_cfg, RadioConfig { phy: None, cbw: None, primary_chan: None }.to_fidl());
                assert_eq!(req.deprecated_scan_type, fidl_fuchsia_wlan_common::ScanType::Active);
                 // Send connection response.
                let (_stream, ctrl) = txn.expect("connect txn unused")
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl.send_on_finished(fidl_sme::ConnectResultCode::Success)
                    .expect("failed to send connection completion");
            }
        );

        // Progress the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Check for a connect update
        let client_state_update = ClientStateUpdate {
            state: None,
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from(next_network_ssid).into_bytes(),
                    type_: fidl_policy::SecurityType::Wpa2,
                },
                state: fidl_policy::ConnectionState::Connected,
                status: None,
            }],
        };
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert_eq!(updates, client_state_update);
        });

        // Progress the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Ensure no further updates were sent to listeners
        assert_variant!(
            exec.run_until_stalled(&mut test_values.update_receiver.into_future()),
            Poll::Pending
        );
    }

    #[test]
    fn connecting_state_gets_different_connect_request() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = exec.run_singlethreaded(test_setup());

        let first_network_ssid = "foo";
        let second_network_ssid = "bar";
        let connect_request = ConnectRequest {
            network: types::NetworkIdentifier {
                ssid: first_network_ssid.as_bytes().to_vec(),
                type_: types::SecurityType::Wpa2,
            },
            credential: Credential::None,
        };
        let (connect_sender, mut connect_receiver) = oneshot::channel();
        let connecting_options = ConnectingOptions {
            connect_responder: Some(connect_sender),
            connect_request: connect_request.clone(),
            attempt_counter: 0,
        };
        let initial_state = connecting_state(test_values.common_options, connecting_options);
        let fut = run_state_machine(initial_state);
        pin_mut!(fut);
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Check the responder was acknowledged
        assert_variant!(exec.run_until_stalled(&mut connect_receiver), Poll::Ready(Ok(())));

        // Check for a connecting update
        let client_state_update = ClientStateUpdate {
            state: None,
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from(first_network_ssid).into_bytes(),
                    type_: fidl_policy::SecurityType::Wpa2,
                },
                state: fidl_policy::ConnectionState::Connecting,
                status: None,
            }],
        };
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert_eq!(updates, client_state_update);
        });

        // Send a different connect request
        let mut client = Client::new(test_values.client_req_sender);
        let (connect_sender2, mut connect_receiver2) = oneshot::channel();
        let connect_request2 = ConnectRequest {
            network: types::NetworkIdentifier {
                ssid: second_network_ssid.as_bytes().to_vec(),
                type_: types::SecurityType::Wpa2,
            },
            credential: Credential::None,
        };
        client.connect(connect_request2.clone(), connect_sender2).expect("failed to make request");

        // Progress the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Check for a disconnect update
        let client_state_update = ClientStateUpdate {
            state: None,
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from(first_network_ssid).into_bytes(),
                    type_: fidl_policy::SecurityType::Wpa2,
                },
                state: fidl_policy::ConnectionState::Disconnected,
                status: Some(fidl_policy::DisconnectStatus::ConnectionStopped),
            }],
        };
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert_eq!(updates, client_state_update);
        });

        // There should be 3 requests to the SME stacked up
        // First SME request: connect to the first network
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Connect{ req, txn: _, control_handle: _ }) => {
                assert_eq!(req.ssid, first_network_ssid.as_bytes().to_vec());
                // Don't bother sending response, listener is gone
            }
        );
        // Second SME request: disconnect
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Disconnect{ responder }) => {
                responder.send().expect("could not send sme response");
            }
        );
        // Progress the state machine
        // TODO(fxbug.dev/53505): remove this once the disconnect request is fire-and-forget
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        // Third SME request: connect to the second network
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Connect{ req, txn, control_handle: _ }) => {
                assert_eq!(req.ssid, second_network_ssid.as_bytes().to_vec());
                 // Send connection response.
                let (_stream, ctrl) = txn.expect("connect txn unused")
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl.send_on_finished(fidl_sme::ConnectResultCode::Success)
                    .expect("failed to send connection completion");
            }
        );

        // Progress the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Check the responder was acknowledged
        assert_variant!(exec.run_until_stalled(&mut connect_receiver2), Poll::Ready(Ok(())));

        // Check for a connecting update
        let client_state_update = ClientStateUpdate {
            state: None,
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from(second_network_ssid).into_bytes(),
                    type_: fidl_policy::SecurityType::Wpa2,
                },
                state: fidl_policy::ConnectionState::Connecting,
                status: None,
            }],
        };
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert_eq!(updates, client_state_update);
        });
        // Check for a connected update
        let client_state_update = ClientStateUpdate {
            state: None,
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from(second_network_ssid).into_bytes(),
                    type_: fidl_policy::SecurityType::Wpa2,
                },
                state: fidl_policy::ConnectionState::Connected,
                status: None,
            }],
        };
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert_eq!(updates, client_state_update);
        });

        // Progress the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Ensure no further updates were sent to listeners
        assert_variant!(
            exec.run_until_stalled(&mut test_values.update_receiver.into_future()),
            Poll::Pending
        );
    }

    #[test]
    fn connecting_state_gets_disconnect_request() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = exec.run_singlethreaded(test_setup());

        let first_network_ssid = "foo";
        let connect_request = ConnectRequest {
            network: types::NetworkIdentifier {
                ssid: first_network_ssid.as_bytes().to_vec(),
                type_: types::SecurityType::Wpa2,
            },
            credential: Credential::None,
        };
        let (connect_sender, mut connect_receiver) = oneshot::channel();
        let connecting_options = ConnectingOptions {
            connect_responder: Some(connect_sender),
            connect_request: connect_request.clone(),
            attempt_counter: 0,
        };
        let initial_state = connecting_state(test_values.common_options, connecting_options);
        let fut = run_state_machine(initial_state);
        pin_mut!(fut);
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Check the responder was acknowledged
        assert_variant!(exec.run_until_stalled(&mut connect_receiver), Poll::Ready(Ok(())));

        // Check for a connecting update
        let client_state_update = ClientStateUpdate {
            state: None,
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from(first_network_ssid).into_bytes(),
                    type_: fidl_policy::SecurityType::Wpa2,
                },
                state: fidl_policy::ConnectionState::Connecting,
                status: None,
            }],
        };
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert_eq!(updates, client_state_update);
        });

        // Send a disconnect request
        let mut client = Client::new(test_values.client_req_sender);
        let (disconnect_sender, mut disconnect_receiver) = oneshot::channel();
        client.disconnect(disconnect_sender).expect("failed to make request");

        // Progress the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Check for a disconnect update
        let client_state_update = ClientStateUpdate {
            state: None,
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from(first_network_ssid).into_bytes(),
                    type_: fidl_policy::SecurityType::Wpa2,
                },
                state: fidl_policy::ConnectionState::Disconnected,
                status: Some(fidl_policy::DisconnectStatus::ConnectionStopped),
            }],
        };
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert_eq!(updates, client_state_update);
        });

        // There should be 2 requests to the SME stacked up
        // First SME request: connect to the first network
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Connect{ req, txn: _, control_handle: _ }) => {
                assert_eq!(req.ssid, first_network_ssid.as_bytes().to_vec());
                // Don't bother sending response, listener is gone
            }
        );
        // Second SME request: disconnect
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Disconnect{ responder }) => {
                responder.send().expect("could not send sme response");
            }
        );
        // Progress the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Check the disconnect responder
        assert_variant!(exec.run_until_stalled(&mut disconnect_receiver), Poll::Ready(Ok(())));

        // Ensure no further updates were sent to listeners
        assert_variant!(
            exec.run_until_stalled(&mut test_values.update_receiver.into_future()),
            Poll::Pending
        );
    }

    #[test]
    fn connecting_state_has_broken_sme() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = exec.run_singlethreaded(test_setup());

        let first_network_ssid = "foo";
        let connect_request = ConnectRequest {
            network: types::NetworkIdentifier {
                ssid: first_network_ssid.as_bytes().to_vec(),
                type_: types::SecurityType::Wpa2,
            },
            credential: Credential::None,
        };
        let (connect_sender, mut connect_receiver) = oneshot::channel();
        let connecting_options = ConnectingOptions {
            connect_responder: Some(connect_sender),
            connect_request: connect_request.clone(),
            attempt_counter: 0,
        };
        let initial_state = connecting_state(test_values.common_options, connecting_options);
        let fut = run_state_machine(initial_state);
        pin_mut!(fut);

        // Break the SME by dropping the server end of the SME stream, so it causes an error
        drop(test_values.sme_req_stream);

        // Ensure the state machine exits
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));

        // Expect the responder to have an error
        assert_variant!(exec.run_until_stalled(&mut connect_receiver), Poll::Ready(Err(_)));
    }

    #[test]
    fn connected_state_gets_exit_request() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = exec.run_singlethreaded(test_setup());

        let connect_request = ConnectRequest {
            network: types::NetworkIdentifier {
                ssid: "test".as_bytes().to_vec(),
                type_: types::SecurityType::Wpa2,
            },
            credential: Credential::None,
        };
        let initial_state = connected_state(test_values.common_options, connect_request);
        let fut = run_state_machine(initial_state);
        pin_mut!(fut);
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Send an exit request
        let mut client = Client::new(test_values.client_req_sender);
        let (sender, mut receiver) = oneshot::channel();
        client.exit(sender).expect("failed to make request");

        // Ensure the state machine has no further actions and is exited
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));

        // Expect the responder to be acknowledged
        assert_variant!(exec.run_until_stalled(&mut receiver), Poll::Ready(Ok(())));
    }

    #[test]
    fn connected_state_gets_disconnect_request() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = exec.run_singlethreaded(test_setup());

        let network_ssid = "test";
        let connect_request = ConnectRequest {
            network: types::NetworkIdentifier {
                ssid: network_ssid.as_bytes().to_vec(),
                type_: types::SecurityType::Wpa2,
            },
            credential: Credential::None,
        };
        let initial_state = connected_state(test_values.common_options, connect_request);
        let fut = run_state_machine(initial_state);
        pin_mut!(fut);
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Clear the SME status request
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Status{ responder }) => {
                responder.send(&mut fidl_sme::ClientStatusResponse{
                    connecting_to_ssid: vec![],
                    connected_to: Some(Box::new(fidl_sme::BssInfo{
                        bssid: [0, 0, 0, 0, 0, 0],
                        ssid: network_ssid.as_bytes().to_vec(),
                        rx_dbm: 0,
                        snr_db: 0,
                        channel: 0,
                        protection: fidl_sme::Protection::Unknown,
                        compatible: true,
                    }))
                }).expect("could not send sme response");
            }
        );

        // Send a disconnect request
        let mut client = Client::new(test_values.client_req_sender);
        let (sender, mut receiver) = oneshot::channel();
        client.disconnect(sender).expect("failed to make request");

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Respond to the SME disconnect
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Disconnect{ responder }) => {
                responder.send().expect("could not send sme response");
            }
        );

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Check for a disconnect update and the responder
        let client_state_update = ClientStateUpdate {
            state: None,
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from(network_ssid).into_bytes(),
                    type_: fidl_policy::SecurityType::Wpa2,
                },
                state: fidl_policy::ConnectionState::Disconnected,
                status: Some(fidl_policy::DisconnectStatus::ConnectionStopped),
            }],
        };
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert_eq!(updates, client_state_update);
        });
        assert_variant!(exec.run_until_stalled(&mut receiver), Poll::Ready(Ok(())));
    }

    #[test]
    fn connected_state_gets_duplicate_connect_request() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = exec.run_singlethreaded(test_setup());

        let network_ssid = "test";
        let connect_request = ConnectRequest {
            network: types::NetworkIdentifier {
                ssid: network_ssid.as_bytes().to_vec(),
                type_: types::SecurityType::Wpa2,
            },
            credential: Credential::None,
        };
        let initial_state = connected_state(test_values.common_options, connect_request.clone());
        let fut = run_state_machine(initial_state);
        pin_mut!(fut);
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Clear the SME status request
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Status{ responder }) => {
                responder.send(&mut fidl_sme::ClientStatusResponse{
                    connecting_to_ssid: vec![],
                    connected_to: Some(Box::new(fidl_sme::BssInfo{
                        bssid: [0, 0, 0, 0, 0, 0],
                        ssid: network_ssid.as_bytes().to_vec(),
                        rx_dbm: 0,
                        snr_db: 0,
                        channel: 0,
                        protection: fidl_sme::Protection::Unknown,
                        compatible: true,
                    }))
                }).expect("could not send sme response");
            }
        );

        // Send another duplicate request
        let mut client = Client::new(test_values.client_req_sender);
        let (sender, mut receiver) = oneshot::channel();
        client.connect(connect_request.clone(), sender).expect("failed to make request");

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Ensure nothing was sent to the SME
        assert_variant!(poll_sme_req(&mut exec, &mut sme_fut), Poll::Pending);

        // Check the responder was acknowledged
        assert_variant!(exec.run_until_stalled(&mut receiver), Poll::Ready(Ok(())));
    }

    #[test]
    fn connected_state_gets_different_connect_request() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = exec.run_singlethreaded(test_setup());

        let first_network_ssid = "foo";
        let second_network_ssid = "bar";
        let connect_request = ConnectRequest {
            network: types::NetworkIdentifier {
                ssid: first_network_ssid.as_bytes().to_vec(),
                type_: types::SecurityType::Wpa2,
            },
            credential: Credential::None,
        };
        let initial_state = connected_state(test_values.common_options, connect_request.clone());
        let fut = run_state_machine(initial_state);
        pin_mut!(fut);
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Clear the SME status request
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Status{ responder }) => {
                responder.send(&mut fidl_sme::ClientStatusResponse{
                    connecting_to_ssid: vec![],
                    connected_to: Some(Box::new(fidl_sme::BssInfo{
                        bssid: [0, 0, 0, 0, 0, 0],
                        ssid: first_network_ssid.as_bytes().to_vec(),
                        rx_dbm: 0,
                        snr_db: 0,
                        channel: 0,
                        protection: fidl_sme::Protection::Unknown,
                        compatible: true,
                    }))
                }).expect("could not send sme response");
            }
        );

        // Send a different connect request
        let mut client = Client::new(test_values.client_req_sender);
        let (connect_sender2, mut connect_receiver2) = oneshot::channel();
        let connect_request2 = ConnectRequest {
            network: types::NetworkIdentifier {
                ssid: second_network_ssid.as_bytes().to_vec(),
                type_: types::SecurityType::Wpa2,
            },
            credential: Credential::None,
        };
        client.connect(connect_request2.clone(), connect_sender2).expect("failed to make request");

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // There should be 2 requests to the SME stacked up
        // First SME request: disconnect
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Disconnect{ responder }) => {
                responder.send().expect("could not send sme response");
            }
        );
        // Progress the state machine
        // TODO(fxbug.dev/53505): remove this once the disconnect request is fire-and-forget
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        // Second SME request: connect to the second network
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Connect{ req, txn, control_handle: _ }) => {
                assert_eq!(req.ssid, second_network_ssid.as_bytes().to_vec());
                 // Send connection response.
                let (_stream, ctrl) = txn.expect("connect txn unused")
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl.send_on_finished(fidl_sme::ConnectResultCode::Success)
                    .expect("failed to send connection completion");
            }
        );
        // Progress the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Check for a disconnect update
        let client_state_update = ClientStateUpdate {
            state: None,
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from(first_network_ssid).into_bytes(),
                    type_: fidl_policy::SecurityType::Wpa2,
                },
                state: fidl_policy::ConnectionState::Disconnected,
                status: Some(fidl_policy::DisconnectStatus::ConnectionStopped),
            }],
        };
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert_eq!(updates, client_state_update);
        });

        // Check the responder was acknowledged
        assert_variant!(exec.run_until_stalled(&mut connect_receiver2), Poll::Ready(Ok(())));

        // Check for a connecting update
        let client_state_update = ClientStateUpdate {
            state: None,
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from(second_network_ssid).into_bytes(),
                    type_: fidl_policy::SecurityType::Wpa2,
                },
                state: fidl_policy::ConnectionState::Connecting,
                status: None,
            }],
        };
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert_eq!(updates, client_state_update);
        });
        // Check for a connected update
        let client_state_update = ClientStateUpdate {
            state: None,
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from(second_network_ssid).into_bytes(),
                    type_: fidl_policy::SecurityType::Wpa2,
                },
                state: fidl_policy::ConnectionState::Connected,
                status: None,
            }],
        };
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert_eq!(updates, client_state_update);
        });

        // Progress the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Ensure no further updates were sent to listeners
        assert_variant!(
            exec.run_until_stalled(&mut test_values.update_receiver.into_future()),
            Poll::Pending
        );
    }

    #[test]
    fn connected_state_detects_network_disconnect() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = exec.run_singlethreaded(test_setup());

        let network_ssid = "foo";
        let connect_request = ConnectRequest {
            network: types::NetworkIdentifier {
                ssid: network_ssid.as_bytes().to_vec(),
                type_: types::SecurityType::Wpa2,
            },
            credential: Credential::None,
        };
        let initial_state = connected_state(test_values.common_options, connect_request.clone());
        let fut = run_state_machine(initial_state);
        pin_mut!(fut);
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Respond to the SME status request with a disconnected status
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Status{ responder }) => {
                responder.send(&mut fidl_sme::ClientStatusResponse{
                    connecting_to_ssid: vec![],
                    connected_to: None
                }).expect("could not send sme response");
            }
        );

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Check for an SME disconnect request
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Disconnect{ responder }) => {
                responder.send().expect("could not send sme response");
            }
        );
        // Progress the state machine
        // TODO(fxbug.dev/53505): remove this once the disconnect request is fire-and-forget
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Check for a disconnect update
        let client_state_update = ClientStateUpdate {
            state: None,
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from(network_ssid).into_bytes(),
                    type_: fidl_policy::SecurityType::Wpa2,
                },
                state: fidl_policy::ConnectionState::Disconnected,
                status: Some(fidl_policy::DisconnectStatus::ConnectionFailed),
            }],
        };
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert_eq!(updates, client_state_update);
        });

        // Check for an SME request to reconnect
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Connect{ req, txn, control_handle: _ }) => {
                assert_eq!(req.ssid, network_ssid.as_bytes().to_vec());
                 // Send connection response.
                let (_stream, ctrl) = txn.expect("connect txn unused")
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl.send_on_finished(fidl_sme::ConnectResultCode::Success)
                    .expect("failed to send connection completion");
            }
        );

        // Check for a connecting update
        let client_state_update = ClientStateUpdate {
            state: None,
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from(network_ssid).into_bytes(),
                    type_: fidl_policy::SecurityType::Wpa2,
                },
                state: fidl_policy::ConnectionState::Connecting,
                status: None,
            }],
        };
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert_eq!(updates, client_state_update);
        });
    }

    #[test]
    fn disconnecting_state_completes_disconnect_to_idle() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = exec.run_singlethreaded(test_setup());

        let (sender, mut receiver) = oneshot::channel();
        let disconnecting_options = DisconnectingOptions {
            disconnect_responder: Some(sender),
            previous_network: None,
            next_network: None,
        };
        let initial_state = disconnecting_state(test_values.common_options, disconnecting_options);
        let fut = run_state_machine(initial_state);
        pin_mut!(fut);
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Ensure a disconnect request is sent to the SME
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Disconnect{ responder }) => {
                responder.send().expect("could not send sme response");
            }
        );

        // Ensure the state machine has no further actions
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Ensure no further requests were sent to the SME
        assert_variant!(poll_sme_req(&mut exec, &mut sme_fut), Poll::Pending);

        // Ensure no updates were sent to listeners
        assert_variant!(
            exec.run_until_stalled(&mut test_values.update_receiver.into_future()),
            Poll::Pending
        );

        // Expect the responder to be acknowledged
        assert_variant!(exec.run_until_stalled(&mut receiver), Poll::Ready(Ok(())));
    }

    #[test]
    fn disconnecting_state_completes_disconnect_to_connecting() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = exec.run_singlethreaded(test_setup());

        let previous_network_ssid = "foo";
        let next_network_ssid = "bar";
        let connect_request = ConnectRequest {
            network: types::NetworkIdentifier {
                ssid: next_network_ssid.as_bytes().to_vec(),
                type_: types::SecurityType::Wpa2,
            },
            credential: Credential::None,
        };
        let (connect_sender, _connect_receiver) = oneshot::channel();
        let connecting_options = ConnectingOptions {
            connect_responder: Some(connect_sender),
            connect_request: connect_request.clone(),
            attempt_counter: 0,
        };
        let (disconnect_sender, mut disconnect_receiver) = oneshot::channel();
        // Include both a "previous" and "next" network
        let disconnecting_options = DisconnectingOptions {
            disconnect_responder: Some(disconnect_sender),
            previous_network: Some((
                types::NetworkIdentifier {
                    ssid: previous_network_ssid.as_bytes().to_vec(),
                    type_: types::SecurityType::Wpa2,
                },
                fidl_policy::DisconnectStatus::ConnectionStopped,
            )),
            next_network: Some(connecting_options),
        };
        let initial_state = disconnecting_state(test_values.common_options, disconnecting_options);
        let fut = run_state_machine(initial_state);
        pin_mut!(fut);
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        // Run the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Ensure a disconnect request is sent to the SME
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Disconnect{ responder }) => {
                responder.send().expect("could not send sme response");
            }
        );

        // Progress the state machine
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Check for a disconnect update and the disconnect responder
        let client_state_update = ClientStateUpdate {
            state: None,
            networks: vec![ClientNetworkState {
                id: fidl_policy::NetworkIdentifier {
                    ssid: String::from(previous_network_ssid).into_bytes(),
                    type_: fidl_policy::SecurityType::Wpa2,
                },
                state: fidl_policy::ConnectionState::Disconnected,
                status: Some(fidl_policy::DisconnectStatus::ConnectionStopped),
            }],
        };
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert_eq!(updates, client_state_update);
        });
        assert_variant!(exec.run_until_stalled(&mut disconnect_receiver), Poll::Ready(Ok(())));

        // Ensure a connect request is sent to the SME
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Connect{ req, txn, control_handle: _ }) => {
                assert_eq!(req.ssid, next_network_ssid.as_bytes().to_vec());
                assert_eq!(req.credential, sme_credential_from_policy(&connect_request.credential));
                assert_eq!(req.radio_cfg, RadioConfig { phy: None, cbw: None, primary_chan: None }.to_fidl());
                assert_eq!(req.deprecated_scan_type, fidl_fuchsia_wlan_common::ScanType::Active);
                 // Send connection response.
                let (_stream, ctrl) = txn.expect("connect txn unused")
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl.send_on_finished(fidl_sme::ConnectResultCode::Success)
                    .expect("failed to send connection completion");
            }
        );
    }

    #[test]
    fn disconnecting_state_has_broken_sme() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = exec.run_singlethreaded(test_setup());

        let (sender, mut receiver) = oneshot::channel();
        let disconnecting_options = DisconnectingOptions {
            disconnect_responder: Some(sender),
            previous_network: None,
            next_network: None,
        };
        let initial_state = disconnecting_state(test_values.common_options, disconnecting_options);
        let fut = run_state_machine(initial_state);
        pin_mut!(fut);

        // Break the SME by dropping the server end of the SME stream, so it causes an error
        drop(test_values.sme_req_stream);

        // Ensure the state machine exits
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));

        // Expect the responder to have an error
        assert_variant!(exec.run_until_stalled(&mut receiver), Poll::Ready(Err(_)));
    }

    #[test]
    fn serve_loop_handles_startup() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = exec.run_singlethreaded(test_setup());
        let sme_proxy = test_values.common_options.proxy;
        let sme_event_stream = sme_proxy.take_event_stream();
        let (iface_manager_sender, _iface_manager_stream) = mpsc::channel(1);
        let (_client_req_sender, client_req_stream) = mpsc::channel(1);
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        let fut = serve(
            0,
            sme_proxy,
            sme_event_stream,
            client_req_stream,
            test_values.common_options.update_sender,
            iface_manager_sender,
            test_values.common_options.saved_networks_manager,
        );
        pin_mut!(fut);

        // Run the state machine so it sends the initial SME disconnect request.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Disconnect{ responder }) => {
                responder.send().expect("could not send sme response");
            }
        );

        // Run the future again and ensure that it has not exited after receiving the response.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
    }

    #[test]
    fn serve_loop_handles_sme_disappearance() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = exec.run_singlethreaded(test_setup());
        let (iface_manager_sender, _iface_manager_stream) = mpsc::channel(1);
        let (_client_req_sender, client_req_stream) = mpsc::channel(1);

        // Make our own SME proxy for this test
        let (sme_proxy, sme_server) =
            create_proxy::<fidl_sme::ClientSmeMarker>().expect("failed to create an sme channel");
        let (sme_req_stream, sme_control_handle) = sme_server
            .into_stream_and_control_handle()
            .expect("could not create SME request stream");

        let sme_fut = sme_req_stream.into_future();
        pin_mut!(sme_fut);

        let sme_event_stream = sme_proxy.take_event_stream();

        let fut = serve(
            0,
            sme_proxy,
            sme_event_stream,
            client_req_stream,
            test_values.common_options.update_sender,
            iface_manager_sender,
            test_values.common_options.saved_networks_manager,
        );
        pin_mut!(fut);

        // Run the state machine so it sends the initial SME disconnect request.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Disconnect{ responder }) => {
                responder.send().expect("could not send sme response");
            }
        );

        // Run the future again and ensure that it has not exited after receiving the response.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        sme_control_handle.shutdown_with_epitaph(fuchsia_zircon::Status::UNAVAILABLE);

        // Ensure the state machine has no further actions and is exited
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
    }

    #[test]
    fn serve_loop_handles_exit() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = exec.run_singlethreaded(test_setup());
        let sme_proxy = test_values.common_options.proxy;
        let sme_event_stream = sme_proxy.take_event_stream();
        let (iface_manager_sender, _iface_manager_stream) = mpsc::channel(1);
        let (client_req_sender, client_req_stream) = mpsc::channel(1);
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        let fut = serve(
            0,
            sme_proxy,
            sme_event_stream,
            client_req_stream,
            test_values.common_options.update_sender,
            iface_manager_sender,
            test_values.common_options.saved_networks_manager,
        );
        pin_mut!(fut);

        // Run the state machine so it sends the initial SME disconnect request.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ClientSmeRequest::Disconnect{ responder }) => {
                responder.send().expect("could not send sme response");
            }
        );

        // Run the future again and ensure that it has not exited after receiving the response.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Send an exit request
        let mut client = Client::new(client_req_sender);
        let (sender, mut receiver) = oneshot::channel();
        client.exit(sender).expect("failed to make request");

        // Ensure the state machine has no further actions and is exited
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));

        // Expect the responder to be acknowledged
        assert_variant!(exec.run_until_stalled(&mut receiver), Poll::Ready(Ok(())));
    }

    #[test]
    fn serve_loop_handles_state_machine_error() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = exec.run_singlethreaded(test_setup());
        let sme_proxy = test_values.common_options.proxy;
        let sme_event_stream = sme_proxy.take_event_stream();
        let (iface_manager_sender, _iface_manager_stream) = mpsc::channel(1);
        let (_client_req_sender, client_req_stream) = mpsc::channel(1);

        let fut = serve(
            0,
            sme_proxy,
            sme_event_stream,
            client_req_stream,
            test_values.common_options.update_sender,
            iface_manager_sender,
            test_values.common_options.saved_networks_manager,
        );
        pin_mut!(fut);

        // Drop the server end of the SME stream, so it causes an error
        drop(test_values.sme_req_stream);

        // Ensure the state machine exits
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
    }
}
