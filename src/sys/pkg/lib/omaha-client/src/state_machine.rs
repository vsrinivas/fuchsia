// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    clock,
    common::{App, AppSet, CheckOptions},
    configuration::Config,
    http_request::HttpRequest,
    installer::{Installer, Plan},
    metrics::{Metrics, MetricsReporter, UpdateCheckFailureReason},
    policy::{CheckDecision, PolicyEngine, UpdateDecision},
    protocol::{
        self,
        request::{Event, EventErrorCode, EventResult, EventType},
        response::{parse_json_response, OmahaStatus, Response},
    },
    request_builder::{self, RequestBuilder, RequestParams},
    storage::Storage,
};
#[cfg(test)]
use crate::{
    common::{ProtocolState, UpdateCheckSchedule},
    http_request::StubHttpRequest,
    installer::stub::StubInstaller,
    metrics::StubMetricsReporter,
    policy::StubPolicyEngine,
    storage::StubStorage,
};
use chrono::{DateTime, Utc};
use futures::{
    channel::{mpsc, oneshot},
    compat::Stream01CompatExt,
    lock::Mutex,
    prelude::*,
    select,
};
use http::response::Parts;
use log::{error, info, warn};
use std::rc::Rc;
use std::str::Utf8Error;
use std::time::{Duration, Instant, SystemTime};
use thiserror::Error;

mod time;
pub mod update_check;

pub mod timer;
#[cfg(test)]
pub use timer::MockTimer;
pub use timer::Timer;

mod observer;
pub use observer::StateMachineEvent;

const LAST_CHECK_TIME: &str = "last_check_time";
const INSTALL_PLAN_ID: &str = "install_plan_id";
const UPDATE_FIRST_SEEN_TIME: &str = "update_first_seen_time";
const CONSECUTIVE_FAILED_UPDATE_CHECKS: &str = "consecutive_failed_update_checks";

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
    client_config: Config,

    policy_engine: PE,

    http: HR,

    installer: IN,

    timer: TM,

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
    #[error("Unexpected JSON error constructing update check: {}", _0)]
    Json(serde_json::Error),

    #[error("Error building update check HTTP request: {}", _0)]
    HttpBuilder(http::Error),

    #[error("Hyper error performing update check: {}", _0)]
    Hyper(hyper::Error),

    #[error("HTTP error performing update check: {}", _0)]
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

impl From<hyper::Error> for OmahaRequestError {
    fn from(err: hyper::Error) -> Self {
        OmahaRequestError::Hyper(err)
    }
}

impl From<serde_json::Error> for OmahaRequestError {
    fn from(err: serde_json::Error) -> Self {
        OmahaRequestError::Json(err)
    }
}

