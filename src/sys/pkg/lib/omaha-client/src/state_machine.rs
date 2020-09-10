// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    common::{App, AppSet, CheckOptions, CheckTiming},
    configuration::Config,
    http_request::{self, HttpRequest},
    installer::{Installer, Plan},
    metrics::{Metrics, MetricsReporter, UpdateCheckFailureReason},
    policy::{CheckDecision, PolicyEngine, UpdateDecision},
    protocol::{
        self,
        request::{Event, EventErrorCode, EventResult, EventType, InstallSource, GUID},
        response::{parse_json_response, OmahaStatus, Response},
    },
    request_builder::{self, RequestBuilder, RequestParams},
    storage::{Storage, StorageExt},
    time::{TimeSource, Timer},
};

use anyhow::anyhow;
use futures::{
    channel::{mpsc, oneshot},
    future::{self, BoxFuture, Fuse},
    lock::Mutex,
    prelude::*,
    select,
};
use http::response::Parts;
use log::{error, info, warn};
use std::{
    cmp::min,
    rc::Rc,
    str::Utf8Error,
    time::{Duration, Instant, SystemTime},
};
use thiserror::Error;

pub mod update_check;

mod builder;
pub use builder::StateMachineBuilder;

mod observer;
use observer::StateMachineProgressObserver;
pub use observer::{InstallProgress, StateMachineEvent};

const LAST_CHECK_TIME: &str = "last_check_time";
const INSTALL_PLAN_ID: &str = "install_plan_id";
const UPDATE_FIRST_SEEN_TIME: &str = "update_first_seen_time";
const UPDATE_FINISH_TIME: &str = "update_finish_time";
const TARGET_VERSION: &str = "target_version";
const CONSECUTIVE_FAILED_UPDATE_CHECKS: &str = "consecutive_failed_update_checks";
// How long do we wait after not allowed to reboot to check again.
const CHECK_REBOOT_ALLOWED_INTERVAL: Duration = Duration::from_secs(30 * 60);
// This header contains the number of seconds client must not contact server again.
const X_RETRY_AFTER: &str = "X-Retry-After";

