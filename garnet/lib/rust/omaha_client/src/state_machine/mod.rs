// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    common::{App, CheckOptions},
    configuration::Config,
    http_request::HttpRequest,
    installer::{Installer, Plan},
    policy::{CheckDecision, PolicyEngine, UpdateDecision},
    protocol::{
        request::{Event, EventErrorCode, EventResult, EventType},
        response::{parse_json_response, OmahaStatus, Response},
    },
    request_builder::{self, RequestBuilder, RequestParams},
};
use failure::Fail;
use futures::{compat::Stream01CompatExt, prelude::*};
use http::response::Parts;
use log::{error, info, warn};
use std::str::Utf8Error;

pub mod update_check;

mod timer;
pub use timer::Timer;

/// This is the core state machine for a client's update check.  It is instantiated and used to
/// perform a single update check process.
pub struct StateMachine<PE, HR, IN>
where
    PE: PolicyEngine,
    HR: HttpRequest,
    IN: Installer,
{
    /// The immutable configuration of the client itself.
    client_config: Config,

    policy_engine: PE,

    http: HR,

    installer: IN,
}

/// This is the set of errors that can occur when making a request to Omaha.  This is an internal
/// collection of error types.
#[derive(Fail, Debug)]
enum OmahaRequestError {
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

/// This is the set of errors that can occur when parsing the response body from Omaha.  This is an
/// internal collection of error types.
#[derive(Fail, Debug)]
#[allow(dead_code)]
enum ResponseParseError {
    #[fail(display = "Response was not valid UTF-8")]
    Utf8(#[cause] Utf8Error),

    #[fail(display = "Unexpected JSON error parsing update check response: {}", _0)]
    Json(#[cause] serde_json::Error),
}

impl<PE, HR, IN> StateMachine<PE, HR, IN>
where
    PE: PolicyEngine,
    HR: HttpRequest,
    IN: Installer,
{
    pub fn new(policy_engine: PE, http: HR, installer: IN, config: &Config) -> Self {
        StateMachine { client_config: config.clone(), policy_engine, http, installer }
    }

    /// This function constructs the chain of async futures needed to perform all of the async tasks
    /// that comprise an update check.
    /// TODO: change from sync to fire and forget that sets up the future for execution.
    pub async fn perform_update_check<'a>(
        &'a mut self,
        options: CheckOptions,
        apps: &'a [App],
        context: update_check::Context,
    ) -> Result<(), failure::Error> {
        info!("Checking to see if an update check is allowed at this time for {:?}", apps);
        let decision = await!(self.policy_engine.update_check_allowed(
            apps,
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
                return Ok(());
            }
            CheckDecision::ThrottledByPolicy => {
                info!("Update check has been throttled by the Policy, ending");
                // TODO: Report status
                return Ok(());
            }
            CheckDecision::DeniedByPolicy => {
                info!("Update check has ben denied by the Policy");
                // TODO: Report status
                return Ok(());
            }
        };

        // Construct a request for the app(s).
        let mut request_builder = RequestBuilder::new(&self.client_config, &request_params);
        for app in apps {
            request_builder = request_builder.add_update_check(app);
        }

        let (_parts, data) = match await!(Self::do_omaha_request(&mut self.http, request_builder)) {
            Ok(res) => res,
            Err(OmahaRequestError::Json(e)) => {
                error!("Unable to construct request body! {:?}", e);
                // TODO:  Report status
                return Ok(());
            }
            Err(OmahaRequestError::HttpBuilder(e)) => {
                error!("Unable to construct HTTP request! {:?}", e);
                // TODO:  Report status
                return Ok(());
            }
            Err(OmahaRequestError::Hyper(e)) => {
                warn!("Unable to contact Omaha: {:?}", e);
                // TODO:  Report status
                // TODO:  Parse for proper retry behavior
                return Ok(());
            }
            Err(OmahaRequestError::HttpStatus(e)) => {
                warn!("Unable to contact Omaha: {:?}", e);
                // TODO:  Report status
                // TODO:  Parse for proper retry behavior
                return Ok(());
            }
        };

        let response = match Self::parse_omaha_response(&data) {
            Ok(res) => res,
            Err(err) => {
                warn!("Unable to parse Omaha response: {:?}", err);
                await!(self.report_error_event(
                    apps,
                    &request_params,
                    EventErrorCode::ParseResponse,
                ));
                return Ok(());
            }
        };

        info!("result: {:?}", response);

        let statuses = Self::get_app_update_statuses(&response);
        for (app_id, status) in &statuses {
            info!("Omaha update check status: {} => {:?}", app_id, status);
        }

        if statuses.iter().any(|(_id, status)| **status == OmahaStatus::Ok) {
            info!(
                "At least one app has an update, proceeding to build and process an Install Plan"
            );

            let install_plan = match IN::InstallPlan::try_create_from(&response) {
                Ok(plan) => plan,
                Err(e) => {
                    error!("Unable to construct install plan! {}", e);
                    await!(self.report_error_event(
                        apps,
                        &request_params,
                        EventErrorCode::ConstructInstallPlan
                    ));
                    return Ok(());
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
                    // TODO: Report status (Deferred)
                    let event = Event {
                        event_type: EventType::UpdateComplete,
                        event_result: EventResult::UpdateDeferred,
                        ..Event::default()
                    };
                    await!(self.report_omaha_event(apps, &request_params, event));
                    return Ok(());
                }
                UpdateDecision::DeniedByPolicy => {
                    warn!("Install plan was denied by Policy, see Policy logs for reasoning");
                    // TODO: Report status (Error)
                    await!(self.report_error_event(
                        apps,
                        &request_params,
                        EventErrorCode::DeniedByPolicy
                    ));
                    return Ok(());
                }
            }

            // TODO: Report Status (Updating)
            await!(self.report_success_event(
                apps,
                &request_params,
                EventType::UpdateDownloadStarted
            ));

            let install_result = await!(self.installer.perform_install(&install_plan, None));
            if let Err(e) = install_result {
                warn!("Installation failed: {}", e);
                await!(self.report_error_event(
                    apps,
                    &request_params,
                    EventErrorCode::Installation
                ));
                return Ok(());
            }

            // TODO: Report Status (Done)
            await!(self.report_success_event(
                apps,
                &request_params,
                EventType::UpdateDownloadFinished
            ));

            // TODO: Verify downloaded update if needed.

            await!(self.report_success_event(apps, &request_params, EventType::UpdateComplete));

            // TODO: Consult Policy for next update time.
        }

        Ok(())
    }

    /// Report an error event to Omaha.
    async fn report_error_event<'a>(
        &'a mut self,
        apps: &'a [App],
        request_params: &'a RequestParams,
        errorcode: EventErrorCode,
    ) {
        // TODO: Report ENCOUNTERED_ERROR status
        let event = Event {
            event_type: EventType::UpdateComplete,
            errorcode: Some(errorcode),
            ..Event::default()
        };
        await!(self.report_omaha_event(apps, &request_params, event));
    }