impl From<http::Error> for OmahaRequestError {
    fn from(err: http::Error) -> Self {
        OmahaRequestError::HttpBuilder(err)
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
    #[error("Check not performed per policy: {:?}", _0)]
    Policy(CheckDecision),

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

/// Helper type to build/start a `StateMachine`.
#[derive(Debug)]
pub struct StateMachineBuilder<PE, HR, IN, TM, MR, ST>
where
    PE: PolicyEngine,
    HR: HttpRequest,
    IN: Installer,
    TM: Timer,
    MR: MetricsReporter,
    ST: Storage,
{
    policy_engine: PE,
    http: HR,
    installer: IN,
    timer: TM,
    metrics_reporter: MR,
    storage: Rc<Mutex<ST>>,
    config: Config,
    app_set: AppSet,
}

impl<'a, PE, HR, IN, TM, MR, ST> StateMachineBuilder<PE, HR, IN, TM, MR, ST>
where
    PE: 'a + PolicyEngine,
    HR: 'a + HttpRequest,
    IN: 'a + Installer,
    TM: 'a + Timer,
    MR: 'a + MetricsReporter,
    ST: 'a + Storage,
{
    /// Creates a new `StateMachineBuilder` using the given trait implementations.
    pub fn new(
        policy_engine: PE,
        http: HR,
        installer: IN,
        timer: TM,
        metrics_reporter: MR,
        storage: Rc<Mutex<ST>>,
        config: Config,
        app_set: AppSet,
    ) -> Self {
        Self { policy_engine, http, installer, timer, metrics_reporter, storage, config, app_set }
    }

    async fn build(self) -> StateMachine<PE, HR, IN, TM, MR, ST> {
        let StateMachineBuilder {
            policy_engine,
            http,
            installer,
            timer,
            metrics_reporter,
            storage,
            config,
            mut app_set,
        } = self;

        let ((), context) = {
            let storage = storage.lock().await;
            futures::join!(app_set.load(&*storage), update_check::Context::load(&*storage))
        };

        StateMachine {
            client_config: config,
            policy_engine,
            http,
            installer,
            timer,
            metrics_reporter,
            storage_ref: storage,
            context,
            state: State::Idle,
            app_set,
        }
    }

    /// Start the StateMachine to do periodic update checks in the background or when requested
    /// through the returned control handle.  The returned stream must be polled to make
    /// forward progress.
    // TODO: find a better name for this function.
    pub async fn start(self) -> (ControlHandle, impl Stream<Item = StateMachineEvent> + 'a) {
        let state_machine = self.build().await;

        let (send, recv) = mpsc::channel(0);
        (
            ControlHandle(send),
            async_generator::generate(move |co| state_machine.run(recv, co)).into_yielded(),
        )
    }

    /// Run start_upate_check once, returning a stream of the states it produces.
    #[cfg(test)]
    pub async fn oneshot_check(
        self,
        options: CheckOptions,
    ) -> impl Stream<Item = StateMachineEvent> + 'a {
        let mut state_machine = self.build().await;

        async_generator::generate(move |mut co| async move {
            state_machine.start_update_check(options, &mut co).await
        })
        .into_yielded()
    }

    /// Run perform_update_check once, returning the update check result.
    #[cfg(test)]
    pub async fn oneshot(self) -> Result<update_check::Response, UpdateCheckError> {
        self.build().await.oneshot().await
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
    /// Need to do this in a mutable method because the borrow checker isn't smart enough to know
    /// that different fields of the same struct (even if it's not self) are separate variables and
    /// can be borrowed at the same time.
    async fn update_next_update_time(
        &mut self,
        co: &mut async_generator::Yield<StateMachineEvent>,
    ) {
        let apps = self.app_set.to_vec().await;
        self.context.schedule = self
            .policy_engine
            .compute_next_update_time(&apps, &self.context.schedule, &self.context.state)
            .await;

        co.yield_(StateMachineEvent::ScheduleChange(self.context.schedule.clone())).await;
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

            // Still serve the control handle to let manual update checks happen.
            // TODO(fxb/48631): Remove this once integration tests have valid app set.
            loop {
                while let Some(request) = control.next().await {
                    let options = match request {
                        ControlRequest::StartUpdateCheck { options, responder } => {
                            let _ = responder.send(StartUpdateCheckResponse::Started);
                            options
                        }
                    };

                    let update_check = self.start_update_check(options, &mut co).fuse();
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
            }
        }

        loop {
            info!("Initial context: {:?}", self.context);

            self.update_next_update_time(&mut co).await;

            let now = clock::now();
            // Wait if |next_update_time| is in the future.
            let delay = if let Ok(mut duration) =
                self.context.schedule.next_update_time.duration_since(clock::now())
            {
                if duration > Duration::from_secs(1 * 60 * 60) {
                    warn!(
                        "update check duration exceeds hard 1 hour limit! The value was {} secs",
                        duration.as_secs()
                    );

                    // Cap the duration at 1 hour.
                    duration = Duration::from_secs(1 * 60 * 60);
                }
                let duration = duration; // strip mutability

                info!(
                    "Waiting until {} (or {} seconds) for the next update check (currently: {})",
                    DateTime::<Utc>::from(self.context.schedule.next_update_time).to_string(),
                    duration.as_secs(),
                    DateTime::<Utc>::from(now).to_string()
                );

                Some(self.timer.wait(duration).fuse())
            } else {
                None
            };

            let options = match delay {
                Some(mut delay) => {
                    select! {
                        () = delay => CheckOptions::default(),
                        ControlRequest::StartUpdateCheck{options, responder} = control.select_next_some() => {
                            let _ = responder.send(StartUpdateCheckResponse::Started);
                            options
                        }
                    }
                }
                None => CheckOptions::default(),
            };

            let update_check = self.start_update_check(options, &mut co).fuse();
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
    }

    /// Report update check interval based on the last check time stored in storage.
    /// It will also persist the new last check time to storage.
    async fn report_check_interval(&mut self) {
        // Clone the Rc first to avoid borrowing self for the rest of the function.
        let storage_ref = self.storage_ref.clone();
        let mut storage = storage_ref.lock().await;
        let now = clock::now();
        if let Some(last_check_time) = storage.get_int(LAST_CHECK_TIME).await {
            match now.duration_since(time::i64_to_time(last_check_time)) {
                Ok(duration) => self.report_metrics(Metrics::UpdateCheckInterval(duration)),
                Err(e) => warn!("Last check time is in the future: {}", e),
            }
        }
        if let Err(e) = storage.set_int(LAST_CHECK_TIME, time::time_to_i64(now)).await {
            error!("Unable to persist {}: {}", LAST_CHECK_TIME, e);
            return;
        }
        if let Err(e) = storage.commit().await {
            error!("Unable to commit persisted data: {}", e);
        }
    }

    /// Perform update check and handle the result, including updating the update check context
    /// and cohort.
    pub async fn start_update_check(
        &mut self,
        options: CheckOptions,
        co: &mut async_generator::Yield<StateMachineEvent>,
    ) {
        let apps = self.app_set.to_vec().await;
        let result = self.perform_update_check(options, self.context.clone(), apps, co).await;
        match &result {
            Ok(result) => {
                info!("Update check result: {:?}", result);
                // Update check succeeded, update |last_update_time|.
                self.context.schedule.last_update_time = clock::now();

                // Update the service dictated poll interval (which is an Option<>, so doesn't
                // need to be tested for existence here).
                self.context.state.server_dictated_poll_interval =
                    result.server_dictated_poll_interval;

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
                        self.context.schedule.last_update_time = clock::now();

                        UpdateCheckFailureReason::Omaha
                    }
                    UpdateCheckError::Policy(_) => UpdateCheckFailureReason::Internal,
                    UpdateCheckError::OmahaRequest(request_error) => match request_error {
                        OmahaRequestError::Json(_) | OmahaRequestError::HttpBuilder(_) => {
                            UpdateCheckFailureReason::Internal
                        }
                        OmahaRequestError::Hyper(_) | OmahaRequestError::HttpStatus(_) => {
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

        // TODO: This is the last place we read self.state, we should see if we can find another
        // way to achieve this so that we can remove self.state entirely.
        if self.state == State::WaitingForReboot {
            info!("Rebooting the system at the end of a successful update");
            if let Err(e) = self.installer.perform_reboot().await {
                error!("Unable to reboot the system: {}", e);
            }
        } else {
            self.set_state(State::Idle, co).await;
        }
    }

    /// Update `CONSECUTIVE_FAILED_UPDATE_CHECKS` in storage and report the metrics if `success`.
    /// Does not commit the change to storage.
    async fn report_attempts_to_succeed(&mut self, success: bool) {
        let storage_ref = self.storage_ref.clone();
        let mut storage = storage_ref.lock().await;
        let attempts = storage.get_int(CONSECUTIVE_FAILED_UPDATE_CHECKS).await.unwrap_or(0) + 1;
        if success {
            if let Err(e) = storage.remove(CONSECUTIVE_FAILED_UPDATE_CHECKS).await {
                error!("Unable to remove {}: {}", CONSECUTIVE_FAILED_UPDATE_CHECKS, e);
            }
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

        if let Err(e) = storage.commit().await {
            error!("Unable to commit persisted data: {}", e);
        }
    }

    /// This function constructs the chain of async futures needed to perform all of the async tasks
    /// that comprise an update check.
    async fn perform_update_check(
        &mut self,
        options: CheckOptions,
        context: update_check::Context,
        apps: Vec<App>,
        co: &mut async_generator::Yield<StateMachineEvent>,
    ) -> Result<update_check::Response, UpdateCheckError> {
        // TODO: Move this check outside perform_update_check() so that FIDL server can know if
        // update check is throttled.
        info!("Checking to see if an update check is allowed at this time for {:?}", apps);
        let decision = self
            .policy_engine
            .update_check_allowed(&apps, &context.schedule, &context.state, &options)
            .await;

        info!("The update check decision is: {:?}", decision);

        let request_params = match decision {
            // Positive results, will continue with the update check process
            CheckDecision::Ok(rp) | CheckDecision::OkUpdateDeferred(rp) => rp,

            // Negative results, exit early
            CheckDecision::TooSoon => {
                info!("Too soon for update check, ending");
                // TODO: Report status
                return Err(UpdateCheckError::Policy(decision));
            }
            CheckDecision::ThrottledByPolicy => {
                info!("Update check has been throttled by the Policy, ending");
                // TODO: Report status
                return Err(UpdateCheckError::Policy(decision));
            }
            CheckDecision::DeniedByPolicy => {
                info!("Update check has ben denied by the Policy");
                // TODO: Report status
                return Err(UpdateCheckError::Policy(decision));
            }
        };

        self.set_state(State::CheckingForUpdates, co).await;

        self.report_check_interval().await;

        // Construct a request for the app(s).
        let mut request_builder = RequestBuilder::new(&self.client_config, &request_params);
        for app in &apps {
            request_builder = request_builder.add_update_check(app).add_ping(app);
        }

        let update_check_start_time = Instant::now();
        let mut omaha_request_attempt = 1;
        let max_omaha_request_attempts = 3;
        let (_parts, data) = loop {
            match Self::do_omaha_request(&mut self.http, &request_builder).await {
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
                Err(OmahaRequestError::Hyper(e)) => {
                    warn!("Unable to contact Omaha: {:?}", e);
                    // Don't retry if the error was caused by user code, which means we weren't
                    // using the library correctly.
                    if omaha_request_attempt >= max_omaha_request_attempts || e.is_user() {
                        self.set_state(State::ErrorCheckingForUpdate, co).await;
                        return Err(UpdateCheckError::OmahaRequest(e.into()));
                    }
                }
                Err(OmahaRequestError::HttpStatus(e)) => {
                    warn!("Unable to contact Omaha: {:?}", e);
                    if omaha_request_attempt >= max_omaha_request_attempts {
                        self.set_state(State::ErrorCheckingForUpdate, co).await;
                        return Err(UpdateCheckError::OmahaRequest(e.into()));
                    }
                }
            }

            // TODO(41738): Move this to Policy.
            // Randomized exponential backoff of 1, 2, 4, ... seconds.
            let backoff_time_secs = 1 << (omaha_request_attempt - 1);
            let backoff_time = randomize(backoff_time_secs * 1000, 1000);
            info!("Waiting {} ms before retrying...", backoff_time);
            self.timer.wait(Duration::from_millis(backoff_time)).await;

            omaha_request_attempt += 1;
        };

        self.report_metrics(Metrics::UpdateCheckResponseTime(update_check_start_time.elapsed()));
        self.report_metrics(Metrics::UpdateCheckRetries(omaha_request_attempt));

        let response = match Self::parse_omaha_response(&data) {
            Ok(res) => res,
            Err(err) => {
                warn!("Unable to parse Omaha response: {:?}", err);
                self.set_state(State::ErrorCheckingForUpdate, co).await;
                let event = Event {
                    event_type: EventType::UpdateComplete,
                    errorcode: Some(EventErrorCode::ParseResponse),
                    ..Event::default()
                };
                self.report_omaha_event(&request_params, event, &apps).await;
                return Err(UpdateCheckError::ResponseParser(err));
            }
        };

        info!("result: {:?}", response);

        let statuses = Self::get_app_update_statuses(&response);
        for (app_id, status) in &statuses {
            // TODO:  Report or metric statuses other than 'no-update' and 'ok'
            info!("Omaha update check status: {} => {:?}", app_id, status);
        }

        let some_app_has_update = statuses.iter().any(|(_id, status)| **status == OmahaStatus::Ok);
        if !some_app_has_update {
            // A succesfull, no-update, check

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
                    self.report_error(
                        &request_params,
                        EventErrorCode::ConstructInstallPlan,
                        &apps,
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
                    self.report_omaha_event(&request_params, event, &apps).await;

                    self.set_state(State::InstallationDeferredByPolicy, co).await;
                    return Ok(Self::make_response(
                        response,
                        update_check::Action::DeferredByPolicy,
                    ));
                }
                UpdateDecision::DeniedByPolicy => {
                    warn!("Install plan was denied by Policy, see Policy logs for reasoning");
                    self.report_error(&request_params, EventErrorCode::DeniedByPolicy, &apps, co)
                        .await;
                    return Ok(Self::make_response(response, update_check::Action::DeniedByPolicy));
                }
            }

            self.set_state(State::InstallingUpdate, co).await;
            self.report_success_event(&request_params, EventType::UpdateDownloadStarted, &apps)
                .await;

            let install_plan_id = install_plan.id();
            let update_start_time = clock::now();
            let update_first_seen_time =
                self.record_update_first_seen_time(&install_plan_id, update_start_time).await;

            let install_result = self.installer.perform_install(&install_plan, None).await;
            if let Err(e) = install_result {
                warn!("Installation failed: {}", e);
                self.report_error(&request_params, EventErrorCode::Installation, &apps, co).await;

                match clock::now().duration_since(update_start_time) {
                    Ok(duration) => self.report_metrics(Metrics::FailedUpdateDuration(duration)),
                    Err(e) => warn!("Update start time is in the future: {}", e),
                }
                return Ok(Self::make_response(
                    response,
                    update_check::Action::InstallPlanExecutionError,
                ));
            }

            self.report_success_event(&request_params, EventType::UpdateDownloadFinished, &apps)
                .await;

            // TODO: Verify downloaded update if needed.

            self.report_success_event(&request_params, EventType::UpdateComplete, &apps).await;

            let update_finish_time = clock::now();
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

            self.set_state(State::WaitingForReboot, co).await;
            Ok(Self::make_response(response, update_check::Action::Updated))
        }
    }

    /// Set the current state to |InstallationError| and report the error event to Omaha.
    async fn report_error<'a>(
        &'a mut self,
        request_params: &'a RequestParams,
        errorcode: EventErrorCode,
        apps: &'a Vec<App>,
        co: &mut async_generator::Yield<StateMachineEvent>,
    ) {
        self.set_state(State::InstallationError, co).await;

        let event = Event {
            event_type: EventType::UpdateComplete,
            errorcode: Some(errorcode),
            ..Event::default()
        };
        self.report_omaha_event(&request_params, event, apps).await;
    }

    /// Report a successful event to Omaha, for example download started, download finished, etc.
    async fn report_success_event<'a>(
        &'a mut self,
        request_params: &'a RequestParams,
        event_type: EventType,
        apps: &'a Vec<App>,
    ) {
        let event = Event { event_type, event_result: EventResult::Success, ..Event::default() };
        self.report_omaha_event(&request_params, event, apps).await;
    }

    /// Report the given |event| to Omaha, errors occurred during reporting are logged but not
    /// acted on.
    async fn report_omaha_event<'a>(
        &'a mut self,
        request_params: &'a RequestParams,
        event: Event,
        apps: &'a Vec<App>,
    ) {
        let mut request_builder = RequestBuilder::new(&self.client_config, &request_params);
        for app in apps {
            request_builder = request_builder.add_event(app, &event);
        }
        if let Err(e) = Self::do_omaha_request(&mut self.http, &request_builder).await {
            warn!("Unable to report event to Omaha: {:?}", e);
        }
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
    async fn do_omaha_request<'a>(
        http: &'a mut HR,
        builder: &RequestBuilder<'a>,
    ) -> Result<(Parts, Vec<u8>), OmahaRequestError> {
        let (parts, body) = Self::make_request(http, builder.build()?).await?;
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
    ) -> Result<(Parts, Vec<u8>), hyper::Error> {
        info!("Making http request to: {}", request.uri());
        let res = http_client.request(request).await.map_err(|err| {
            warn!("Unable to perform request: {}", err);
            err
        })?;

        let (parts, body) = res.into_parts();
        let data = body.compat().try_concat().await?;

        Ok((parts, data.to_vec()))
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
            server_dictated_poll_interval: None,
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
                return storage
                    .get_int(UPDATE_FIRST_SEEN_TIME)
                    .await
                    .map(time::i64_to_time)
                    .unwrap_or(now);
            }
        }
        // Update INSTALL_PLAN_ID and UPDATE_FIRST_SEEN_TIME for new update.
        if let Err(e) = storage.set_string(INSTALL_PLAN_ID, install_plan_id).await {
            error!("Unable to persist {}: {}", INSTALL_PLAN_ID, e);
            return now;
        }
        if let Err(e) = storage.set_int(UPDATE_FIRST_SEEN_TIME, time::time_to_i64(now)).await {
            error!("Unable to persist {}: {}", UPDATE_FIRST_SEEN_TIME, e);
            let _ = storage.remove(INSTALL_PLAN_ID).await;
            return now;
        }
        if let Err(e) = storage.commit().await {
            error!("Unable to commit persisted data: {}", e);
        }
        now
    }
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
        let options = CheckOptions::default();

        let context = update_check::Context {
            schedule: UpdateCheckSchedule {
                last_update_time: clock::now() - Duration::new(500, 0),
                next_update_time: clock::now(),
                next_update_window_start: clock::now(),
            },
            state: ProtocolState::default(),
        };

        let apps = self.app_set.to_vec().await;

        async_generator::generate(move |mut co| async move {
            self.perform_update_check(options, context, apps, &mut co).await
        })
        .into_complete()
        .await
    }

