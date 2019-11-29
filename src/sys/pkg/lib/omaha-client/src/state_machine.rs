// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    clock,
    common::{App, AppSet, CheckOptions},
    configuration::Config,
    http_request::{HttpRequest, StubHttpRequest},
    installer::{stub::StubInstaller, Installer, Plan},
    metrics::{Metrics, MetricsReporter, StubMetricsReporter, UpdateCheckFailureReason},
    policy::{CheckDecision, PolicyEngine, StubPolicyEngine, UpdateDecision},
    protocol::{
        self,
        request::{Event, EventErrorCode, EventResult, EventType, InstallSource},
        response::{parse_json_response, OmahaStatus, Response},
    },
    request_builder::{self, RequestBuilder, RequestParams},
    storage::{MemStorage, Storage},
};
use chrono::{DateTime, Utc};
use failure::Fail;
use futures::{compat::Stream01CompatExt, lock::Mutex, prelude::*};
use http::response::Parts;
use log::{error, info, warn};
use std::cell::RefCell;
use std::rc::Rc;
use std::str::Utf8Error;
use std::time::{Duration, Instant, SystemTime};

mod time;
pub mod update_check;

mod timer;
#[cfg(test)]
pub use timer::MockTimer;
pub use timer::StubTimer;
pub use timer::Timer;

mod observer;
pub use observer::Observer;

const LAST_CHECK_TIME: &str = "last_check_time";
const INSTALL_PLAN_ID: &str = "install_plan_id";
const UPDATE_FIRST_SEEN_TIME: &str = "update_first_seen_time";
const CONSECUTIVE_FAILED_UPDATE_CHECKS: &str = "consecutive_failed_update_checks";

/// This is the core state machine for a client's update check.  It is instantiated and used to
/// perform a single update check process.
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

    /// Observe changes in the state machine.
    observer: Option<Box<dyn Observer>>,

    /// The list of apps used for update check.
    app_set: AppSet,
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum State {
    Idle,
    CheckingForUpdates,
    UpdateAvailable,
    PerformingUpdate,
    WaitingForReboot,
    FinalizingUpdate,
    EncounteredError,
}

