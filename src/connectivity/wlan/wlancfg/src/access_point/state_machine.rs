// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        access_point::types,
        util::{
            listener::{
                ApListenerMessageSender, ApStateUpdate, ApStatesUpdate, ConnectedClientInformation,
                Message::NotifyListeners,
            },
            state_machine::{self, ExitReason, IntoStateExt},
        },
    },
    anyhow::format_err,
    fidl_fuchsia_wlan_sme as fidl_sme, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{
        channel::{mpsc, oneshot},
        future::FutureExt,
        select,
        stream::{self, FuturesUnordered, StreamExt, TryStreamExt},
    },
    log::info,
    void::ResultVoidErrExt,
    wlan_common::{
        channel::{Cbw, Channel, Phy},
        RadioConfig,
    },
};

const AP_STATUS_INTERVAL_SEC: i64 = 10;
const DEFAULT_CBW: Cbw = Cbw::Cbw20;
const DEFAULT_CHANNEL: u8 = 6;
const DEFAULT_PHY: Phy = Phy::Ht;

type State = state_machine::State<ExitReason>;
type NextReqFut = stream::StreamFuture<mpsc::Receiver<ManualRequest>>;

pub type StartResponder = oneshot::Sender<fidl_sme::StartApResultCode>;

pub trait AccessPointApi {
    fn start(&mut self, request: ApConfig, responder: StartResponder) -> Result<(), anyhow::Error>;
    fn stop(&mut self, responder: oneshot::Sender<()>) -> Result<(), anyhow::Error>;
    fn exit(&mut self, responder: oneshot::Sender<()>) -> Result<(), anyhow::Error>;
}

pub struct AccessPoint {
    req_sender: mpsc::Sender<ManualRequest>,
}

impl AccessPoint {
    pub fn new(req_sender: mpsc::Sender<ManualRequest>) -> Self {
        Self { req_sender }
    }
}

impl AccessPointApi for AccessPoint {
    fn start(&mut self, request: ApConfig, responder: StartResponder) -> Result<(), anyhow::Error> {
        self.req_sender
            .try_send(ManualRequest::Start((request, responder)))
            .map_err(|e| format_err!("failed to send start request: {:?}", e))
    }

    fn stop(&mut self, responder: oneshot::Sender<()>) -> Result<(), anyhow::Error> {
        self.req_sender
            .try_send(ManualRequest::Stop(responder))
            .map_err(|e| format_err!("failed to send stop request: {:?}", e))
    }

    fn exit(&mut self, responder: oneshot::Sender<()>) -> Result<(), anyhow::Error> {
        self.req_sender
            .try_send(ManualRequest::Exit(responder))
            .map_err(|e| format_err!("failed to send exit request: {:?}", e))
    }
}

pub enum ManualRequest {
    Start((ApConfig, StartResponder)),
    Stop(oneshot::Sender<()>),
    Exit(oneshot::Sender<()>),
}

#[derive(Clone, Debug, PartialEq)]
pub struct ApConfig {
    pub id: types::NetworkIdentifier,
    pub credential: Vec<u8>,
    pub radio_config: RadioConfig,
    pub mode: types::ConnectivityMode,
    pub band: types::OperatingBand,
}

impl From<ApConfig> for fidl_sme::ApConfig {
    fn from(config: ApConfig) -> Self {
        fidl_sme::ApConfig {
            ssid: config.id.ssid,
            password: config.credential,
            radio_cfg: config.radio_config.to_fidl(),
        }
    }
}

fn send_state_update(
    sender: &ApListenerMessageSender,
    update: ApStateUpdate,
) -> Result<(), anyhow::Error> {
    let updates = ApStatesUpdate { access_points: [update].to_vec() };
    sender
        .clone()
        .unbounded_send(NotifyListeners(updates))
        .or_else(|e| Err(format_err!("failed to send state update: {}", e)))
}

fn send_ap_stopped_update(sender: &ApListenerMessageSender) -> Result<(), anyhow::Error> {
    let updates = ApStatesUpdate { access_points: [].to_vec() };
    sender
        .clone()
        .unbounded_send(NotifyListeners(updates))
        .or_else(|e| Err(format_err!("failed to send state update: {}", e)))
}

fn consume_sme_status_update(state: &mut ApStateUpdate, cbw: Cbw, update: fidl_sme::Ap) {
    let channel = Channel::new(update.channel, cbw);
    let frequency = match channel.get_center_freq() {
        Ok(frequency) => Some(frequency as u32),
        Err(e) => {
            info!("failed to convert channel to frequency: {}", e);
            None
        }
    };
    state.frequency = frequency;
    state.clients = Some(ConnectedClientInformation { count: update.num_clients as u8 })
}

