// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        access_point::types,
        telemetry::{TelemetryEvent, TelemetrySender},
        util::{
            listener::{
                ApListenerMessageSender, ApStateUpdate, ApStatesUpdate, ConnectedClientInformation,
                Message::NotifyListeners,
            },
            state_machine::{self, ExitReason, IntoStateExt},
        },
    },
    anyhow::format_err,
    fidl_fuchsia_wlan_sme as fidl_sme,
    fuchsia_async::{self as fasync, DurationExt},
    fuchsia_zircon::{self as zx, DurationNum},
    futures::{
        channel::{mpsc, oneshot},
        future::FutureExt,
        select,
        stream::{self, Fuse, FuturesUnordered, StreamExt, TryStreamExt},
    },
    log::info,
    parking_lot::Mutex,
    std::sync::Arc,
    void::ResultVoidErrExt,
    wlan_common::{
        channel::{Cbw, Channel},
        RadioConfig,
    },
};

const AP_STATUS_INTERVAL_SEC: i64 = 10;

// If a scan is occurring on a PHY and that same PHY is asked to start an AP, the request to start
// the AP will likely fail.  Scans are allowed a maximum to 10s to complete.  The timeout is
// defined in //src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/cfg80211.h as
//
// #define BRCMF_ESCAN_TIMER_INTERVAL_MS 10000 /* E-Scan timeout */
//
// As such, a minimum of 10s worth of retries should be allowed when starting the soft AP.  Allow
// 12s worth of retries to ensure adequate time for the scan to finish.
const AP_START_RETRY_INTERVAL: i64 = 2;
const AP_START_MAX_RETRIES: u16 = 6;

type State = state_machine::State<ExitReason>;
type ReqStream = stream::Fuse<mpsc::Receiver<ManualRequest>>;

pub trait AccessPointApi {
    fn start(
        &mut self,
        request: ApConfig,
        responder: oneshot::Sender<()>,
    ) -> Result<(), anyhow::Error>;
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
    fn start(
        &mut self,
        request: ApConfig,
        responder: oneshot::Sender<()>,
    ) -> Result<(), anyhow::Error> {
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
    Start((ApConfig, oneshot::Sender<()>)),
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
            ssid: config.id.ssid.to_vec(),
            password: config.credential,
            radio_cfg: config.radio_config.into(),
        }
    }
}

struct ApStateTrackerInner {
    state: Option<ApStateUpdate>,
    sender: ApListenerMessageSender,
}

impl ApStateTrackerInner {
    fn send_update(&mut self) -> Result<(), anyhow::Error> {
        let updates = match self.state.clone() {
            Some(state) => ApStatesUpdate { access_points: [state].to_vec() },
            None => ApStatesUpdate { access_points: [].to_vec() },
        };

        self.sender
            .clone()
            .unbounded_send(NotifyListeners(updates))
            .or_else(|e| Err(format_err!("failed to send state update: {}", e)))
    }
}

struct ApStateTracker {
    inner: Mutex<ApStateTrackerInner>,
}

impl ApStateTracker {
    fn new(sender: ApListenerMessageSender) -> Self {
        ApStateTracker { inner: Mutex::new(ApStateTrackerInner { state: None, sender }) }
    }

    fn reset_state(&self, state: ApStateUpdate) -> Result<(), anyhow::Error> {
        let mut inner = self.inner.lock();
        inner.state = Some(state);
        inner.send_update()
    }

    fn consume_sme_status_update(
        &self,
        cbw: Cbw,
        update: fidl_sme::Ap,
    ) -> Result<(), anyhow::Error> {
        let mut inner = self.inner.lock();

        if let Some(ref mut state) = inner.state {
            let channel = Channel::new(update.channel, cbw);
            let frequency = match channel.get_center_freq() {
                Ok(frequency) => Some(frequency as u32),
                Err(e) => {
                    info!("failed to convert channel to frequency: {}", e);
                    None
                }
            };

            let client_info = Some(ConnectedClientInformation { count: update.num_clients as u8 });

            if frequency != state.frequency || client_info != state.clients {
                state.frequency = frequency;
                state.clients = client_info;
                inner.send_update()?;
            }
        }

        Ok(())
    }

    fn update_operating_state(
        &self,
        new_state: types::OperatingState,
    ) -> Result<(), anyhow::Error> {
        let mut inner = self.inner.lock();

        // If there is a new operating state, update the existing operating state if present.
        if let Some(ref mut state) = inner.state {
            if state.state != new_state {
                state.state = new_state;
            }
            inner.send_update()?;
        }

        Ok(())
    }

    fn set_stopped_state(&self) -> Result<(), anyhow::Error> {
        let mut inner = self.inner.lock();
        inner.state = None;
        inner.send_update()
    }
}

struct CommonStateDependencies {
    proxy: fidl_sme::ApSmeProxy,
    req_stream: ReqStream,
    state_tracker: Arc<ApStateTracker>,
    telemetry_sender: TelemetrySender,
}

pub async fn serve(
    iface_id: u16,
    proxy: fidl_sme::ApSmeProxy,
    sme_event_stream: fidl_sme::ApSmeEventStream,
    req_stream: Fuse<mpsc::Receiver<ManualRequest>>,
    message_sender: ApListenerMessageSender,
    telemetry_sender: TelemetrySender,
) {
    let state_tracker = Arc::new(ApStateTracker::new(message_sender));
    let deps = CommonStateDependencies {
        proxy,
        req_stream,
        state_tracker: state_tracker.clone(),
        telemetry_sender,
    };
    let state_machine = stopped_state(deps).into_state_machine();
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
        removal_watcher = removal_watcher.fuse() => {
            match removal_watcher {
                Ok(()) => info!("AP interface was unexpectedly removed: {}", iface_id),
                Err(e) => {
                    info!("Error reading from AP SME channel of iface #{}: {}", iface_id, e);
                }
            }
            let _ = state_tracker.update_operating_state(types::OperatingState::Failed);
        }
    }
}

