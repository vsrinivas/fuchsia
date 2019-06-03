// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    common::{App, CheckOptions},
    configuration::Config,
    http_request::HttpRequest,
    installer::{Installer, Plan},
    policy::{CheckDecision, PolicyEngine, UpdateDecision},
    protocol::response::{parse_json_response, OmahaStatus, Response},
    request_builder::{self, RequestBuilder},
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
    pub fn perform_update_check<'a>(
        &'a mut self,
        options: CheckOptions,
        apps: &'a [App],
        context: update_check::Context,
    ) -> impl Future<Output = Result<(), serde_json::Error>> + 'a {
        // This async is the main flow for a single update check to Omaha, and subsequent performing
        // of an update (if directed).
        async move {
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

            let (_parts, data) =
                match await!(Self::do_omaha_request(&mut self.http, request_builder)) {
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
                    // TODO: Report status
                    // TODO: Report parse error to Omaha
                    return Ok(());
                }
            };

            info!("result: {:?}", response);

            let statuses = Self::get_app_update_statuses(&response);
            for (app_id, status) in &statuses {
                info!("Omaha update check status: {} => {:?}", app_id, status);
            }

            if statuses.iter().any(|(_id, status)| **status == OmahaStatus::Ok) {
                info!("At least one app has an update, proceeding to build and process an Install Plan");

                let install_plan = match IN::InstallPlan::try_create_from(&response) {
                    Ok(plan) => plan,
                    Err(e) => {
                        error!("Unable to construct install plan! {}", e);
                        // TODO: Report status (Error)
                        // TODO: Report error to Omaha
                        return Ok(());
                    }
                };

                info!("Validating Install Plan with Policy");
                let install_plan_decision =
                    await!(self.policy_engine.update_can_start(&install_plan));
                match install_plan_decision {
                    UpdateDecision::Ok => {
                        info!("Proceeding with install plan.");
                    }
                    UpdateDecision::DeferredByPolicy => {
                        info!("Install plan was deferred by Policy.");
                        // TODO: Report status (Deferred)
                        // TODO: Report "error" to Omaha (as this is an event that needs reporting
                        // TODO:   as the install isn't starting immediately.
                        return Ok(());
                    }
                    UpdateDecision::DeniedByPolicy => {
                        warn!("Install plan was denied by Policy, see Policy logs for reasoning");
                        // TODO: Report status (Error)
                        // TODO: Report error to Omaha
                        return Ok(());
                    }
                }

                // TODO: Report Status (Updating)
                // TODO: Notify Omaha of download start event.

                let install_result = await!(self.installer.perform_install(&install_plan, None));
                if let Err(e) = install_result {
                    warn!("Installation failed: {}", e);
                    // TODO: Report Status
                    // TODO: Report error to Omaha
                    return Ok(());
                }

                // TODO: Report Status (Done)
                // TODO: Notify Omaha of download complete event.

                // TODO: Consult Policy for next update time.
            }

            Ok(())
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
        common::{App, CheckOptions, ProtocolState, UpdateCheckSchedule, UserCounting, Version},
        configuration::test_support::config_generator,
        http_request::mock::MockHttpRequest,
        installer::stub::StubInstaller,
        policy::stub::StubPolicyEngine,
        protocol::{request::InstallSource, Cohort},
    };
    use futures::executor::block_on;
    use log::info;
    use std::time::SystemTime;

    #[test]
    pub fn run_simple_check_with_noupdate_result() {
        block_on(async {
            let config = config_generator();
            let http = MockHttpRequest::new(hyper::Response::new("  ".into()));
            let policy_engine = StubPolicyEngine;
            let installer = StubInstaller;
            let options = CheckOptions { source: InstallSource::OnDemand };
            let apps = vec![App {
                id: "{00000000-0000-0000-0000-000000000001}".to_string(),
                version: Version::from([1, 2, 3, 4]),
                fingerprint: None,
                cohort: Cohort::new("stable-channel"),
                user_counting: UserCounting::ClientRegulatedByDate(None),
            }];
            let context = update_check::Context {
                schedule: UpdateCheckSchedule {
                    last_update_time: SystemTime::now() - std::time::Duration::new(500, 0),
                    next_update_time: SystemTime::now(),
                    next_update_window_start: SystemTime::now(),
                },
                state: ProtocolState::default(),
            };

            let mut state_machine = StateMachine::new(policy_engine, http, installer, &config);
            await!(state_machine.perform_update_check(options, &apps, context)).unwrap();

            info!("update check complete!");
        });
    }
}