pub async fn serve(
    iface_id: u16,
    proxy: fidl_sme::ApSmeProxy,
    sme_event_stream: fidl_sme::ApSmeEventStream,
    req_stream: mpsc::Receiver<ManualRequest>,
    message_sender: ApListenerMessageSender,
) {
    let state_machine =
        stopped_state(proxy, req_stream.into_future(), message_sender).into_state_machine();
    let removal_watcher = sme_event_stream.map_ok(|_| ()).try_collect::<()>();
    select! {
        state_machine = state_machine.fuse() => {
            match state_machine.void_unwrap_err() {
                ExitReason(Ok(())) => info!("AP state machine for iface #{} exited", iface_id),
                ExitReason(Err(e)) => {
                    info!("AP state machine for iface #{} terminated with an error: {}", iface_id, e)
                }
            }
        },
        removal_watcher = removal_watcher.fuse() => if let Err(e) = removal_watcher {
            info!("Error reading from AP SME channel of iface #{}: {}",
                iface_id, e);
        },
    }
    info!("Removed AP for iface #{}", iface_id);
}

fn perform_manual_request(
    proxy: fidl_sme::ApSmeProxy,
    req: Option<ManualRequest>,
    req_stream: mpsc::Receiver<ManualRequest>,
    sender: ApListenerMessageSender,
) -> Result<State, ExitReason> {
    match req {
        Some(ManualRequest::Start((req, responder))) => {
            Ok(starting_state(proxy, req_stream.into_future(), req, Some(responder), sender)
                .into_state())
        }
        Some(ManualRequest::Stop(responder)) => {
            Ok(stopping_state(responder, proxy, req_stream.into_future(), sender).into_state())
        }
        Some(ManualRequest::Exit(responder)) => {
            responder.send(()).unwrap_or_else(|_| ());
            Err(ExitReason(Ok(())))
        }
        None => {
            return Err(ExitReason(Err(format_err!(
                "The stream of user requests ended unexpectedly"
            ))))
        }
    }
}

/// In the starting state, a request to ApSmeProxy::Start is made.  If the start request fails,
/// the state machine exits with an error.  On success, the state machine transitions into the
/// started state to monitor the SME.
///
/// The starting state can be entered in the following ways.
/// 1. When the state machine is stopped and it is asked to start an AP.
/// 2. When the state machine is started and the AP fails.
///
/// The starting state can be exited in the following ways.
/// 1. When the start request finishes, transition into the started state.
/// 2. If the start request fails, exit the state machine along the error path.
async fn starting_state(
    proxy: fidl_sme::ApSmeProxy,
    next_req: NextReqFut,
    mut req: ApConfig,
    responder: Option<StartResponder>,
    sender: ApListenerMessageSender,
) -> Result<State, ExitReason> {
    // Apply default PHY, CBW, and channel settings.
    req.radio_config.phy.get_or_insert(DEFAULT_PHY);
    req.radio_config.cbw.get_or_insert(DEFAULT_CBW);
    req.radio_config.primary_chan.get_or_insert(DEFAULT_CHANNEL);

    // Send a stop request to ensure that the AP begin in an unstarting state.
    proxy.stop().await.map_err(|e| ExitReason(Err(anyhow::Error::from(e))))?;
    send_ap_stopped_update(&sender).map_err(|e| ExitReason(Err(anyhow::Error::from(e))))?;

    // Create an initial AP state
    let mut state =
        ApStateUpdate::new(req.id.clone(), types::OperatingState::Starting, req.mode, req.band);
    send_state_update(&sender, state.clone())
        .map_err(|e| ExitReason(Err(anyhow::Error::from(e))))?;

    let mut ap_config = fidl_sme::ApConfig::from(req.clone());
    let result = proxy.start(&mut ap_config).await;

    // If 'start' call to SME failed, return an error since we can't
    // recover from it
    let result = result.map_err(|e| {
        let mut state = state.clone();
        state.state = types::OperatingState::Failed;
        let _ = send_state_update(&sender, state);
        ExitReason(Err(format_err!("Failed to send a start command to wlanstack: {}", e)))
    })?;
    match responder {
        Some(responder) => responder.send(result).unwrap_or_else(|_| ()),
        None => {}
    }
    state.state = types::OperatingState::Active;
    send_state_update(&sender, state.clone())
        .map_err(|e| ExitReason(Err(anyhow::Error::from(e))))?;
    return Ok(started_state(proxy, next_req, req, state, sender).into_state());
}