fn perform_manual_request(
    deps: CommonStateDependencies,
    req: Option<ManualRequest>,
) -> Result<State, ExitReason> {
    match req {
        Some(ManualRequest::Start((req, responder))) => {
            Ok(starting_state(deps, req, AP_START_MAX_RETRIES, Some(responder)).into_state())
        }
        Some(ManualRequest::Stop(responder)) => Ok(stopping_state(deps, responder).into_state()),
        Some(ManualRequest::Exit(responder)) => {
            responder.send(()).unwrap_or_else(|_| ());
            Err(ExitReason(Ok(())))
        }
        None => {
            // It is possible that the state machine will be cleaned up before it has the
            // opportunity to realize that the SME is no longer functional.  In this scenario,
            // listeners need to be notified of the failure.
            deps.state_tracker
                .update_operating_state(types::OperatingState::Failed)
                .map_err(|e| ExitReason(Err(e)))?;

            return Err(ExitReason(Err(format_err!(
                "The stream of user requests ended unexpectedly"
            ))));
        }
    }
}

// This intermediate state supresses a compiler warning on detection of a cycle.
fn transition_to_starting(
    deps: CommonStateDependencies,
    req: ApConfig,
    remaining_retries: u16,
    responder: Option<oneshot::Sender<()>>,
) -> Result<State, ExitReason> {
    Ok(starting_state(deps, req, remaining_retries, responder).into_state())
}

/// In the starting state, a request to ApSmeProxy::Start is made.  If the start request fails,
/// the state machine exits with an error.  On success, the state machine transitions into the
/// started state to monitor the SME.
///
/// The starting state can be entered in the following ways.
/// 1. When the state machine is stopped and it is asked to start an AP.
/// 2. When the state machine is started and the AP fails.
/// 3. When retrying a failed start attempt.
///
/// The starting state can be exited in the following ways.
/// 1. If stopping the AP SME fails, exit the state machine.  The stop operation should be a very
///    brief interaction with the firmware.  Failure can only occur if the SME layer times out the
///    operation or the driver crashes.  Either scenario should be considered fatal.
/// 2. If the start request fails because the AP state machine cannot communicate with the SME,
///    the state machine will exit with an error.
/// 3. If the start request fails due to an error reported by the SME, it's possible that a client
///    interface associated with the same PHY is scanning.  In this case, allow the operation to be
///    retried by transitioning back through the starting state.  Once the retries are exhausted,
///    exit the state machine with an error.
/// 4. When the start request finishes, transition into the started state.
async fn starting_state(
    mut deps: CommonStateDependencies,
    req: ApConfig,
    remaining_retries: u16,
    responder: Option<oneshot::Sender<()>>,
) -> Result<State, ExitReason> {
    // Send a stop request to ensure that the AP begins in an unstarting state.
    let stop_result = match deps.proxy.stop().await {
        Ok(fidl_sme::StopApResultCode::Success) => Ok(()),
        Ok(code) => Err(format_err!("Unexpected StopApResultCode: {:?}", code)),
        Err(e) => Err(format_err!("Failed to send a stop command to wlanstack: {}", e)),
    };

    // If the stop operation failed, send a failure update and exit the state machine.
    if stop_result.is_err() {
        deps.state_tracker
            .reset_state(ApStateUpdate::new(
                req.id.clone(),
                types::OperatingState::Failed,
                req.mode,
                req.band,
            ))
            .map_err(|e| ExitReason(Err(e)))?;

        stop_result.map_err(|e| ExitReason(Err(e)))?;
    }

    // If the stop operation was successful, update all listeners that the AP is stopped.
    deps.state_tracker.set_stopped_state().map_err(|e| ExitReason(Err(e)))?;

    // Update all listeners that a new AP is starting if this is the first attempt to start the AP.
    if remaining_retries == AP_START_MAX_RETRIES {
        deps.state_tracker
            .reset_state(ApStateUpdate::new(
                req.id.clone(),
                types::OperatingState::Starting,
                req.mode,
                req.band,
            ))
            .map_err(|e| ExitReason(Err(e)))?;
    }

    let mut ap_config = fidl_sme::ApConfig::from(req.clone());
    let start_result = match deps.proxy.start(&mut ap_config).await {
        Ok(fidl_sme::StartApResultCode::Success) => Ok(()),
        Ok(code) => {
            // Log a metric indicating that starting the AP failed.
            deps.telemetry_sender.send(TelemetryEvent::ApStartFailure);

            // For any non-Success response, attempt to retry the start operation.  A successful
            // stop operation followed by an unsuccessful start operation likely indicates that the
            // PHY associated with this AP interface is busy scanning.  A future attempt to start
            // may succeed.
            if remaining_retries > 0 {
                let retry_timer = fasync::Timer::new(AP_START_RETRY_INTERVAL.seconds().after_now());

                // To ensure that the state machine remains responsive, process any incoming
                // requests while waiting for the timer to expire.
                select! {
                    () = retry_timer.fuse() => {
                        return transition_to_starting(
                            deps,
                            req,
                            remaining_retries - 1,
                            responder,
                        );
                    },
                    req = deps.req_stream.next() => {
                        // If a new request comes in, clear out the current AP state.
                        deps.state_tracker
                            .set_stopped_state()
                            .map_err(|e| ExitReason(Err(e)))?;
                        return perform_manual_request(
                            deps,
                            req,
                        );
                    }
                }
            }

            // Return an error if all retries have been exhausted.
            Err(format_err!("Failed to start AP: {:?}", code))
        }
        Err(e) => {
            // If communicating with the SME fails, further attempts to start the AP are guaranteed
            // to fail.
            Err(format_err!("Failed to send a start command to wlanstack: {}", e))
        }
    };

    start_result.map_err(|e| {
        // Send a failure notification.
        if let Err(e) = deps.state_tracker.reset_state(ApStateUpdate::new(
            req.id.clone(),
            types::OperatingState::Failed,
            req.mode,
            req.band,
        )) {
            info!("Unable to notify listeners of AP start failure: {:?}", e);
        }
        ExitReason(Err(e))
    })?;

    match responder {
        Some(responder) => responder.send(()).unwrap_or_else(|_| ()),
        None => {}
    }

    deps.state_tracker
        .update_operating_state(types::OperatingState::Active)
        .map_err(|e| ExitReason(Err(e)))?;
    return Ok(started_state(deps, req).into_state());
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
    deps: CommonStateDependencies,
    responder: oneshot::Sender<()>,
) -> Result<State, ExitReason> {
    let result = match deps.proxy.stop().await {
        Ok(fidl_sme::StopApResultCode::Success) => Ok(()),
        Ok(code) => Err(format_err!("Unexpected StopApResultCode: {:?}", code)),
        Err(e) => Err(format_err!("Failed to send a stop command to wlanstack: {}", e)),
    };

    // If the stop command fails, the SME is probably unusable.  If the state is not updated before
    // evaluating the stop result code, the AP state updates may end up with a lingering reference
    // to a started or starting AP.
    deps.state_tracker.set_stopped_state().map_err(|e| ExitReason(Err(e)))?;
    result.map_err(|e| ExitReason(Err(e)))?;

    // Ack the request to stop the AP.
    responder.send(()).unwrap_or_else(|_| ());

    Ok(stopped_state(deps).into_state())
}