/// This is the set of errors that can occur when making a request to Omaha.  This is an internal
/// collection of error types.
#[derive(Fail, Debug)]
pub enum OmahaRequestError {
    #[fail(display = "Unexpected JSON error constructing update check: {}", _0)]
    Json(#[cause] serde_json::Error),

    #[fail(display = "Error building update check HTTP request: {}", _0)]
    HttpBuilder(#[cause] http::Error),

    #[fail(display = "Hyper error performing update check: {}", _0)]
    Hyper(#[cause] hyper::Error),

    #[fail(display = "HTTP error performing update check: {}", _0)]
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
#[derive(Fail, Debug)]
#[allow(dead_code)]
pub enum ResponseParseError {
    #[fail(display = "Response was not valid UTF-8")]
    Utf8(#[cause] Utf8Error),

    #[fail(display = "Unexpected JSON error parsing update check response: {}", _0)]
    Json(#[cause] serde_json::Error),
}

#[derive(Fail, Debug)]
pub enum UpdateCheckError {
    #[fail(display = "Check not performed per policy: {:?}", _0)]
    Policy(CheckDecision),

    #[fail(display = "Error checking with Omaha: {:?}", _0)]
    OmahaRequest(OmahaRequestError),

    #[fail(display = "Error parsing Omaha response: {:?}", _0)]
    ResponseParser(ResponseParseError),

    #[fail(display = "Unable to create an install plan: {:?}", _0)]
    InstallPlan(failure::Error),
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
    pub async fn new(
        policy_engine: PE,
        http: HR,
        installer: IN,
        config: &Config,
        timer: TM,
        metrics_reporter: MR,
        storage_ref: Rc<Mutex<ST>>,
        mut app_set: AppSet,
    ) -> Self {
        let context = {
            let storage = storage_ref.lock().await;
            app_set.load(&*storage).await;

            update_check::Context::load(&*storage).await
        };
        StateMachine {
            client_config: config.clone(),
            policy_engine,
            http,
            installer,
            timer,
            metrics_reporter,
            storage_ref,
            context,
            state: State::Idle,
            observer: None,
            app_set,
        }
    }

    /// Need to do this in a mutable method because the borrow checker isn't smart enough to know
    /// that different fields of the same struct (even if it's not self) are separate variables and
    /// can be borrowed at the same time.
    async fn update_next_update_time(&mut self) {
        let apps = self.app_set.to_vec().await;
        self.context.schedule = self
            .policy_engine
            .compute_next_update_time(&apps, &self.context.schedule, &self.context.state)
            .await;
    }

    /// Start the StateMachine to do periodic update check in the background. The future this
    /// function returns never finishes!
    pub async fn start(state_machine_ref: Rc<RefCell<Self>>) {
        {
            let state_machine = state_machine_ref.borrow();
            if !state_machine.app_set.valid().await {
                error!(
                    "App set not valid, not starting state machine: {:#?}",
                    state_machine.app_set.to_vec().await
                );
                return;
            }
        }
        loop {
            {
                let mut state_machine = state_machine_ref.borrow_mut();
                state_machine.update_next_update_time().await;
                // Wait if |next_update_time| is in the future.
                if let Ok(duration) =
                    state_machine.context.schedule.next_update_time.duration_since(clock::now())
                {
                    let date_time =
                        DateTime::<Utc>::from(state_machine.context.schedule.next_update_time);
                    info!("Waiting until {} for the next update check...", date_time.to_rfc3339());

                    let fut = state_machine.timer.wait(duration);
                    // Don't borrow the state machine while waiting for the timer, because update
                    // check could be triggered through an API while we are waiting.
                    drop(state_machine);
                    fut.await;
                }
            }

            let options = CheckOptions { source: InstallSource::ScheduledTask };
            let mut state_machine = state_machine_ref.borrow_mut();
            state_machine.start_update_check(options).await;
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
    pub async fn start_update_check(&mut self, options: CheckOptions) {
        let apps = self.app_set.to_vec().await;
        match self.perform_update_check(options, self.context.clone(), apps).await {
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

                self.app_set.update_from_omaha(result.app_responses).await;

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

        self.persist_data().await;

        if self.state != State::WaitingForReboot {
            self.set_state(State::Idle).await;
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

        self.set_state(State::CheckingForUpdates).await;

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
                    self.set_state(State::EncounteredError).await;
                    return Err(UpdateCheckError::OmahaRequest(e.into()));
                }
                Err(OmahaRequestError::HttpBuilder(e)) => {
                    error!("Unable to construct HTTP request! {:?}", e);
                    self.set_state(State::EncounteredError).await;
                    return Err(UpdateCheckError::OmahaRequest(e.into()));
                }
                Err(OmahaRequestError::Hyper(e)) => {
                    warn!("Unable to contact Omaha: {:?}", e);
                    // Don't retry if the error was caused by user code, which means we weren't
                    // using the library correctly.
                    if omaha_request_attempt >= max_omaha_request_attempts || e.is_user() {
                        self.set_state(State::EncounteredError).await;
                        return Err(UpdateCheckError::OmahaRequest(e.into()));
                    }
                }
                Err(OmahaRequestError::HttpStatus(e)) => {
                    warn!("Unable to contact Omaha: {:?}", e);
                    if omaha_request_attempt >= max_omaha_request_attempts {
                        self.set_state(State::EncounteredError).await;
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
                self.report_error(&request_params, EventErrorCode::ParseResponse, &apps).await;
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

            // TODO: Report progress status (done)
            Ok(Self::make_response(response, update_check::Action::NoUpdate))
        } else {
            info!(
                "At least one app has an update, proceeding to build and process an Install Plan"
            );
            self.set_state(State::UpdateAvailable).await;

            let install_plan = match IN::InstallPlan::try_create_from(&request_params, &response) {
                Ok(plan) => plan,
                Err(e) => {
                    error!("Unable to construct install plan! {}", e);
                    self.report_error(&request_params, EventErrorCode::ConstructInstallPlan, &apps)
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

                    // TODO: Report progress status (Deferred)
                    return Ok(Self::make_response(
                        response,
                        update_check::Action::DeferredByPolicy,
                    ));
                }
                UpdateDecision::DeniedByPolicy => {
                    warn!("Install plan was denied by Policy, see Policy logs for reasoning");
                    self.report_error(&request_params, EventErrorCode::DeniedByPolicy, &apps).await;
                    return Ok(Self::make_response(response, update_check::Action::DeniedByPolicy));
                }
            }

            self.set_state(State::PerformingUpdate).await;
            self.report_success_event(&request_params, EventType::UpdateDownloadStarted, &apps)
                .await;

            let install_plan_id = install_plan.id();
            let update_start_time = clock::now();
            let update_first_seen_time =
                self.record_update_first_seen_time(&install_plan_id, update_start_time).await;

            let install_result = self.installer.perform_install(&install_plan, None).await;
            if let Err(e) = install_result {
                warn!("Installation failed: {}", e);
                self.report_error(&request_params, EventErrorCode::Installation, &apps).await;

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
            self.set_state(State::FinalizingUpdate).await;

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

            self.set_state(State::WaitingForReboot).await;
            Ok(Self::make_response(response, update_check::Action::Updated))
        }
    }

    /// Set the current state to |EncounteredError| and report the error event to Omaha.
    async fn report_error<'a>(
        &'a mut self,
        request_params: &'a RequestParams,
        errorcode: EventErrorCode,
        apps: &'a Vec<App>,
    ) {
        self.set_state(State::EncounteredError).await;

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
    async fn set_state(&mut self, state: State) {
        self.state = state.clone();
        if let Some(observer) = &mut self.observer {
            observer.on_state_change(state).await;
        }
    }

    /// Set the observer that will be called every changes.
    pub fn set_observer(&mut self, observer: impl Observer + 'static) {
        self.observer = Some(Box::new(observer));
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

impl
    StateMachine<
        StubPolicyEngine,
        StubHttpRequest,
        StubInstaller,
        StubTimer,
        StubMetricsReporter,
        MemStorage,
    >
{
    /// Create a new StateMachine with stub implementations.
    pub async fn new_stub(config: &Config, app_set: AppSet) -> Self {
        Self::new(
            StubPolicyEngine,
            StubHttpRequest,
            StubInstaller::default(),
            config,
            StubTimer,
            StubMetricsReporter,
            Rc::new(Mutex::new(MemStorage::new())),
            app_set,
        )
        .await
    }
}

/// Return a random number in [n - range / 2, n - range / 2 + range).
fn randomize(n: u64, range: u64) -> u64 {
    n - range / 2 + rand::random::<u64>() % range
}

#[cfg(test)]
mod tests {
    use super::time::time_to_i64;
    use super::update_check::*;
    use super::*;
    use crate::{
        common::{
            App, CheckOptions, PersistedApp, ProtocolState, UpdateCheckSchedule, UserCounting,
        },
        configuration::test_support::config_generator,
        http_request::{mock::MockHttpRequest, StubHttpRequest},
        installer::stub::StubInstaller,
        metrics::{MockMetricsReporter, StubMetricsReporter},
        policy::{MockPolicyEngine, StubPolicyEngine},
        protocol::Cohort,
    };
    use futures::executor::{block_on, LocalPool};
    use futures::future::LocalBoxFuture;
    use futures::task::LocalSpawnExt;
    use log::info;
    use matches::assert_matches;
    use pretty_assertions::assert_eq;
    use serde_json::json;
    use std::time::{Duration, SystemTime};

    async fn do_update_check<PE, HR, IN, TM, MR, ST>(
        state_machine: &mut StateMachine<PE, HR, IN, TM, MR, ST>,
    ) -> Result<update_check::Response, UpdateCheckError>
    where
        PE: PolicyEngine,
        HR: HttpRequest,
        IN: Installer,
        TM: Timer,
        MR: MetricsReporter,
        ST: Storage,
    {
        let options = CheckOptions::default();

        let context = update_check::Context {
            schedule: UpdateCheckSchedule {
                last_update_time: clock::now() - Duration::new(500, 0),
                next_update_time: clock::now(),
                next_update_window_start: clock::now(),
            },
            state: ProtocolState::default(),
        };

        let apps = state_machine.app_set.to_vec().await;
        state_machine.perform_update_check(options, context, apps).await
    }

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

            let mut state_machine = StateMachine::new(
                StubPolicyEngine,
                http,
                StubInstaller::default(),
                &config,
                StubTimer,
                StubMetricsReporter,
                Rc::new(Mutex::new(MemStorage::new())),
                make_test_app_set(),
            )
            .await;
            do_update_check(&mut state_machine).await.unwrap();

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

            let mut state_machine = StateMachine::new(
                StubPolicyEngine,
                http,
                StubInstaller::default(),
                &config,
                StubTimer,
                StubMetricsReporter,
                Rc::new(Mutex::new(MemStorage::new())),
                make_test_app_set(),
            )
            .await;
            let response = do_update_check(&mut state_machine).await.unwrap();
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

            let mut state_machine = StateMachine::new(
                StubPolicyEngine,
                http,
                StubInstaller::default(),
                &config,
                StubTimer,
                StubMetricsReporter,
                Rc::new(Mutex::new(MemStorage::new())),
                make_test_app_set(),
            )
            .await;
            match do_update_check(&mut state_machine).await {
                Err(UpdateCheckError::ResponseParser(_)) => {} // expected
                result @ _ => {
                    panic!("Unexpected result from do_update_check: {:?}", result);
                }
            }

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

            let mut state_machine = StateMachine::new(
                StubPolicyEngine,
                http,
                StubInstaller::default(),
                &config,
                StubTimer,
                StubMetricsReporter,
                Rc::new(Mutex::new(MemStorage::new())),
                make_test_app_set(),
            )
            .await;
            match do_update_check(&mut state_machine).await {
                Err(UpdateCheckError::InstallPlan(_)) => {} // expected
                result @ _ => {
                    panic!("Unexpected result from do_update_check: {:?}", result);
                }
            }

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

            let mut state_machine = StateMachine::new(
                StubPolicyEngine,
                http,
                StubInstaller { should_fail: true },
                &config,
                StubTimer,
                StubMetricsReporter,
                Rc::new(Mutex::new(MemStorage::new())),
                make_test_app_set(),
            )
            .await;
            let response = do_update_check(&mut state_machine).await.unwrap();
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
            let mut state_machine = StateMachine::new(
                policy_engine,
                http,
                StubInstaller::default(),
                &config,
                StubTimer,
                StubMetricsReporter,
                Rc::new(Mutex::new(MemStorage::new())),
                make_test_app_set(),
            )
            .await;
            let response = do_update_check(&mut state_machine).await.unwrap();
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
            let mut state_machine = StateMachine::new(
                policy_engine,
                http,
                StubInstaller::default(),
                &config,
                StubTimer,
                StubMetricsReporter,
                Rc::new(Mutex::new(MemStorage::new())),
                make_test_app_set(),
            )
            .await;
            let response = do_update_check(&mut state_machine).await.unwrap();
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

        let state_machine = block_on(StateMachine::new(
            policy_engine,
            StubHttpRequest,
            StubInstaller::default(),
            &config,
            timer,
            StubMetricsReporter,
            Rc::new(Mutex::new(MemStorage::new())),
            make_test_app_set(),
        ));
        let state_machine = Rc::new(RefCell::new(state_machine));

        let mut pool = LocalPool::new();
        pool.spawner()
            .spawn_local(async move {
                StateMachine::start(state_machine).await;
            })
            .unwrap();
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

        let state_machine = block_on(StateMachine::new(
            policy_engine,
            http,
            StubInstaller::default(),
            &config,
            timer,
            StubMetricsReporter,
            Rc::new(Mutex::new(MemStorage::new())),
            make_test_app_set(),
        ));
        let state_machine = Rc::new(RefCell::new(state_machine));

        {
            let state_machine = state_machine.clone();
            let mut pool = LocalPool::new();
            pool.spawner()
                .spawn_local(async move {
                    StateMachine::start(state_machine).await;
                })
                .unwrap();
            pool.run_until_stalled();
        }

        // Move |state_machine| out of Rc and RefCell.
        let state_machine = Rc::try_unwrap(state_machine).unwrap().into_inner();

        // Check that apps are updated based on Omaha response.
        let apps = block_on(state_machine.app_set.to_vec());
        assert_eq!(Some("1".to_string()), apps[0].cohort.id);
        assert_eq!(None, apps[0].cohort.hint);
        assert_eq!(Some("stable-channel".to_string()), apps[0].cohort.name);
        assert_eq!(UserCounting::ClientRegulatedByDate(Some(1234567)), apps[0].user_counting);

        // Check that the next update check uses the new app.
        let request_params = RequestParams::default();
        let mut request_builder = RequestBuilder::new(&config, &request_params);
        request_builder = request_builder.add_update_check(&apps[0]);
        request_builder = request_builder.add_ping(&apps[0]);
        block_on(assert_request(state_machine.http, request_builder));
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

            let mut state_machine = StateMachine::new(
                StubPolicyEngine,
                http,
                StubInstaller::default(),
                &config,
                StubTimer,
                StubMetricsReporter,
                Rc::new(Mutex::new(MemStorage::new())),
                make_test_app_set(),
            )
            .await;
            let response = do_update_check(&mut state_machine).await.unwrap();

            assert_eq!(
                UserCounting::ClientRegulatedByDate(Some(1234567)),
                response.app_responses[0].user_counting
            );
        });
    }

    #[derive(Clone, Debug, Default)]
    struct TestObserver {
        actual_states: Option<Rc<RefCell<Vec<State>>>>,
    }

    impl Observer for TestObserver {
        fn on_state_change(&mut self, state: State) -> LocalBoxFuture<'_, ()> {
            // For `test_report_successful_update_duration()`.
            if state == State::FinalizingUpdate {
                clock::mock::set(time::i64_to_time(222222222))
            }

            if let Some(actual_states) = &self.actual_states {
                let mut actual_states = actual_states.borrow_mut();
                actual_states.push(state);
            }
            future::ready(()).boxed_local()
        }
    }

    #[test]
    fn test_observe_state() {
        block_on(async {
            let config = config_generator();
            let mut state_machine = StateMachine::new_stub(&config, make_test_app_set()).await;
            let actual_states = Vec::new();
            let actual_states = Rc::new(RefCell::new(actual_states));
            state_machine.set_observer(TestObserver { actual_states: Some(actual_states.clone()) });
            state_machine.start_update_check(CheckOptions::default()).await;
            drop(state_machine);
            let actual_states = actual_states.borrow();
            let expected_states =
                vec![State::CheckingForUpdates, State::EncounteredError, State::Idle];
            assert_eq!(*actual_states, expected_states);
        });
    }

    #[test]
    fn test_metrics_report_update_check_response_time() {
        block_on(async {
            let config = config_generator();
            let mut state_machine = StateMachine::new(
                StubPolicyEngine,
                StubHttpRequest,
                StubInstaller::default(),
                &config,
                StubTimer,
                MockMetricsReporter::new(false),
                Rc::new(Mutex::new(MemStorage::new())),
                make_test_app_set(),
            )
            .await;
            let _result = do_update_check(&mut state_machine).await;

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
            let mut state_machine = StateMachine::new(
                StubPolicyEngine,
                StubHttpRequest,
                StubInstaller::default(),
                &config,
                MockTimer::new(),
                MockMetricsReporter::new(false),
                Rc::new(Mutex::new(MemStorage::new())),
                make_test_app_set(),
            )
            .await;
            let _result = do_update_check(&mut state_machine).await;

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
            let mut state_machine = StateMachine::new(
                StubPolicyEngine,
                MockHttpRequest::empty(),
                StubInstaller::default(),
                &config,
                timer,
                MockMetricsReporter::new(false),
                Rc::new(Mutex::new(MemStorage::new())),
                make_test_app_set(),
            )
            .await;

            assert_matches!(
                do_update_check(&mut state_machine).await,
                Err(UpdateCheckError::OmahaRequest(OmahaRequestError::HttpStatus(_)))
            );
        });
    }

    #[test]
    fn test_metrics_report_update_check_failure_reason_omaha() {
        block_on(async {
            let config = config_generator();
            let mut state_machine = StateMachine::new(
                StubPolicyEngine,
                StubHttpRequest,
                StubInstaller::default(),
                &config,
                StubTimer,
                MockMetricsReporter::new(false),
                Rc::new(Mutex::new(MemStorage::new())),
                make_test_app_set(),
            )
            .await;
            state_machine.start_update_check(CheckOptions::default()).await;

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
            let mut state_machine = StateMachine::new(
                StubPolicyEngine,
                MockHttpRequest::empty(),
                StubInstaller::default(),
                &config,
                StubTimer,
                MockMetricsReporter::new(false),
                Rc::new(Mutex::new(MemStorage::new())),
                make_test_app_set(),
            )
            .await;
            state_machine.start_update_check(CheckOptions::default()).await;

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
            let mut state_machine = StateMachine::new_stub(&config, make_test_app_set()).await;
            state_machine.start_update_check(CheckOptions::default()).await;

            let storage = state_machine.storage_ref.lock().await;
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
            let mut state_machine = StateMachine::new_stub(&config, make_test_app_set()).await;
            state_machine.start_update_check(CheckOptions::default()).await;

            let storage = state_machine.storage_ref.lock().await;
            assert!(storage.get_int(SERVER_DICTATED_POLL_INTERVAL).await.is_none());
            assert!(storage.committed());
        });
    }

    #[test]
    fn test_persist_app() {
        block_on(async {
            let config = config_generator();
            let mut state_machine = StateMachine::new_stub(&config, make_test_app_set()).await;
            state_machine.start_update_check(CheckOptions::default()).await;

            let storage = state_machine.storage_ref.lock().await;
            let apps = state_machine.app_set.to_vec().await;
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
            let state_machine = StateMachine::new(
                StubPolicyEngine,
                StubHttpRequest,
                StubInstaller::default(),
                &config,
                StubTimer,
                StubMetricsReporter,
                Rc::new(Mutex::new(storage)),
                make_test_app_set(),
            )
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
            let state_machine = StateMachine::new(
                StubPolicyEngine,
                StubHttpRequest,
                StubInstaller::default(),
                &config,
                StubTimer,
                StubMetricsReporter,
                Rc::new(Mutex::new(storage)),
                make_test_app_set(),
            )
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
            let _state_machine = StateMachine::new(
                StubPolicyEngine,
                StubHttpRequest,
                StubInstaller::default(),
                &config,
                StubTimer,
                StubMetricsReporter,
                Rc::new(Mutex::new(storage)),
                app_set.clone(),
            )
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
            let mut state_machine = StateMachine::new(
                StubPolicyEngine,
                StubHttpRequest,
                StubInstaller::default(),
                &config,
                StubTimer,
                MockMetricsReporter::new(false),
                Rc::new(Mutex::new(MemStorage::new())),
                make_test_app_set(),
            )
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
            let mut state_machine = StateMachine::new(
                StubPolicyEngine,
                http,
                StubInstaller::default(),
                &config,
                StubTimer,
                MockMetricsReporter::new(false),
                Rc::new(Mutex::new(MemStorage::new())),
                make_test_app_set(),
            )
            .await;

            clock::mock::set(time::i64_to_time(123456789));
            {
                let mut storage = state_machine.storage_ref.lock().await;
                storage.set_string(INSTALL_PLAN_ID, "").await.unwrap();
                storage.set_int(UPDATE_FIRST_SEEN_TIME, 23456789).await.unwrap();
                storage.commit().await.unwrap();
            }
            state_machine.set_observer(TestObserver::default());
            state_machine.start_update_check(CheckOptions::default()).await;

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
            let mut state_machine = StateMachine::new(
                StubPolicyEngine,
                http,
                StubInstaller { should_fail: true },
                &config,
                StubTimer,
                MockMetricsReporter::new(false),
                Rc::new(Mutex::new(MemStorage::new())),
                make_test_app_set(),
            )
            .await;

            clock::mock::set(time::i64_to_time(123456789));
            state_machine.start_update_check(CheckOptions::default()).await;

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
            let mut state_machine = StateMachine::new_stub(&config, make_test_app_set()).await;

            let now = time::i64_to_time(123456789);
            assert_eq!(state_machine.record_update_first_seen_time("id", now).await, now);
            {
                let storage = state_machine.storage_ref.lock().await;
                assert_eq!(storage.get_string(INSTALL_PLAN_ID).await, Some("id".to_string()));
                assert_eq!(storage.get_int(UPDATE_FIRST_SEEN_TIME).await, Some(time_to_i64(now)));
                assert_eq!(storage.len(), 2);
                assert!(storage.committed());
            }

            let now2 = now + Duration::from_secs(1000);
            assert_eq!(state_machine.record_update_first_seen_time("id", now2).await, now);
            {
                let storage = state_machine.storage_ref.lock().await;
                assert_eq!(storage.get_string(INSTALL_PLAN_ID).await, Some("id".to_string()));
                assert_eq!(storage.get_int(UPDATE_FIRST_SEEN_TIME).await, Some(time_to_i64(now)));
                assert_eq!(storage.len(), 2);
                assert!(storage.committed());
            }
            assert_eq!(state_machine.record_update_first_seen_time("id2", now2).await, now2);
            {
                let storage = state_machine.storage_ref.lock().await;
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
            let mut state_machine = StateMachine::new(
                StubPolicyEngine,
                StubHttpRequest,
                StubInstaller { should_fail: true },
                &config,
                StubTimer,
                MockMetricsReporter::new(false),
                Rc::new(Mutex::new(MemStorage::new())),
                make_test_app_set(),
            )
            .await;

            state_machine.report_attempts_to_succeed(true).await;
            {
                let storage = state_machine.storage_ref.lock().await;
                assert_eq!(storage.get_int(CONSECUTIVE_FAILED_UPDATE_CHECKS).await, None);
                assert_eq!(storage.len(), 0);
            }
            assert_eq!(state_machine.metrics_reporter.metrics, vec![Metrics::AttemptsToSucceed(1)]);

            state_machine.report_attempts_to_succeed(false).await;
            {
                let storage = state_machine.storage_ref.lock().await;
                assert_eq!(storage.get_int(CONSECUTIVE_FAILED_UPDATE_CHECKS).await, Some(1));
                assert_eq!(storage.len(), 1);
            }

            state_machine.report_attempts_to_succeed(false).await;
            {
                let storage = state_machine.storage_ref.lock().await;
                assert_eq!(storage.get_int(CONSECUTIVE_FAILED_UPDATE_CHECKS).await, Some(2));
                assert_eq!(storage.len(), 1);
            }

            state_machine.report_attempts_to_succeed(true).await;
            {
                let storage = state_machine.storage_ref.lock().await;
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
    fn test_randomize() {
        let n = randomize(10, 10);
        assert!(n >= 5 && n < 15, "n = {}", n);
        let n = randomize(1000, 100);
        assert!(n >= 950 && n < 1050, "n = {}", n);
        // only one integer in [123456, 123457)
        assert_eq!(randomize(123456, 1), 123456);
    }
}