    /// Run start_upate_check once, discarding its states.
    pub async fn run_once(&mut self) {
        let options = CheckOptions::default();

        async_generator::generate(move |mut co| async move {
            self.start_update_check(options, &mut co).await;
        })
        .map(|_| ())
        .collect::<()>()
        .await;
    }
}

#[cfg(test)]
impl
    StateMachineBuilder<
        StubPolicyEngine,
        StubHttpRequest,
        StubInstaller,
        timer::StubTimer,
        StubMetricsReporter,
        StubStorage,
    >
{
    /// Create a new StateMachine with stub implementations.
    pub fn new_stub(config: Config, app_set: AppSet) -> Self {
        Self::new(
            StubPolicyEngine,
            StubHttpRequest,
            StubInstaller::default(),
            timer::StubTimer,
            StubMetricsReporter,
            Rc::new(Mutex::new(StubStorage)),
            config,
            app_set,
        )
    }
}

/// Return a random number in [n - range / 2, n - range / 2 + range).
fn randomize(n: u64, range: u64) -> u64 {
    n - range / 2 + rand::random::<u64>() % range
}

#[cfg(test)]
mod tests {
    use super::time::time_to_i64;
    use super::timer::StubTimer;
    use super::update_check::*;
    use super::*;
    use crate::{
        common::{
            App, CheckOptions, PersistedApp, ProtocolState, UpdateCheckSchedule, UserCounting,
        },
        configuration::test_support::config_generator,
        http_request::{mock::MockHttpRequest, StubHttpRequest},
        installer::{
            stub::{StubInstallErrors, StubInstaller, StubPlan},
            ProgressObserver,
        },
        metrics::{MockMetricsReporter, StubMetricsReporter},
        policy::{MockPolicyEngine, StubPolicyEngine},
        protocol::Cohort,
        storage::MemStorage,
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
    use std::time::{Duration, SystemTime};

    fn make_test_app_set() -> AppSet {
        AppSet::new(vec![App::new(
            "{00000000-0000-0000-0000-000000000001}",
            [1, 2, 3, 4],
            Cohort::new("stable-channel"),
        )])
    }

    // Assert that the last request made to |http| is equal to the request built by
    // |request_builder|.
    async fn assert_request<'a>(http: MockHttpRequest, request_builder: RequestBuilder<'a>) {
        let body = request_builder
            .build()
            .unwrap()
            .into_body()
            .compat()
            .try_concat()
            .await
            .unwrap()
            .to_vec();
        // Compare string instead of Vec<u8> for easier debugging.
        let body_str = String::from_utf8_lossy(&body);
        http.assert_body_str(&body_str).await;
    }

