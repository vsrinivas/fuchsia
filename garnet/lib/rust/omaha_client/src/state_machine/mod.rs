// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    clock,
    common::{App, CheckOptions, ProtocolState, UpdateCheckSchedule},
    configuration::Config,
    http_request::HttpRequest,
    installer::{Installer, Plan},
    policy::{CheckDecision, PolicyEngine, UpdateDecision},
    protocol::{
        self,
        request::{Event, EventErrorCode, EventResult, EventType, InstallSource},
        response::{parse_json_response, OmahaStatus, Response},
    },
    request_builder::{self, RequestBuilder, RequestParams},
};
use chrono::{DateTime, Utc};
use failure::Fail;
use futures::{compat::Stream01CompatExt, prelude::*};
use http::response::Parts;
use log::{error, info, warn};
use std::cell::RefCell;
use std::fmt;
use std::rc::Rc;
use std::str::Utf8Error;
use std::time::SystemTime;

pub mod update_check;

mod timer;
#[cfg(test)]
pub use timer::MockTimer;
pub use timer::StubTimer;
pub use timer::Timer;

/// This is the core state machine for a client's update check.  It is instantiated and used to
/// perform a single update check process.
#[derive(Debug)]
pub struct StateMachine<PE, HR, IN, TM>
where
    PE: PolicyEngine,
    HR: HttpRequest,
    IN: Installer,
    TM: Timer,
{
    /// The immutable configuration of the client itself.
    client_config: Config,

    policy_engine: PE,

    http: HR,

    installer: IN,

    timer: TM,

    /// Context for update check.
    context: update_check::Context,

    /// The current State of the StateMachine.
    state: State,

    /// Called whenever the state is changed.
    state_callback: Option<Box<StateCallback>>,

    /// The list of apps used for update check.
    apps: Vec<App>,
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

// We need to define our own trait because we need Debug trait but FnMut doesn't implement it.
pub trait StateCallback: FnMut(State) + 'static {}
impl<T: FnMut(State) + 'static> StateCallback for T {}

impl fmt::Debug for StateCallback {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "StateCallback")
    }
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