/// This is the core state machine for a client's update check.  It is instantiated and used to
/// perform update checks over time or to perform a single update check process.
#[derive(Debug)]
pub struct StateMachine<PE, HR, IN, TM, MR, ST>
where
    PE: PolicyEngine,
    HR: HttpRequest,
    IN: Installer,
    TM: Timer,
    MR: MetricsReporter,
    ST: Storage,
{
    /// The immutable configuration of the client itself.
    config: Config,

    policy_engine: PE,

    http: HR,

    installer: IN,

    timer: TM,

    time_source: PE::TimeSource,

    metrics_reporter: MR,

    storage_ref: Rc<Mutex<ST>>,

    /// Context for update check.
    context: update_check::Context,

    /// The current State of the StateMachine.
    state: State,

    /// The list of apps used for update check.
    app_set: AppSet,
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum State {
    Idle,
    CheckingForUpdates,
    ErrorCheckingForUpdate,
    NoUpdateAvailable,
    InstallationDeferredByPolicy,
    InstallingUpdate,
    WaitingForReboot,
    InstallationError,
}

/// This is the set of errors that can occur when making a request to Omaha.  This is an internal
/// collection of error types.
#[derive(Error, Debug)]
pub enum OmahaRequestError {
    #[error("Unexpected JSON error constructing update check: {0}")]
    Json(#[from] serde_json::Error),

    #[error("Error building update check HTTP request: {0}")]
    HttpBuilder(#[from] http::Error),

    // TODO: This still contains hyper user error which should be split out.
    #[error("HTTP transport error performing update check: {0}")]
    HttpTransport(#[from] http_request::Error),

    #[error("HTTP error performing update check: {0}")]
    HttpStatus(hyper::StatusCode),
}

impl From<request_builder::Error> for OmahaRequestError {
    fn from(err: request_builder::Error) -> Self {
        match err {
            request_builder::Error::Json(e) => OmahaRequestError::Json(e),
            request_builder::Error::Http(e) => OmahaRequestError::HttpBuilder(e),
        }
    }
}

impl From<http::StatusCode> for OmahaRequestError {
    fn from(sc: http::StatusCode) -> Self {
        OmahaRequestError::HttpStatus(sc)
    }
}

/// This is the set of errors that can occur when parsing the response body from Omaha.  This is an
/// internal collection of error types.
#[derive(Error, Debug)]
#[allow(dead_code)]
pub enum ResponseParseError {
    #[error("Response was not valid UTF-8")]
    Utf8(Utf8Error),

    #[error("Unexpected JSON error parsing update check response: {}", _0)]
    Json(serde_json::Error),
}

#[derive(Error, Debug)]
pub enum UpdateCheckError {
    #[error("Error checking with Omaha: {:?}", _0)]
    OmahaRequest(OmahaRequestError),

    #[error("Error parsing Omaha response: {:?}", _0)]
    ResponseParser(ResponseParseError),

    #[error("Unable to create an install plan: {:?}", _0)]
    InstallPlan(anyhow::Error),
}

/// A handle to interact with the state machine running in another task.
#[derive(Clone)]
pub struct ControlHandle(mpsc::Sender<ControlRequest>);

/// Error indicating that the state machine task no longer exists.
#[derive(Debug, Clone, Error, PartialEq, Eq)]
#[error("state machine dropped before all its control handles")]
pub struct StateMachineGone;

impl From<mpsc::SendError> for StateMachineGone {
    fn from(_: mpsc::SendError) -> Self {
        StateMachineGone
    }
}

impl From<oneshot::Canceled> for StateMachineGone {
    fn from(_: oneshot::Canceled) -> Self {
        StateMachineGone
    }
}

enum ControlRequest {
    StartUpdateCheck { options: CheckOptions, responder: oneshot::Sender<StartUpdateCheckResponse> },
}

/// Responses to a request to start an update check now.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum StartUpdateCheckResponse {
    /// The state machine was idle and the request triggered an update check.
    Started,

    /// The state machine was already processing an update check and ignored this request and
    /// options.
    AlreadyRunning,

    /// The update check was throttled by policy.
    Throttled,
}

impl ControlHandle {
    /// Ask the state machine to start an update check with the provided options, returning whether
    /// or not the state machine started a check or was already running one.
    pub async fn start_update_check(
        &mut self,
        options: CheckOptions,
    ) -> Result<StartUpdateCheckResponse, StateMachineGone> {
        let (responder, receive_response) = oneshot::channel();
        self.0.send(ControlRequest::StartUpdateCheck { options, responder }).await?;
        Ok(receive_response.await?)
    }
}

impl<PE, HR, IN, TM, MR, ST> StateMachine<PE, HR, IN, TM, MR, ST>
where
    PE: PolicyEngine,
    HR: HttpRequest,
    IN: Installer,
    TM: Timer,
    MR: MetricsReporter,
    ST: Storage,
{
    /// Ask policy engine for the next update check time and update the context and yield event.
    async fn update_next_update_time(
        &mut self,
        co: &mut async_generator::Yield<StateMachineEvent>,
    ) -> CheckTiming {
        let apps = self.app_set.to_vec().await;
        let timing = self
            .policy_engine
            .compute_next_update_time(&apps, &self.context.schedule, &self.context.state)
            .await;
        self.context.schedule.next_update_time = Some(timing);

        co.yield_(StateMachineEvent::ScheduleChange(self.context.schedule.clone())).await;
        info!("Calculated check timing: {}", timing);
        timing
    }

    /// Return a future that will wait until the given check timing.
    async fn make_wait_to_next_check(
        &mut self,
        check_timing: CheckTiming,
    ) -> Fuse<BoxFuture<'static, ()>> {
        if let Some(minimum_wait) = check_timing.minimum_wait {
            // If there's a minimum wait, also wait at least that long, by joining the two
            // timers so that both need to be true (in case `next_update_time` turns out to be
            // very close to now)
            future::join(
                self.timer.wait_for(minimum_wait),
                self.timer.wait_until(check_timing.time),
            )
            .map(|_| ())
            .boxed()
            .fuse()
        } else {
            // Otherwise just setup the timer for the waiting until the next time.  This is a
            // wait until either the monotonic or wall times have passed.
            self.timer.wait_until(check_timing.time).fuse()
        }
    }

    async fn run(
        mut self,
        mut control: mpsc::Receiver<ControlRequest>,
        mut co: async_generator::Yield<StateMachineEvent>,
    ) {
        if !self.app_set.valid().await {
            error!(
                "App set not valid, not starting state machine: {:#?}",
                self.app_set.to_vec().await
            );
            return;
        }

        let state_machine_start_monotonic_time = self.time_source.now_in_monotonic();

        let mut should_report_waited_for_reboot_duration = false;

        let update_finish_time = {
            let storage = self.storage_ref.lock().await;
            let update_finish_time = storage.get_time(UPDATE_FINISH_TIME).await;
            if update_finish_time.is_some() {
                if let Some(target_version) = storage.get_string(TARGET_VERSION).await {
                    if target_version == self.config.os.version {
                        should_report_waited_for_reboot_duration = true;
                    }
                }
            }
            update_finish_time
        };

        loop {
            info!("Initial context: {:?}", self.context);

            if should_report_waited_for_reboot_duration {
                let now = self.time_source.now();
                // If `update_finish_time` is in the future we don't have correct time, try again
                // on the next loop.
                if let Ok(update_finish_time_to_now) =
                    now.wall_duration_since(update_finish_time.unwrap())
                {
                    // It might take a while for us to get here, but we only want to report the
                    // time from update finish to state machine start after reboot, so we subtract
                    // the duration since then using monotonic time.
                    let waited_for_reboot_duration = update_finish_time_to_now
                        - now.mono.duration_since(state_machine_start_monotonic_time);
                    info!("Waited {} seconds for reboot.", waited_for_reboot_duration.as_secs());
                    self.report_metrics(Metrics::WaitedForRebootDuration(
                        waited_for_reboot_duration,
                    ));
                    should_report_waited_for_reboot_duration = false;

                    let mut storage = self.storage_ref.lock().await;
                    storage.remove_or_log(UPDATE_FINISH_TIME).await;
                    storage.remove_or_log(TARGET_VERSION).await;
                    storage.commit_or_log().await;
                }
            }

            let (options, responder) = {
                let check_timing = self.update_next_update_time(&mut co).await;
                let mut wait_to_next_check = self.make_wait_to_next_check(check_timing).await;

                // Wait for either the next check time or a request to start an update check.  Use
                // the default check options with the timed check, or those sent with a request.
                select! {
                    () = wait_to_next_check => (CheckOptions::default(), None),
                    ControlRequest::StartUpdateCheck{options, responder} = control.select_next_some() => {
                        (options, Some(responder))
                    }
                }
            };

            {
                let apps = self.app_set.to_vec().await;
                info!("Checking to see if an update check is allowed at this time for {:?}", apps);
                let decision = self
                    .policy_engine
                    .update_check_allowed(
                        &apps,
                        &self.context.schedule,
                        &self.context.state,
                        &options,
                    )
                    .await;

                info!("The update check decision is: {:?}", decision);

                let request_params = match decision {
                    // Positive results, will continue with the update check process
                    CheckDecision::Ok(rp) | CheckDecision::OkUpdateDeferred(rp) => rp,

                    // Negative results, exit early
                    CheckDecision::TooSoon
                    | CheckDecision::ThrottledByPolicy
                    | CheckDecision::DeniedByPolicy => {
                        info!("The update check is not allowed at this time.");
                        if let Some(responder) = responder {
                            let _ = responder.send(StartUpdateCheckResponse::Throttled);
                        }
                        continue;
                    }
                };
                if let Some(responder) = responder {
                    let _ = responder.send(StartUpdateCheckResponse::Started);
                }

                // "start" the update check itself (well, create the future that is the update check)
                let update_check = self.start_update_check(request_params, &mut co).fuse();
                futures::pin_mut!(update_check);

                // Wait for the update check to complete, handling any control requests that come in
                // during the check.
                loop {
                    select! {
                        () = update_check => break,
                        ControlRequest::StartUpdateCheck{options, responder} = control.select_next_some() => {
                            let _ = responder.send(StartUpdateCheckResponse::AlreadyRunning);
                        }
                    }
                }
            }

            // TODO: This is the last place we read self.state, we should see if we can find another
            // way to achieve this so that we can remove self.state entirely.
            if self.state == State::WaitingForReboot {
                self.wait_for_reboot(&options, &mut control, &mut co).await;
            }

            self.set_state(State::Idle, &mut co).await;
        }
    }

    async fn wait_for_reboot(
        &mut self,
        options: &CheckOptions,
        control: &mut mpsc::Receiver<ControlRequest>,
        co: &mut async_generator::Yield<StateMachineEvent>,
    ) {
        if !self.policy_engine.reboot_allowed(&options).await {
            let wait_to_see_if_reboot_allowed =
                self.timer.wait_for(CHECK_REBOOT_ALLOWED_INTERVAL).fuse();
            futures::pin_mut!(wait_to_see_if_reboot_allowed);

            let check_timing = self.update_next_update_time(co).await;
            let wait_to_next_ping = self.make_wait_to_next_check(check_timing).await;
            futures::pin_mut!(wait_to_next_ping);

            loop {
                // Wait for either the next time to check if reboot allowed or the next
                // ping time or a request to start an update check.
                select! {
                    () = wait_to_see_if_reboot_allowed => {
                        if self.policy_engine.reboot_allowed(&options).await {
                            break;
                        }
                        info!("Reboot not allowed at the moment, will try again in 30 minutes...");
                        wait_to_see_if_reboot_allowed.set(
                            self.timer.wait_for(CHECK_REBOOT_ALLOWED_INTERVAL).fuse()
                        );
                    },
                    () = wait_to_next_ping => {
                        self.ping_omaha(co).await;
                        let check_timing = self.update_next_update_time(co).await;
                        wait_to_next_ping.set(self.make_wait_to_next_check(check_timing).await);
                    },
                    ControlRequest::StartUpdateCheck{options, responder} = control.select_next_some() => {
                        let _ = responder.send(StartUpdateCheckResponse::AlreadyRunning);
                    }
                }
            }
        }
        info!("Rebooting the system at the end of a successful update");
        if let Err(e) = self.installer.perform_reboot().await {
            error!("Unable to reboot the system: {}", e);
        }
    }

    /// Report update check interval based on the last check time stored in storage.
    /// It will also persist the new last check time to storage.
    async fn report_check_interval(&mut self) {
        // Clone the Rc first to avoid borrowing self for the rest of the function.
        let storage_ref = self.storage_ref.clone();
        let mut storage = storage_ref.lock().await;
        let now = self.time_source.now();
        if let Some(last_check_time) = storage.get_time(LAST_CHECK_TIME).await {
            match now.wall_duration_since(last_check_time) {
                Ok(duration) => self.report_metrics(Metrics::UpdateCheckInterval(duration)),
                Err(e) => warn!("Last check time is in the future: {}", e),
            }
        }
        if let Err(e) = storage.set_time(LAST_CHECK_TIME, now).await {
            error!("Unable to persist {}: {}", LAST_CHECK_TIME, e);
            return;
        }
        storage.commit_or_log().await;
    }

    /// Perform update check and handle the result, including updating the update check context
    /// and cohort.
    pub async fn start_update_check(
        &mut self,
        request_params: RequestParams,
        co: &mut async_generator::Yield<StateMachineEvent>,
    ) {
        let apps = self.app_set.to_vec().await;
        let result = self.perform_update_check(request_params, apps, co).await;
        match &result {
            Ok(result) => {
                info!("Update check result: {:?}", result);
                // Update check succeeded, update |last_update_time|.
                self.context.schedule.last_update_time = Some(self.time_source.now().into());

                // Increment |consecutive_failed_update_attempts| if any app failed to install,
                // otherwise reset it to 0.
                if result
                    .app_responses
                    .iter()
                    .any(|app| app.result == update_check::Action::InstallPlanExecutionError)
                {
                    self.context.state.consecutive_failed_update_attempts += 1;
                } else {
                    self.context.state.consecutive_failed_update_attempts = 0;
                }

                // Update check succeeded, reset |consecutive_failed_update_checks| to 0.
                self.context.state.consecutive_failed_update_checks = 0;

                self.app_set.update_from_omaha(&result.app_responses).await;

                self.report_attempts_to_succeed(true).await;

                // TODO: update consecutive_proxied_requests
            }
            Err(error) => {
                error!("Update check failed: {:?}", error);
                // Update check failed, increment |consecutive_failed_update_checks|.
                self.context.state.consecutive_failed_update_checks += 1;

                let failure_reason = match error {
                    UpdateCheckError::ResponseParser(_) | UpdateCheckError::InstallPlan(_) => {
                        // We talked to Omaha, update |last_update_time|.
                        self.context.schedule.last_update_time =
                            Some(self.time_source.now().into());

                        UpdateCheckFailureReason::Omaha
                    }
                    UpdateCheckError::OmahaRequest(request_error) => match request_error {
                        OmahaRequestError::Json(_) | OmahaRequestError::HttpBuilder(_) => {
                            UpdateCheckFailureReason::Internal
                        }
                        OmahaRequestError::HttpTransport(_) | OmahaRequestError::HttpStatus(_) => {
                            UpdateCheckFailureReason::Network
                        }
                    },
                };
                self.report_metrics(Metrics::UpdateCheckFailureReason(failure_reason));

                self.report_attempts_to_succeed(false).await;
            }
        }

        co.yield_(StateMachineEvent::ScheduleChange(self.context.schedule.clone())).await;
        co.yield_(StateMachineEvent::ProtocolStateChange(self.context.state.clone())).await;
        co.yield_(StateMachineEvent::UpdateCheckResult(result)).await;

        self.persist_data().await;
    }

    /// Update `CONSECUTIVE_FAILED_UPDATE_CHECKS` in storage and report the metrics if `success`.
    /// Does not commit the change to storage.
    async fn report_attempts_to_succeed(&mut self, success: bool) {
        let storage_ref = self.storage_ref.clone();
        let mut storage = storage_ref.lock().await;
        let attempts = storage.get_int(CONSECUTIVE_FAILED_UPDATE_CHECKS).await.unwrap_or(0) + 1;
        if success {
            storage.remove_or_log(CONSECUTIVE_FAILED_UPDATE_CHECKS).await;
            self.report_metrics(Metrics::AttemptsToSucceed(attempts as u64));
        } else {
            if let Err(e) = storage.set_int(CONSECUTIVE_FAILED_UPDATE_CHECKS, attempts).await {
                error!("Unable to persist {}: {}", CONSECUTIVE_FAILED_UPDATE_CHECKS, e);
            }
        }
    }

    /// Persist all necessary data to storage.
    async fn persist_data(&self) {
        let mut storage = self.storage_ref.lock().await;
        self.context.persist(&mut *storage).await;
        self.app_set.persist(&mut *storage).await;

        storage.commit_or_log().await;
    }

    /// This function constructs the chain of async futures needed to perform all of the async tasks
    /// that comprise an update check.
    async fn perform_update_check(
        &mut self,
        request_params: RequestParams,
        apps: Vec<App>,
        co: &mut async_generator::Yield<StateMachineEvent>,
    ) -> Result<update_check::Response, UpdateCheckError> {
        self.set_state(State::CheckingForUpdates, co).await;

        self.report_check_interval().await;

        // Construct a request for the app(s).
        let config = self.config.clone();
        let mut request_builder = RequestBuilder::new(&config, &request_params);
        for app in &apps {
            request_builder = request_builder.add_update_check(app).add_ping(app);
        }
        let session_id = GUID::new();
        request_builder = request_builder.session_id(session_id.clone());

        let update_check_start_time = Instant::now();
        let mut omaha_request_attempt = 1;
        let max_omaha_request_attempts = 3;
        let (_parts, data) = loop {
            request_builder = request_builder.request_id(GUID::new());
            match self.do_omaha_request_and_update_context(&request_builder, co).await {
                Ok(res) => {
                    break res;
                }
                Err(OmahaRequestError::Json(e)) => {
                    error!("Unable to construct request body! {:?}", e);
                    self.set_state(State::ErrorCheckingForUpdate, co).await;
                    return Err(UpdateCheckError::OmahaRequest(e.into()));
                }
                Err(OmahaRequestError::HttpBuilder(e)) => {
                    error!("Unable to construct HTTP request! {:?}", e);
                    self.set_state(State::ErrorCheckingForUpdate, co).await;
                    return Err(UpdateCheckError::OmahaRequest(e.into()));
                }
                Err(OmahaRequestError::HttpTransport(e)) => {
                    warn!("Unable to contact Omaha: {:?}", e);
                    // Don't retry if the error was caused by user code, which means we weren't
                    // using the library correctly.
                    if omaha_request_attempt >= max_omaha_request_attempts
                        || e.is_user()
                        || self.context.state.server_dictated_poll_interval.is_some()
                    {
                        self.set_state(State::ErrorCheckingForUpdate, co).await;
                        return Err(UpdateCheckError::OmahaRequest(e.into()));
                    }
                }
                Err(OmahaRequestError::HttpStatus(e)) => {
                    warn!("Unable to contact Omaha: {:?}", e);
                    if omaha_request_attempt >= max_omaha_request_attempts
                        || self.context.state.server_dictated_poll_interval.is_some()
                    {
                        self.set_state(State::ErrorCheckingForUpdate, co).await;
                        return Err(UpdateCheckError::OmahaRequest(e.into()));
                    }
                }
            }

            // TODO(41738): Move this to Policy.
            // Randomized exponential backoff of 1, 2, & 4 seconds, +/- 500ms.
            let backoff_time_secs = 1 << (omaha_request_attempt - 1);
            let backoff_time = randomize(backoff_time_secs * 1000, 1000);
            info!("Waiting {} ms before retrying...", backoff_time);
            self.timer.wait_for(Duration::from_millis(backoff_time)).await;

            omaha_request_attempt += 1;
        };

        self.report_metrics(Metrics::UpdateCheckResponseTime(update_check_start_time.elapsed()));
        self.report_metrics(Metrics::UpdateCheckRetries(omaha_request_attempt));

        let response = match Self::parse_omaha_response(&data) {
            Ok(res) => res,
            Err(err) => {
                warn!("Unable to parse Omaha response: {:?}", err);
                self.set_state(State::ErrorCheckingForUpdate, co).await;
                self.report_omaha_event_and_update_context(
                    &request_params,
                    Event::error(EventErrorCode::ParseResponse),
                    &apps,
                    &session_id,
                    co,
                )
                .await;
                return Err(UpdateCheckError::ResponseParser(err));
            }
        };

        info!("result: {:?}", response);

        co.yield_(StateMachineEvent::OmahaServerResponse(response.clone())).await;

        let statuses = Self::get_app_update_statuses(&response);
        for (app_id, status) in &statuses {
            // TODO:  Report or metric statuses other than 'no-update' and 'ok'
            info!("Omaha update check status: {} => {:?}", app_id, status);
        }

        let some_app_has_update = statuses.iter().any(|(_id, status)| **status == OmahaStatus::Ok);
        if !some_app_has_update {
            // A successful, no-update, check

            self.set_state(State::NoUpdateAvailable, co).await;
            Ok(Self::make_response(response, update_check::Action::NoUpdate))
        } else {
            info!(
                "At least one app has an update, proceeding to build and process an Install Plan"
            );
            let install_plan = match IN::InstallPlan::try_create_from(&request_params, &response) {
                Ok(plan) => plan,
                Err(e) => {
                    error!("Unable to construct install plan! {}", e);
                    self.set_state(State::InstallingUpdate, co).await;
                    self.set_state(State::InstallationError, co).await;
                    self.report_omaha_event_and_update_context(
                        &request_params,
                        Event::error(EventErrorCode::ConstructInstallPlan),
                        &apps,
                        &session_id,
                        co,
                    )
                    .await;
                    return Err(UpdateCheckError::InstallPlan(e.into()));
                }
            };

            info!("Validating Install Plan with Policy");
            let install_plan_decision = self.policy_engine.update_can_start(&install_plan).await;
            match install_plan_decision {
                UpdateDecision::Ok => {
                    info!("Proceeding with install plan.");
                }
                UpdateDecision::DeferredByPolicy => {
                    info!("Install plan was deferred by Policy.");
                    // Report "error" to Omaha (as this is an event that needs reporting as the
                    // install isn't starting immediately.
                    let event = Event {
                        event_type: EventType::UpdateComplete,
                        event_result: EventResult::UpdateDeferred,
                        ..Event::default()
                    };
                    self.report_omaha_event_and_update_context(
                        &request_params,
                        event,
                        &apps,
                        &session_id,
                        co,
                    )
                    .await;

                    self.set_state(State::InstallationDeferredByPolicy, co).await;
                    return Ok(Self::make_response(
                        response,
                        update_check::Action::DeferredByPolicy,
                    ));
                }
                UpdateDecision::DeniedByPolicy => {
                    warn!("Install plan was denied by Policy, see Policy logs for reasoning");
                    self.report_omaha_event_and_update_context(
                        &request_params,
                        Event::error(EventErrorCode::DeniedByPolicy),
                        &apps,
                        &session_id,
                        co,
                    )
                    .await;
                    return Ok(Self::make_response(response, update_check::Action::DeniedByPolicy));
                }
            }

            self.set_state(State::InstallingUpdate, co).await;
            self.report_omaha_event_and_update_context(
                &request_params,
                Event::success(EventType::UpdateDownloadStarted),
                &apps,
                &session_id,
                co,
            )
            .await;

            let install_plan_id = install_plan.id();
            let update_start_time = SystemTime::from(self.time_source.now());
            let update_first_seen_time =
                self.record_update_first_seen_time(&install_plan_id, update_start_time).await;

            let (send, mut recv) = mpsc::channel(0);
            let observer = StateMachineProgressObserver(send);
            let perform_install = async {
                let result = self.installer.perform_install(&install_plan, Some(&observer)).await;
                // Drop observer so that we can stop waiting for the next progress.
                drop(observer);
                result
            };
            let yield_progress = async {
                while let Some(progress) = recv.next().await {
                    co.yield_(StateMachineEvent::InstallProgressChange(progress)).await;
                }
            };

            let (install_result, ()) = future::join(perform_install, yield_progress).await;
            if let Err(e) = install_result {
                warn!("Installation failed: {}", e);
                self.set_state(State::InstallationError, co).await;
                self.report_omaha_event_and_update_context(
                    &request_params,
                    Event::error(EventErrorCode::Installation),
                    &apps,
                    &session_id,
                    co,
                )
                .await;

                match SystemTime::from(self.time_source.now()).duration_since(update_start_time) {
                    Ok(duration) => self.report_metrics(Metrics::FailedUpdateDuration(duration)),
                    Err(e) => warn!("Update start time is in the future: {}", e),
                }
                return Ok(Self::make_response(
                    response,
                    update_check::Action::InstallPlanExecutionError,
                ));
            }

            self.report_omaha_event_and_update_context(
                &request_params,
                Event::success(EventType::UpdateDownloadFinished),
                &apps,
                &session_id,
                co,
            )
            .await;

            // TODO: Verify downloaded update if needed.

            self.report_omaha_event_and_update_context(
                &request_params,
                Event::success(EventType::UpdateComplete),
                &apps,
                &session_id,
                co,
            )
            .await;

            let update_finish_time = SystemTime::from(self.time_source.now());
            match update_finish_time.duration_since(update_start_time) {
                Ok(duration) => self.report_metrics(Metrics::SuccessfulUpdateDuration(duration)),
                Err(e) => warn!("Update start time is in the future: {}", e),
            }
            match update_finish_time.duration_since(update_first_seen_time) {
                Ok(duration) => {
                    self.report_metrics(Metrics::SuccessfulUpdateFromFirstSeen(duration))
                }
                Err(e) => warn!("Update first seen time is in the future: {}", e),
            }
            {
                let mut storage = self.storage_ref.lock().await;
                if let Err(e) = storage.set_time(UPDATE_FINISH_TIME, update_finish_time).await {
                    error!("Unable to persist {}: {}", UPDATE_FINISH_TIME, e);
                }
                let target_version = response
                    .apps
                    .iter()
                    .nth(0)
                    .and_then(|app| app.update_check.as_ref())
                    .and_then(|update_check| update_check.manifest.as_ref())
                    .map(|manifest| manifest.version.as_str())
                    .unwrap_or_else(|| {
                        error!("Target version string not found in Omaha response.");
                        "UNKNOWN"
                    });
                if let Err(e) = storage.set_string(TARGET_VERSION, target_version).await {
                    error!("Unable to persist {}: {}", TARGET_VERSION, e);
                }
                storage.commit_or_log().await;
            }
            self.set_state(State::WaitingForReboot, co).await;
            Ok(Self::make_response(response, update_check::Action::Updated))
        }
    }

    /// Report the given |event| to Omaha, errors occurred during reporting are logged but not
    /// acted on.
    async fn report_omaha_event_and_update_context<'a>(
        &'a mut self,
        request_params: &'a RequestParams,
        event: Event,
        apps: &'a Vec<App>,
        session_id: &GUID,
        co: &mut async_generator::Yield<StateMachineEvent>,
    ) {
        let config = self.config.clone();
        let mut request_builder = RequestBuilder::new(&config, &request_params);
        for app in apps {
            request_builder = request_builder.add_event(app, &event);
        }
        request_builder = request_builder.session_id(session_id.clone()).request_id(GUID::new());
        if let Err(e) = self.do_omaha_request_and_update_context(&request_builder, co).await {
            warn!("Unable to report event to Omaha: {:?}", e);
        }
    }

    /// Sends a ping to Omaha and updates context and app_set.
    async fn ping_omaha(&mut self, co: &mut async_generator::Yield<StateMachineEvent>) {
        let apps = self.app_set.to_vec().await;
        let request_params =
            RequestParams { source: InstallSource::ScheduledTask, use_configured_proxies: true };
        let config = self.config.clone();
        let mut request_builder = RequestBuilder::new(&config, &request_params);
        for app in &apps {
            request_builder = request_builder.add_ping(app);
        }
        request_builder = request_builder.session_id(GUID::new()).request_id(GUID::new());

        let (_parts, data) =
            match self.do_omaha_request_and_update_context(&request_builder, co).await {
                Ok(res) => res,
                Err(e) => {
                    error!("Ping Omaha failed: {:#}", anyhow!(e));
                    return;
                }
            };

        let response = match Self::parse_omaha_response(&data) {
            Ok(res) => res,
            Err(e) => {
                error!("Unable to parse Omaha response: {:#}", anyhow!(e));
                return;
            }
        };

        // Even though this is a ping, we should still update the last_update_time for
        // policy to compute the next ping time.
        self.context.schedule.last_update_time = Some(self.time_source.now().into());
        co.yield_(StateMachineEvent::ScheduleChange(self.context.schedule.clone())).await;

        let result = Self::make_response(response, update_check::Action::NoUpdate);

        self.app_set.update_from_omaha(&result.app_responses).await;

        self.persist_data().await;
    }

    /// Make an http request to Omaha, and collect the response into an error or a blob of bytes
    /// that can be parsed.
    ///
    /// Given the http client and the request build, this makes the http request, and then coalesces
    /// the various errors into a single error type for easier error handling by the make process
    /// flow.
    ///
    /// This function also converts an HTTP error response into an Error, to divert those into the
    /// error handling paths instead of the Ok() path.
    ///
    /// If a valid X-Retry-After header is found in the response, this function will update the
    /// server dictated poll interval in context.
    async fn do_omaha_request_and_update_context<'a>(
        &'a mut self,
        builder: &RequestBuilder<'a>,
        co: &mut async_generator::Yield<StateMachineEvent>,
    ) -> Result<(Parts, Vec<u8>), OmahaRequestError> {
        let (parts, body) = Self::make_request(&mut self.http, builder.build()?).await?;
        // Clients MUST respect this header even if paired with non-successful HTTP response code.
        let server_dictated_poll_interval = parts.headers.get(X_RETRY_AFTER).and_then(|header| {
            match header
                .to_str()
                .map_err(|e| anyhow!(e))
                .and_then(|s| s.parse::<u64>().map_err(|e| anyhow!(e)))
            {
                Ok(seconds) => {
                    // Servers SHOULD NOT send a value in excess of 86400 (24 hours), and clients
                    // SHOULD treat values greater than 86400 as 86400.
                    Some(Duration::from_secs(min(seconds, 86400)))
                }
                Err(e) => {
                    error!("Unable to parse {} header: {:#}", X_RETRY_AFTER, e);
                    None
                }
            }
        });
        if self.context.state.server_dictated_poll_interval != server_dictated_poll_interval {
            self.context.state.server_dictated_poll_interval = server_dictated_poll_interval;
            co.yield_(StateMachineEvent::ProtocolStateChange(self.context.state.clone())).await;
            let mut storage = self.storage_ref.lock().await;
            self.context.persist(&mut *storage).await;
            storage.commit_or_log().await;
        }
        if !parts.status.is_success() {
            // Convert HTTP failure responses into Errors.
            Err(OmahaRequestError::HttpStatus(parts.status))
        } else {
            // Pass successful responses to the caller.
            info!("Omaha HTTP response: {}", parts.status);
            Ok((parts, body))
        }
    }

    /// Make an http request and collect the response body into a Vec of bytes.
    ///
    /// Specifically, this takes the body of the response and concatenates it into a single Vec of
    /// bytes so that any errors in receiving it can be captured immediately, instead of needing to
    /// handle them as part of parsing the response body.
    async fn make_request(
        http_client: &mut HR,
        request: http::Request<hyper::Body>,
    ) -> Result<(Parts, Vec<u8>), http_request::Error> {
        info!("Making http request to: {}", request.uri());
        let res = http_client.request(request).await.map_err(|err| {
            warn!("Unable to perform request: {}", err);
            err
        })?;

        let (parts, body) = res.into_parts();

        let data = body
            .try_fold(Vec::new(), |mut vec, b| async move {
                vec.extend(b);
                Ok(vec)
            })
            .await?;

        Ok((parts, data))
    }

    /// This method takes the response bytes from Omaha, and converts them into a protocol::Response
    /// struct, returning all of the various errors that can occur in that process as a consolidated
    /// error enum.
    fn parse_omaha_response(data: &[u8]) -> Result<Response, ResponseParseError> {
        parse_json_response(&data).map_err(ResponseParseError::Json)
    }

    /// Utility to extract pairs of app id => omaha status response, to make it easier to ask
    /// questions about the response.
    fn get_app_update_statuses(response: &Response) -> Vec<(&str, &OmahaStatus)> {
        response
            .apps
            .iter()
            .filter_map(|app| match &app.update_check {
                None => None,
                Some(u) => Some((app.id.as_str(), &u.status)),
            })
            .collect()
    }

    /// Utility to take a set of protocol::response::Apps and then construct a response from the
    /// update check based on those app IDs.
    ///
    /// TODO: Change the Policy and Installer to return a set of results, one for each app ID, then
    ///       make this match that.
    fn make_response(
        response: protocol::response::Response,
        action: update_check::Action,
    ) -> update_check::Response {
        update_check::Response {
            app_responses: response
                .apps
                .iter()
                .map(|app| update_check::AppResponse {
                    app_id: app.id.clone(),
                    cohort: app.cohort.clone(),
                    user_counting: response.daystart.clone().into(),
                    result: action.clone(),
                })
                .collect(),
        }
    }

    /// Update the state internally and send it to the observer.
    async fn set_state(
        &mut self,
        state: State,
        co: &mut async_generator::Yield<StateMachineEvent>,
    ) {
        self.state = state.clone();
        co.yield_(StateMachineEvent::StateChange(state)).await;
    }

    fn report_metrics(&mut self, metrics: Metrics) {
        if let Err(err) = self.metrics_reporter.report_metrics(metrics) {
            warn!("Unable to report metrics: {:?}", err);
        }
    }

    async fn record_update_first_seen_time(
        &mut self,
        install_plan_id: &str,
        now: SystemTime,
    ) -> SystemTime {
        let mut storage = self.storage_ref.lock().await;
        let previous_id = storage.get_string(INSTALL_PLAN_ID).await;
        if let Some(previous_id) = previous_id {
            if previous_id == install_plan_id {
                return storage.get_time(UPDATE_FIRST_SEEN_TIME).await.unwrap_or(now);
            }
        }
        // Update INSTALL_PLAN_ID and UPDATE_FIRST_SEEN_TIME for new update.
        if let Err(e) = storage.set_string(INSTALL_PLAN_ID, install_plan_id).await {
            error!("Unable to persist {}: {}", INSTALL_PLAN_ID, e);
            return now;
        }
        if let Err(e) = storage.set_time(UPDATE_FIRST_SEEN_TIME, now).await {
            error!("Unable to persist {}: {}", UPDATE_FIRST_SEEN_TIME, e);
            let _ = storage.remove(INSTALL_PLAN_ID).await;
            return now;
        }
        storage.commit_or_log().await;
        now
    }
}