    #[test]
    fn run_simple_check_with_noupdate_result() {
        block_on(async {
            let config = config_generator();
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

            StateMachineBuilder::new(
                StubPolicyEngine,
                http,
                StubInstaller::default(),
                StubTimer,
                StubMetricsReporter,
                Rc::new(Mutex::new(StubStorage)),
                config,
                make_test_app_set(),
            )
            .oneshot()
            .await
            .unwrap();

            info!("update check complete!");
        });
    }

    #[test]
    fn test_cohort_returned_with_noupdate_result() {
        block_on(async {
            let config = config_generator();
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

            let response = StateMachineBuilder::new(
                StubPolicyEngine,
                http,
                StubInstaller::default(),
                StubTimer,
                StubMetricsReporter,
                Rc::new(Mutex::new(StubStorage)),
                config,
                make_test_app_set(),
            )
            .oneshot()
            .await
            .unwrap();
            assert_eq!("{00000000-0000-0000-0000-000000000001}", response.app_responses[0].app_id);
            assert_eq!(Some("1".into()), response.app_responses[0].cohort.id);
            assert_eq!(Some("stable-channel".into()), response.app_responses[0].cohort.name);
            assert_eq!(None, response.app_responses[0].cohort.hint);
        });
    }

    #[test]
    fn test_report_parse_response_error() {
        block_on(async {
            let config = config_generator();
            let http = MockHttpRequest::new(hyper::Response::new("invalid response".into()));

            let mut state_machine = StateMachineBuilder::new(
                StubPolicyEngine,
                http,
                StubInstaller::default(),
                StubTimer,
                StubMetricsReporter,
                Rc::new(Mutex::new(StubStorage)),
                config.clone(),
                make_test_app_set(),
            )
            .build()
            .await;

            let response = state_machine.oneshot().await;
            assert_matches!(response, Err(UpdateCheckError::ResponseParser(_)));

            let request_params = RequestParams::default();
            let mut request_builder = RequestBuilder::new(&config, &request_params);
            let event = Event {
                event_type: EventType::UpdateComplete,
                errorcode: Some(EventErrorCode::ParseResponse),
                ..Event::default()
            };
            let apps = state_machine.app_set.to_vec().await;
            request_builder = request_builder.add_event(&apps[0], &event);
            assert_request(state_machine.http, request_builder).await;
        });
    }