/// In the stopping state, an ApSmeProxy::Stop is requested.  Once the stop request has been
/// processed by the ApSmeProxy, all requests to stop the AP are acknowledged.  The state machine
/// then transitions into the stopped state.
///
/// The stopping state can be entered in the following ways.
/// 1. When a manual stop request is made when the state machine is in the started state.
///
/// The stopping state can be exited in the following ways.
/// 1. When the request to stop the SME completes, the state machine will transition to the stopped
///    state.
/// 2. If an SME interaction fails, exits the state machine with an error.
async fn stopping_state(
    responder: oneshot::Sender<()>,
    proxy: fidl_sme::ApSmeProxy,
    next_req: NextReqFut,
    sender: ApListenerMessageSender,
) -> Result<State, ExitReason> {
    let result = match proxy.stop().await {
        Ok(fidl_sme::StopApResultCode::Success) => Ok(()),
        Ok(code) => Err(format_err!("Unexpected StopApResultCode: {:?}", code)),
        Err(e) => Err(format_err!("Failed to send a stop command to wlanstack: {}", e)),
    };
    result.map_err(|e| ExitReason(Err(e)))?;

    responder.send(()).unwrap_or_else(|_| ());
    send_ap_stopped_update(&sender).map_err(|e| ExitReason(Err(anyhow::Error::from(e))))?;

    Ok(stopped_state(proxy, next_req, sender).into_state())
}

async fn stopped_state(
    proxy: fidl_sme::ApSmeProxy,
    mut next_req: NextReqFut,
    sender: ApListenerMessageSender,
) -> Result<State, ExitReason> {
    // Wait for the next request from the caller
    loop {
        let (req, req_stream) = next_req.await;
        next_req = match req {
            // Immediately reply to stop requests indicating that the AP is already stopped
            Some(ManualRequest::Stop(responder)) => {
                responder.send(()).unwrap_or_else(|_| ());
                req_stream.into_future()
            }
            // All other requests are handled manually
            other => return perform_manual_request(proxy.clone(), other, req_stream, sender),
        }
    }
}