/// Return a random number in [n - range / 2, n - range / 2 + range).
fn randomize(n: u64, range: u64) -> u64 {
    n - range / 2 + rand::random::<u64>() % range
}

#[cfg(test)]
impl<PE, HR, IN, TM, MR, ST> StateMachine<PE, HR, IN, TM, MR, ST>
where
    PE: PolicyEngine,
    HR: HttpRequest,
    IN: Installer,
    TM: Timer,
    MR: MetricsReporter,
    ST: Storage,
{
    /// Run perform_update_check once, returning the update check result.
    pub async fn oneshot(&mut self) -> Result<update_check::Response, UpdateCheckError> {
        let request_params = RequestParams::default();

        let apps = self.app_set.to_vec().await;

        async_generator::generate(move |mut co| async move {
            self.perform_update_check(request_params, apps, &mut co).await
        })
        .into_complete()
        .await
    }

    /// Run start_upate_check once, discarding its states.
    pub async fn run_once(&mut self) {
        let request_params = RequestParams::default();

        async_generator::generate(move |mut co| async move {
            self.start_update_check(request_params, &mut co).await;
        })
        .map(|_| ())
        .collect::<()>()
        .await;
    }
}

#[cfg(test)]
mod tests {
    use super::update_check::*;
    use super::*;
    use crate::{
        common::{
            App, CheckOptions, PersistedApp, ProtocolState, UpdateCheckSchedule, UserCounting,
        },
        configuration::Updater,
        http_request::mock::MockHttpRequest,
        installer::{
            stub::{StubInstallErrors, StubInstaller, StubPlan},
            ProgressObserver,
        },
        metrics::MockMetricsReporter,
        policy::{MockPolicyEngine, StubPolicyEngine},
        protocol::{request::OS, response, Cohort},
        storage::MemStorage,
        time::{
            timers::{BlockingTimer, MockTimer, RequestedWait},
            MockTimeSource, PartialComplexTime,
        },
    };
    use anyhow::anyhow;
    use futures::executor::{block_on, LocalPool};
    use futures::future::BoxFuture;
    use futures::task::LocalSpawnExt;
    use log::info;
    use matches::assert_matches;
    use pretty_assertions::assert_eq;
    use serde_json::json;
    use std::cell::RefCell;
    use std::time::Duration;
    use version::Version;