    #[test]
    fn test_report_construct_install_plan_error() {
        block_on(async {
            let config = config_generator();
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

            let mut state_machine = StateMachineBuilder::new(
                StubPolicyEngine,
                http,
                StubInstaller::default(),
                StubTimer,
                StubMetricsReporter,
                Rc::new(Mutex::new(StubStorage)),
                config.clone(),
                make_test_app_set(),
            )
            .build()
            .await;

            let response = state_machine.oneshot().await;
            assert_matches!(response, Err(UpdateCheckError::InstallPlan(_)));

            let request_params = RequestParams::default();
            let mut request_builder = RequestBuilder::new(&config, &request_params);
            let event = Event {
                event_type: EventType::UpdateComplete,
                errorcode: Some(EventErrorCode::ConstructInstallPlan),
                ..Event::default()
            };
            let apps = state_machine.app_set.to_vec().await;
            request_builder = request_builder.add_event(&apps[0], &event);
            assert_request(state_machine.http, request_builder).await;
        });
    }

    #[test]
    fn test_report_installation_error() {
        block_on(async {
            let config = config_generator();
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

            let mut state_machine = StateMachineBuilder::new(
                StubPolicyEngine,
                http,
                StubInstaller { should_fail: true },
                StubTimer,
                StubMetricsReporter,
                Rc::new(Mutex::new(StubStorage)),
                config.clone(),
                make_test_app_set(),
            )
            .build()
            .await;

            let response = state_machine.oneshot().await.unwrap();
            assert_eq!(Action::InstallPlanExecutionError, response.app_responses[0].result);

            let request_params = RequestParams::default();
            let mut request_builder = RequestBuilder::new(&config, &request_params);
            let event = Event {
                event_type: EventType::UpdateComplete,
                errorcode: Some(EventErrorCode::Installation),
                ..Event::default()
            };
            let apps = state_machine.app_set.to_vec().await;
            request_builder = request_builder.add_event(&apps[0], &event);
            assert_request(state_machine.http, request_builder).await;
        });
    }

    #[test]
    fn test_report_deferred_by_policy() {
        block_on(async {
            let config = config_generator();
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
            let mut state_machine = StateMachineBuilder::new(
                policy_engine,
                http,
                StubInstaller::default(),
                StubTimer,
                StubMetricsReporter,
                Rc::new(Mutex::new(StubStorage)),
                config.clone(),
                make_test_app_set(),
            )
            .build()
            .await;

            let response = state_machine.oneshot().await.unwrap();
            assert_eq!(Action::DeferredByPolicy, response.app_responses[0].result);

            let request_params = RequestParams::default();
            let mut request_builder = RequestBuilder::new(&config, &request_params);
            let event = Event {
                event_type: EventType::UpdateComplete,
                event_result: EventResult::UpdateDeferred,
                ..Event::default()
            };
            let apps = state_machine.app_set.to_vec().await;
            request_builder = request_builder.add_event(&apps[0], &event);
            assert_request(state_machine.http, request_builder).await;
        });
    }

    #[test]
    fn test_report_denied_by_policy() {
        block_on(async {
            let config = config_generator();
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
            let mut state_machine = StateMachineBuilder::new(
                policy_engine,
                http,
                StubInstaller::default(),
                StubTimer,
                StubMetricsReporter,
                Rc::new(Mutex::new(StubStorage)),
                config.clone(),
                make_test_app_set(),
            )
            .build()
            .await;

            let response = state_machine.oneshot().await.unwrap();
            assert_eq!(Action::DeniedByPolicy, response.app_responses[0].result);

            let request_params = RequestParams::default();
            let mut request_builder = RequestBuilder::new(&config, &request_params);
            let event = Event {
                event_type: EventType::UpdateComplete,
                errorcode: Some(EventErrorCode::DeniedByPolicy),
                ..Event::default()
            };
            let apps = state_machine.app_set.to_vec().await;
            request_builder = request_builder.add_event(&apps[0], &event);
            assert_request(state_machine.http, request_builder).await;
        });
    }

    #[test]
    fn test_wait_timer() {
        let mut pool = LocalPool::new();
        let config = config_generator();
        let mut timer = MockTimer::new();
        timer.expect(Duration::from_secs(111));
        let next_update_time = clock::now() + Duration::from_secs(111);
        let policy_engine = MockPolicyEngine {
            check_schedule: UpdateCheckSchedule {
                last_update_time: SystemTime::UNIX_EPOCH,
                next_update_time,
                next_update_window_start: next_update_time,
            },
            ..MockPolicyEngine::default()
        };

        let (_ctl, state_machine) = pool.run_until(
            StateMachineBuilder::new(
                policy_engine,
                StubHttpRequest,
                StubInstaller::default(),
                timer,
                StubMetricsReporter,
                Rc::new(Mutex::new(StubStorage)),
                config,
                make_test_app_set(),
            )
            .start(),
        );

        pool.spawner().spawn_local(state_machine.map(|_| ()).collect()).unwrap();

        // With otherwise stub implementations, the pool stalls when a timer is awaited.  Dropping
        // the state machine will panic if any timer durations were not used.
        pool.run_until_stalled();
    }

    /// Test that when the last_update_time is in the future, the StateMachine does not wait more
    /// than 1 hour for the next update time.
    #[test]
    fn test_wait_duration_is_capped() {
        let mut pool = LocalPool::new();
        let config = config_generator();
        let mut timer = MockTimer::new();
        // The timer should be capped at 1 hour.  The MockTimer will assert if it's asked to wait
        // a different amount of time.
        timer.expect(Duration::from_secs(1 * 60 * 60));
        let now = clock::now();
        let policy_engine = MockPolicyEngine {
            check_schedule: UpdateCheckSchedule {
                // A day into the future, which triggers the capping logic
                last_update_time: now + Duration::from_secs(24 * 60 * 60),
                // By policy, the next update time would be an hour from then
                next_update_time: now
                    + Duration::from_secs(24 * 60 * 60)
                    + Duration::from_secs(1 * 60 * 60),
                next_update_window_start: now
                    + Duration::from_secs(24 * 60 * 60)
                    + Duration::from_secs(1 * 60 * 60),
            },
            ..MockPolicyEngine::default()
        };

        let (_ctl, state_machine) = pool.run_until(
            StateMachineBuilder::new(
                policy_engine,
                StubHttpRequest,
                StubInstaller::default(),
                timer,
                StubMetricsReporter,
                Rc::new(Mutex::new(StubStorage)),
                config,
                make_test_app_set(),
            )
            .start(),
        );

        pool.spawner().spawn_local(state_machine.map(|_| ()).collect()).unwrap();

        // With otherwise stub implementations, the pool stalls when a timer is awaited.  Dropping
        // the state machine will panic if any timer durations were not used.
        pool.run_until_stalled();
    }