async fn stopped_state(mut deps: CommonStateDependencies) -> Result<State, ExitReason> {
    // Wait for the next request from the caller
    loop {
        let req = deps.req_stream.next().await;
        match req {
            // Immediately reply to stop requests indicating that the AP is already stopped
            Some(ManualRequest::Stop(responder)) => {
                responder.send(()).unwrap_or_else(|_| ());
            }
            // All other requests are handled manually
            other => return perform_manual_request(deps, other),
        }
    }
}

async fn started_state(
    mut deps: CommonStateDependencies,
    req: ApConfig,
) -> Result<State, ExitReason> {
    // Holds a pending status request.  Request status immediately upon entering the started state.
    let mut pending_status_req = FuturesUnordered::new();
    pending_status_req.push(deps.proxy.status());

    let mut status_timer =
        fasync::Interval::new(zx::Duration::from_seconds(AP_STATUS_INTERVAL_SEC));

    // Channel bandwidth is required for frequency computation when reporting state updates.
    let cbw = req.radio_config.channel.cbw;

    loop {
        select! {
            status_response = pending_status_req.select_next_some() => {
                let status_response = match status_response {
                    Ok(status_response) => status_response,
                    Err(e) => {
                        // If querying AP status fails, notify listeners and exit the state
                        // machine.
                        deps.state_tracker.update_operating_state(types::OperatingState::Failed)
                            .map_err(|e| { ExitReason(Err(e)) })?;

                        return Err(ExitReason(Err(anyhow::Error::from(e))));
                    }
                };

                match status_response.running_ap {
                    Some(sme_state) => {
                        deps.state_tracker.consume_sme_status_update(cbw, *sme_state)
                            .map_err(|e| { ExitReason(Err(e)) })?;
                    }
                    None => {
                        deps.state_tracker.update_operating_state(types::OperatingState::Failed)
                            .map_err(|e| { ExitReason(Err(e)) })?;

                        return transition_to_starting(
                            deps,
                            req,
                            AP_START_MAX_RETRIES,
                            None,
                        );
                    }
                }
            },
            _ = status_timer.select_next_some() => {
                if pending_status_req.is_empty() {
                    pending_status_req.push(deps.proxy.clone().status());
                }
            },
            req = deps.req_stream.next() => {
                return perform_manual_request(
                    deps,
                    req,
                );
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
        fidl_fuchsia_wlan_common as fidl_common,
        futures::{stream::StreamFuture, task::Poll, Future},
        pin_utils::pin_mut,
        std::convert::TryFrom,
        wlan_common::{assert_variant, channel::Cbw},
    };

    struct TestValues {
        deps: CommonStateDependencies,
        sme_req_stream: fidl_sme::ApSmeRequestStream,
        ap_req_sender: mpsc::Sender<ManualRequest>,
        update_receiver: mpsc::UnboundedReceiver<listener::ApMessage>,
        telemetry_receiver: mpsc::Receiver<TelemetryEvent>,
    }

    fn test_setup() -> TestValues {
        let (ap_req_sender, ap_req_stream) = mpsc::channel(1);
        let (update_sender, update_receiver) = mpsc::unbounded();
        let (sme_proxy, sme_server) =
            create_proxy::<fidl_sme::ApSmeMarker>().expect("failed to create an sme channel");
        let sme_req_stream = sme_server.into_stream().expect("could not create SME request stream");
        let (telemetry_sender, telemetry_receiver) = mpsc::channel(100);
        let telemetry_sender = TelemetrySender::new(telemetry_sender);

        let deps = CommonStateDependencies {
            proxy: sme_proxy,
            req_stream: ap_req_stream.fuse(),
            state_tracker: Arc::new(ApStateTracker::new(update_sender)),
            telemetry_sender,
        };

        TestValues { deps, sme_req_stream, ap_req_sender, update_receiver, telemetry_receiver }
    }

    fn create_network_id() -> types::NetworkIdentifier {
        types::NetworkIdentifier {
            ssid: types::Ssid::try_from("test_ssid").unwrap(),
            security_type: types::SecurityType::None,
        }
    }

    fn poll_sme_req(
        exec: &mut fasync::TestExecutor,
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
            _state_machine = state_machine.fuse() => return,
        }
    }

    #[fuchsia::test]
    fn test_stop_during_started() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();

        let radio_config = RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };
        {
            let state = ApStateUpdate::new(
                create_network_id(),
                types::OperatingState::Starting,
                types::ConnectivityMode::Unrestricted,
                types::OperatingBand::Any,
            );
            test_values.deps.state_tracker.inner.lock().state = Some(state);
        }

        // Run the started state and ignore the status request
        let fut = started_state(test_values.deps, req);
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

    #[fuchsia::test]
    fn test_exit_during_started() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();

        let radio_config = RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };
        {
            let state = ApStateUpdate::new(
                create_network_id(),
                types::OperatingState::Starting,
                types::ConnectivityMode::Unrestricted,
                types::OperatingBand::Any,
            );
            test_values.deps.state_tracker.inner.lock().state = Some(state);
        }

        // Run the started state and ignore the status request
        let fut = started_state(test_values.deps, req);
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

    #[fuchsia::test]
    fn test_start_during_started() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();

        let radio_config = RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };
        {
            let state = ApStateUpdate::new(
                create_network_id(),
                types::OperatingState::Starting,
                types::ConnectivityMode::Unrestricted,
                types::OperatingBand::Any,
            );
            test_values.deps.state_tracker.inner.lock().state = Some(state);
        }

        // Run the started state and ignore the status request
        let fut = started_state(test_values.deps, req);
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
        let radio_config = RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, 6);
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
        assert_variant!(exec.run_until_stalled(&mut receiver), Poll::Ready(Ok(())));
    }

    #[fuchsia::test]
    fn test_duplicate_status_during_started() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();

        let radio_config = RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };
        {
            let mut state = ApStateUpdate::new(
                create_network_id(),
                types::OperatingState::Starting,
                types::ConnectivityMode::Unrestricted,
                types::OperatingBand::Any,
            );
            state.frequency = Some(2437);
            state.clients = Some(ConnectedClientInformation { count: 0 });
            test_values.deps.state_tracker.inner.lock().state = Some(state);
        }

        // Run the started state and send back an identical status.
        let fut = started_state(test_values.deps, req);
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

    #[fuchsia::test]
    fn test_new_status_during_started() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();

        let radio_config = RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };
        {
            let mut state = ApStateUpdate::new(
                create_network_id(),
                types::OperatingState::Starting,
                types::ConnectivityMode::Unrestricted,
                types::OperatingBand::Any,
            );
            state.frequency = Some(0);
            state.clients = Some(ConnectedClientInformation { count: 0 });
            test_values.deps.state_tracker.inner.lock().state = Some(state);
        }

        // Run the started state and send back an identical status.
        let fut = started_state(test_values.deps, req);
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

    #[fuchsia::test]
    fn test_sme_failure_during_started() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        // Drop the serving side of the SME so that a status request will result in an error.
        drop(test_values.sme_req_stream);

        let radio_config = RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };
        {
            let mut state = ApStateUpdate::new(
                create_network_id(),
                types::OperatingState::Starting,
                types::ConnectivityMode::Unrestricted,
                types::OperatingBand::Any,
            );
            state.frequency = Some(0);
            state.clients = Some(ConnectedClientInformation { count: 0 });
            test_values.deps.state_tracker.inner.lock().state = Some(state);
        }

        // Run the started state and send back an identical status.
        let fut = started_state(test_values.deps, req);
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        // The state machine should exit when it is unable to query status.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));

        // Verify that a failure notification is send to listeners.
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(mut updates))) => {
            let update = updates.access_points.pop().expect("no new updates available.");
            assert_eq!(update.state, types::OperatingState::Failed);
        });
    }

    #[fuchsia::test]
    fn test_stop_while_stopped() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();

        // Run the stopped state.
        let fut = stopped_state(test_values.deps);
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

    #[fuchsia::test]
    fn test_exit_while_stopped() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();

        // Run the stopped state.
        let fut = stopped_state(test_values.deps);
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

    #[fuchsia::test]
    fn test_start_while_stopped() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        // Run the stopped state.
        let fut = stopped_state(test_values.deps);
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        // Issue a start request.
        let (sender, mut receiver) = oneshot::channel();
        let radio_config = RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, 6);
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
        assert_variant!(exec.run_until_stalled(&mut receiver), Poll::Ready(Ok(())));

        // There should be a pending active state notification
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(mut updates))) => {
            let update = updates.access_points.pop().expect("no new updates available.");
            assert_eq!(update.state, types::OperatingState::Active);
        });
    }

    #[fuchsia::test]
    fn test_exit_while_stopping() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();

        // Run the stopping state.
        let (stop_sender, mut stop_receiver) = oneshot::channel();
        let fut = stopping_state(test_values.deps, stop_sender);
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

    #[fuchsia::test]
    fn test_stop_while_stopping() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        // Run the stopping state.
        let (stop_sender, mut stop_receiver) = oneshot::channel();
        let fut = stopping_state(test_values.deps, stop_sender);
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

    #[fuchsia::test]
    fn test_start_while_stopping() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();

        // Run the stopping state.
        let (stop_sender, mut stop_receiver) = oneshot::channel();
        let fut = stopping_state(test_values.deps, stop_sender);
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        // Issue a start request.
        let (start_sender, mut start_receiver) = oneshot::channel();
        let radio_config = RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, 6);
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
        assert_variant!(exec.run_until_stalled(&mut start_receiver), Poll::Ready(Ok(())));
    }

    #[fuchsia::test]
    fn test_sme_failure_while_stopping() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        // Drop the serving side of the SME so that the stop request will result in an error.
        drop(test_values.sme_req_stream);

        // Run the stopping state.
        let (stop_sender, mut stop_receiver) = oneshot::channel();
        let fut = stopping_state(test_values.deps, stop_sender);
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        // The state machine should exit when it is unable to issue the stop command.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        assert_variant!(exec.run_until_stalled(&mut stop_receiver), Poll::Ready(Err(_)));

        // There should be a new update indicating that no AP's are active.
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert!(updates.access_points.is_empty());
        });
    }

    #[fuchsia::test]
    fn test_failed_result_code_while_stopping() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        // Run the stopping state.
        let (stop_sender, mut stop_receiver) = oneshot::channel();
        let fut = stopping_state(test_values.deps, stop_sender);
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

        // There should be a new update indicating that no AP's are active.
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert!(updates.access_points.is_empty());
        });
    }

    #[fuchsia::test]
    fn test_stop_while_starting() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        let (start_sender, mut start_receiver) = oneshot::channel();
        let radio_config = RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };

        // Start off in the starting state
        let fut = starting_state(test_values.deps, req, 0, Some(start_sender));
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
        assert_variant!(exec.run_until_stalled(&mut start_receiver), Poll::Ready(Ok(())));

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

        // No metric should be logged in this case.
        assert_variant!(test_values.telemetry_receiver.try_next(), Err(_));
    }

    #[fuchsia::test]
    fn test_start_while_starting() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        let (start_sender, mut start_receiver) = oneshot::channel();
        let radio_config = RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };

        // Start off in the starting state
        let fut = starting_state(test_values.deps, req, 0, Some(start_sender));
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
        let radio_config = RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, 6);
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
        assert_variant!(exec.run_until_stalled(&mut start_receiver), Poll::Ready(Ok(())));
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
        assert_variant!(exec.run_until_stalled(&mut second_start_receiver), Poll::Ready(Ok(())));

        // No metric should be logged in this case.
        assert_variant!(test_values.telemetry_receiver.try_next(), Err(_));
    }

    #[fuchsia::test]
    fn test_exit_while_starting() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        let (start_sender, mut start_receiver) = oneshot::channel();
        let radio_config = RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };

        // Start off in the starting state
        let fut = starting_state(test_values.deps, req, 0, Some(start_sender));
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
        assert_variant!(exec.run_until_stalled(&mut start_receiver), Poll::Ready(Ok(())));

        // No metric should be logged in this case and the sender should be dropped.
        assert_variant!(test_values.telemetry_receiver.try_next(), Ok(None));
    }

    #[fuchsia::test]
    fn test_sme_breaks_while_starting() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        // Drop the serving side of the SME so that client requests fail.
        drop(test_values.sme_req_stream);

        let (start_sender, _start_receiver) = oneshot::channel();
        let radio_config = RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };

        // Start off in the starting state
        let fut = starting_state(test_values.deps, req, 0, Some(start_sender));
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        // Run the state machine and expect it to exit
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));

        // No metric should be logged in this case and the sender should have been dropped.
        assert_variant!(test_values.telemetry_receiver.try_next(), Ok(None));
    }

    #[fuchsia::test]
    fn test_sme_fails_to_stop_while_starting() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        let (start_sender, _start_receiver) = oneshot::channel();
        let radio_config = RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };

        // Start off in the starting state
        let fut = starting_state(test_values.deps, req, 0, Some(start_sender));
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        // Handle the initial disconnect request and send back a failure.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Stop{ responder }) => {
                responder
                    .send(fidl_sme::StopApResultCode::TimedOut)
                    .expect("could not send AP stop response");
            }
        );

        // The future should complete.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));

        // There should also be a failed state update.
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(mut updates))) => {
            let update = updates.access_points.pop().expect("no new updates available.");
            assert_eq!(update.state, types::OperatingState::Failed);
        });

        // No metric should be logged in this case and the sender should have been dropped.
        assert_variant!(test_values.telemetry_receiver.try_next(), Ok(None));
    }

    #[fuchsia::test]
    fn test_sme_fails_to_start_while_starting() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        let (start_sender, mut start_receiver) = oneshot::channel();
        let radio_config = RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };

        // Start off in the starting state with AP_START_MAX_RETRIES retry attempts.
        let fut = starting_state(test_values.deps, req, AP_START_MAX_RETRIES, Some(start_sender));
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        // We'll need to inject some SME responses.
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        for retry_number in 0..(AP_START_MAX_RETRIES + 1) {
            // Handle the initial stop request.
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
            assert_variant!(
                poll_sme_req(&mut exec, &mut sme_fut),
                Poll::Ready(fidl_sme::ApSmeRequest::Stop{ responder }) => {
                    responder
                        .send(fidl_sme::StopApResultCode::Success)
                        .expect("could not send AP stop response");
                }
            );

            // There should also be a stopped state update.
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
            assert_variant!(
                test_values.update_receiver.try_next(),
                Ok(Some(listener::Message::NotifyListeners(_)))
            );

            // If this is the first attempt, there should be a starting notification, otherwise
            // there should be no update.
            if retry_number == 0 {
                assert_variant!(
                    test_values.update_receiver.try_next(),
                    Ok(Some(listener::Message::NotifyListeners(mut updates))) => {
                    let update = updates.access_points.pop().expect("no new updates available.");
                    assert_eq!(update.state, types::OperatingState::Starting);
                });
            } else {
                assert_variant!(test_values.update_receiver.try_next(), Err(_));
            }

            // Wait for a start request and send back a timeout.
            assert_variant!(
                poll_sme_req(&mut exec, &mut sme_fut),
                Poll::Ready(fidl_sme::ApSmeRequest::Start{ config: _, responder }) => {
                    responder
                        .send(fidl_sme::StartApResultCode::TimedOut)
                        .expect("could not send AP stop response");
                }
            );

            if retry_number < AP_START_MAX_RETRIES {
                // The future should still be running.
                assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

                // Verify that no new message has been reported yet.
                assert_variant!(exec.run_until_stalled(&mut start_receiver), Poll::Pending);

                // The state machine should then retry following the retry interval.
                assert_variant!(exec.wake_next_timer(), Some(_));
            }
        }

        // The future should complete.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));

        // Verify that the start receiver got an error.
        assert_variant!(exec.run_until_stalled(&mut start_receiver), Poll::Ready(Err(_)));

        // There should be a failure notification at the end of the retries.
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(mut updates))) => {
            let update = updates.access_points.pop().expect("no new updates available.");
            assert_eq!(update.state, types::OperatingState::Failed);
        });

        // A metric should be logged for the failure to start the AP.
        assert_variant!(
            test_values.telemetry_receiver.try_next(),
            Ok(Some(TelemetryEvent::ApStartFailure))
        );
    }

    #[fuchsia::test]
    fn test_stop_after_start_failure() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        let (start_sender, mut start_receiver) = oneshot::channel();
        let radio_config = RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };

        // Insert a stop request to be processed after starting the AP fails.
        let (stop_sender, mut stop_receiver) = oneshot::channel();
        test_values
            .ap_req_sender
            .try_send(ManualRequest::Stop(stop_sender))
            .expect("failed to request AP stop");

        // Start off in the starting state with AP_START_MAX_RETRIES retry attempts.
        let fut = starting_state(test_values.deps, req, AP_START_MAX_RETRIES, Some(start_sender));
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        // We'll need to inject some SME responses.
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        // Handle the initial stop request.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Stop{ responder }) => {
                responder
                    .send(fidl_sme::StopApResultCode::Success)
                    .expect("could not send AP stop response");
            }
        );

        // There should also be a stopped state update.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(_)))
        );

        // Followed by a starting update.
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(mut updates))) => {
            let update = updates.access_points.pop().expect("no new updates available.");
            assert_eq!(update.state, types::OperatingState::Starting);
        });

        // Wait for a start request and send back a timeout.
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Start{ config: _, responder }) => {
                responder
                    .send(fidl_sme::StartApResultCode::TimedOut)
                    .expect("could not send AP stop response");
            }
        );

        // At this point, the state machine should pause before retrying the start request.  It
        // should also check to see if there are any incoming AP commands and find the initial stop
        // request.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // A metric should be logged for the failure to start the AP.
        assert_variant!(
            test_values.telemetry_receiver.try_next(),
            Ok(Some(TelemetryEvent::ApStartFailure))
        );

        // The start sender will be dropped in this transition.
        assert_variant!(exec.run_until_stalled(&mut start_receiver), Poll::Ready(Err(_)));

        // There should be a pending AP stop request.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Stop{ responder }) => {
                responder
                    .send(fidl_sme::StopApResultCode::Success)
                    .expect("could not send AP stop response");
            }
        );

        // The future should be parked in the stopped state.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Verify that the stop receiver is acknowledged.
        assert_variant!(exec.run_until_stalled(&mut stop_receiver), Poll::Ready(Ok(())));

        // There should be a new update indicating that no AP's are active.
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert!(updates.access_points.is_empty());
        });
    }

    #[fuchsia::test]
    fn test_start_after_start_failure() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        let (start_sender, mut start_receiver) = oneshot::channel();
        let radio_config = RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config: radio_config.clone(),
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };

        // Insert a stop request to be processed after starting the AP fails.
        let mut requested_id = create_network_id();
        requested_id.ssid = types::Ssid::try_from("second_test_ssid").unwrap();

        let requested_config = ApConfig {
            id: requested_id.clone(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };

        let (start_response_sender, _) = oneshot::channel();
        test_values
            .ap_req_sender
            .try_send(ManualRequest::Start((requested_config, start_response_sender)))
            .expect("failed to request AP stop");

        // Start off in the starting state with AP_START_MAX_RETRIES retry attempts.
        let fut = starting_state(test_values.deps, req, AP_START_MAX_RETRIES, Some(start_sender));
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        // We'll need to inject some SME responses.
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        // Handle the initial stop request.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Stop{ responder }) => {
                responder
                    .send(fidl_sme::StopApResultCode::Success)
                    .expect("could not send AP stop response");
            }
        );

        // There should also be a stopped state update.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(_)))
        );

        // Followed by a starting update.
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(mut updates))) => {
            let update = updates.access_points.pop().expect("no new updates available.");
            assert_eq!(update.state, types::OperatingState::Starting);
        });

        // Wait for a start request and send back a timeout.
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Start{ config: _, responder }) => {
                responder
                    .send(fidl_sme::StartApResultCode::TimedOut)
                    .expect("could not send AP stop response");
            }
        );

        // At this point, the state machine should pause before retrying the start request.  It
        // should also check to see if there are any incoming AP commands and find the initial
        // start request.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // A metric should be logged for the failure to start the AP.
        assert_variant!(
            test_values.telemetry_receiver.try_next(),
            Ok(Some(TelemetryEvent::ApStartFailure))
        );

        // The original start sender will be dropped in this transition.
        assert_variant!(exec.run_until_stalled(&mut start_receiver), Poll::Ready(Err(_)));

        // There should be a pending AP stop request.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Stop{ responder }) => {
                responder
                    .send(fidl_sme::StopApResultCode::Success)
                    .expect("could not send AP stop response");
            }
        );

        // This should be followed by another start request that matches the requested config.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Start{ config, responder: _ }) => {
                assert_eq!(config.ssid, requested_id.ssid);
            }
        );
    }

    #[fuchsia::test]
    fn test_exit_after_start_failure() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        let (start_sender, _) = oneshot::channel();
        let radio_config = RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, 6);
        let req = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };

        // Insert a stop request to be processed after starting the AP fails.
        let (exit_sender, mut exit_receiver) = oneshot::channel();
        test_values
            .ap_req_sender
            .try_send(ManualRequest::Exit(exit_sender))
            .expect("failed to request AP stop");

        // Start off in the starting state with AP_START_MAX_RETRIES retry attempts.
        let fut = starting_state(test_values.deps, req, AP_START_MAX_RETRIES, Some(start_sender));
        let fut = run_state_machine(fut);
        pin_mut!(fut);

        // We'll need to inject some SME responses.
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        // Handle the initial stop request.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Stop{ responder }) => {
                responder
                    .send(fidl_sme::StopApResultCode::Success)
                    .expect("could not send AP stop response");
            }
        );

        // There should also be a stopped state update.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(_)))
        );

        // Followed by a starting update.
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(mut updates))) => {
            let update = updates.access_points.pop().expect("no new updates available.");
            assert_eq!(update.state, types::OperatingState::Starting);
        });

        // Wait for a start request and send back a timeout.
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Start{ config: _, responder }) => {
                responder
                    .send(fidl_sme::StartApResultCode::TimedOut)
                    .expect("could not send AP stop response");
            }
        );

        // At this point, the state machine should pause before retrying the start request.  It
        // should also check to see if there are any incoming AP commands and find the initial exit
        // request at which point it should exit.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        assert_variant!(exec.run_until_stalled(&mut exit_receiver), Poll::Ready(Ok(())));

        // A metric should be logged for the failure to start the AP.
        assert_variant!(
            test_values.telemetry_receiver.try_next(),
            Ok(Some(TelemetryEvent::ApStartFailure))
        );
    }

    #[fuchsia::test]
    fn test_manual_start_causes_starting_notification() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();

        // Create a start request and enter the state machine with a manual start request.
        let radio_config = RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, 6);
        let requested_config = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };

        let (start_response_sender, _) = oneshot::channel();
        let manual_request = ManualRequest::Start((requested_config, start_response_sender));

        let fut = async move { perform_manual_request(test_values.deps, Some(manual_request)) };
        let fut = run_state_machine(fut);
        pin_mut!(fut);
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // We should get a stop request
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Stop{ responder }) => {
                responder
                    .send(fidl_sme::StopApResultCode::Success)
                    .expect("could not send SME stop response");
            }
        );

        // We should then get a notification that the AP is inactive followed by a new starting
        // notification.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
                assert!(updates.access_points.is_empty());
        });

        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(mut updates))) => {
            let update = updates.access_points.pop().expect("no new updates available.");
            assert_eq!(update.state, types::OperatingState::Starting);
        });
    }

    #[fuchsia::test]
    fn test_serve_does_not_terminate_right_away() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let sme_event_stream = test_values.deps.proxy.take_event_stream();
        let sme_fut = test_values.sme_req_stream.into_future();
        pin_mut!(sme_fut);

        let update_sender = test_values.deps.state_tracker.inner.lock().sender.clone();

        let fut = serve(
            0,
            test_values.deps.proxy,
            sme_event_stream,
            test_values.deps.req_stream,
            update_sender,
            test_values.deps.telemetry_sender,
        );
        pin_mut!(fut);

        // Run the state machine. No request is made initially.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(poll_sme_req(&mut exec, &mut sme_fut), Poll::Pending);
    }

    #[test]
    fn test_no_notification_when_sme_fails_while_stopped() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup();
        let sme_event_stream = test_values.deps.proxy.take_event_stream();
        let update_sender = test_values.deps.state_tracker.inner.lock().sender.clone();

        let fut = serve(
            0,
            test_values.deps.proxy,
            sme_event_stream,
            test_values.deps.req_stream,
            update_sender,
            test_values.deps.telemetry_sender,
        );
        pin_mut!(fut);

        // Cause the SME event stream to terminate.
        drop(test_values.sme_req_stream);

        // Run the state machine and observe that it has terminated.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));

        // There should be no notification of failure since no AP is actively running.
        assert_variant!(
            exec.run_until_stalled(&mut test_values.update_receiver.into_future()),
            Poll::Pending
        );
    }

    #[test]
    fn test_failure_notification_when_configured() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup();
        let sme_event_stream = test_values.deps.proxy.take_event_stream();
        let mut sme_fut = Box::pin(test_values.sme_req_stream.into_future());

        let update_sender = test_values.deps.state_tracker.inner.lock().sender.clone();
        let fut = serve(
            0,
            test_values.deps.proxy,
            sme_event_stream,
            test_values.deps.req_stream,
            update_sender,
            test_values.deps.telemetry_sender,
        );
        pin_mut!(fut);

        // Make a request to start the access point.
        let mut ap = AccessPoint::new(test_values.ap_req_sender);
        let (sender, _receiver) = oneshot::channel();
        let radio_config = RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, 6);
        let config = ApConfig {
            id: create_network_id(),
            credential: vec![],
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        };
        ap.start(config, sender).expect("failed to make start request");

        // Expect that the state machine issues a stop request followed by a start request.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_fut),
            Poll::Ready(fidl_sme::ApSmeRequest::Stop{ responder }) => {
                responder.send(fidl_sme::StopApResultCode::Success).expect("could not send AP stop response");
            }
        );

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // At this point, the state machine will have sent an empty notification and a starting
        // notification.
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(update))) => {
                assert!(update.access_points.is_empty());
            }
        );
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(update))) => {
                assert_eq!(update.access_points.len(), 1);
                assert_eq!(update.access_points[0].state, types::OperatingState::Starting);
            }
        );

        // Cause the SME event stream to terminate.
        drop(sme_fut);

        // Run the state machine and observe that it has terminated.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));

        // There should be a failure notification.
        assert_variant!(
            test_values.update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(update))) => {
                assert_eq!(update.access_points.len(), 1);
                assert_eq!(update.access_points[0].state, types::OperatingState::Failed);
            }
        );
    }

    #[test]
    fn test_state_tracker_reset() {
        let _exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (sender, mut receiver) = mpsc::unbounded();

        // A new state tracker should initially have no state.
        let state = ApStateTracker::new(sender);
        {
            assert!(state.inner.lock().state.is_none());
        }

        // And there should be no updates.
        assert_variant!(receiver.try_next(), Err(_));

        // Reset the state to starting and verify that the internal state has been updated.
        let new_state = ApStateUpdate::new(
            create_network_id(),
            types::OperatingState::Starting,
            types::ConnectivityMode::Unrestricted,
            types::OperatingBand::Any,
        );
        state.reset_state(new_state).expect("failed to reset state");
        assert_variant!(state.inner.lock().state.as_ref(), Some(ApStateUpdate {
                id: types::NetworkIdentifier {
                    ssid,
                    security_type: types::SecurityType::None,
                },
                state: types::OperatingState::Starting,
                mode: Some(types::ConnectivityMode::Unrestricted),
                band: Some(types::OperatingBand::Any),
                frequency: None,
                clients: None,
        }) => {
            let expected_ssid = types::Ssid::try_from("test_ssid").unwrap();
            assert_eq!(ssid, &expected_ssid);
        });

        // Resetting the state should result in an update.
        assert_variant!(
            receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(ApStatesUpdate { access_points }))) => {
            assert_eq!(access_points.len(), 1);

            let expected_id = types::NetworkIdentifier {
                ssid: types::Ssid::try_from("test_ssid").unwrap(),
                security_type: types::SecurityType::None,
            };
            assert_eq!(access_points[0].id, expected_id);
            assert_eq!(access_points[0].state, types::OperatingState::Starting);
            assert_eq!(access_points[0].mode, Some(types::ConnectivityMode::Unrestricted));
            assert_eq!(access_points[0].band, Some(types::OperatingBand::Any));
            assert_eq!(access_points[0].frequency, None);
            assert_eq!(access_points[0].clients, None);
            }
        );
    }

    #[test]
    fn test_state_tracker_consume_sme_update() {
        let _exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (sender, mut receiver) = mpsc::unbounded();
        let state = ApStateTracker::new(sender);

        // Reset the state to started and send an update.
        let new_state = ApStateUpdate::new(
            create_network_id(),
            types::OperatingState::Active,
            types::ConnectivityMode::Unrestricted,
            types::OperatingBand::Any,
        );
        state.reset_state(new_state).expect("failed to reset state");

        // The update should note that the AP is active.
        assert_variant!(
            receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(ApStatesUpdate { access_points }))
        ) => {
            assert_eq!(access_points.len(), 1);

            let expected_id = types::NetworkIdentifier {
                ssid: types::Ssid::try_from("test_ssid").unwrap(),
                security_type: types::SecurityType::None,
            };
            assert_eq!(access_points[0].id, expected_id);
            assert_eq!(access_points[0].state, types::OperatingState::Active);
            assert_eq!(access_points[0].mode, Some(types::ConnectivityMode::Unrestricted));
            assert_eq!(access_points[0].band, Some(types::OperatingBand::Any));
            assert_eq!(access_points[0].frequency, None);
            assert_eq!(access_points[0].clients, None);
        });

        // Consume a status update and expect a new notification to be generated.
        let ap_info = fidl_sme::Ap {
            ssid: types::Ssid::try_from("test_ssid").unwrap().to_vec(),
            channel: 6,
            num_clients: 123,
        };
        state
            .consume_sme_status_update(Cbw::Cbw20, ap_info)
            .expect("failure while updating SME status");

        assert_variant!(
            receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(ApStatesUpdate { access_points }))
        ) => {
            assert_eq!(access_points.len(), 1);

            let expected_id = types::NetworkIdentifier {
                ssid: types::Ssid::try_from("test_ssid").unwrap(),
                security_type: types::SecurityType::None,
            };
            assert_eq!(access_points[0].id, expected_id);
            assert_eq!(access_points[0].state, types::OperatingState::Active);
            assert_eq!(access_points[0].mode, Some(types::ConnectivityMode::Unrestricted));
            assert_eq!(access_points[0].band, Some(types::OperatingBand::Any));
            assert_eq!(access_points[0].frequency, Some(2437));
            assert_eq!(access_points[0].clients, Some(ConnectedClientInformation { count: 123 }));
        });
    }

    #[test]
    fn test_state_tracker_update_operating_state() {
        let _exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (sender, mut receiver) = mpsc::unbounded();
        let state = ApStateTracker::new(sender);

        // Reset the state to started and send an update.
        let new_state = ApStateUpdate::new(
            create_network_id(),
            types::OperatingState::Starting,
            types::ConnectivityMode::Unrestricted,
            types::OperatingBand::Any,
        );
        state.reset_state(new_state).expect("failed to reset state");

        // The update should note that the AP is starting.
        assert_variant!(
            receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(ApStatesUpdate { access_points }))
        ) => {
            assert_eq!(access_points.len(), 1);

            let expected_id = types::NetworkIdentifier {
                ssid: types::Ssid::try_from("test_ssid").unwrap(),
                security_type: types::SecurityType::None,
            };
            assert_eq!(access_points[0].id, expected_id);
            assert_eq!(access_points[0].state, types::OperatingState::Starting);
            assert_eq!(access_points[0].mode, Some(types::ConnectivityMode::Unrestricted));
            assert_eq!(access_points[0].band, Some(types::OperatingBand::Any));
            assert_eq!(access_points[0].frequency, None);
            assert_eq!(access_points[0].clients, None);
        });

        // Give another update that the state is starting and ensure that a notification is sent.
        state
            .update_operating_state(types::OperatingState::Starting)
            .expect("failed to send duplicate update.");
        assert_variant!(
            receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(ApStatesUpdate { access_points }))
        ) => {
            assert_eq!(access_points.len(), 1);

            let expected_id = types::NetworkIdentifier {
                ssid: types::Ssid::try_from("test_ssid").unwrap(),
                security_type: types::SecurityType::None,
            };
            assert_eq!(access_points[0].id, expected_id);
            assert_eq!(access_points[0].state, types::OperatingState::Starting);
            assert_eq!(access_points[0].mode, Some(types::ConnectivityMode::Unrestricted));
            assert_eq!(access_points[0].band, Some(types::OperatingBand::Any));
            assert_eq!(access_points[0].frequency, None);
            assert_eq!(access_points[0].clients, None);
        });

        // Now update that the state is active and expect a notification to be generated.
        state
            .update_operating_state(types::OperatingState::Active)
            .expect("failed to send active update.");
        assert_variant!(
            receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(ApStatesUpdate { access_points }))
        ) => {
            assert_eq!(access_points.len(), 1);

            let expected_id = types::NetworkIdentifier {
                ssid: types::Ssid::try_from("test_ssid").unwrap(),
                security_type: types::SecurityType::None,
            };
            assert_eq!(access_points[0].id, expected_id);
            assert_eq!(access_points[0].state, types::OperatingState::Active);
            assert_eq!(access_points[0].mode, Some(types::ConnectivityMode::Unrestricted));
            assert_eq!(access_points[0].band, Some(types::OperatingBand::Any));
            assert_eq!(access_points[0].frequency, None);
            assert_eq!(access_points[0].clients, None);
        });
    }

    #[test]
    fn test_state_tracker_set_stopped_state() {
        let _exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (sender, mut receiver) = mpsc::unbounded();
        let state = ApStateTracker::new(sender);

        // Set up some initial state.
        {
            let new_state = ApStateUpdate::new(
                create_network_id(),
                types::OperatingState::Active,
                types::ConnectivityMode::Unrestricted,
                types::OperatingBand::Any,
            );
            state.inner.lock().state = Some(new_state);
        }

        // Set the state to stopped and verify that the internal state information has been
        // removed.
        state.set_stopped_state().expect("failed to send stopped notification");
        {
            assert!(state.inner.lock().state.is_none());
        }

        // Verify that an empty update has arrived.
        assert_variant!(
            receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(ApStatesUpdate { access_points }))
        ) => {
            assert!(access_points.is_empty());
        });
    }

    #[test]
    fn test_state_tracker_failure_modes() {
        let _exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (sender, receiver) = mpsc::unbounded();
        let state = ApStateTracker::new(sender);
        {
            let new_state = ApStateUpdate::new(
                create_network_id(),
                types::OperatingState::Active,
                types::ConnectivityMode::Unrestricted,
                types::OperatingBand::Any,
            );
            state.inner.lock().state = Some(new_state);
        }

        // Currently, the only reason any of the state tracker methods might fail is because of a
        // failure to enqueue a state change notification.  Drop the receiving end to trigger this
        // condition.
        drop(receiver);

        let _ = state
            .update_operating_state(types::OperatingState::Failed)
            .expect_err("unexpectedly able to set operating state");
        let _ = state
            .consume_sme_status_update(
                Cbw::Cbw20,
                fidl_sme::Ap {
                    ssid: types::Ssid::try_from("test_ssid").unwrap().to_vec(),
                    channel: 6,
                    num_clients: 123,
                },
            )
            .expect_err("unexpectedly able to update SME status");
        let _ = state.set_stopped_state().expect_err("unexpectedly able to set stopped state");
    }
}