    fn make_test_app_set() -> AppSet {
        AppSet::new(vec![App::builder("{00000000-0000-0000-0000-000000000001}", [1, 2, 3, 4])
            .with_cohort(Cohort::new("stable-channel"))
            .build()])
    }

    // Assert that the last request made to |http| is equal to the request built by
    // |request_builder|.
    async fn assert_request<'a>(http: MockHttpRequest, request_builder: RequestBuilder<'a>) {
        let body = request_builder.build().unwrap().into_body();
        let body = body
            .try_fold(Vec::new(), |mut vec, b| async move {
                vec.extend(b);
                Ok(vec)
            })
            .await
            .unwrap();
        // Compare string instead of Vec<u8> for easier debugging.
        let body_str = String::from_utf8_lossy(&body);
        http.assert_body_str(&body_str).await;
    }

    #[test]
    fn run_simple_check_with_noupdate_result() {
        block_on(async {
            let response = json!({"response":{
              "server": "prod",
              "protocol": "3.0",
              "app": [{
                "appid": "{00000000-0000-0000-0000-000000000001}",
                "status": "ok",
                "updatecheck": {
                  "status": "noupdate"
                }
              }]
            }});
            let response = serde_json::to_vec(&response).unwrap();
            let http = MockHttpRequest::new(hyper::Response::new(response.into()));

            StateMachineBuilder::new_stub().http(http).oneshot().await.unwrap();

            info!("update check complete!");
        });
    }

    #[test]
    fn test_cohort_returned_with_noupdate_result() {
        block_on(async {
            let response = json!({"response":{
              "server": "prod",
              "protocol": "3.0",
              "app": [{
                "appid": "{00000000-0000-0000-0000-000000000001}",
                "status": "ok",
                "cohort": "1",
                "cohortname": "stable-channel",
                "updatecheck": {
                  "status": "noupdate"
                }
              }]
            }});
            let response = serde_json::to_vec(&response).unwrap();
            let http = MockHttpRequest::new(hyper::Response::new(response.into()));

            let response = StateMachineBuilder::new_stub().http(http).oneshot().await.unwrap();
            assert_eq!("{00000000-0000-0000-0000-000000000001}", response.app_responses[0].app_id);
            assert_eq!(Some("1".into()), response.app_responses[0].cohort.id);
            assert_eq!(Some("stable-channel".into()), response.app_responses[0].cohort.name);
            assert_eq!(None, response.app_responses[0].cohort.hint);
        });
    }

    #[test]
    fn test_report_parse_response_error() {
        block_on(async {
            let http = MockHttpRequest::new(hyper::Response::new("invalid response".into()));

            let mut state_machine = StateMachineBuilder::new_stub().http(http).build().await;

            let response = state_machine.oneshot().await;
            assert_matches!(response, Err(UpdateCheckError::ResponseParser(_)));

            let request_params = RequestParams::default();
            let mut request_builder = RequestBuilder::new(&state_machine.config, &request_params);
            let event = Event::error(EventErrorCode::ParseResponse);
            let apps = state_machine.app_set.to_vec().await;
            request_builder = request_builder
                .add_event(&apps[0], &event)
                .session_id(GUID::from_u128(0))
                .request_id(GUID::from_u128(2));
            assert_request(state_machine.http, request_builder).await;
        });
    }

    #[test]
    fn test_report_construct_install_plan_error() {
        block_on(async {
            let response = json!({"response":{
              "server": "prod",
              "protocol": "4.0",
              "app": [{
                "appid": "{00000000-0000-0000-0000-000000000001}",
                "status": "ok",
                "updatecheck": {
                  "status": "ok"
                }
              }],
            }});
            let response = serde_json::to_vec(&response).unwrap();
            let http = MockHttpRequest::new(hyper::Response::new(response.into()));

            let mut state_machine = StateMachineBuilder::new_stub().http(http).build().await;

            let response = state_machine.oneshot().await;
            assert_matches!(response, Err(UpdateCheckError::InstallPlan(_)));

            let request_params = RequestParams::default();
            let mut request_builder = RequestBuilder::new(&state_machine.config, &request_params);
            let event = Event::error(EventErrorCode::ConstructInstallPlan);
            let apps = state_machine.app_set.to_vec().await;
            request_builder = request_builder
                .add_event(&apps[0], &event)
                .session_id(GUID::from_u128(0))
                .request_id(GUID::from_u128(2));
            assert_request(state_machine.http, request_builder).await;
        });
    }

    #[test]
    fn test_report_installation_error() {
        block_on(async {
            let response = json!({"response":{
              "server": "prod",
              "protocol": "3.0",
              "app": [{
                "appid": "{00000000-0000-0000-0000-000000000001}",
                "status": "ok",
                "updatecheck": {
                  "status": "ok"
                }
              }],
            }});
            let response = serde_json::to_vec(&response).unwrap();
            let http = MockHttpRequest::new(hyper::Response::new(response.into()));

            let mut state_machine = StateMachineBuilder::new_stub()
                .http(http)
                .installer(StubInstaller { should_fail: true })
                .build()
                .await;

            let response = state_machine.oneshot().await.unwrap();
            assert_eq!(Action::InstallPlanExecutionError, response.app_responses[0].result);

            let request_params = RequestParams::default();
            let mut request_builder = RequestBuilder::new(&state_machine.config, &request_params);
            let event = Event::error(EventErrorCode::Installation);
            let apps = state_machine.app_set.to_vec().await;
            request_builder = request_builder
                .add_event(&apps[0], &event)
                .session_id(GUID::from_u128(0))
                .request_id(GUID::from_u128(3));
            assert_request(state_machine.http, request_builder).await;
        });
    }

    #[test]
    fn test_report_deferred_by_policy() {
        block_on(async {
            let response = json!({"response":{
              "server": "prod",
              "protocol": "3.0",
              "app": [{
                "appid": "{00000000-0000-0000-0000-000000000001}",
                "status": "ok",
                "updatecheck": {
                  "status": "ok"
                }
              }],
            }});
            let response = serde_json::to_vec(&response).unwrap();
            let http = MockHttpRequest::new(hyper::Response::new(response.into()));

            let policy_engine = MockPolicyEngine {
                update_decision: UpdateDecision::DeferredByPolicy,
                ..MockPolicyEngine::default()
            };
            let mut state_machine = StateMachineBuilder::new_stub()
                .policy_engine(policy_engine)
                .http(http)
                .build()
                .await;

            let response = state_machine.oneshot().await.unwrap();
            assert_eq!(Action::DeferredByPolicy, response.app_responses[0].result);

            let request_params = RequestParams::default();
            let mut request_builder = RequestBuilder::new(&state_machine.config, &request_params);
            let event = Event {
                event_type: EventType::UpdateComplete,
                event_result: EventResult::UpdateDeferred,
                ..Event::default()
            };
            let apps = state_machine.app_set.to_vec().await;
            request_builder = request_builder
                .add_event(&apps[0], &event)
                .session_id(GUID::from_u128(0))
                .request_id(GUID::from_u128(2));
            assert_request(state_machine.http, request_builder).await;
        });
    }

    #[test]
    fn test_report_denied_by_policy() {
        block_on(async {
            let response = json!({"response":{
              "server": "prod",
              "protocol": "3.0",
              "app": [{
                "appid": "{00000000-0000-0000-0000-000000000001}",
                "status": "ok",
                "updatecheck": {
                  "status": "ok"
                }
              }],
            }});
            let response = serde_json::to_vec(&response).unwrap();
            let http = MockHttpRequest::new(hyper::Response::new(response.into()));
            let policy_engine = MockPolicyEngine {
                update_decision: UpdateDecision::DeniedByPolicy,
                ..MockPolicyEngine::default()
            };

            let mut state_machine = StateMachineBuilder::new_stub()
                .policy_engine(policy_engine)
                .http(http)
                .build()
                .await;

            let response = state_machine.oneshot().await.unwrap();
            assert_eq!(Action::DeniedByPolicy, response.app_responses[0].result);

            let request_params = RequestParams::default();
            let mut request_builder = RequestBuilder::new(&state_machine.config, &request_params);
            let event = Event::error(EventErrorCode::DeniedByPolicy);
            let apps = state_machine.app_set.to_vec().await;
            request_builder = request_builder
                .add_event(&apps[0], &event)
                .session_id(GUID::from_u128(0))
                .request_id(GUID::from_u128(2));
            assert_request(state_machine.http, request_builder).await;
        });
    }

    #[test]
    fn test_wait_timer() {
        let mut pool = LocalPool::new();
        let mock_time = MockTimeSource::new_from_now();
        let next_update_time = mock_time.now() + Duration::from_secs(111);
        let (timer, mut timers) = BlockingTimer::new();
        let policy_engine = MockPolicyEngine {
            check_timing: Some(CheckTiming::builder().time(next_update_time).build()),
            time_source: mock_time,
            ..MockPolicyEngine::default()
        };

        let (_ctl, state_machine) = pool.run_until(
            StateMachineBuilder::new_stub().policy_engine(policy_engine).timer(timer).start(),
        );

        pool.spawner().spawn_local(state_machine.map(|_| ()).collect()).unwrap();

        // With otherwise stub implementations, the pool stalls when a timer is awaited.  Dropping
        // the state machine will panic if any timer durations were not used.
        let blocked_timer = pool.run_until(timers.next()).unwrap();
        assert_eq!(blocked_timer.requested_wait(), RequestedWait::Until(next_update_time.into()));
    }

    #[test]
    fn test_cohort_and_user_counting_updates_are_used_in_subsequent_requests() {
        block_on(async {
            let response = json!({"response":{
                "server": "prod",
                "protocol": "3.0",
                "daystart": {
                  "elapsed_days": 1234567,
                  "elapsed_seconds": 3645
                },
                "app": [{
                  "appid": "{00000000-0000-0000-0000-000000000001}",
                  "status": "ok",
                  "cohort": "1",
                  "cohortname": "stable-channel",
                  "updatecheck": {
                    "status": "noupdate"
                  }
                }]
            }});
            let response = serde_json::to_vec(&response).unwrap();
            let mut http = MockHttpRequest::new(hyper::Response::new(response.clone().into()));
            http.add_response(hyper::Response::new(response.into()));
            let last_request_viewer = MockHttpRequest::from_request_cell(http.get_request_cell());
            let apps = make_test_app_set();

            let mut state_machine =
                StateMachineBuilder::new_stub().http(http).app_set(apps.clone()).build().await;

            // Run it the first time.
            state_machine.run_once().await;

            let apps = apps.to_vec().await;
            assert_eq!(Some("1".to_string()), apps[0].cohort.id);
            assert_eq!(None, apps[0].cohort.hint);
            assert_eq!(Some("stable-channel".to_string()), apps[0].cohort.name);
            assert_eq!(UserCounting::ClientRegulatedByDate(Some(1234567)), apps[0].user_counting);

            // Run it the second time.
            state_machine.run_once().await;

            let request_params = RequestParams::default();
            let expected_request_builder =
                RequestBuilder::new(&state_machine.config, &request_params)
                    .add_update_check(&apps[0])
                    .add_ping(&apps[0])
                    .session_id(GUID::from_u128(2))
                    .request_id(GUID::from_u128(3));
            // Check that the second update check used the new app.
            assert_request(last_request_viewer, expected_request_builder).await;
        });
    }

    #[test]
    fn test_user_counting_returned() {
        block_on(async {
            let response = json!({"response":{
            "server": "prod",
            "protocol": "3.0",
            "daystart": {
              "elapsed_days": 1234567,
              "elapsed_seconds": 3645
            },
            "app": [{
              "appid": "{00000000-0000-0000-0000-000000000001}",
              "status": "ok",
              "cohort": "1",
              "cohortname": "stable-channel",
              "updatecheck": {
                "status": "noupdate"
                  }
              }]
            }});
            let response = serde_json::to_vec(&response).unwrap();
            let http = MockHttpRequest::new(hyper::Response::new(response.into()));

            let response = StateMachineBuilder::new_stub().http(http).oneshot().await.unwrap();

            assert_eq!(
                UserCounting::ClientRegulatedByDate(Some(1234567)),
                response.app_responses[0].user_counting
            );
        });
    }

    #[test]
    fn test_observe_state() {
        block_on(async {
            let actual_states = StateMachineBuilder::new_stub()
                .oneshot_check()
                .await
                .filter_map(|event| {
                    future::ready(match event {
                        StateMachineEvent::StateChange(state) => Some(state),
                        _ => None,
                    })
                })
                .collect::<Vec<State>>()
                .await;

            let expected_states = vec![State::CheckingForUpdates, State::ErrorCheckingForUpdate];
            assert_eq!(actual_states, expected_states);
        });
    }

    #[test]
    fn test_observe_schedule() {
        block_on(async {
            let mock_time = MockTimeSource::new_from_now();
            let actual_schedules = StateMachineBuilder::new_stub()
                .policy_engine(StubPolicyEngine::new(&mock_time))
                .oneshot_check()
                .await
                .filter_map(|event| {
                    future::ready(match event {
                        StateMachineEvent::ScheduleChange(schedule) => Some(schedule),
                        _ => None,
                    })
                })
                .collect::<Vec<UpdateCheckSchedule>>()
                .await;

            // The resultant schedule should only contain the timestamp of the above update check.
            let expected_schedule =
                UpdateCheckSchedule::builder().last_time(mock_time.now()).build();

            assert_eq!(actual_schedules, vec![expected_schedule]);
        });
    }

    #[test]
    fn test_observe_protocol_state() {
        block_on(async {
            let actual_protocol_states = StateMachineBuilder::new_stub()
                .oneshot_check()
                .await
                .filter_map(|event| {
                    future::ready(match event {
                        StateMachineEvent::ProtocolStateChange(state) => Some(state),
                        _ => None,
                    })
                })
                .collect::<Vec<ProtocolState>>()
                .await;

            let expected_protocol_state =
                ProtocolState { consecutive_failed_update_checks: 1, ..ProtocolState::default() };

            assert_eq!(actual_protocol_states, vec![expected_protocol_state]);
        });
    }

    #[test]
    fn test_observe_omaha_server_response() {
        block_on(async {
            let response = json!({"response":{
              "server": "prod",
              "protocol": "3.0",
              "app": [{
                "appid": "{00000000-0000-0000-0000-000000000001}",
                "status": "ok",
                "cohort": "1",
                "cohortname": "stable-channel",
                "updatecheck": {
                  "status": "noupdate"
                }
              }]
            }});
            let response = serde_json::to_vec(&response).unwrap();
            let expected_omaha_response = response::parse_json_response(&response).unwrap();
            let http = MockHttpRequest::new(hyper::Response::new(response.into()));

            let actual_omaha_response = StateMachineBuilder::new_stub()
                .http(http)
                .oneshot_check()
                .await
                .filter_map(|event| {
                    future::ready(match event {
                        StateMachineEvent::OmahaServerResponse(response) => Some(response),
                        _ => None,
                    })
                })
                .collect::<Vec<response::Response>>()
                .await;

            assert_eq!(actual_omaha_response, vec![expected_omaha_response]);
        });
    }

    #[test]
    fn test_metrics_report_update_check_response_time() {
        block_on(async {
            let mut metrics_reporter = MockMetricsReporter::new();
            let _response = StateMachineBuilder::new_stub()
                .metrics_reporter(&mut metrics_reporter)
                .oneshot()
                .await;

            assert!(!metrics_reporter.metrics.is_empty());
            match &metrics_reporter.metrics[0] {
                Metrics::UpdateCheckResponseTime(_) => {} // expected
                metric => panic!("Unexpected metric {:?}", metric),
            }
        });
    }

    #[test]
    fn test_metrics_report_update_check_retries() {
        block_on(async {
            let mut metrics_reporter = MockMetricsReporter::new();
            let _response = StateMachineBuilder::new_stub()
                .metrics_reporter(&mut metrics_reporter)
                .oneshot()
                .await;

            assert!(metrics_reporter.metrics.contains(&Metrics::UpdateCheckRetries(1)));
        });
    }

    #[test]
    fn test_update_check_retries_backoff_with_mock_timer() {
        block_on(async {
            let mut timer = MockTimer::new();
            timer.expect_for_range(Duration::from_millis(500), Duration::from_millis(1500));
            timer.expect_for_range(Duration::from_millis(1500), Duration::from_millis(2500));
            let requested_waits = timer.get_requested_waits_view();
            let response = StateMachineBuilder::new_stub()
                .http(MockHttpRequest::empty())
                .timer(timer)
                .oneshot()
                .await;

            let waits = requested_waits.borrow();
            assert_eq!(waits.len(), 2);
            assert_matches!(
                waits[0],
                RequestedWait::For(d) if d >= Duration::from_millis(500) && d <= Duration::from_millis(1500)
            );
            assert_matches!(
                waits[1],
                RequestedWait::For(d) if d >= Duration::from_millis(1500) && d <= Duration::from_millis(2500)
            );

            assert_matches!(
                response,
                Err(UpdateCheckError::OmahaRequest(OmahaRequestError::HttpStatus(_)))
            );
        });
    }

    #[test]
    fn test_metrics_report_update_check_failure_reason_omaha() {
        block_on(async {
            let mut metrics_reporter = MockMetricsReporter::new();
            let mut state_machine = StateMachineBuilder::new_stub()
                .metrics_reporter(&mut metrics_reporter)
                .build()
                .await;

            state_machine.run_once().await;

            assert!(metrics_reporter
                .metrics
                .contains(&Metrics::UpdateCheckFailureReason(UpdateCheckFailureReason::Omaha)));
        });
    }

    #[test]
    fn test_metrics_report_update_check_failure_reason_network() {
        block_on(async {
            let mut metrics_reporter = MockMetricsReporter::new();
            let mut state_machine = StateMachineBuilder::new_stub()
                .http(MockHttpRequest::empty())
                .metrics_reporter(&mut metrics_reporter)
                .build()
                .await;

            state_machine.run_once().await;

            assert!(metrics_reporter
                .metrics
                .contains(&Metrics::UpdateCheckFailureReason(UpdateCheckFailureReason::Network)));
        });
    }

    #[test]
    fn test_persist_last_update_time() {
        block_on(async {
            let storage = Rc::new(Mutex::new(MemStorage::new()));

            StateMachineBuilder::new_stub()
                .storage(Rc::clone(&storage))
                .oneshot_check()
                .await
                .map(|_| ())
                .collect::<()>()
                .await;

            let storage = storage.lock().await;
            storage.get_int(LAST_UPDATE_TIME).await.unwrap();
            assert_eq!(true, storage.committed());
        });
    }

    #[test]
    fn test_persist_server_dictated_poll_interval() {
        block_on(async {
            let response = json!({"response":{
              "server": "prod",
              "protocol": "3.0",
              "app": [{
                "appid": "{00000000-0000-0000-0000-000000000001}",
                "status": "ok",
                "updatecheck": {
                  "status": "noupdate"
                }
              }]
            }});
            let response = serde_json::to_vec(&response).unwrap();
            let response = hyper::Response::builder()
                .header(X_RETRY_AFTER, 1234)
                .body(response.into())
                .unwrap();
            let http = MockHttpRequest::new(response);
            let storage = Rc::new(Mutex::new(MemStorage::new()));

            let mut state_machine = StateMachineBuilder::new_stub()
                .http(http)
                .storage(Rc::clone(&storage))
                .build()
                .await;
            state_machine.oneshot().await.unwrap();

            assert_eq!(
                state_machine.context.state.server_dictated_poll_interval,
                Some(Duration::from_secs(1234))
            );

            let storage = storage.lock().await;
            assert_eq!(storage.get_int(SERVER_DICTATED_POLL_INTERVAL).await, Some(1234000000));
            assert!(storage.committed());
        });
    }

    #[test]
    fn test_persist_server_dictated_poll_interval_http_error() {
        block_on(async {
            let response = hyper::Response::builder()
                .status(hyper::StatusCode::INTERNAL_SERVER_ERROR)
                .header(X_RETRY_AFTER, 1234)
                .body(hyper::Body::empty())
                .unwrap();
            let http = MockHttpRequest::new(response);
            let storage = Rc::new(Mutex::new(MemStorage::new()));

            let mut state_machine = StateMachineBuilder::new_stub()
                .http(http)
                .storage(Rc::clone(&storage))
                .build()
                .await;
            assert_matches!(
                state_machine.oneshot().await,
                Err(UpdateCheckError::OmahaRequest(OmahaRequestError::HttpStatus(_)))
            );

            assert_eq!(
                state_machine.context.state.server_dictated_poll_interval,
                Some(Duration::from_secs(1234))
            );

            let storage = storage.lock().await;
            assert_eq!(storage.get_int(SERVER_DICTATED_POLL_INTERVAL).await, Some(1234000000));
            assert!(storage.committed());
        });
    }

    #[test]
    fn test_persist_server_dictated_poll_interval_max_duration() {
        block_on(async {
            let response = hyper::Response::builder()
                .status(hyper::StatusCode::INTERNAL_SERVER_ERROR)
                .header(X_RETRY_AFTER, 123456789)
                .body(hyper::Body::empty())
                .unwrap();
            let http = MockHttpRequest::new(response);
            let storage = Rc::new(Mutex::new(MemStorage::new()));

            let mut state_machine = StateMachineBuilder::new_stub()
                .http(http)
                .storage(Rc::clone(&storage))
                .build()
                .await;
            assert_matches!(
                state_machine.oneshot().await,
                Err(UpdateCheckError::OmahaRequest(OmahaRequestError::HttpStatus(_)))
            );

            assert_eq!(
                state_machine.context.state.server_dictated_poll_interval,
                Some(Duration::from_secs(86400))
            );

            let storage = storage.lock().await;
            assert_eq!(storage.get_int(SERVER_DICTATED_POLL_INTERVAL).await, Some(86400000000));
            assert!(storage.committed());
        });
    }

    #[test]
    fn test_server_dictated_poll_interval_with_transport_error_no_retry() {
        block_on(async {
            let mut http = MockHttpRequest::empty();
            http.add_error(http_request::mock_errors::make_transport_error());
            let mut storage = MemStorage::new();
            storage.set_int(SERVER_DICTATED_POLL_INTERVAL, 1234000000);
            storage.commit();
            let storage = Rc::new(Mutex::new(storage));

            let mut state_machine = StateMachineBuilder::new_stub()
                .http(http)
                .storage(Rc::clone(&storage))
                .build()
                .await;
            // This verifies that state machine does not retry because MockHttpRequest will only
            // return the transport error on the first request, any additional requests will get
            // HttpStatus error.
            assert_matches!(
                state_machine.oneshot().await,
                Err(UpdateCheckError::OmahaRequest(OmahaRequestError::HttpTransport(_)))
            );

            assert_eq!(
                state_machine.context.state.server_dictated_poll_interval,
                Some(Duration::from_secs(1234))
            );
        });
    }

    #[test]
    fn test_persist_app() {
        block_on(async {
            let storage = Rc::new(Mutex::new(MemStorage::new()));
            let app_set = make_test_app_set();

            StateMachineBuilder::new_stub()
                .storage(Rc::clone(&storage))
                .app_set(app_set.clone())
                .oneshot_check()
                .await
                .map(|_| ())
                .collect::<()>()
                .await;

            let storage = storage.lock().await;
            let apps = app_set.to_vec().await;
            storage.get_string(&apps[0].id).await.unwrap();
            assert!(storage.committed());
        });
    }

    #[test]
    fn test_load_last_update_time() {
        block_on(async {
            let mut storage = MemStorage::new();
            let mut mock_time = MockTimeSource::new_from_now();
            mock_time.truncate_submicrosecond_walltime();
            let last_update_time = mock_time.now_in_walltime() - Duration::from_secs(999);
            storage.set_time(LAST_UPDATE_TIME, last_update_time).await.unwrap();

            let state_machine = StateMachineBuilder::new_stub()
                .policy_engine(StubPolicyEngine::new(&mock_time))
                .storage(Rc::new(Mutex::new(storage)))
                .build()
                .await;

            assert_eq!(
                state_machine.context.schedule.last_update_time.unwrap(),
                PartialComplexTime::Wall(last_update_time)
            );
        });
    }

    #[test]
    fn test_load_server_dictated_poll_interval() {
        block_on(async {
            let mut storage = MemStorage::new();
            storage.set_int(SERVER_DICTATED_POLL_INTERVAL, 56789).await.unwrap();

            let state_machine =
                StateMachineBuilder::new_stub().storage(Rc::new(Mutex::new(storage))).build().await;

            assert_eq!(
                Some(Duration::from_micros(56789)),
                state_machine.context.state.server_dictated_poll_interval
            );
        });
    }

    #[test]
    fn test_load_app() {
        block_on(async {
            let app_set = AppSet::new(vec![App::builder(
                "{00000000-0000-0000-0000-000000000001}",
                [1, 2, 3, 4],
            )
            .build()]);
            let mut storage = MemStorage::new();
            let persisted_app = PersistedApp {
                cohort: Cohort {
                    id: Some("cohort_id".to_string()),
                    hint: Some("test_channel".to_string()),
                    name: None,
                },
                user_counting: UserCounting::ClientRegulatedByDate(Some(22222)),
            };
            let json = serde_json::to_string(&persisted_app).unwrap();
            let apps = app_set.to_vec().await;
            storage.set_string(&apps[0].id, &json).await.unwrap();

            let _state_machine = StateMachineBuilder::new_stub()
                .storage(Rc::new(Mutex::new(storage)))
                .app_set(app_set.clone())
                .build()
                .await;

            let apps = app_set.to_vec().await;
            assert_eq!(persisted_app.cohort, apps[0].cohort);
            assert_eq!(UserCounting::ClientRegulatedByDate(Some(22222)), apps[0].user_counting);
        });
    }

    #[test]
    fn test_report_check_interval() {
        block_on(async {
            let mut mock_time = MockTimeSource::new_from_now();
            mock_time.truncate_submicrosecond_walltime();
            let start_time = mock_time.now();
            let mut state_machine = StateMachineBuilder::new_stub()
                .policy_engine(StubPolicyEngine::new(mock_time.clone()))
                .metrics_reporter(MockMetricsReporter::new())
                .storage(Rc::new(Mutex::new(MemStorage::new())))
                .build()
                .await;

            state_machine.report_check_interval().await;
            // No metrics should be reported because no last check time in storage.
            assert!(state_machine.metrics_reporter.metrics.is_empty());
            {
                let storage = state_machine.storage_ref.lock().await;
                assert_eq!(storage.get_time(LAST_CHECK_TIME).await.unwrap(), start_time.wall);
                assert_eq!(storage.len(), 1);
                assert!(storage.committed());
            }
            // A second update check should report metrics.
            let duration = Duration::from_micros(999999);
            mock_time.advance(duration);

            let later_time = mock_time.now();

            state_machine.report_check_interval().await;

            assert_eq!(
                state_machine.metrics_reporter.metrics,
                vec![Metrics::UpdateCheckInterval(duration)]
            );
            let storage = state_machine.storage_ref.lock().await;
            assert_eq!(storage.get_time(LAST_CHECK_TIME).await.unwrap(), later_time.wall);
            assert_eq!(storage.len(), 1);
            assert!(storage.committed());
        });
    }

    #[derive(Debug)]
    pub struct TestInstaller {
        reboot_called: Rc<RefCell<bool>>,
        should_fail: bool,
        mock_time: MockTimeSource,
    }
    struct TestInstallerBuilder {
        should_fail: Option<bool>,
        mock_time: MockTimeSource,
    }
    impl TestInstaller {
        fn builder(mock_time: MockTimeSource) -> TestInstallerBuilder {
            TestInstallerBuilder { should_fail: None, mock_time }
        }
    }
    impl TestInstallerBuilder {
        fn should_fail(mut self, should_fail: bool) -> Self {
            self.should_fail = Some(should_fail);
            self
        }
        fn build(self) -> TestInstaller {
            TestInstaller {
                reboot_called: Rc::new(RefCell::new(false)),
                should_fail: self.should_fail.unwrap_or(false),
                mock_time: self.mock_time,
            }
        }
    }
    const INSTALL_DURATION: Duration = Duration::from_micros(98765433);

    impl Installer for TestInstaller {
        type InstallPlan = StubPlan;
        type Error = StubInstallErrors;
        fn perform_install<'a>(
            &'a mut self,
            _install_plan: &StubPlan,
            observer: Option<&'a dyn ProgressObserver>,
        ) -> BoxFuture<'a, Result<(), Self::Error>> {
            if self.should_fail {
                future::ready(Err(StubInstallErrors::Failed)).boxed()
            } else {
                self.mock_time.advance(INSTALL_DURATION);
                async move {
                    if let Some(observer) = observer {
                        observer.receive_progress(None, 0.0, None, None).await;
                        observer.receive_progress(None, 0.3, None, None).await;
                        observer.receive_progress(None, 0.9, None, None).await;
                        observer.receive_progress(None, 1.0, None, None).await;
                    }
                    Ok(())
                }
                .boxed()
            }
        }

        fn perform_reboot(&mut self) -> BoxFuture<'_, Result<(), anyhow::Error>> {
            self.reboot_called.replace(true);
            if self.should_fail {
                future::ready(Err(anyhow!("reboot failed"))).boxed()
            } else {
                future::ready(Ok(())).boxed()
            }
        }
    }

    #[test]
    fn test_report_successful_update_duration() {
        block_on(async {
            let response = json!({"response":{
              "server": "prod",
              "protocol": "3.0",
              "app": [{
                "appid": "{00000000-0000-0000-0000-000000000001}",
                "status": "ok",
                "updatecheck": {
                  "status": "ok"
                }
              }],
            }});
            let response = serde_json::to_vec(&response).unwrap();
            let http = MockHttpRequest::new(hyper::Response::new(response.into()));
            let storage = Rc::new(Mutex::new(MemStorage::new()));

            let mut mock_time = MockTimeSource::new_from_now();
            mock_time.truncate_submicrosecond_walltime();
            let now = mock_time.now();

            let update_completed_time = now + INSTALL_DURATION;
            let expected_update_duration = update_completed_time.wall_duration_since(now).unwrap();

            let first_seen_time = now - Duration::from_micros(100000000);

            let expected_duration_since_first_seen =
                update_completed_time.wall_duration_since(first_seen_time).unwrap();

            let mut state_machine = StateMachineBuilder::new_stub()
                .http(http)
                .installer(TestInstaller::builder(mock_time.clone()).build())
                .policy_engine(StubPolicyEngine::new(mock_time.clone()))
                .metrics_reporter(MockMetricsReporter::new())
                .storage(Rc::clone(&storage))
                .build()
                .await;

            {
                let mut storage = storage.lock().await;
                storage.set_string(INSTALL_PLAN_ID, "").await.unwrap();
                storage.set_time(UPDATE_FIRST_SEEN_TIME, first_seen_time).await.unwrap();
                storage.commit().await.unwrap();
            }

            state_machine.run_once().await;

            let reported_metrics = state_machine.metrics_reporter.metrics;
            assert_eq!(
                reported_metrics
                    .iter()
                    .filter(|m| match m {
                        Metrics::SuccessfulUpdateDuration(_) => true,
                        _ => false,
                    })
                    .collect::<Vec<_>>(),
                vec![&Metrics::SuccessfulUpdateDuration(expected_update_duration)]
            );
            assert_eq!(
                reported_metrics
                    .iter()
                    .filter(|m| match m {
                        Metrics::SuccessfulUpdateFromFirstSeen(_) => true,
                        _ => false,
                    })
                    .collect::<Vec<_>>(),
                vec![&Metrics::SuccessfulUpdateFromFirstSeen(expected_duration_since_first_seen)]
            );
        });
    }

    #[test]
    fn test_report_failed_update_duration() {
        block_on(async {
            let response = json!({"response":{
              "server": "prod",
              "protocol": "3.0",
              "app": [{
                "appid": "{00000000-0000-0000-0000-000000000001}",
                "status": "ok",
                "updatecheck": {
                  "status": "ok"
                }
              }],
            }});
            let response = serde_json::to_vec(&response).unwrap();
            let http = MockHttpRequest::new(hyper::Response::new(response.into()));
            let mut state_machine = StateMachineBuilder::new_stub()
                .http(http)
                .installer(StubInstaller { should_fail: true })
                .metrics_reporter(MockMetricsReporter::new())
                .build()
                .await;
            // clock::mock::set(time::i64_to_time(123456789));

            state_machine.run_once().await;

            assert!(state_machine
                .metrics_reporter
                .metrics
                .contains(&Metrics::FailedUpdateDuration(Duration::from_micros(0))));
        });
    }

    #[test]
    fn test_record_update_first_seen_time() {
        block_on(async {
            let storage = Rc::new(Mutex::new(MemStorage::new()));
            let mut state_machine =
                StateMachineBuilder::new_stub().storage(Rc::clone(&storage)).build().await;

            let mut mock_time = MockTimeSource::new_from_now();
            mock_time.truncate_submicrosecond_walltime();
            let now = mock_time.now_in_walltime();
            assert_eq!(state_machine.record_update_first_seen_time("id", now).await, now);
            {
                let storage = storage.lock().await;
                assert_eq!(storage.get_string(INSTALL_PLAN_ID).await, Some("id".to_string()));
                assert_eq!(storage.get_time(UPDATE_FIRST_SEEN_TIME).await, Some(now));
                assert_eq!(storage.len(), 2);
                assert!(storage.committed());
            }

            mock_time.advance(Duration::from_secs(1000));
            let now2 = mock_time.now_in_walltime();
            assert_eq!(state_machine.record_update_first_seen_time("id", now2).await, now);
            {
                let storage = storage.lock().await;
                assert_eq!(storage.get_string(INSTALL_PLAN_ID).await, Some("id".to_string()));
                assert_eq!(storage.get_time(UPDATE_FIRST_SEEN_TIME).await, Some(now));
                assert_eq!(storage.len(), 2);
                assert!(storage.committed());
            }
            assert_eq!(state_machine.record_update_first_seen_time("id2", now2).await, now2);
            {
                let storage = storage.lock().await;
                assert_eq!(storage.get_string(INSTALL_PLAN_ID).await, Some("id2".to_string()));
                assert_eq!(storage.get_time(UPDATE_FIRST_SEEN_TIME).await, Some(now2));
                assert_eq!(storage.len(), 2);
                assert!(storage.committed());
            }
        });
    }

    #[test]
    fn test_report_attempts_to_succeed() {
        block_on(async {
            let storage = Rc::new(Mutex::new(MemStorage::new()));
            let mut state_machine = StateMachineBuilder::new_stub()
                .installer(StubInstaller { should_fail: true })
                .metrics_reporter(MockMetricsReporter::new())
                .storage(Rc::clone(&storage))
                .build()
                .await;

            state_machine.report_attempts_to_succeed(true).await;
            {
                let storage = storage.lock().await;
                assert_eq!(storage.get_int(CONSECUTIVE_FAILED_UPDATE_CHECKS).await, None);
                assert_eq!(storage.len(), 0);
            }
            assert_eq!(state_machine.metrics_reporter.metrics, vec![Metrics::AttemptsToSucceed(1)]);

            state_machine.report_attempts_to_succeed(false).await;
            {
                let storage = storage.lock().await;
                assert_eq!(storage.get_int(CONSECUTIVE_FAILED_UPDATE_CHECKS).await, Some(1));
                assert_eq!(storage.len(), 1);
            }

            state_machine.report_attempts_to_succeed(false).await;
            {
                let storage = storage.lock().await;
                assert_eq!(storage.get_int(CONSECUTIVE_FAILED_UPDATE_CHECKS).await, Some(2));
                assert_eq!(storage.len(), 1);
            }

            state_machine.report_attempts_to_succeed(true).await;
            {
                let storage = storage.lock().await;
                assert_eq!(storage.get_int(CONSECUTIVE_FAILED_UPDATE_CHECKS).await, None);
                assert_eq!(storage.len(), 0);
            }
            assert_eq!(
                state_machine.metrics_reporter.metrics,
                vec![Metrics::AttemptsToSucceed(1), Metrics::AttemptsToSucceed(3)]
            );
        });
    }

    #[test]
    fn test_successful_update_triggers_reboot() {
        let mut pool = LocalPool::new();
        let spawner = pool.spawner();

        let response = json!({"response":{
          "server": "prod",
          "protocol": "3.0",
          "app": [{
            "appid": "{00000000-0000-0000-0000-000000000001}",
            "status": "ok",
            "updatecheck": {
              "status": "ok"
            }
          }],
        }});
        let response = serde_json::to_vec(&response).unwrap();
        let http = MockHttpRequest::new(hyper::Response::new(response.into()));
        let mock_time = MockTimeSource::new_from_now();
        let next_update_time = mock_time.now();
        let (timer, mut timers) = BlockingTimer::new();

        let installer = TestInstaller::builder(mock_time.clone()).build();
        let reboot_called = Rc::clone(&installer.reboot_called);
        let (_ctl, state_machine) = pool.run_until(
            StateMachineBuilder::new_stub()
                .http(http)
                .installer(installer)
                .policy_engine(StubPolicyEngine::new(mock_time))
                .timer(timer)
                .start(),
        );
        let observer = TestObserver::default();
        spawner.spawn_local(observer.observe(state_machine)).unwrap();

        let blocked_timer = pool.run_until(timers.next()).unwrap();
        assert_eq!(blocked_timer.requested_wait(), RequestedWait::Until(next_update_time.into()));
        blocked_timer.unblock();
        pool.run_until_stalled();

        assert!(*reboot_called.borrow());
    }

    #[test]
    fn test_failed_update_does_not_trigger_reboot() {
        let mut pool = LocalPool::new();
        let spawner = pool.spawner();

        let response = json!({"response":{
          "server": "prod",
          "protocol": "3.0",
          "app": [{
            "appid": "{00000000-0000-0000-0000-000000000001}",
            "status": "ok",
            "updatecheck": {
              "status": "ok"
            }
          }],
        }});
        let response = serde_json::to_vec(&response).unwrap();
        let http = MockHttpRequest::new(hyper::Response::new(response.into()));
        let mock_time = MockTimeSource::new_from_now();
        let next_update_time = mock_time.now();
        let (timer, mut timers) = BlockingTimer::new();

        let installer = TestInstaller::builder(mock_time.clone()).should_fail(true).build();
        let reboot_called = Rc::clone(&installer.reboot_called);
        let (_ctl, state_machine) = pool.run_until(
            StateMachineBuilder::new_stub()
                .http(http)
                .installer(installer)
                .policy_engine(StubPolicyEngine::new(mock_time))
                .timer(timer)
                .start(),
        );
        let observer = TestObserver::default();
        spawner.spawn_local(observer.observe(state_machine)).unwrap();

        let blocked_timer = pool.run_until(timers.next()).unwrap();
        assert_eq!(blocked_timer.requested_wait(), RequestedWait::Until(next_update_time.into()));
        blocked_timer.unblock();
        pool.run_until_stalled();

        assert!(!*reboot_called.borrow());
    }

    // Verifies that if reboot is not allowed, state machine will send pings to Omaha while waiting
    // for reboot, and it will reply AlreadyRunning to any StartUpdateCheck requests, and when it's
    // finally time to reboot, it will trigger reboot.
    #[test]
    fn test_wait_for_reboot() {
        let mut pool = LocalPool::new();
        let spawner = pool.spawner();

        let response = json!({"response":{
          "server": "prod",
          "protocol": "3.0",
          "app": [{
            "appid": "{00000000-0000-0000-0000-000000000001}",
            "status": "ok",
            "updatecheck": {
              "status": "ok"
            }
          }],
        }});
        let response = serde_json::to_vec(&response).unwrap();
        let mut http = MockHttpRequest::new(hyper::Response::new(response.clone().into()));
        // Responses to events.
        http.add_response(hyper::Response::new(response.clone().into()));
        http.add_response(hyper::Response::new(response.clone().into()));
        http.add_response(hyper::Response::new(response.clone().into()));
        // Response to the ping.
        http.add_response(hyper::Response::new(response.into()));
        let ping_request_viewer = MockHttpRequest::from_request_cell(http.get_request_cell());
        let second_ping_request_viewer =
            MockHttpRequest::from_request_cell(http.get_request_cell());
        let mut mock_time = MockTimeSource::new_from_now();
        mock_time.truncate_submicrosecond_walltime();
        let next_update_time = mock_time.now() + Duration::from_secs(1000);
        let (timer, mut timers) = BlockingTimer::new();
        let reboot_allowed = Rc::new(RefCell::new(false));
        let policy_engine = MockPolicyEngine {
            time_source: mock_time.clone(),
            reboot_allowed: Rc::clone(&reboot_allowed),
            check_timing: Some(CheckTiming::builder().time(next_update_time).build()),
            ..MockPolicyEngine::default()
        };
        let installer = TestInstaller::builder(mock_time.clone()).build();
        let reboot_called = Rc::clone(&installer.reboot_called);
        let storage_ref = Rc::new(Mutex::new(MemStorage::new()));
        let apps = make_test_app_set();

        let (mut ctl, state_machine) = pool.run_until(
            StateMachineBuilder::new_stub()
                .app_set(apps.clone())
                .http(http)
                .installer(installer)
                .policy_engine(policy_engine)
                .timer(timer)
                .storage(Rc::clone(&storage_ref))
                .start(),
        );

        let observer = TestObserver::default();
        spawner.spawn_local(observer.observe(state_machine)).unwrap();

        // The first wait before update check.
        let blocked_timer = pool.run_until(timers.next()).unwrap();
        assert_eq!(blocked_timer.requested_wait(), RequestedWait::Until(next_update_time.into()));
        blocked_timer.unblock();
        pool.run_until_stalled();

        // The timers for reboot and ping, even though the order should be deterministic, but that
        // is an implementation detail, the test should still pass if that order changes.
        let blocked_timer1 = pool.run_until(timers.next()).unwrap();
        let blocked_timer2 = pool.run_until(timers.next()).unwrap();
        let (wait_for_reboot_timer, wait_for_next_ping_timer) =
            match blocked_timer1.requested_wait() {
                RequestedWait::For(_) => (blocked_timer1, blocked_timer2),
                RequestedWait::Until(_) => (blocked_timer2, blocked_timer1),
            };
        // This is the timer waiting for next reboot_allowed check.
        assert_eq!(
            wait_for_reboot_timer.requested_wait(),
            RequestedWait::For(CHECK_REBOOT_ALLOWED_INTERVAL)
        );
        // This is the timer waiting for the next ping.
        assert_eq!(
            wait_for_next_ping_timer.requested_wait(),
            RequestedWait::Until(next_update_time.into())
        );
        // Unblock the ping.
        mock_time.advance(Duration::from_secs(1000));
        wait_for_next_ping_timer.unblock();
        pool.run_until_stalled();

        // Verify that it sends a ping.
        let config = crate::configuration::test_support::config_generator();
        let request_params = RequestParams::default();
        let apps = pool.run_until(apps.to_vec());
        let mut expected_request_builder = RequestBuilder::new(&config, &request_params)
            // 0: session id for update check
            // 1: request id for update check
            // 2-4: request id for events
            .session_id(GUID::from_u128(5))
            .request_id(GUID::from_u128(6));
        for app in &apps {
            expected_request_builder = expected_request_builder.add_ping(&app);
        }
        pool.run_until(assert_request(ping_request_viewer, expected_request_builder));

        pool.run_until(async {
            assert_eq!(
                ctl.start_update_check(CheckOptions::default()).await,
                Ok(StartUpdateCheckResponse::AlreadyRunning)
            );
        });

        // Last update time is updated in storage.
        pool.run_until(async {
            let storage = storage_ref.lock().await;
            let context = update_check::Context::load(&*storage).await;
            assert_eq!(context.schedule.last_update_time, Some(mock_time.now_in_walltime().into()));
        });

        // State machine should be waiting for the next ping.
        let wait_for_next_ping_timer = pool.run_until(timers.next()).unwrap();
        assert_eq!(
            wait_for_next_ping_timer.requested_wait(),
            RequestedWait::Until(next_update_time.into())
        );

        // Let state machine check reboot_allowed again, but still don't allow it.
        wait_for_reboot_timer.unblock();
        pool.run_until_stalled();
        assert!(!*reboot_called.borrow());

        // State machine should be waiting for the next reboot.
        let wait_for_reboot_timer = pool.run_until(timers.next()).unwrap();
        assert_eq!(
            wait_for_reboot_timer.requested_wait(),
            RequestedWait::For(CHECK_REBOOT_ALLOWED_INTERVAL)
        );

        // Time for a second ping.
        wait_for_next_ping_timer.unblock();
        pool.run_until_stalled();

        // Verify that it sends another ping.
        let mut expected_request_builder = RequestBuilder::new(&config, &request_params)
            .session_id(GUID::from_u128(7))
            .request_id(GUID::from_u128(8));
        for app in &apps {
            expected_request_builder = expected_request_builder.add_ping(&app);
        }
        pool.run_until(assert_request(second_ping_request_viewer, expected_request_builder));

        assert!(!*reboot_called.borrow());

        // Now allow reboot.
        *reboot_called.borrow_mut() = true;
        wait_for_reboot_timer.unblock();
        pool.run_until_stalled();
        assert!(*reboot_called.borrow());
    }

    #[derive(Debug)]
    struct BlockingInstaller {
        on_install: mpsc::Sender<oneshot::Sender<Result<(), StubInstallErrors>>>,
    }

    impl Installer for BlockingInstaller {
        type InstallPlan = StubPlan;
        type Error = StubInstallErrors;

        fn perform_install(
            &mut self,
            _install_plan: &StubPlan,
            _observer: Option<&dyn ProgressObserver>,
        ) -> BoxFuture<'_, Result<(), StubInstallErrors>> {
            let (send, recv) = oneshot::channel();
            let send_fut = self.on_install.send(send);

            async move {
                send_fut.await.unwrap();
                recv.await.unwrap()
            }
            .boxed()
        }

        fn perform_reboot(&mut self) -> BoxFuture<'_, Result<(), anyhow::Error>> {
            future::ready(Ok(())).boxed()
        }
    }

    #[derive(Debug, Default)]
    struct TestObserver {
        states: Rc<RefCell<Vec<State>>>,
    }

    impl TestObserver {
        fn observe(&self, s: impl Stream<Item = StateMachineEvent>) -> impl Future<Output = ()> {
            let states = Rc::clone(&self.states);
            async move {
                futures::pin_mut!(s);
                while let Some(event) = s.next().await {
                    match event {
                        StateMachineEvent::StateChange(state) => {
                            states.borrow_mut().push(state);
                        }
                        _ => {}
                    }
                }
            }
        }

        fn observe_until_terminal(
            &self,
            s: impl Stream<Item = StateMachineEvent>,
        ) -> impl Future<Output = ()> {
            let states = Rc::clone(&self.states);
            async move {
                futures::pin_mut!(s);
                while let Some(event) = s.next().await {
                    match event {
                        StateMachineEvent::StateChange(state) => {
                            states.borrow_mut().push(state);
                            match state {
                                State::Idle | State::WaitingForReboot => return,
                                _ => {}
                            }
                        }
                        _ => {}
                    }
                }
            }
        }

        fn take_states(&self) -> Vec<State> {
            std::mem::replace(&mut *self.states.borrow_mut(), vec![])
        }
    }

    #[test]
    fn test_start_update_during_update_replies_with_in_progress() {
        let mut pool = LocalPool::new();
        let spawner = pool.spawner();

        let response = json!({"response":{
          "server": "prod",
          "protocol": "3.0",
          "app": [{
            "appid": "{00000000-0000-0000-0000-000000000001}",
            "status": "ok",
            "updatecheck": {
              "status": "ok"
            }
          }],
        }});
        let response = serde_json::to_vec(&response).unwrap();
        let http = MockHttpRequest::new(hyper::Response::new(response.into()));
        let (send_install, mut recv_install) = mpsc::channel(0);
        let (mut ctl, state_machine) = pool.run_until(
            StateMachineBuilder::new_stub()
                .http(http)
                .installer(BlockingInstaller { on_install: send_install })
                .start(),
        );

        let observer = TestObserver::default();
        spawner.spawn_local(observer.observe_until_terminal(state_machine)).unwrap();

        let unblock_install = pool.run_until(recv_install.next()).unwrap();
        pool.run_until_stalled();
        assert_eq!(
            observer.take_states(),
            vec![State::CheckingForUpdates, State::InstallingUpdate]
        );

        pool.run_until(async {
            assert_eq!(
                ctl.start_update_check(CheckOptions::default()).await,
                Ok(StartUpdateCheckResponse::AlreadyRunning)
            );
        });
        pool.run_until_stalled();
        assert_eq!(observer.take_states(), vec![]);

        unblock_install.send(Ok(())).unwrap();
        pool.run_until_stalled();

        assert_eq!(observer.take_states(), vec![State::WaitingForReboot]);
    }

    #[test]
    fn test_start_update_during_timer_starts_update() {
        let mut pool = LocalPool::new();
        let spawner = pool.spawner();

        let mut mock_time = MockTimeSource::new_from_now();
        let next_update_time = mock_time.now() + Duration::from_secs(321);

        let (timer, mut timers) = BlockingTimer::new();
        let policy_engine = MockPolicyEngine {
            check_timing: Some(CheckTiming::builder().time(next_update_time).build()),
            time_source: mock_time.clone(),
            ..MockPolicyEngine::default()
        };
        let (mut ctl, state_machine) = pool.run_until(
            StateMachineBuilder::new_stub().policy_engine(policy_engine).timer(timer).start(),
        );

        let observer = TestObserver::default();
        spawner.spawn_local(observer.observe(state_machine)).unwrap();

        let blocked_timer = pool.run_until(timers.next()).unwrap();
        assert_eq!(blocked_timer.requested_wait(), RequestedWait::Until(next_update_time.into()));
        mock_time.advance(Duration::from_secs(200));
        assert_eq!(observer.take_states(), vec![]);

        // Nothing happens while the timer is waiting.
        pool.run_until_stalled();
        assert_eq!(observer.take_states(), vec![]);

        blocked_timer.unblock();
        let blocked_timer = pool.run_until(timers.next()).unwrap();
        assert_eq!(blocked_timer.requested_wait(), RequestedWait::Until(next_update_time.into()));
        assert_eq!(
            observer.take_states(),
            vec![State::CheckingForUpdates, State::ErrorCheckingForUpdate, State::Idle]
        );

        // Unless a control signal to start an update check comes in.
        pool.run_until(async {
            assert_eq!(
                ctl.start_update_check(CheckOptions::default()).await,
                Ok(StartUpdateCheckResponse::Started)
            );
        });
        pool.run_until_stalled();
        assert_eq!(
            observer.take_states(),
            vec![State::CheckingForUpdates, State::ErrorCheckingForUpdate, State::Idle]
        );
    }

    #[test]
    fn test_start_update_check_returns_throttled() {
        let mut pool = LocalPool::new();
        let spawner = pool.spawner();

        let mut mock_time = MockTimeSource::new_from_now();
        let next_update_time = mock_time.now() + Duration::from_secs(321);

        let (timer, mut timers) = BlockingTimer::new();
        let policy_engine = MockPolicyEngine {
            check_timing: Some(CheckTiming::builder().time(next_update_time).build()),
            time_source: mock_time.clone(),
            check_decision: CheckDecision::ThrottledByPolicy,
            ..MockPolicyEngine::default()
        };
        let (mut ctl, state_machine) = pool.run_until(
            StateMachineBuilder::new_stub().policy_engine(policy_engine).timer(timer).start(),
        );

        let observer = TestObserver::default();
        spawner.spawn_local(observer.observe(state_machine)).unwrap();

        let blocked_timer = pool.run_until(timers.next()).unwrap();
        assert_eq!(blocked_timer.requested_wait(), RequestedWait::Until(next_update_time.into()));
        mock_time.advance(Duration::from_secs(200));
        assert_eq!(observer.take_states(), vec![]);

        pool.run_until(async {
            assert_eq!(
                ctl.start_update_check(CheckOptions::default()).await,
                Ok(StartUpdateCheckResponse::Throttled)
            );
        });
        pool.run_until_stalled();
        assert_eq!(observer.take_states(), vec![]);
    }

    #[test]
    fn test_progress_observer() {
        block_on(async {
            let response = json!({"response":{
              "server": "prod",
              "protocol": "3.0",
              "app": [{
                "appid": "{00000000-0000-0000-0000-000000000001}",
                "status": "ok",
                "updatecheck": {
                  "status": "ok"
                }
              }],
            }});
            let response = serde_json::to_vec(&response).unwrap();
            let http = MockHttpRequest::new(hyper::Response::new(response.into()));
            let mock_time = MockTimeSource::new_from_now();
            let progresses = StateMachineBuilder::new_stub()
                .http(http)
                .installer(TestInstaller::builder(mock_time.clone()).build())
                .policy_engine(StubPolicyEngine::new(mock_time))
                .oneshot_check()
                .await
                .filter_map(|event| {
                    future::ready(match event {
                        StateMachineEvent::InstallProgressChange(InstallProgress { progress }) => {
                            Some(progress)
                        }
                        _ => None,
                    })
                })
                .collect::<Vec<f32>>()
                .await;
            assert_eq!(progresses, [0.0, 0.3, 0.9, 1.0]);
        });
    }

    #[test]
    fn test_report_waited_for_reboot_duration() {
        let mut pool = LocalPool::new();
        let spawner = pool.spawner();

        let response = json!({"response": {
            "server": "prod",
            "protocol": "3.0",
            "app": [{
            "appid": "{00000000-0000-0000-0000-000000000001}",
            "status": "ok",
            "updatecheck": {
                "status": "ok",
                "manifest": {
                    "version": "1.2.3.5",
                    "actions": {
                        "action": [],
                    },
                    "packages": {
                        "package": [],
                    },
                }
            }
            }],
        }});
        let response = serde_json::to_vec(&response).unwrap();
        let http = MockHttpRequest::new(hyper::Response::new(response.into()));
        let mut mock_time = MockTimeSource::new_from_now();
        mock_time.truncate_submicrosecond_walltime();
        let storage = Rc::new(Mutex::new(MemStorage::new()));

        // Do one update.
        assert_matches!(
            pool.run_until(
                StateMachineBuilder::new_stub()
                    .http(http)
                    .policy_engine(StubPolicyEngine::new(mock_time.clone()))
                    .storage(Rc::clone(&storage))
                    .oneshot()
            ),
            Ok(_)
        );

        mock_time.advance(Duration::from_secs(999));

        // Execute state machine `run()`, simulating that we already rebooted.
        let config = Config {
            updater: Updater { name: "updater".to_string(), version: Version::from([0, 1]) },
            os: OS { version: "1.2.3.5".to_string(), ..OS::default() },
            service_url: "http://example.com/".to_string(),
        };
        let metrics_reporter = Rc::new(RefCell::new(MockMetricsReporter::new()));
        let (_ctl, state_machine) = pool.run_until(
            StateMachineBuilder::new_stub()
                .config(config)
                .metrics_reporter(Rc::clone(&metrics_reporter))
                .policy_engine(StubPolicyEngine::new(mock_time.clone()))
                .storage(Rc::clone(&storage))
                .timer(MockTimer::new())
                .start(),
        );

        // Move state machine forward using observer.
        let observer = TestObserver::default();
        spawner.spawn_local(observer.observe(state_machine)).unwrap();
        pool.run_until_stalled();

        assert_eq!(
            metrics_reporter
                .borrow()
                .metrics
                .iter()
                .filter(|m| match m {
                    Metrics::WaitedForRebootDuration(_) => true,
                    _ => false,
                })
                .collect::<Vec<_>>(),
            vec![&Metrics::WaitedForRebootDuration(Duration::from_secs(999))]
        );

        // Verify that storage is cleaned up.
        pool.run_until(async {
            let storage = storage.lock().await;
            assert_eq!(storage.get_time(UPDATE_FINISH_TIME).await, None);
            assert_eq!(storage.get_string(TARGET_VERSION).await, None);
            assert!(storage.committed());
        })
    }
}