    #[test]
    fn test_update_cohort_and_user_counting() {
        let config = config_generator();
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
        let http2 = MockHttpRequest::from_request_cell(http.get_request_cell());
        http.add_response(hyper::Response::new(response.into()));
        let mut timer = MockTimer::new();
        // Run the update check twice and then block.
        timer.expect(Duration::from_secs(111));
        timer.expect(Duration::from_secs(111));
        let next_update_time = clock::now() + Duration::from_secs(111);
        let policy_engine = MockPolicyEngine {
            check_schedule: UpdateCheckSchedule {
                last_update_time: SystemTime::UNIX_EPOCH,
                next_update_time,
                next_update_window_start: next_update_time,
            },
            ..MockPolicyEngine::default()
        };
        let apps = make_test_app_set();

        let (_ctl, state_machine) = block_on(
            StateMachineBuilder::new(
                policy_engine,
                http,
                StubInstaller::default(),
                timer,
                StubMetricsReporter,
                Rc::new(Mutex::new(StubStorage)),
                config.clone(),
                apps.clone(),
            )
            .start(),
        );

        let mut pool = LocalPool::new();
        pool.spawner().spawn_local(state_machine.map(|_| ()).collect::<()>()).unwrap();
        pool.run_until_stalled();
        drop(pool);

        // Check that apps are updated based on Omaha response.
        let apps = block_on(apps.to_vec());
        assert_eq!(Some("1".to_string()), apps[0].cohort.id);
        assert_eq!(None, apps[0].cohort.hint);
        assert_eq!(Some("stable-channel".to_string()), apps[0].cohort.name);
        assert_eq!(UserCounting::ClientRegulatedByDate(Some(1234567)), apps[0].user_counting);

        // Check that the next update check uses the new app.
        let request_params = RequestParams::default();
        let mut request_builder = RequestBuilder::new(&config, &request_params);
        request_builder = request_builder.add_update_check(&apps[0]);
        request_builder = request_builder.add_ping(&apps[0]);
        block_on(assert_request(http2, request_builder));
    }

    #[test]
    fn test_user_counting_returned() {
        block_on(async {
            let config = config_generator();
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

            let response = StateMachineBuilder::new(
                StubPolicyEngine,
                http,
                StubInstaller::default(),
                StubTimer,
                StubMetricsReporter,
                Rc::new(Mutex::new(StubStorage)),
                config,
                make_test_app_set(),
            )
            .oneshot()
            .await
            .unwrap();

            assert_eq!(
                UserCounting::ClientRegulatedByDate(Some(1234567)),
                response.app_responses[0].user_counting
            );
        });
    }

    #[test]
    fn test_observe_state() {
        block_on(async {
            let config = config_generator();

            let actual_states = StateMachineBuilder::new_stub(config, make_test_app_set())
                .oneshot_check(CheckOptions::default())
                .await
                .filter_map(|event| {
                    future::ready(match event {
                        StateMachineEvent::StateChange(state) => Some(state),
                        _ => None,
                    })
                })
                .collect::<Vec<State>>()
                .await;

            let expected_states =
                vec![State::CheckingForUpdates, State::ErrorCheckingForUpdate, State::Idle];
            assert_eq!(actual_states, expected_states);
        });
    }

    #[test]
    fn test_observe_schedule() {
        block_on(async {
            let config = config_generator();
            let actual_schedules = StateMachineBuilder::new_stub(config, make_test_app_set())
                .oneshot_check(CheckOptions::default())
                .await
                .filter_map(|event| {
                    future::ready(match event {
                        StateMachineEvent::ScheduleChange(schedule) => Some(schedule),
                        _ => None,
                    })
                })
                .collect::<Vec<UpdateCheckSchedule>>()
                .await;

            let expected_schedule = UpdateCheckSchedule {
                last_update_time: clock::now(),
                next_update_time: clock::now(),
                next_update_window_start: clock::now(),
            };

            assert_eq!(actual_schedules, vec![expected_schedule]);
        });
    }