async fn started_state(
    proxy: fidl_sme::ApSmeProxy,
    mut next_req: NextReqFut,
    req: ApConfig,
    mut prev_state: ApStateUpdate,
    sender: ApListenerMessageSender,
) -> Result<State, ExitReason> {
    // Holds a pending status request.  Request status immediately upon entering the started state.
    let mut pending_status_req = FuturesUnordered::new();
    pending_status_req.push(proxy.status());

    let mut status_timer =
        fasync::Interval::new(zx::Duration::from_seconds(AP_STATUS_INTERVAL_SEC));

    // Channel bandwidth is required for frequency computation when reporting state updates.
    let cbw = req.radio_config.cbw.as_ref().map_or(Cbw::Cbw20, |cbw| *cbw);

    loop {
        select! {
            status_response = pending_status_req.select_next_some() => {
                let status_response = status_response
                    .map_err(|e| { ExitReason(Err(anyhow::Error::from(e))) })?;
                match status_response.running_ap {
                    Some(state) => {
                        let mut new_state = prev_state.clone();
                        consume_sme_status_update(&mut new_state, cbw, *state);
                        if new_state != prev_state {
                            prev_state = new_state;
                            send_state_update(&sender, prev_state.clone())
                                .map_err(|e| { ExitReason(Err(anyhow::Error::from(e))) })?;
                        }
                    }
                    None => {
                        prev_state.state = types::OperatingState::Failed;
                        send_state_update(&sender, prev_state)
                            .map_err(|e| { ExitReason(Err(anyhow::Error::from(e))) })?;

                        // The compiler detects a cycle on a direct transition back to the
                        // starting state.  This intermediate state allows the transition from
                        // started back to starting.
                        fn transition_from_started_to_starting(
                            proxy: fidl_sme::ApSmeProxy,
                            next_req: NextReqFut,
                            req: ApConfig,
                            sender: ApListenerMessageSender
                        ) -> Result<State, ExitReason> {
                            Ok(starting_state(proxy, next_req, req, None, sender).into_state())
                        }

                        return transition_from_started_to_starting(proxy, next_req, req, sender);
                    }
                }
            },
            _ = status_timer.select_next_some() => {
                if pending_status_req.is_empty() {
                    pending_status_req.push(proxy.clone().status());
                }
            },
            (req, req_stream) = next_req => {
                return perform_manual_request(proxy, req, req_stream, sender);
            },
            complete => {
                panic!("AP state machine terminated unexpectedly");
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::util::listener,
        fidl::endpoints::create_proxy,
        fidl_fuchsia_wlan_policy as fidl_policy,
        futures::{stream::StreamFuture, task::Poll, Future},
        pin_utils::pin_mut,
        wlan_common::{
            assert_variant,
            channel::{Cbw, Phy},
        },
    };

    struct TestValues {
        sme_proxy: fidl_sme::ApSmeProxy,
        sme_req_stream: fidl_sme::ApSmeRequestStream,
        ap_req_sender: mpsc::Sender<ManualRequest>,
        ap_req_stream: mpsc::Receiver<ManualRequest>,
        update_sender: mpsc::UnboundedSender<listener::ApMessage>,
        update_receiver: mpsc::UnboundedReceiver<listener::ApMessage>,
    }

    fn test_setup() -> TestValues {
        let (ap_req_sender, ap_req_stream) = mpsc::channel(1);
        let (update_sender, update_receiver) = mpsc::unbounded();
        let (sme_proxy, sme_server) =
            create_proxy::<fidl_sme::ApSmeMarker>().expect("failed to create an sme channel");
        let sme_req_stream = sme_server.into_stream().expect("could not create SME request stream");

        TestValues {
            sme_proxy,
            sme_req_stream,
            ap_req_sender,
            ap_req_stream,
            update_sender,
            update_receiver,
        }
    }

    fn create_network_id() -> types::NetworkIdentifier {
        types::NetworkIdentifier {
            ssid: b"test_ssid".to_vec(),
            type_: fidl_policy::SecurityType::None,
        }
    }

    fn poll_sme_req(
        exec: &mut fasync::Executor,
        next_sme_req: &mut StreamFuture<fidl_sme::ApSmeRequestStream>,
    ) -> Poll<fidl_sme::ApSmeRequest> {
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

    #[test]
    fn test_stop_during_started() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup();

        let radio_config = RadioConfig::new(Phy::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };
        let state = ApStateUpdate::new(
            create_network_id(),
            types::OperatingState::Starting,
            types::ConnectivityMode::Unrestricted,
            types::OperatingBand::Any,
        );

        // Run the started state and ignore the status request
        let fut = started_state(
            test_values.sme_proxy,
            test_values.ap_req_stream.into_future(),
            req,
            state,
            test_values.update_sender,
        );
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Status{ responder }) => {
                let ap_info = fidl_sme::Ap { ssid: vec![], channel: 0, num_clients: 0 };
                let mut response = fidl_sme::ApStatusResponse {
                    running_ap: Some(Box::new(ap_info))
                };
                responder.send(&mut response).expect("could not send AP status response");
            }
        );

        // Issue a stop request.
        let mut ap = AccessPoint::new(test_values.ap_req_sender);
        let (sender, mut receiver) = oneshot::channel();
        ap.stop(sender).expect("failed to make stop request");

        // Run the state machine and ensure that a stop request is issued by the SME proxy.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Stop{ responder }) => {
                responder.send(fidl_sme::StopApResultCode::Success).expect("could not send SME stop response");
            }
        );

        // Expect the responder to be acknowledged
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut receiver), Poll::Ready(Ok(())));
    }

    #[test]
    fn test_exit_during_started() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup();

        let radio_config = RadioConfig::new(Phy::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };
        let state = ApStateUpdate::new(
            create_network_id(),
            types::OperatingState::Starting,
            types::ConnectivityMode::Unrestricted,
            types::OperatingBand::Any,
        );

        // Run the started state and ignore the status request
        let fut = started_state(
            test_values.sme_proxy,
            test_values.ap_req_stream.into_future(),
            req,
            state,
            test_values.update_sender,
        );
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Status{ responder }) => {
                let ap_info = fidl_sme::Ap { ssid: vec![], channel: 0, num_clients: 0 };
                let mut response = fidl_sme::ApStatusResponse {
                    running_ap: Some(Box::new(ap_info))
                };
                responder.send(&mut response).expect("could not send AP status response");
            }
        );

        // Issue an exit request.
        let mut ap = AccessPoint::new(test_values.ap_req_sender);
        let (sender, mut receiver) = oneshot::channel();
        ap.exit(sender).expect("failed to make stop request");

        // Expect the responder to be acknowledged and the state machine to exit.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        assert_variant!(exec.run_until_stalled(&mut receiver), Poll::Ready(Ok(())));
    }

    #[test]
    fn test_start_during_started() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup();

        let radio_config = RadioConfig::new(Phy::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };
        let state = ApStateUpdate::new(
            create_network_id(),
            types::OperatingState::Starting,
            types::ConnectivityMode::Unrestricted,
            types::OperatingBand::Any,
        );

        // Run the started state and ignore the status request
        let fut = started_state(
            test_values.sme_proxy,
            test_values.ap_req_stream.into_future(),
            req,
            state,
            test_values.update_sender,
        );
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Status{ responder }) => {
                let ap_info = fidl_sme::Ap { ssid: vec![], channel: 0, num_clients: 0 };
                let mut response = fidl_sme::ApStatusResponse {
                    running_ap: Some(Box::new(ap_info))
                };
                responder.send(&mut response).expect("could not send AP status response");
            }
        );

        // Issue a start request.
        let mut ap = AccessPoint::new(test_values.ap_req_sender);
        let (sender, mut receiver) = oneshot::channel();
        let radio_config = RadioConfig::new(Phy::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };
        ap.start(req, sender).expect("failed to make stop request");

        // Expect that the state machine issues a stop request followed by a start request.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Stop{ responder }) => {
                responder.send(fidl_sme::StopApResultCode::Success).expect("could not send AP stop response");
            }
        );

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Start{ config: _, responder }) => {
                responder
                    .send(fidl_sme::StartApResultCode::Success)
                    .expect("could not send AP stop response");
            }
        );

        // Verify that the SME response is plumbed back to the caller.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut receiver),
            Poll::Ready(Ok(fidl_sme::StartApResultCode::Success))
        );
    }

    #[test]
    fn test_duplicate_status_during_started() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup();

        let radio_config = RadioConfig::new(Phy::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };
        let mut state = ApStateUpdate::new(
            create_network_id(),
            types::OperatingState::Starting,
            types::ConnectivityMode::Unrestricted,
            types::OperatingBand::Any,
        );
        state.frequency = Some(2437);
        state.clients = Some(ConnectedClientInformation { count: 0 });

        // Run the started state and send back an identical status.
        let fut = started_state(
            test_values.sme_proxy,
            test_values.ap_req_stream.into_future(),
            req,
            state,
            test_values.update_sender,
        );
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Status{ responder }) => {
                let ap_info = fidl_sme::Ap { ssid: vec![], channel: 6, num_clients: 0 };
                let mut response = fidl_sme::ApStatusResponse {
                    running_ap: Some(Box::new(ap_info))
                };
                responder.send(&mut response).expect("could not send AP status response");
            }
        );

        // Run the state machine and ensure no update has been sent.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut test_values.update_receiver.into_future()),
            Poll::Pending
        );
    }

    #[test]
    fn test_new_status_during_started() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup();

        let radio_config = RadioConfig::new(Phy::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };
        let mut state = ApStateUpdate::new(
            create_network_id(),
            types::OperatingState::Starting,
            types::ConnectivityMode::Unrestricted,
            types::OperatingBand::Any,
        );
        state.frequency = Some(0);
        state.clients = Some(ConnectedClientInformation { count: 0 });

        // Run the started state and send back an identical status.
        let fut = started_state(
            test_values.sme_proxy,
            test_values.ap_req_stream.into_future(),
            req,
            state,
            test_values.update_sender,
        );
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Status{ responder }) => {
                let ap_info = fidl_sme::Ap { ssid: vec![], channel: 0, num_clients: 1 };
                let mut response = fidl_sme::ApStatusResponse {
                    running_ap: Some(Box::new(ap_info))
                };
                responder.send(&mut response).expect("could not send AP status response");
            }
        );

        // Run the state machine and ensure an update has been sent.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut test_values.update_receiver.into_future()),
            Poll::Ready((Some(listener::Message::NotifyListeners(updates)), _)) => {
                assert!(!updates.access_points.is_empty());
        });
    }

    #[test]
    fn test_sme_failure_during_started() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup();

        // Drop the serving side of the SME so that a status request will result in an error.
        drop(test_values.sme_req_stream);

        let radio_config = RadioConfig::new(Phy::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };
        let mut state = ApStateUpdate::new(
            create_network_id(),
            types::OperatingState::Starting,
            types::ConnectivityMode::Unrestricted,
            types::OperatingBand::Any,
        );
        state.frequency = Some(0);
        state.clients = Some(ConnectedClientInformation { count: 0 });

        // Run the started state and send back an identical status.
        let fut = started_state(
            test_values.sme_proxy,
            test_values.ap_req_stream.into_future(),
            req,
            state,
            test_values.update_sender,
        );
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        // The state machine should exit when it is unable to query status.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
    }

    #[test]
    fn test_stop_while_stopped() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup();

        // Run the stopped state.
        let fut = stopped_state(
            test_values.sme_proxy,
            test_values.ap_req_stream.into_future(),
            test_values.update_sender,
        );
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Issue a stop request.
        let mut ap = AccessPoint::new(test_values.ap_req_sender);
        let (sender, mut receiver) = oneshot::channel();
        ap.stop(sender).expect("failed to make stop request");

        // Expect the responder to be acknowledged immediately.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut receiver), Poll::Ready(Ok(())));
    }

    #[test]
    fn test_exit_while_stopped() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup();

        // Run the stopped state.
        let fut = stopped_state(
            test_values.sme_proxy,
            test_values.ap_req_stream.into_future(),
            test_values.update_sender,
        );
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        // Issue an exit request.
        let mut ap = AccessPoint::new(test_values.ap_req_sender);
        let (sender, mut receiver) = oneshot::channel();
        ap.exit(sender).expect("failed to make stop request");

        // Expect the responder to be acknowledged and the state machine to exit.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        assert_variant!(exec.run_until_stalled(&mut receiver), Poll::Ready(Ok(())));
    }

    #[test]
    fn test_start_while_stopped() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        // Run the stopped state.
        let fut = stopped_state(
            test_values.sme_proxy,
            test_values.ap_req_stream.into_future(),
            test_values.update_sender,
        );
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        // Issue a start request.
        let (sender, mut receiver) = oneshot::channel();
        let radio_config = RadioConfig::new(Phy::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };

        let mut ap = AccessPoint::new(test_values.ap_req_sender);
        ap.start(req, sender).expect("failed to make stop request");

        // Expect that the state machine issues a stop request.
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Stop{ responder }) => {
                responder.send(fidl_sme::StopApResultCode::Success).expect("could not send AP stop response");
            }
        );

        // An empty update should be sent after stopping.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert!(updates.access_points.is_empty());
        });

        // The empty update should be quickly followed by a starting update.
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(mut updates))) => {
            let update = updates.access_points.pop().expect("no new updates available.");
            assert_eq!(update.state, types::OperatingState::Starting);
        });

        // A start request should have been issues to the SME proxy.
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Start{ config: _, responder }) => {
                responder
                    .send(fidl_sme::StartApResultCode::Success)
                    .expect("could not send AP stop response");
            }
        );

        // Verify that the SME response is plumbed back to the caller.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut receiver),
            Poll::Ready(Ok(fidl_sme::StartApResultCode::Success))
        );

        // There should be a pending active state notification
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(mut updates))) => {
            let update = updates.access_points.pop().expect("no new updates available.");
            assert_eq!(update.state, types::OperatingState::Active);
        });
    }

    #[test]
    fn test_exit_while_stopping() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup();

        // Run the stopping state.
        let (stop_sender, mut stop_receiver) = oneshot::channel();
        let fut = stopping_state(
            stop_sender,
            test_values.sme_proxy,
            test_values.ap_req_stream.into_future(),
            test_values.update_sender,
        );
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        // Issue an exit request.
        let mut ap = AccessPoint::new(test_values.ap_req_sender);
        let (exit_sender, mut exit_receiver) = oneshot::channel();
        ap.exit(exit_sender).expect("failed to make stop request");

        // While stopping is still in progress, exit and stop should not be responded to yet.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut stop_receiver), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut exit_receiver), Poll::Pending);

        // Once stop AP request is finished, the state machine can terminate
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Stop{ responder }) => {
                responder.send(fidl_sme::StopApResultCode::Success).expect("could not send AP stop response");
            }
        );
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        assert_variant!(exec.run_until_stalled(&mut stop_receiver), Poll::Ready(Ok(())));
        assert_variant!(exec.run_until_stalled(&mut exit_receiver), Poll::Ready(Ok(())));
    }

    #[test]
    fn test_stop_while_stopping() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        // Run the stopping state.
        let (stop_sender, mut stop_receiver) = oneshot::channel();
        let fut = stopping_state(
            stop_sender,
            test_values.sme_proxy,
            test_values.ap_req_stream.into_future(),
            test_values.update_sender,
        );
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        // Verify that no state update is ready yet.
        assert_variant!(&mut test_values.update_receiver.try_next(), Err(_));

        // Issue a stop request.
        let mut ap = AccessPoint::new(test_values.ap_req_sender);
        let (second_stop_sender, mut second_stop_receiver) = oneshot::channel();
        ap.stop(second_stop_sender).expect("failed to make stop request");

        // Expect the stop request from the SME proxy
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Stop{ responder }) => {
                responder.send(fidl_sme::StopApResultCode::Success).expect("could not send AP stop response");
            }
        );

        // Expect both responders to be acknowledged.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut stop_receiver), Poll::Ready(Ok(())));
        assert_variant!(exec.run_until_stalled(&mut second_stop_receiver), Poll::Ready(Ok(())));

        // There should be a new update indicating that no AP's are active.
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert!(updates.access_points.is_empty());
        });
    }

    #[test]
    fn test_start_while_stopping() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup();

        // Run the stopping state.
        let (stop_sender, mut stop_receiver) = oneshot::channel();
        let fut = stopping_state(
            stop_sender,
            test_values.sme_proxy,
            test_values.ap_req_stream.into_future(),
            test_values.update_sender,
        );
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        // Issue a start request.
        let (start_sender, mut start_receiver) = oneshot::channel();
        let radio_config = RadioConfig::new(Phy::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };

        let mut ap = AccessPoint::new(test_values.ap_req_sender);
        ap.start(req, start_sender).expect("failed to make stop request");

        // The state machine should not respond to the stop request yet until it's finished.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut stop_receiver), Poll::Pending);
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);
        let stop_responder = assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Stop{ responder }) => responder
        );

        // The state machine should not send new request yet since stop is still unfinished
        assert_variant!(poll_sme_req(&mut exec, &mut sme_fut), Poll::Pending);

        // After SME sends response, the state machine can proceed
        stop_responder
            .send(fidl_sme::StopApResultCode::Success)
            .expect("could not send AP stop response");
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut stop_receiver), Poll::Ready(Ok(())));

        // Expect another stop request from the state machine entering the starting state.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Stop{ responder }) => {
                responder.send(fidl_sme::StopApResultCode::Success).expect("could not send AP stop response");
            }
        );

        // Expect a start request
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Start{ config: _, responder }) => {
                responder
                    .send(fidl_sme::StartApResultCode::Success)
                    .expect("could not send AP stop response");
            }
        );

        // Expect the start responder to be acknowledged
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut start_receiver),
            Poll::Ready(Ok(fidl_sme::StartApResultCode::Success))
        );
    }

    #[test]
    fn test_sme_failure_while_stopping() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup();

        // Drop the serving side of the SME so that the stop request will result in an error.
        drop(test_values.sme_req_stream);

        // Run the stopping state.
        let (stop_sender, mut stop_receiver) = oneshot::channel();
        let fut = stopping_state(
            stop_sender,
            test_values.sme_proxy,
            test_values.ap_req_stream.into_future(),
            test_values.update_sender,
        );
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        // The state machine should exit when it is unable to issue the stop command.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        assert_variant!(exec.run_until_stalled(&mut stop_receiver), Poll::Ready(Err(_)));
    }

    #[test]
    fn test_failed_result_code_while_stopping() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        // Run the stopping state.
        let (stop_sender, mut stop_receiver) = oneshot::channel();
        let fut = stopping_state(
            stop_sender,
            test_values.sme_proxy,
            test_values.ap_req_stream.into_future(),
            test_values.update_sender,
        );
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        // Verify that no state update is ready yet.
        assert_variant!(&mut test_values.update_receiver.try_next(), Err(_));

        // Expect the stop request from the SME proxy
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Stop{ responder }) => {
                responder.send(fidl_sme::StopApResultCode::InternalError).expect("could not send AP stop response");
            }
        );

        // The state machine should exit.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        assert_variant!(exec.run_until_stalled(&mut stop_receiver), Poll::Ready(Err(_)));
    }

    #[test]
    fn test_stop_while_starting() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup();

        let (start_sender, mut start_receiver) = oneshot::channel();
        let radio_config = RadioConfig::new(Phy::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };

        // Start off in the starting state
        let fut = starting_state(
            test_values.sme_proxy,
            test_values.ap_req_stream.into_future(),
            req,
            Some(start_sender),
            test_values.update_sender,
        );
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        // Handle the initial disconnect request
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Stop{ responder }) => {
                responder.send(fidl_sme::StopApResultCode::Success).expect("could not send AP stop response");
            }
        );

        // Wait for a start request, but don't reply to it yet.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        let start_responder = assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Start{ config: _, responder }) => {
                responder
            }
        );

        // Issue a stop request.
        let mut ap = AccessPoint::new(test_values.ap_req_sender);
        let (stop_sender, mut stop_receiver) = oneshot::channel();
        ap.stop(stop_sender).expect("failed to make stop request");

        // Run the state machine and ensure that a stop request is not issued by the SME proxy yet.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(poll_sme_req(&mut exec, &mut sme_fut), Poll::Pending);

        // After SME responds to start request, the state machine can continue
        start_responder
            .send(fidl_sme::StartApResultCode::Success)
            .expect("could not send SME start response");
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut start_receiver),
            Poll::Ready(Ok(fidl_sme::StartApResultCode::Success))
        );

        // When state machine goes to started state, it issues a status request. Ignore it.
        let _status_responder = assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Status{ responder }) => responder
        );

        // Stop request should be issued now to SME
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Stop{ responder }) => {
                responder.send(fidl_sme::StopApResultCode::Success).expect("could not send SME stop response");
            }
        );

        // Expect the responder to be acknowledged
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut stop_receiver), Poll::Ready(Ok(())));
    }

    #[test]
    fn test_start_while_starting() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup();

        let (start_sender, mut start_receiver) = oneshot::channel();
        let radio_config = RadioConfig::new(Phy::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };

        // Start off in the starting state
        let fut = starting_state(
            test_values.sme_proxy,
            test_values.ap_req_stream.into_future(),
            req,
            Some(start_sender),
            test_values.update_sender,
        );
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        // Handle the initial disconnect request
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Stop{ responder }) => {
                responder.send(fidl_sme::StopApResultCode::Success).expect("could not send AP stop response");
            }
        );

        // Wait for a start request, but don't reply to it.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        let start_responder = assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Start{ config: _, responder }) => {
                responder
            }
        );

        // Issue a second start request.
        let (second_start_sender, mut second_start_receiver) = oneshot::channel();
        let radio_config = RadioConfig::new(Phy::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };
        let mut ap = AccessPoint::new(test_values.ap_req_sender);
        ap.start(req, second_start_sender).expect("failed to make start request");

        // Run the state machine and ensure that the first start request is still pending.
        // Furthermore, no new start request is issued yet.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut start_receiver), Poll::Pending);
        assert_variant!(poll_sme_req(&mut exec, &mut sme_fut), Poll::Pending);

        // Respond to the first start request
        start_responder
            .send(fidl_sme::StartApResultCode::Success)
            .expect("failed to send start response");

        // The first request should receive the acknowledgement, the second one shouldn't.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut start_receiver),
            Poll::Ready(Ok(fidl_sme::StartApResultCode::Success))
        );
        assert_variant!(exec.run_until_stalled(&mut second_start_receiver), Poll::Pending);

        // The state machine checks for status to make sure AP is started
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Status{ responder }) => {
                let ap_info = fidl_sme::Ap { ssid: vec![], channel: 0, num_clients: 0 };
                let mut response = fidl_sme::ApStatusResponse {
                    running_ap: Some(Box::new(ap_info))
                };
                responder.send(&mut response).expect("could not send AP status response");
            }
        );

        // The state machine should transition back into the starting state and issue a stop
        // request, due to the second start request.
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Stop{ responder }) => {
                responder.send(fidl_sme::StopApResultCode::Success).expect("could not send SME stop response");
            }
        );

        // The state machine should then issue a start request.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Start{ config: _, responder }) => {
                responder
                    .send(fidl_sme::StartApResultCode::Success)
                    .expect("failed to send start response");
            }
        );

        // The second start request should receive the acknowledgement now.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut second_start_receiver),
            Poll::Ready(Ok(fidl_sme::StartApResultCode::Success))
        );
    }

    #[test]
    fn test_exit_while_starting() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup();

        let (start_sender, mut start_receiver) = oneshot::channel();
        let radio_config = RadioConfig::new(Phy::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };

        // Start off in the starting state
        let fut = starting_state(
            test_values.sme_proxy,
            test_values.ap_req_stream.into_future(),
            req,
            Some(start_sender),
            test_values.update_sender,
        );
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        // Handle the initial disconnect request
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Stop{ responder }) => {
                responder.send(fidl_sme::StopApResultCode::Success).expect("could not send AP stop response");
            }
        );

        // Wait for a start request, but don't reply to it.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        let start_responder = assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Start{ config: _, responder }) => {
                responder
            }
        );

        // Issue an exit request.
        let mut ap = AccessPoint::new(test_values.ap_req_sender);
        let (exit_sender, mut exit_receiver) = oneshot::channel();
        ap.exit(exit_sender).expect("failed to make stop request");

        // While starting is still in progress, exit and start should not be responded to yet.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut start_receiver), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut exit_receiver), Poll::Pending);

        // Once start AP request is finished, the state machine can terminate
        start_responder
            .send(fidl_sme::StartApResultCode::Success)
            .expect("could not send AP start response");

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        assert_variant!(exec.run_until_stalled(&mut exit_receiver), Poll::Ready(Ok(())));
        assert_variant!(
            exec.run_until_stalled(&mut start_receiver),
            Poll::Ready(Ok(fidl_sme::StartApResultCode::Success))
        );
    }

    #[test]
    fn test_sme_breaks_while_starting() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup();

        // Drop the serving side of the SME so that client requests fail.
        drop(test_values.sme_req_stream);

        let (start_sender, _start_receiver) = oneshot::channel();
        let radio_config = RadioConfig::new(Phy::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };

        // Start off in the starting state
        let fut = starting_state(
            test_values.sme_proxy,
            test_values.ap_req_stream.into_future(),
            req,
            Some(start_sender),
            test_values.update_sender,
        );
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        // Run the state machine and expect it to exit
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
    }

    #[test]
    fn test_serve_does_not_terminate_right_away() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let sme_event_stream = test_values.sme_proxy.take_event_stream();
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        let fut = serve(
            0,
            test_values.sme_proxy,
            sme_event_stream,
            test_values.ap_req_stream,
            test_values.update_sender,
        );
        pin_mut!(fut);

        // Run the state machine. No request is made initially.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(poll_sme_req(&mut exec, &mut sme_fut), Poll::Pending);
    }
}