impl<PE, HR, IN, TM> StateMachine<PE, HR, IN, TM>
where
    PE: PolicyEngine,
    HR: HttpRequest,
    IN: Installer,
    TM: Timer,
{
    pub fn new(policy_engine: PE, http: HR, installer: IN, config: &Config, timer: TM) -> Self {
        StateMachine {
            client_config: config.clone(),
            policy_engine,
            http,
            installer,
            timer,
            context: Self::load_context(),
            state: State::Idle,
            state_callback: None,
            apps: vec![],
        }
    }

    /// Add |apps| to the list of apps the StateMachine use for update check.
    pub fn add_apps(&mut self, mut apps: Vec<App>) {
        self.apps.append(&mut apps);
    }

    /// Load and initialize update check context from persistent storage.
    fn load_context() -> update_check::Context {
        // TODO: Read last_update_time, server_dictated_poll_interval, etc from storage.
        update_check::Context {
            schedule: UpdateCheckSchedule {
                last_update_time: SystemTime::UNIX_EPOCH,
                next_update_time: clock::now(),
                next_update_window_start: clock::now(),
            },
            state: ProtocolState::default(),
        }
    }

    /// Need to do this in a mutable method because the borrow checker isn't smart enough to know
    /// that different fields of the same struct (even if it's not self) are separate variables and
    /// can be borrowed at the same time.
    async fn update_next_update_time(&mut self) {
        self.context.schedule = await!(self.policy_engine.compute_next_update_time(
            &self.apps,
            &self.context.schedule,
            &self.context.state,
        ));
    }

    /// Start the StateMachine to do periodic update check in the background. The future this
    /// function returns never finishes!
    pub async fn start(state_machine_ref: Rc<RefCell<Self>>) {
        loop {
            {
                let mut state_machine = state_machine_ref.borrow_mut();
                await!(state_machine.update_next_update_time());
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
                    await!(fut);
                }
            }

            let options = CheckOptions { source: InstallSource::ScheduledTask };
            let mut state_machine = state_machine_ref.borrow_mut();
            await!(state_machine.start_update_check(options));
        }
    }

    /// Perform update check and handle the result, including updating the update check context
    /// and cohort.
    pub async fn start_update_check(&mut self, options: CheckOptions) {
        match await!(self.perform_update_check(options, self.context.clone())) {
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

                // Update the cohort for each app from Omaha.
                for app_response in result.app_responses {
                    for app in self.apps.iter_mut() {
                        if app.id == app_response.app_id {
                            app.cohort = app_response.cohort;
                            break;
                        }
                    }
                }

                // TODO: update consecutive_proxied_requests
            }
            Err(error) => {
                error!("Update check failed: {:?}", error);
                // Update check failed, increment |consecutive_failed_update_checks|.
                self.context.state.consecutive_failed_update_checks += 1;

                match error {
                    UpdateCheckError::ResponseParser(_) | UpdateCheckError::InstallPlan(_) => {
                        // We talked to Omaha, update |last_update_time|.
                        self.context.schedule.last_update_time = clock::now();
                    }
                    _ => {}
                }
            }
        }

        if self.state != State::WaitingForReboot {
            self.set_state(State::Idle);
        }
    }

    /// This function constructs the chain of async futures needed to perform all of the async tasks
    /// that comprise an update check.
    pub async fn perform_update_check(
        &mut self,
        options: CheckOptions,
        context: update_check::Context,
    ) -> Result<update_check::Response, UpdateCheckError> {
        // TODO: Move this check outside perform_update_check() so that FIDL server can know if
        // update check is throttled.
        info!("Checking to see if an update check is allowed at this time for {:?}", self.apps);
        let decision = await!(self.policy_engine.update_check_allowed(
            &self.apps,
            &context.schedule,
            &context.state,
            &options,
        ));

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

        self.set_state(State::CheckingForUpdates);

        // Construct a request for the app(s).
        let mut request_builder = RequestBuilder::new(&self.client_config, &request_params);
        for app in &self.apps {
            request_builder = request_builder.add_update_check(app);
        }

        let (_parts, data) = match await!(Self::do_omaha_request(&mut self.http, request_builder)) {
            Ok(res) => res,
            Err(OmahaRequestError::Json(e)) => {
                error!("Unable to construct request body! {:?}", e);
                self.set_state(State::EncounteredError);
                return Err(UpdateCheckError::OmahaRequest(e.into()));
            }
            Err(OmahaRequestError::HttpBuilder(e)) => {
                error!("Unable to construct HTTP request! {:?}", e);
                self.set_state(State::EncounteredError);
                return Err(UpdateCheckError::OmahaRequest(e.into()));
            }
            Err(OmahaRequestError::Hyper(e)) => {
                warn!("Unable to contact Omaha: {:?}", e);
                self.set_state(State::EncounteredError);
                // TODO:  Parse for proper retry behavior
                return Err(UpdateCheckError::OmahaRequest(e.into()));
            }
            Err(OmahaRequestError::HttpStatus(e)) => {
                warn!("Unable to contact Omaha: {:?}", e);
                self.set_state(State::EncounteredError);
                // TODO:  Parse for proper retry behavior
                return Err(UpdateCheckError::OmahaRequest(e.into()));
            }
        };

        let response = match Self::parse_omaha_response(&data) {
            Ok(res) => res,
            Err(err) => {
                warn!("Unable to parse Omaha response: {:?}", err);
                await!(self.report_error(&request_params, EventErrorCode::ParseResponse));
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
            self.set_state(State::UpdateAvailable);

            let install_plan = match IN::InstallPlan::try_create_from(&request_params, &response) {
                Ok(plan) => plan,
                Err(e) => {
                    error!("Unable to construct install plan! {}", e);
                    await!(self.report_error(&request_params, EventErrorCode::ConstructInstallPlan));
                    return Err(UpdateCheckError::InstallPlan(e.into()));
                }
            };

            info!("Validating Install Plan with Policy");
            let install_plan_decision = await!(self.policy_engine.update_can_start(&install_plan));
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
                    await!(self.report_omaha_event(&request_params, event));

                    // TODO: Report progress status (Deferred)
                    return Ok(Self::make_response(
                        response,
                        update_check::Action::DeferredByPolicy,
                    ));
                }
                UpdateDecision::DeniedByPolicy => {
                    warn!("Install plan was denied by Policy, see Policy logs for reasoning");
                    await!(self.report_error(&request_params, EventErrorCode::DeniedByPolicy));
                    return Ok(Self::make_response(response, update_check::Action::DeniedByPolicy));
                }
            }

            self.set_state(State::PerformingUpdate);
            await!(self.report_success_event(&request_params, EventType::UpdateDownloadStarted));

            let install_result = await!(self.installer.perform_install(&install_plan, None));
            if let Err(e) = install_result {
                warn!("Installation failed: {}", e);
                await!(self.report_error(&request_params, EventErrorCode::Installation));
                return Ok(Self::make_response(
                    response,
                    update_check::Action::InstallPlanExecutionError,
                ));
            }

            await!(self.report_success_event(&request_params, EventType::UpdateDownloadFinished));
            self.set_state(State::FinalizingUpdate);

            // TODO: Verify downloaded update if needed.

            await!(self.report_success_event(&request_params, EventType::UpdateComplete));

            self.set_state(State::WaitingForReboot);
            Ok(Self::make_response(response, update_check::Action::Updated))
        }
    }

    /// Set the current state to |EncounteredError| and report the error event to Omaha.
    async fn report_error<'a>(
        &'a mut self,
        request_params: &'a RequestParams,
        errorcode: EventErrorCode,
    ) {
        self.set_state(State::EncounteredError);

        let event = Event {
            event_type: EventType::UpdateComplete,
            errorcode: Some(errorcode),
            ..Event::default()
        };
        await!(self.report_omaha_event(&request_params, event));
    }

    /// Report a successful event to Omaha, for example download started, download finished, etc.
    async fn report_success_event<'a>(
        &'a mut self,
        request_params: &'a RequestParams,
        event_type: EventType,
    ) {
        let event = Event { event_type, event_result: EventResult::Success, ..Event::default() };
        await!(self.report_omaha_event(&request_params, event));
    }

    /// Report the given |event| to Omaha, errors occurred during reporting are logged but not
    /// acted on.
    async fn report_omaha_event<'a>(&'a mut self, request_params: &'a RequestParams, event: Event) {
        let mut request_builder = RequestBuilder::new(&self.client_config, &request_params);
        for app in &self.apps {
            request_builder = request_builder.add_event(app, &event);
        }
        if let Err(e) = await!(Self::do_omaha_request(&mut self.http, request_builder)) {
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
        builder: RequestBuilder<'a>,
    ) -> Result<(Parts, Vec<u8>), OmahaRequestError> {
        let (parts, body) = await!(Self::make_request(http, builder.build()?))?;
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
        let res = await!(http_client.request(request)).map_err(|err| {
            warn!("Unable to perform request: {}", err);
            err
        })?;

        let (parts, body) = res.into_parts();
        let data = await!(body.compat().try_concat())?;

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

    /// Update the state internally and send it to the callback.
    fn set_state(&mut self, state: State) {
        self.state = state.clone();
        if let Some(callback) = &mut self.state_callback {
            callback(state);
        }
    }

    /// Set the callback function that will be called every time state changes.
    pub fn set_state_callback(&mut self, callback: impl StateCallback) {
        self.state_callback = Some(Box::new(callback));
    }
}