    #[test]
    fn test_observe_protocol_state() {
        block_on(async {
            let config = config_generator();
            let actual_protocol_states = StateMachineBuilder::new_stub(config, make_test_app_set())
                .oneshot_check(CheckOptions::default())
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
    fn test_metrics_report_update_check_response_time() {
        block_on(async {
            let config = config_generator();
            let mut state_machine = StateMachineBuilder::new(
                StubPolicyEngine,
                StubHttpRequest,
                StubInstaller::default(),
                StubTimer,
                MockMetricsReporter::new(false),
                Rc::new(Mutex::new(StubStorage)),
                config,
                make_test_app_set(),
            )
            .build()
            .await;

            let _response = state_machine.oneshot().await;

            assert!(!state_machine.metrics_reporter.metrics.is_empty());
            match &state_machine.metrics_reporter.metrics[0] {
                Metrics::UpdateCheckResponseTime(_) => {} // expected
                metric => panic!("Unexpected metric {:?}", metric),
            }
        });
    }

    #[test]
    fn test_metrics_report_update_check_retries() {
        block_on(async {
            let config = config_generator();
            let mut state_machine = StateMachineBuilder::new(
                StubPolicyEngine,
                StubHttpRequest,
                StubInstaller::default(),
                MockTimer::new(),
                MockMetricsReporter::new(false),
                Rc::new(Mutex::new(StubStorage)),
                config,
                make_test_app_set(),
            )
            .build()
            .await;

            let _response = state_machine.oneshot().await;

            assert!(state_machine
                .metrics_reporter
                .metrics
                .contains(&Metrics::UpdateCheckRetries(1)));
        });
    }

    #[test]
    fn test_update_check_retries_backoff() {
        block_on(async {
            let config = config_generator();
            let mut timer = MockTimer::new();
            timer.expect_range(Duration::from_millis(500), Duration::from_millis(1500));
            timer.expect_range(Duration::from_millis(1500), Duration::from_millis(2500));
            let response = StateMachineBuilder::new(
                StubPolicyEngine,
                MockHttpRequest::empty(),
                StubInstaller::default(),
                timer,
                MockMetricsReporter::new(false),
                Rc::new(Mutex::new(StubStorage)),
                config,
                make_test_app_set(),
            )
            .oneshot()
            .await;

            assert_matches!(
                response,
                Err(UpdateCheckError::OmahaRequest(OmahaRequestError::HttpStatus(_)))
            );
        });
    }

    #[test]
    fn test_metrics_report_update_check_failure_reason_omaha() {
        block_on(async {
            let config = config_generator();
            let mut state_machine = StateMachineBuilder::new(
                StubPolicyEngine,
                StubHttpRequest,
                StubInstaller::default(),
                StubTimer,
                MockMetricsReporter::new(false),
                Rc::new(Mutex::new(StubStorage)),
                config,
                make_test_app_set(),
            )
            .build()
            .await;

            state_machine.run_once().await;

            assert!(state_machine
                .metrics_reporter
                .metrics
                .contains(&Metrics::UpdateCheckFailureReason(UpdateCheckFailureReason::Omaha)));
        });
    }

    #[test]
    fn test_metrics_report_update_check_failure_reason_network() {
        block_on(async {
            let config = config_generator();
            let mut state_machine = StateMachineBuilder::new(
                StubPolicyEngine,
                MockHttpRequest::empty(),
                StubInstaller::default(),
                StubTimer,
                MockMetricsReporter::new(false),
                Rc::new(Mutex::new(StubStorage)),
                config,
                make_test_app_set(),
            )
            .build()
            .await;

            state_machine.run_once().await;

            assert!(state_machine
                .metrics_reporter
                .metrics
                .contains(&Metrics::UpdateCheckFailureReason(UpdateCheckFailureReason::Network)));
        });
    }

    #[test]
    fn test_persist_last_update_time() {
        block_on(async {
            let config = config_generator();
            let storage = Rc::new(Mutex::new(MemStorage::new()));

            StateMachineBuilder::new(
                StubPolicyEngine,
                StubHttpRequest,
                StubInstaller::default(),
                StubTimer,
                StubMetricsReporter,
                Rc::clone(&storage),
                config,
                make_test_app_set(),
            )
            .oneshot_check(CheckOptions::default())
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
            // TODO: update this test to have a mocked http response with server dictated poll
            // interval when out code support parsing it from the response.
            let config = config_generator();
            let storage = Rc::new(Mutex::new(MemStorage::new()));

            StateMachineBuilder::new(
                StubPolicyEngine,
                StubHttpRequest,
                StubInstaller::default(),
                StubTimer,
                StubMetricsReporter,
                Rc::clone(&storage),
                config,
                make_test_app_set(),
            )
            .oneshot_check(CheckOptions::default())
            .await
            .map(|_| ())
            .collect::<()>()
            .await;

            let storage = storage.lock().await;
            assert!(storage.get_int(SERVER_DICTATED_POLL_INTERVAL).await.is_none());
            assert!(storage.committed());
        });
    }

    #[test]
    fn test_persist_app() {
        block_on(async {
            let config = config_generator();
            let storage = Rc::new(Mutex::new(MemStorage::new()));
            let app_set = make_test_app_set();

            StateMachineBuilder::new(
                StubPolicyEngine,
                StubHttpRequest,
                StubInstaller::default(),
                StubTimer,
                StubMetricsReporter,
                Rc::clone(&storage),
                config,
                app_set.clone(),
            )
            .oneshot_check(CheckOptions::default())
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
            let config = config_generator();
            let mut storage = MemStorage::new();
            let last_update_time = clock::now() - Duration::from_secs(999);
            storage.set_int(LAST_UPDATE_TIME, time_to_i64(last_update_time)).await.unwrap();

            let state_machine = StateMachineBuilder::new(
                StubPolicyEngine,
                StubHttpRequest,
                StubInstaller::default(),
                StubTimer,
                StubMetricsReporter,
                Rc::new(Mutex::new(storage)),
                config,
                make_test_app_set(),
            )
            .build()
            .await;

            assert_eq!(
                time_to_i64(last_update_time),
                time_to_i64(state_machine.context.schedule.last_update_time)
            );
        });
    }

    #[test]
    fn test_load_server_dictated_poll_interval() {
        block_on(async {
            let config = config_generator();
            let mut storage = MemStorage::new();
            storage.set_int(SERVER_DICTATED_POLL_INTERVAL, 56789).await.unwrap();

            let state_machine = StateMachineBuilder::new(
                StubPolicyEngine,
                StubHttpRequest,
                StubInstaller::default(),
                StubTimer,
                StubMetricsReporter,
                Rc::new(Mutex::new(storage)),
                config,
                make_test_app_set(),
            )
            .build()
            .await;

            assert_eq!(
                Some(Duration::from_micros(56789)),
                state_machine.context.state.server_dictated_poll_interval
            );
        });
    }

    #[test]
    fn test_load_app() {
        block_on(async {
            let config = config_generator();
            let app_set = AppSet::new(vec![App::new(
                "{00000000-0000-0000-0000-000000000001}",
                [1, 2, 3, 4],
                Cohort::default(),
            )]);
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

            let _state_machine = StateMachineBuilder::new(
                StubPolicyEngine,
                StubHttpRequest,
                StubInstaller::default(),
                StubTimer,
                StubMetricsReporter,
                Rc::new(Mutex::new(storage)),
                config,
                app_set.clone(),
            )
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
            let config = config_generator();
            let mut state_machine = StateMachineBuilder::new(
                StubPolicyEngine,
                StubHttpRequest,
                StubInstaller::default(),
                StubTimer,
                MockMetricsReporter::new(false),
                Rc::new(Mutex::new(MemStorage::new())),
                config,
                make_test_app_set(),
            )
            .build()
            .await;

            clock::mock::set(time::i64_to_time(123456789));
            state_machine.report_check_interval().await;
            // No metrics should be reported because no last check time in storage.
            assert!(state_machine.metrics_reporter.metrics.is_empty());
            {
                let storage = state_machine.storage_ref.lock().await;
                assert_eq!(storage.get_int(LAST_CHECK_TIME).await, Some(123456789));
                assert_eq!(storage.len(), 1);
                assert!(storage.committed());
            }
            // A second update check should report metrics.
            let duration = Duration::from_micros(999999);
            clock::mock::set(clock::now() + duration);
            state_machine.report_check_interval().await;
            assert_eq!(
                state_machine.metrics_reporter.metrics,
                vec![Metrics::UpdateCheckInterval(duration)]
            );
            let storage = state_machine.storage_ref.lock().await;
            assert_eq!(storage.get_int(LAST_CHECK_TIME).await, Some(123456789 + 999999));
            assert_eq!(storage.len(), 1);
            assert!(storage.committed());
        });
    }

    #[derive(Debug, Default)]
    pub struct TestInstaller {
        reboot_called: bool,
        should_fail: bool,
    }

    impl Installer for TestInstaller {
        type InstallPlan = StubPlan;
        type Error = StubInstallErrors;
        fn perform_install(
            &mut self,
            _install_plan: &StubPlan,
            _observer: Option<&dyn ProgressObserver>,
        ) -> BoxFuture<'_, Result<(), Self::Error>> {
            if self.should_fail {
                future::ready(Err(StubInstallErrors::Failed)).boxed()
            } else {
                clock::mock::set(time::i64_to_time(222222222));
                future::ready(Ok(())).boxed()
            }
        }

        fn perform_reboot(&mut self) -> BoxFuture<'_, Result<(), anyhow::Error>> {
            self.reboot_called = true;
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
            let config = config_generator();
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

            let mut state_machine = StateMachineBuilder::new(
                StubPolicyEngine,
                http,
                TestInstaller::default(),
                StubTimer,
                MockMetricsReporter::new(false),
                Rc::clone(&storage),
                config,
                make_test_app_set(),
            )
            .build()
            .await;

            clock::mock::set(time::i64_to_time(123456789));
            {
                let mut storage = storage.lock().await;
                storage.set_string(INSTALL_PLAN_ID, "").await.unwrap();
                storage.set_int(UPDATE_FIRST_SEEN_TIME, 23456789).await.unwrap();
                storage.commit().await.unwrap();
            }

            state_machine.run_once().await;

            assert!(state_machine
                .metrics_reporter
                .metrics
                .contains(&Metrics::SuccessfulUpdateDuration(Duration::from_micros(98765433))));
            assert!(state_machine.metrics_reporter.metrics.contains(
                &Metrics::SuccessfulUpdateFromFirstSeen(Duration::from_micros(198765433))
            ));
        });
    }