    /// Report a successful event to Omaha, for example download started, download finished, etc.
    async fn report_success_event<'a>(
        &'a mut self,
        apps: &'a [App],
        request_params: &'a RequestParams,
        event_type: EventType,
    ) {
        let event = Event { event_type, event_result: EventResult::Success, ..Event::default() };
        await!(self.report_omaha_event(apps, &request_params, event));
    }

    /// Report the given |event| to Omaha, errors occurred during reporting are logged but not
    /// acted on.
    async fn report_omaha_event<'a>(
        &'a mut self,
        apps: &'a [App],
        request_params: &'a RequestParams,
        event: Event,
    ) {
        let mut request_builder = RequestBuilder::new(&self.client_config, &request_params);
        for app in apps {
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
}

#[cfg(test)]
pub mod tests {
    use super::*;
    use crate::{
        common::{App, CheckOptions, ProtocolState, UpdateCheckSchedule},
        configuration::test_support::config_generator,
        http_request::mock::MockHttpRequest,
        installer::stub::StubInstaller,
        policy::{MockPolicyEngine, StubPolicyEngine},
        protocol::Cohort,
    };
    use futures::executor::block_on;
    use log::info;
    use serde_json::json;
    use std::time::SystemTime;

    async fn do_update_check<'a, PE, HR, IN>(
        state_machine: &'a mut StateMachine<PE, HR, IN>,
        apps: &'a [App],
    ) where
        PE: PolicyEngine,
        HR: HttpRequest,
        IN: Installer,
    {
        let options = CheckOptions::default();

        let context = update_check::Context {
            schedule: UpdateCheckSchedule {
                last_update_time: SystemTime::now() - std::time::Duration::new(500, 0),
                next_update_time: SystemTime::now(),
                next_update_window_start: SystemTime::now(),
            },
            state: ProtocolState::default(),
        };

        await!(state_machine.perform_update_check(options, &apps, context)).unwrap();
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

            let mut state_machine =
                StateMachine::new(StubPolicyEngine, http, StubInstaller::default(), &config);
            let apps = make_test_apps();
            await!(do_update_check(&mut state_machine, &apps));

            info!("update check complete!");
        });
    }

    #[test]
    fn test_report_parse_response_error() {
        block_on(async {
            let config = config_generator();
            let mut http = MockHttpRequest::new(hyper::Response::new("invalid response".into()));
            http.add_response(hyper::Response::new("".into()));

            let mut state_machine =
                StateMachine::new(StubPolicyEngine, http, StubInstaller::default(), &config);
            let apps = make_test_apps();
            await!(do_update_check(&mut state_machine, &apps));

            let request_params = RequestParams::default();
            let mut request_builder = RequestBuilder::new(&config, &request_params);
            let event = Event {
                event_type: EventType::UpdateComplete,
                errorcode: Some(EventErrorCode::ParseResponse),
                ..Event::default()
            };
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

            let mut state_machine =
                StateMachine::new(StubPolicyEngine, http, StubInstaller::default(), &config);
            let apps = make_test_apps();
            await!(do_update_check(&mut state_machine, &apps));

            let request_params = RequestParams::default();
            let mut request_builder = RequestBuilder::new(&config, &request_params);
            let event = Event {
                event_type: EventType::UpdateComplete,
                errorcode: Some(EventErrorCode::ConstructInstallPlan),
                ..Event::default()
            };
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
            );
            let apps = make_test_apps();
            await!(do_update_check(&mut state_machine, &apps));

            let request_params = RequestParams::default();
            let mut request_builder = RequestBuilder::new(&config, &request_params);
            let event = Event {
                event_type: EventType::UpdateComplete,
                errorcode: Some(EventErrorCode::Installation),
                ..Event::default()
            };
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
                check_decision: Some(CheckDecision::Ok(RequestParams::default())),
                update_decision: Some(UpdateDecision::DeferredByPolicy),
                ..MockPolicyEngine::default()
            };
            let mut state_machine =
                StateMachine::new(policy_engine, http, StubInstaller::default(), &config);
            let apps = make_test_apps();
            await!(do_update_check(&mut state_machine, &apps));

            let request_params = RequestParams::default();
            let mut request_builder = RequestBuilder::new(&config, &request_params);
            let event = Event {
                event_type: EventType::UpdateComplete,
                event_result: EventResult::UpdateDeferred,
                ..Event::default()
            };
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
                check_decision: Some(CheckDecision::Ok(RequestParams::default())),
                update_decision: Some(UpdateDecision::DeniedByPolicy),
                ..MockPolicyEngine::default()
            };
            let mut state_machine =
                StateMachine::new(policy_engine, http, StubInstaller::default(), &config);
            let apps = make_test_apps();
            await!(do_update_check(&mut state_machine, &apps));

            let request_params = RequestParams::default();
            let mut request_builder = RequestBuilder::new(&config, &request_params);
            let event = Event {
                event_type: EventType::UpdateComplete,
                errorcode: Some(EventErrorCode::DeniedByPolicy),
                ..Event::default()
            };
            request_builder = request_builder.add_event(&apps[0], &event);
            await!(assert_request(state_machine.http, request_builder));
        });
    }
}