#[cfg(test)]
mod tests {
    use super::update_check::*;
    use super::*;
    use crate::{
        common::{App, CheckOptions, ProtocolState, UpdateCheckSchedule, UserCounting},
        configuration::test_support::config_generator,
        http_request::{mock::MockHttpRequest, StubHttpRequest},
        installer::stub::StubInstaller,
        policy::{MockPolicyEngine, StubPolicyEngine},
        protocol::Cohort,
    };
    use futures::executor::{block_on, LocalPool};
    use futures::task::LocalSpawnExt;
    use log::info;
    use pretty_assertions::assert_eq;
    use serde_json::json;
    use std::time::{Duration, SystemTime};

    async fn do_update_check<'a, PE, HR, IN, TM>(
        state_machine: &'a mut StateMachine<PE, HR, IN, TM>,
    ) -> Result<update_check::Response, UpdateCheckError>
    where
        PE: PolicyEngine,
        HR: HttpRequest,
        IN: Installer,
        TM: Timer,
    {
        let options = CheckOptions::default();

        let context = update_check::Context {
            schedule: UpdateCheckSchedule {
                last_update_time: clock::now() - std::time::Duration::new(500, 0),
                next_update_time: clock::now(),
                next_update_window_start: clock::now(),
            },
            state: ProtocolState::default(),
        };

        state_machine.add_apps(make_test_apps());
        await!(state_machine.perform_update_check(options, context))
    }

    fn make_test_apps() -> Vec<App> {
        vec![App::new(
            "{00000000-0000-0000-0000-000000000001}",
            [1, 2, 3, 4],
            Cohort::new("stable-channel"),
        )]
    }

    // Assert that the last request made to |http| is equal to the request built by
    // |request_builder|.
    async fn assert_request<'a>(http: MockHttpRequest, request_builder: RequestBuilder<'a>) {
        let body = await!(request_builder.build().unwrap().into_body().compat().try_concat())
            .unwrap()
            .to_vec();
        // Compare string instead of Vec<u8> for easier debugging.
        let body_str = String::from_utf8_lossy(&body);
        await!(http.assert_body_str(&body_str));
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
            );
            await!(do_update_check(&mut state_machine)).unwrap();

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
            );
            let response = await!(do_update_check(&mut state_machine)).unwrap();
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
            let mut http = MockHttpRequest::new(hyper::Response::new("invalid response".into()));
            http.add_response(hyper::Response::new("".into()));

            let mut state_machine = StateMachine::new(
                StubPolicyEngine,
                http,
                StubInstaller::default(),
                &config,
                StubTimer,
            );
            match await!(do_update_check(&mut state_machine)) {
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
            let apps = make_test_apps();
            request_builder = request_builder.add_event(&apps[0], &event);
            await!(assert_request(state_machine.http, request_builder));
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
            let mut http = MockHttpRequest::new(hyper::Response::new(response.into()));
            http.add_response(hyper::Response::new("".into()));

            let mut state_machine = StateMachine::new(
                StubPolicyEngine,
                http,
                StubInstaller::default(),
                &config,
                StubTimer,
            );
            match await!(do_update_check(&mut state_machine)) {
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
            let apps = make_test_apps();
            request_builder = request_builder.add_event(&apps[0], &event);
            await!(assert_request(state_machine.http, request_builder));
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
            let mut http = MockHttpRequest::new(hyper::Response::new(response.into()));
            // For reporting UpdateDownloadStarted
            http.add_response(hyper::Response::new("".into()));
            // For reporting installation error
            http.add_response(hyper::Response::new("".into()));

            let mut state_machine = StateMachine::new(
                StubPolicyEngine,
                http,
                StubInstaller { should_fail: true },
                &config,
                StubTimer,
            );
            let response = await!(do_update_check(&mut state_machine)).unwrap();
            assert_eq!(Action::InstallPlanExecutionError, response.app_responses[0].result);

            let request_params = RequestParams::default();
            let mut request_builder = RequestBuilder::new(&config, &request_params);
            let event = Event {
                event_type: EventType::UpdateComplete,
                errorcode: Some(EventErrorCode::Installation),
                ..Event::default()
            };
            let apps = make_test_apps();
            request_builder = request_builder.add_event(&apps[0], &event);
            await!(assert_request(state_machine.http, request_builder));
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
            let mut http = MockHttpRequest::new(hyper::Response::new(response.into()));
            http.add_response(hyper::Response::new("".into()));

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
            );
            let response = await!(do_update_check(&mut state_machine)).unwrap();
            assert_eq!(Action::DeferredByPolicy, response.app_responses[0].result);

            let request_params = RequestParams::default();
            let mut request_builder = RequestBuilder::new(&config, &request_params);
            let event = Event {
                event_type: EventType::UpdateComplete,
                event_result: EventResult::UpdateDeferred,
                ..Event::default()
            };
            let apps = make_test_apps();
            request_builder = request_builder.add_event(&apps[0], &event);
            await!(assert_request(state_machine.http, request_builder));
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
            let mut http = MockHttpRequest::new(hyper::Response::new(response.into()));
            http.add_response(hyper::Response::new("".into()));
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
            );
            let response = await!(do_update_check(&mut state_machine)).unwrap();
            assert_eq!(Action::DeniedByPolicy, response.app_responses[0].result);

            let request_params = RequestParams::default();
            let mut request_builder = RequestBuilder::new(&config, &request_params);
            let event = Event {
                event_type: EventType::UpdateComplete,
                errorcode: Some(EventErrorCode::DeniedByPolicy),
                ..Event::default()
            };
            let apps = make_test_apps();
            request_builder = request_builder.add_event(&apps[0], &event);
            await!(assert_request(state_machine.http, request_builder));
        });
    }

    #[test]
    fn test_wait_timer() {
        let config = config_generator();
        let http = StubHttpRequest;
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

        let mut state_machine =
            StateMachine::new(policy_engine, http, StubInstaller::default(), &config, timer);
        state_machine.add_apps(make_test_apps());
        let state_machine = Rc::new(RefCell::new(state_machine));

        let mut pool = LocalPool::new();
        pool.spawner()
            .spawn_local(async move {
                await!(StateMachine::start(state_machine));
            })
            .unwrap();
        pool.run_until_stalled();
    }

    #[test]
    fn test_update_cohort() {
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

        let mut state_machine =
            StateMachine::new(policy_engine, http, StubInstaller::default(), &config, timer);
        state_machine.add_apps(make_test_apps());
        let state_machine = Rc::new(RefCell::new(state_machine));

        {
            let state_machine = state_machine.clone();
            let mut pool = LocalPool::new();
            pool.spawner()
                .spawn_local(async move {
                    await!(StateMachine::start(state_machine));
                })
                .unwrap();
            pool.run_until_stalled();
        }

        let request_params = RequestParams::default();
        let mut request_builder = RequestBuilder::new(&config, &request_params);
        let mut apps = make_test_apps();
        apps[0].cohort.id = Some("1".to_string());
        apps[0].cohort.name = Some("stable-channel".to_string());
        request_builder = request_builder.add_update_check(&apps[0]);
        // Move |state_machine| out of Rc and RefCell.
        let state_machine = Rc::try_unwrap(state_machine).unwrap().into_inner();
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
            );
            let response = await!(do_update_check(&mut state_machine)).unwrap();

            assert_eq!(
                UserCounting::ClientRegulatedByDate(Some(1234567)),
                response.app_responses[0].user_counting
            );
        });
    }

    #[test]
    fn test_add_apps() {
        let config = config_generator();
        let mut state_machine = StateMachine::new(
            StubPolicyEngine,
            StubHttpRequest,
            StubInstaller::default(),
            &config,
            StubTimer,
        );
        state_machine.add_apps(make_test_apps());
        assert_eq!(state_machine.apps, make_test_apps());
    }

    #[test]
    fn test_state_callback() {
        let config = config_generator();
        let mut state_machine = StateMachine::new(
            StubPolicyEngine,
            StubHttpRequest,
            StubInstaller::default(),
            &config,
            StubTimer,
        );
        state_machine.add_apps(make_test_apps());
        let actual_states = Vec::new();
        let actual_states = Rc::new(RefCell::new(actual_states));
        {
            let actual_states = actual_states.clone();
            state_machine.set_state_callback(move |state| {
                let mut actual_states = actual_states.borrow_mut();
                actual_states.push(state);
            });
        }
        block_on(state_machine.start_update_check(CheckOptions::default()));
        drop(state_machine);
        let actual_states = actual_states.borrow();
        let expected_states = vec![State::CheckingForUpdates, State::EncounteredError, State::Idle];
        assert_eq!(*actual_states, expected_states);
    }
}