    #[test]
    fn test_report_failed_update_duration() {
        block_on(async {
            let config = config_generator();
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
            let mut state_machine = StateMachineBuilder::new(
                StubPolicyEngine,
                http,
                StubInstaller { should_fail: true },
                StubTimer,
                MockMetricsReporter::new(false),
                Rc::new(Mutex::new(StubStorage)),
                config,
                make_test_app_set(),
            )
            .build()
            .await;
            clock::mock::set(time::i64_to_time(123456789));

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
            let config = config_generator();
            let storage = Rc::new(Mutex::new(MemStorage::new()));
            let mut state_machine = StateMachineBuilder::new(
                StubPolicyEngine,
                StubHttpRequest,
                StubInstaller::default(),
                StubTimer,
                StubMetricsReporter,
                Rc::clone(&storage),
                config,
                make_test_app_set(),
            )
            .build()
            .await;

            let now = time::i64_to_time(123456789);
            assert_eq!(state_machine.record_update_first_seen_time("id", now).await, now);
            {
                let storage = storage.lock().await;
                assert_eq!(storage.get_string(INSTALL_PLAN_ID).await, Some("id".to_string()));
                assert_eq!(storage.get_int(UPDATE_FIRST_SEEN_TIME).await, Some(time_to_i64(now)));
                assert_eq!(storage.len(), 2);
                assert!(storage.committed());
            }

            let now2 = now + Duration::from_secs(1000);
            assert_eq!(state_machine.record_update_first_seen_time("id", now2).await, now);
            {
                let storage = storage.lock().await;
                assert_eq!(storage.get_string(INSTALL_PLAN_ID).await, Some("id".to_string()));
                assert_eq!(storage.get_int(UPDATE_FIRST_SEEN_TIME).await, Some(time_to_i64(now)));
                assert_eq!(storage.len(), 2);
                assert!(storage.committed());
            }
            assert_eq!(state_machine.record_update_first_seen_time("id2", now2).await, now2);
            {
                let storage = storage.lock().await;
                assert_eq!(storage.get_string(INSTALL_PLAN_ID).await, Some("id2".to_string()));
                assert_eq!(storage.get_int(UPDATE_FIRST_SEEN_TIME).await, Some(time_to_i64(now2)));
                assert_eq!(storage.len(), 2);
                assert!(storage.committed());
            }
        });
    }

    #[test]
    fn test_report_attempts_to_succeed() {
        block_on(async {
            let config = config_generator();
            let storage = Rc::new(Mutex::new(MemStorage::new()));
            let mut state_machine = StateMachineBuilder::new(
                StubPolicyEngine,
                StubHttpRequest,
                StubInstaller { should_fail: true },
                StubTimer,
                MockMetricsReporter::new(false),
                Rc::clone(&storage),
                config,
                make_test_app_set(),
            )
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
        block_on(async {
            let config = config_generator();
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
            let mut state_machine = StateMachineBuilder::new(
                StubPolicyEngine,
                http,
                TestInstaller::default(),
                StubTimer,
                MockMetricsReporter::new(false),
                Rc::new(Mutex::new(MemStorage::new())),
                config,
                make_test_app_set(),
            )
            .build()
            .await;

            state_machine.run_once().await;

            assert!(state_machine.installer.reboot_called);
        });
    }

    #[test]
    fn test_failed_update_does_not_trigger_reboot() {
        block_on(async {
            let config = config_generator();
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
            let mut state_machine = StateMachineBuilder::new(
                StubPolicyEngine,
                http,
                TestInstaller { should_fail: true, ..Default::default() },
                StubTimer,
                MockMetricsReporter::new(false),
                Rc::new(Mutex::new(StubStorage)),
                config,
                make_test_app_set(),
            )
            .build()
            .await;

            state_machine.run_once().await;

            assert!(!state_machine.installer.reboot_called)
        });
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
            StateMachineBuilder::new(
                StubPolicyEngine,
                http,
                BlockingInstaller { on_install: send_install },
                StubTimer,
                StubMetricsReporter,
                Rc::new(Mutex::new(StubStorage)),
                config_generator(),
                make_test_app_set(),
            )
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

        let (timer, mut timers) = timer::BlockingTimer::new();
        let policy_engine = MockPolicyEngine {
            check_schedule: UpdateCheckSchedule {
                last_update_time: SystemTime::UNIX_EPOCH,
                next_update_time: clock::now() + Duration::from_secs(321),
                next_update_window_start: clock::now() + Duration::from_secs(321),
            },
            ..MockPolicyEngine::default()
        };
        let (mut ctl, state_machine) = pool.run_until(
            StateMachineBuilder::new(
                policy_engine,
                StubHttpRequest,
                StubInstaller::default(),
                timer,
                StubMetricsReporter,
                Rc::new(Mutex::new(StubStorage)),
                config_generator(),
                make_test_app_set(),
            )
            .start(),
        );

        let observer = TestObserver::default();
        spawner.spawn_local(observer.observe(state_machine)).unwrap();

        let blocked_timer = pool.run_until(timers.next()).unwrap();
        assert_eq!(blocked_timer.duration(), &Duration::from_secs(321));
        clock::mock::set(clock::now() + Duration::from_secs(200));
        assert_eq!(observer.take_states(), vec![]);

        // Nothing happens while the timer is waiting.
        pool.run_until_stalled();
        assert_eq!(observer.take_states(), vec![]);

        blocked_timer.unblock();
        let blocked_timer = pool.run_until(timers.next()).unwrap();
        assert_eq!(blocked_timer.duration(), &Duration::from_secs(121));
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
    fn test_randomize() {
        let n = randomize(10, 10);
        assert!(n >= 5 && n < 15, "n = {}", n);
        let n = randomize(1000, 100);
        assert!(n >= 950 && n < 1050, "n = {}", n);
        // only one integer in [123456, 123457)
        assert_eq!(randomize(123456, 1), 123456);
    }
}
