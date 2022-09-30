// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    app_set::{AppSet, AppSetExt as _},
    common::{App, CheckOptions, CheckTiming},
    configuration::Config,
    cup_ecdsa::{CupDecorationError, CupVerificationError, Cupv2Handler, RequestMetadata},
    http_request::{self, HttpRequest},
    installer::{AppInstallResult, Installer, Plan},
    metrics::{ClockType, Metrics, MetricsReporter, UpdateCheckFailureReason},
    policy::{CheckDecision, PolicyEngine, UpdateDecision},
    protocol::{
        self,
        request::{Event, EventErrorCode, EventResult, EventType, InstallSource, GUID},
        response::{parse_json_response, OmahaStatus, Response, UpdateCheck},
    },
    request_builder::{self, RequestBuilder, RequestParams},
    storage::{Storage, StorageExt},
    time::{ComplexTime, PartialComplexTime, TimeSource, Timer},
};

use anyhow::anyhow;
use futures::{
    channel::{mpsc, oneshot},
    future::{self, BoxFuture, Fuse},
    lock::Mutex,
    prelude::*,
    select,
};
use http::{response::Parts, Response as HttpResponse};
use p256::ecdsa::DerSignature;
use std::{
    cmp::min,
    collections::HashMap,
    convert::TryInto,
    rc::Rc,
    str::Utf8Error,
    time::{Duration, Instant, SystemTime},
};
use thiserror::Error;
use tracing::{error, info, warn};

pub mod update_check;

mod builder;
pub use builder::StateMachineBuilder;

mod observer;
use observer::StateMachineProgressObserver;
pub use observer::{InstallProgress, StateMachineEvent};

const INSTALL_PLAN_ID: &str = "install_plan_id";
const UPDATE_FIRST_SEEN_TIME: &str = "update_first_seen_time";
const UPDATE_FINISH_TIME: &str = "update_finish_time";
const TARGET_VERSION: &str = "target_version";
const CONSECUTIVE_FAILED_INSTALL_ATTEMPTS: &str = "consecutive_failed_install_attempts";
// How long do we wait after not allowed to reboot to check again.
const CHECK_REBOOT_ALLOWED_INTERVAL: Duration = Duration::from_secs(30 * 60);
// This header contains the number of seconds client must not contact server again.
const X_RETRY_AFTER: &str = "X-Retry-After";
// How many requests we will make to Omaha before giving up.
const MAX_OMAHA_REQUEST_ATTEMPTS: u64 = 3;

/// This is the core state machine for a client's update check.  It is instantiated and used to
/// perform update checks over time or to perform a single update check process.
#[derive(Debug)]
pub struct StateMachine<PE, HR, IN, TM, MR, ST, AS, CH>
where
    PE: PolicyEngine,
    HR: HttpRequest,
    IN: Installer,
    TM: Timer,
    MR: MetricsReporter,
    ST: Storage,
    AS: AppSet,
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

    /// The list of apps used for update check.
    /// When locking both storage and app_set, make sure to always lock storage first.
    app_set: Rc<Mutex<AS>>,

    cup_handler: Option<CH>,
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum State {
    Idle,
    CheckingForUpdates(InstallSource),
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
    #[error("Unexpected JSON error constructing update check")]
    Json(#[from] serde_json::Error),

    #[error("Error building update check HTTP request")]
    HttpBuilder(#[from] http::Error),

    #[error("Error decorating outgoing request with CUPv2 parameters")]
    CupDecoration(#[from] CupDecorationError),

    #[error("Error validating incoming response with CUPv2 protocol")]
    CupValidation(#[from] CupVerificationError),

    // TODO: This still contains hyper user error which should be split out.
    #[error("HTTP transport error performing update check")]
    HttpTransport(#[from] http_request::Error),

    #[error("HTTP error performing update check: {0}")]
    HttpStatus(hyper::StatusCode),
}

impl From<request_builder::Error> for OmahaRequestError {
    fn from(err: request_builder::Error) -> Self {
        match err {
            request_builder::Error::Json(e) => OmahaRequestError::Json(e),
            request_builder::Error::Http(e) => OmahaRequestError::HttpBuilder(e),
            request_builder::Error::Cup(e) => OmahaRequestError::CupDecoration(e),
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
pub enum ResponseParseError {
    #[error("Response was not valid UTF-8")]
    Utf8(#[from] Utf8Error),

    #[error("Unexpected JSON error parsing update check response")]
    Json(#[from] serde_json::Error),
}

#[derive(Error, Debug)]
pub enum UpdateCheckError {
    #[error("Error checking with Omaha")]
    OmahaRequest(#[from] OmahaRequestError),

    #[error("Error parsing Omaha response")]
    ResponseParser(#[from] ResponseParseError),

    #[error("Unable to create an install plan")]
    InstallPlan(#[source] anyhow::Error),
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

#[derive(Debug)]
enum RebootAfterUpdate<T> {
    Needed(T),
    NotNeeded,
}

impl<PE, HR, IN, TM, MR, ST, AS, IR, PL, CH> StateMachine<PE, HR, IN, TM, MR, ST, AS, CH>
where
    PE: PolicyEngine<InstallResult = IR, InstallPlan = PL>,
    HR: HttpRequest,
    IN: Installer<InstallResult = IR, InstallPlan = PL>,
    TM: Timer,
    MR: MetricsReporter,
    ST: Storage,
    AS: AppSet,
    CH: Cupv2Handler,
    IR: 'static + Send,
    PL: Plan,
{
    /// Ask policy engine for the next update check time and update the context and yield event.
    async fn update_next_update_time(
        &mut self,
        co: &mut async_generator::Yield<StateMachineEvent>,
    ) -> CheckTiming {
        let apps = self.app_set.lock().await.get_apps();
        let timing = self
            .policy_engine
            .compute_next_update_time(&apps, &self.context.schedule, &self.context.state)
            .await;
        self.context.schedule.next_update_time = Some(timing);

        co.yield_(StateMachineEvent::ScheduleChange(self.context.schedule)).await;
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
        {
            let app_set = self.app_set.lock().await;
            if !app_set.all_valid() {
                error!("App set not valid, not starting state machine: {:#?}", app_set.get_apps());
                return;
            }
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
                match self.report_waited_for_reboot_duration(
                    update_finish_time.unwrap(),
                    state_machine_start_monotonic_time,
                    self.time_source.now(),
                ) {
                    Ok(()) => {
                        // If the report was successful, don't try again on the next loop.
                        should_report_waited_for_reboot_duration = false;

                        let mut storage = self.storage_ref.lock().await;
                        storage.remove_or_log(UPDATE_FINISH_TIME).await;
                        storage.remove_or_log(TARGET_VERSION).await;
                        storage.commit_or_log().await;
                    }
                    Err(e) => {
                        warn!("Couldn't report wait for reboot duration: {:#}, will try again", e);
                    }
                }
            }

            let (mut options, responder) = {
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

            let reboot_after_update = {
                let apps = self.app_set.lock().await.get_apps();
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
                        update_check_result = update_check => break update_check_result,
                        ControlRequest::StartUpdateCheck{
                            options: new_options,
                            responder
                        } = control.select_next_some() => {
                            if new_options.source == InstallSource::OnDemand {
                                info!("Got on demand update check request, ensuring ongoing check is on demand");
                                // TODO(63180): merge CheckOptions in Policy, not here.
                                options.source = InstallSource::OnDemand;
                            }

                            let _ = responder.send(StartUpdateCheckResponse::AlreadyRunning);
                        }
                    }
                }
            };

            if let RebootAfterUpdate::Needed(install_result) = reboot_after_update {
                Self::yield_state(State::WaitingForReboot, &mut co).await;
                self.wait_for_reboot(options, &mut control, install_result, &mut co).await;
            }

            Self::yield_state(State::Idle, &mut co).await;
        }
    }

    async fn wait_for_reboot(
        &mut self,
        mut options: CheckOptions,
        control: &mut mpsc::Receiver<ControlRequest>,
        install_result: IN::InstallResult,
        co: &mut async_generator::Yield<StateMachineEvent>,
    ) {
        if !self.policy_engine.reboot_allowed(&options, &install_result).await {
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
                        if self.policy_engine.reboot_allowed(&options, &install_result).await {
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
                    ControlRequest::StartUpdateCheck{
                        options: new_options,
                        responder
                    } = control.select_next_some() => {
                        let _ = responder.send(StartUpdateCheckResponse::AlreadyRunning);
                        if new_options.source == InstallSource::OnDemand {
                            info!("Waiting for reboot, but ensuring that InstallSource is OnDemand");
                            options.source = InstallSource::OnDemand;

                            if self.policy_engine.reboot_allowed(&options, &install_result).await {
                                info!("Upgraded update check request to on demand, policy allowed reboot");
                                break;
                            }
                        };
                    }
                }
            }
        }
        info!("Rebooting the system at the end of a successful update");
        if let Err(e) = self.installer.perform_reboot().await {
            error!("Unable to reboot the system: {}", e);
        }
    }

    /// Report the duration the previous boot waited to reboot based on the update finish time in
    /// storage, and the current time. Does not report a metric if there's an inconsistency in the
    /// times stored or computed, i.e. if the reboot time is later than the current time.
    /// Returns an error if time seems incorrect, e.g. update_finish_time is in the future.
    fn report_waited_for_reboot_duration(
        &mut self,
        update_finish_time: SystemTime,
        state_machine_start_monotonic_time: Instant,
        now: ComplexTime,
    ) -> Result<(), anyhow::Error> {
        // If `update_finish_time` is in the future we don't have correct time, try again
        // on the next loop.
        let update_finish_time_to_now =
            now.wall_duration_since(update_finish_time).map_err(|e| {
                anyhow!(
                    "Update finish time later than now, can't report waited for reboot duration,
                    update finish time: {:?}, now: {:?}, error: {:?}",
                    update_finish_time,
                    now,
                    e,
                )
            })?;

        // It might take a while for us to get here, but we only want to report the
        // time from update finish to state machine start after reboot, so we subtract
        // the duration since then using monotonic time.

        // We only want to report this metric if we can actually compute it.
        // If for whatever reason the clock was wrong on the previous boot, or monotonic
        // time is going backwards, better not to report this metric than to report an
        // incorrect default value.
        let state_machine_start_to_now = now
            .mono
            .checked_duration_since(state_machine_start_monotonic_time)
            .ok_or_else(|| {
                error!("Monotonic time appears to have gone backwards");
                anyhow!(
                    "State machine start later than now, can't report waited for reboot duration. \
                    State machine start: {:?}, now: {:?}",
                    state_machine_start_monotonic_time,
                    now.mono,
                )
            })?;

        let waited_for_reboot_duration =
            update_finish_time_to_now.checked_sub(state_machine_start_to_now).ok_or_else(|| {
                anyhow!(
                    "Can't report waiting for reboot duration, update finish time to now smaller \
                    than state machine start to now. Update finish time to now: {:?}, state \
                    machine start to now: {:?}",
                    update_finish_time_to_now,
                    state_machine_start_to_now,
                )
            })?;

        info!("Waited {} seconds for reboot.", waited_for_reboot_duration.as_secs());
        self.report_metrics(Metrics::WaitedForRebootDuration(waited_for_reboot_duration));
        Ok(())
    }

    /// Report update check interval based on the last check time stored in storage.
    /// It will also persist the new last check time to storage.
    async fn report_check_interval(&mut self, install_source: InstallSource) {
        let now = self.time_source.now();

        match self.context.schedule.last_update_check_time {
            // This is our first run; report the interval between that time and now,
            // and update the context with the complex time.
            Some(PartialComplexTime::Wall(t)) => match now.wall_duration_since(t) {
                Ok(interval) => self.report_metrics(Metrics::UpdateCheckInterval {
                    interval,
                    clock: ClockType::Wall,
                    install_source,
                }),
                Err(e) => warn!("Last check time is in the future: {}", e),
            },

            // We've reported an update check before, or we at least have a
            // PartialComplexTime with a monotonic component. Report our interval
            // between these Instants. (N.B. strictly speaking, we should only
            // ever have a PCT::Complex here.)
            Some(PartialComplexTime::Complex(t)) => match now.mono.checked_duration_since(t.mono) {
                Some(interval) => self.report_metrics(Metrics::UpdateCheckInterval {
                    interval,
                    clock: ClockType::Monotonic,
                    install_source,
                }),
                None => error!("Monotonic time in the past"),
            },

            // No last check time in storage, and no big deal. We'll continue from
            // monotonic time from now on. This is the only place other than loading
            // context from storage where the time can be set, so it's either unset
            // because no storage, or a complex time. No need to match
            // Some(PartialComplexTime::Monotonic)
            _ => {}
        }

        self.context.schedule.last_update_check_time = now.into();
    }

    /// Perform update check and handle the result, including updating the update check context
    /// and cohort.
    /// Returns whether reboot is needed after the update.
    async fn start_update_check(
        &mut self,
        request_params: RequestParams,
        co: &mut async_generator::Yield<StateMachineEvent>,
    ) -> RebootAfterUpdate<IN::InstallResult> {
        let apps = self.app_set.lock().await.get_apps();
        let result = self.perform_update_check(request_params, apps, co).await;

        let (result, reboot_after_update) = match result {
            Ok((result, reboot_after_update)) => {
                info!("Update check result: {:?}", result);
                // Update check succeeded, update |last_update_time|.
                self.context.schedule.last_update_time = Some(self.time_source.now().into());

                // Determine if any app failed to install, or we had a successful update.
                let install_success =
                    result.app_responses.iter().fold(None, |result, app| {
                        match (result, &app.result) {
                            (_, update_check::Action::InstallPlanExecutionError) => Some(false),
                            (None, update_check::Action::Updated) => Some(true),
                            (result, _) => result,
                        }
                    });

                // Update check succeeded, reset |consecutive_failed_update_checks| to 0 and
                // report metrics.
                self.report_attempts_to_successful_check(true).await;

                self.app_set.lock().await.update_from_omaha(&result.app_responses);

                // Only report |attempts_to_successful_install| if we get an error trying to
                // install, or we succeed to install an update without error.
                if let Some(success) = install_success {
                    self.report_attempts_to_successful_install(success).await;
                }

                (Ok(result), reboot_after_update)
                // TODO: update consecutive_proxied_requests
            }
            Err(error) => {
                error!("Update check failed: {:?}", error);

                let failure_reason = match &error {
                    UpdateCheckError::ResponseParser(_) | UpdateCheckError::InstallPlan(_) => {
                        // We talked to Omaha, update |last_update_time|.
                        self.context.schedule.last_update_time =
                            Some(self.time_source.now().into());

                        UpdateCheckFailureReason::Omaha
                    }
                    UpdateCheckError::OmahaRequest(request_error) => match request_error {
                        OmahaRequestError::Json(_)
                        | OmahaRequestError::HttpBuilder(_)
                        | OmahaRequestError::CupDecoration(_)
                        | OmahaRequestError::CupValidation(_) => UpdateCheckFailureReason::Internal,
                        OmahaRequestError::HttpTransport(_) | OmahaRequestError::HttpStatus(_) => {
                            UpdateCheckFailureReason::Network
                        }
                    },
                };
                self.report_metrics(Metrics::UpdateCheckFailureReason(failure_reason));

                self.report_attempts_to_successful_check(false).await;
                (Err(error), RebootAfterUpdate::NotNeeded)
            }
        };

        co.yield_(StateMachineEvent::ScheduleChange(self.context.schedule)).await;
        co.yield_(StateMachineEvent::ProtocolStateChange(self.context.state.clone())).await;
        co.yield_(StateMachineEvent::UpdateCheckResult(result)).await;

        self.persist_data().await;

        reboot_after_update
    }

    // Update self.context.state.consecutive_failed_update_checks and report the metric if
    // `success`. Does not persist the value to storage, but rather relies on the caller.
    async fn report_attempts_to_successful_check(&mut self, success: bool) {
        let attempts = self.context.state.consecutive_failed_update_checks + 1;
        if success {
            self.context.state.consecutive_failed_update_checks = 0;
            self.report_metrics(Metrics::AttemptsToSuccessfulCheck(attempts as u64));
        } else {
            self.context.state.consecutive_failed_update_checks = attempts;
        }
    }

    /// Update `CONSECUTIVE_FAILED_INSTALL_ATTEMPTS` in storage and report the metrics if
    /// `success`. Does not commit the change to storage.
    async fn report_attempts_to_successful_install(&mut self, success: bool) {
        let storage_ref = self.storage_ref.clone();
        let mut storage = storage_ref.lock().await;
        let attempts = storage.get_int(CONSECUTIVE_FAILED_INSTALL_ATTEMPTS).await.unwrap_or(0) + 1;

        self.report_metrics(Metrics::AttemptsToSuccessfulInstall {
            count: attempts as u64,
            successful: success,
        });

        if success {
            storage.remove_or_log(CONSECUTIVE_FAILED_INSTALL_ATTEMPTS).await;
        } else if let Err(e) = storage.set_int(CONSECUTIVE_FAILED_INSTALL_ATTEMPTS, attempts).await
        {
            error!("Unable to persist {}: {}", CONSECUTIVE_FAILED_INSTALL_ATTEMPTS, e);
        }
    }

    /// Persist all necessary data to storage.
    async fn persist_data(&self) {
        let mut storage = self.storage_ref.lock().await;
        self.context.persist(&mut *storage).await;
        self.app_set.lock().await.persist(&mut *storage).await;

        storage.commit_or_log().await;
    }

    /// This function constructs the chain of async futures needed to perform all of the async tasks
    /// that comprise an update check.
    async fn perform_update_check(
        &mut self,
        request_params: RequestParams,
        apps: Vec<App>,
        co: &mut async_generator::Yield<StateMachineEvent>,
    ) -> Result<(update_check::Response, RebootAfterUpdate<IN::InstallResult>), UpdateCheckError>
    {
        Self::yield_state(State::CheckingForUpdates(request_params.source), co).await;

        self.report_check_interval(request_params.source).await;

        // Construct a request for the app(s).
        let config = self.config.clone();
        let mut request_builder = RequestBuilder::new(&config, &request_params);
        for app in &apps {
            request_builder = request_builder.add_update_check(app).add_ping(app);
        }
        let session_id = GUID::new();
        request_builder = request_builder.session_id(session_id.clone());

        let mut omaha_request_attempt = 1;

        // Attempt in an loop of up to MAX_OMAHA_REQUEST_ATTEMPTS to communicate with Omaha.
        // exit the loop early on success or an error that isn't related to a transport issue.
        let loop_result = loop {
            // Mark the start time for the request to omaha.
            let omaha_check_start_time = self.time_source.now_in_monotonic();
            request_builder = request_builder.request_id(GUID::new());
            let result = self.do_omaha_request_and_update_context(&request_builder, co).await;

            // Report the response time of the omaha request.
            {
                // don't use Instant::elapsed(), it doesn't use the right TimeSource, and can panic!
                // as a result
                let now = self.time_source.now_in_monotonic();
                let duration = now.checked_duration_since(omaha_check_start_time);

                if let Some(response_time) = duration {
                    self.report_metrics(Metrics::UpdateCheckResponseTime {
                        response_time,
                        successful: result.is_ok(),
                    });
                } else {
                    // If this happens, it's a bug.
                    error!(
                        "now: {:?}, is before omaha_check_start_time: {:?}",
                        now, omaha_check_start_time
                    );
                }
            }

            match result {
                Ok(res) => {
                    break Ok(res);
                }
                Err(OmahaRequestError::Json(e)) => {
                    error!("Unable to construct request body! {:?}", e);
                    Self::yield_state(State::ErrorCheckingForUpdate, co).await;
                    break Err(UpdateCheckError::OmahaRequest(e.into()));
                }
                Err(OmahaRequestError::HttpBuilder(e)) => {
                    error!("Unable to construct HTTP request! {:?}", e);
                    Self::yield_state(State::ErrorCheckingForUpdate, co).await;
                    break Err(UpdateCheckError::OmahaRequest(e.into()));
                }
                Err(OmahaRequestError::CupDecoration(e)) => {
                    error!("Unable to decorate HTTP request with CUPv2 parameters! {:?}", e);
                    Self::yield_state(State::ErrorCheckingForUpdate, co).await;
                    break Err(UpdateCheckError::OmahaRequest(e.into()));
                }
                Err(OmahaRequestError::CupValidation(e)) => {
                    error!("Unable to validate HTTP response with CUPv2 parameters! {:?}", e);
                    Self::yield_state(State::ErrorCheckingForUpdate, co).await;
                    break Err(UpdateCheckError::OmahaRequest(e.into()));
                }
                Err(OmahaRequestError::HttpTransport(e)) => {
                    warn!("Unable to contact Omaha: {:?}", e);
                    // Don't retry if the error was caused by user code, which means we weren't
                    // using the library correctly.
                    if omaha_request_attempt >= MAX_OMAHA_REQUEST_ATTEMPTS
                        || e.is_user()
                        || self.context.state.server_dictated_poll_interval.is_some()
                    {
                        Self::yield_state(State::ErrorCheckingForUpdate, co).await;
                        break Err(UpdateCheckError::OmahaRequest(e.into()));
                    }
                }
                Err(OmahaRequestError::HttpStatus(e)) => {
                    warn!("Unable to contact Omaha: {:?}", e);
                    if omaha_request_attempt >= MAX_OMAHA_REQUEST_ATTEMPTS
                        || self.context.state.server_dictated_poll_interval.is_some()
                    {
                        Self::yield_state(State::ErrorCheckingForUpdate, co).await;
                        break Err(UpdateCheckError::OmahaRequest(e.into()));
                    }
                }
            }

            // TODO(fxbug.dev/41738): Move this to Policy.
            // Randomized exponential backoff of 1, 2, & 4 seconds, +/- 500ms.
            let backoff_time_secs = 1 << (omaha_request_attempt - 1);
            let backoff_time = randomize(backoff_time_secs * 1000, 1000);
            info!("Waiting {} ms before retrying...", backoff_time);
            self.timer.wait_for(Duration::from_millis(backoff_time)).await;

            omaha_request_attempt += 1;
        };

        self.report_metrics(Metrics::RequestsPerCheck {
            count: omaha_request_attempt,
            successful: loop_result.is_ok(),
        });

        let (_parts, data, request_metadata, signature) = loop_result?;

        let response = match Self::parse_omaha_response(&data) {
            Ok(res) => res,
            Err(err) => {
                warn!("Unable to parse Omaha response: {:?}", err);
                Self::yield_state(State::ErrorCheckingForUpdate, co).await;
                self.report_omaha_event_and_update_context(
                    &request_params,
                    Event::error(EventErrorCode::ParseResponse),
                    &apps,
                    &session_id,
                    &apps.iter().map(|app| (app.id.clone(), None)).collect(),
                    None,
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

        let apps_with_update: Vec<_> = response
            .apps
            .iter()
            .filter(|app| {
                matches!(app.update_check, Some(UpdateCheck { status: OmahaStatus::Ok, .. }))
            })
            .collect();

        if apps_with_update.is_empty() {
            // A successful, no-update, check

            Self::yield_state(State::NoUpdateAvailable, co).await;
            Self::make_not_updated_result(response, update_check::Action::NoUpdate)
        } else {
            info!(
                "At least one app has an update, proceeding to build and process an Install Plan"
            );
            // A map from app id to the new version of the app, if an app has no update, then it
            // won't appear in this map, if an app has update but there's no version in the omaha
            // response, then its entry will be None.
            let next_versions: HashMap<String, Option<String>> = apps_with_update
                .iter()
                .map(|app| (app.id.clone(), app.get_manifest_version()))
                .collect();
            let install_plan = match self
                .installer
                .try_create_install_plan(
                    &request_params,
                    request_metadata.as_ref(),
                    &response,
                    data,
                    signature.map(|s| s.as_bytes().to_vec()),
                )
                .await
            {
                Ok(plan) => plan,
                Err(e) => {
                    error!("Unable to construct install plan! {}", e);
                    Self::yield_state(State::InstallingUpdate, co).await;
                    Self::yield_state(State::InstallationError, co).await;
                    self.report_omaha_event_and_update_context(
                        &request_params,
                        Event::error(EventErrorCode::ConstructInstallPlan),
                        &apps,
                        &session_id,
                        &next_versions,
                        None,
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
                        &next_versions,
                        None,
                        co,
                    )
                    .await;

                    Self::yield_state(State::InstallationDeferredByPolicy, co).await;

                    return Self::make_not_updated_result(
                        response,
                        update_check::Action::DeferredByPolicy,
                    );
                }
                UpdateDecision::DeniedByPolicy => {
                    warn!("Install plan was denied by Policy, see Policy logs for reasoning");
                    self.report_omaha_event_and_update_context(
                        &request_params,
                        Event::error(EventErrorCode::DeniedByPolicy),
                        &apps,
                        &session_id,
                        &next_versions,
                        None,
                        co,
                    )
                    .await;

                    return Self::make_not_updated_result(
                        response,
                        update_check::Action::DeniedByPolicy,
                    );
                }
            }

            Self::yield_state(State::InstallingUpdate, co).await;
            self.report_omaha_event_and_update_context(
                &request_params,
                Event::success(EventType::UpdateDownloadStarted),
                &apps,
                &session_id,
                &next_versions,
                None,
                co,
            )
            .await;

            let install_plan_id = install_plan.id();
            let update_start_time = self.time_source.now_in_walltime();
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

            let ((install_result, mut app_install_results), ()) =
                future::join(perform_install, yield_progress).await;
            let no_apps_failed = app_install_results.iter().all(|result| {
                matches!(result, AppInstallResult::Installed | AppInstallResult::Deferred)
            });
            let update_finish_time = self.time_source.now_in_walltime();
            let install_duration = match update_finish_time.duration_since(update_start_time) {
                Ok(duration) => {
                    let metrics = if no_apps_failed {
                        Metrics::SuccessfulUpdateDuration(duration)
                    } else {
                        Metrics::FailedUpdateDuration(duration)
                    };
                    self.report_metrics(metrics);
                    Some(duration)
                }
                Err(e) => {
                    warn!("Update start time is in the future: {}", e);
                    None
                }
            };

            let config = self.config.clone();
            let mut request_builder = RequestBuilder::new(&config, &request_params);
            let mut events = vec![];
            let mut installed_apps = vec![];
            for (response_app, app_install_result) in
                apps_with_update.iter().zip(&app_install_results)
            {
                match apps.iter().find(|app| app.id == response_app.id) {
                    Some(app) => {
                        let event = match app_install_result {
                            AppInstallResult::Installed => {
                                installed_apps.push(app);
                                Event::success(EventType::UpdateDownloadFinished)
                            }
                            AppInstallResult::Deferred => Event {
                                event_type: EventType::UpdateComplete,
                                event_result: EventResult::UpdateDeferred,
                                ..Event::default()
                            },
                            AppInstallResult::Failed(_) => {
                                Event::error(EventErrorCode::Installation)
                            }
                        };
                        let event = Event {
                            previous_version: Some(app.version.to_string()),
                            next_version: response_app.get_manifest_version(),
                            download_time_ms: install_duration
                                .and_then(|d| d.as_millis().try_into().ok()),
                            ..event
                        };
                        request_builder = request_builder.add_event(app, event.clone());
                        events.push(event);
                    }
                    None => {
                        error!("unknown app id in omaha response: {:?}", response_app.id);
                    }
                }
            }
            request_builder =
                request_builder.session_id(session_id.clone()).request_id(GUID::new());
            if let Err(e) = self.do_omaha_request_and_update_context(&request_builder, co).await {
                for event in events {
                    self.report_metrics(Metrics::OmahaEventLost(event));
                }
                warn!("Unable to report event to Omaha: {:?}", e);
            }

            // TODO: Verify downloaded update if needed.

            // For apps that successfully installed, we need to report an extra `UpdateComplete` event.
            if !installed_apps.is_empty() {
                self.report_omaha_event_and_update_context(
                    &request_params,
                    Event::success(EventType::UpdateComplete),
                    installed_apps,
                    &session_id,
                    &next_versions,
                    install_duration,
                    co,
                )
                .await;
            }

            let mut errors = vec![];
            let daystart = response.daystart;
            let app_responses = response
                .apps
                .into_iter()
                .map(|app| update_check::AppResponse {
                    app_id: app.id,
                    cohort: app.cohort,
                    user_counting: daystart.clone().into(),
                    result: match app.update_check {
                        Some(UpdateCheck { status: OmahaStatus::Ok, .. }) => {
                            match app_install_results.remove(0) {
                                AppInstallResult::Installed => update_check::Action::Updated,
                                AppInstallResult::Deferred => {
                                    update_check::Action::DeferredByPolicy
                                }
                                AppInstallResult::Failed(e) => {
                                    errors.push(e);
                                    update_check::Action::InstallPlanExecutionError
                                }
                            }
                        }
                        _ => update_check::Action::NoUpdate,
                    },
                })
                .collect();

            if !errors.is_empty() {
                for e in errors {
                    co.yield_(StateMachineEvent::InstallerError(Some(Box::new(e)))).await;
                }
                Self::yield_state(State::InstallationError, co).await;

                return Ok((
                    update_check::Response { app_responses },
                    RebootAfterUpdate::NotNeeded,
                ));
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
                let app_set = self.app_set.lock().await;
                let system_app_id = app_set.get_system_app_id();
                // If not found then this is not a system update, so no need to write target version.
                if let Some(next_version) = next_versions.get(system_app_id) {
                    let target_version = next_version.as_deref().unwrap_or_else(|| {
                        error!("Target version string not found in Omaha response.");
                        "UNKNOWN"
                    });
                    if let Err(e) = storage.set_string(TARGET_VERSION, target_version).await {
                        error!("Unable to persist {}: {}", TARGET_VERSION, e);
                    }
                }
                storage.commit_or_log().await;
            }

            let reboot_after_update = if self.policy_engine.reboot_needed(&install_plan).await {
                RebootAfterUpdate::Needed(install_result)
            } else {
                RebootAfterUpdate::NotNeeded
            };

            Ok((update_check::Response { app_responses }, reboot_after_update))
        }
    }

    /// Report the given |event| to Omaha, errors occurred during reporting are logged but not
    /// acted on.
    #[allow(clippy::too_many_arguments)]
    async fn report_omaha_event_and_update_context<'a>(
        &'a mut self,
        request_params: &'a RequestParams,
        event: Event,
        apps: impl IntoIterator<Item = &App>,
        session_id: &GUID,
        next_versions: &HashMap<String, Option<String>>,
        install_duration: Option<Duration>,
        co: &mut async_generator::Yield<StateMachineEvent>,
    ) {
        let config = self.config.clone();
        let mut request_builder = RequestBuilder::new(&config, request_params);
        for app in apps {
            // Skip apps with no update.
            if let Some(next_version) = next_versions.get(&app.id) {
                let event = Event {
                    previous_version: Some(app.version.to_string()),
                    next_version: next_version.clone(),
                    download_time_ms: install_duration.and_then(|d| d.as_millis().try_into().ok()),
                    ..event.clone()
                };
                request_builder = request_builder.add_event(app, event);
            }
        }
        request_builder = request_builder.session_id(session_id.clone()).request_id(GUID::new());
        if let Err(e) = self.do_omaha_request_and_update_context(&request_builder, co).await {
            self.report_metrics(Metrics::OmahaEventLost(event));
            warn!("Unable to report event to Omaha: {:?}", e);
        }
    }

    /// Sends a ping to Omaha and updates context and app_set.
    async fn ping_omaha(&mut self, co: &mut async_generator::Yield<StateMachineEvent>) {
        let apps = self.app_set.lock().await.get_apps();
        let request_params = RequestParams {
            source: InstallSource::ScheduledTask,
            use_configured_proxies: true,
            disable_updates: false,
            offer_update_if_same_version: false,
        };
        let config = self.config.clone();
        let mut request_builder = RequestBuilder::new(&config, &request_params);
        for app in &apps {
            request_builder = request_builder.add_ping(app);
        }
        request_builder = request_builder.session_id(GUID::new()).request_id(GUID::new());

        let (_parts, data, _request_metadata, _signature) =
            match self.do_omaha_request_and_update_context(&request_builder, co).await {
                Ok(res) => res,
                Err(e) => {
                    error!("Ping Omaha failed: {:#}", anyhow!(e));
                    self.context.state.consecutive_failed_update_checks += 1;
                    self.persist_data().await;
                    return;
                }
            };

        let response = match Self::parse_omaha_response(&data) {
            Ok(res) => res,
            Err(e) => {
                error!("Unable to parse Omaha response: {:#}", anyhow!(e));
                self.context.state.consecutive_failed_update_checks += 1;
                self.persist_data().await;
                return;
            }
        };

        self.context.state.consecutive_failed_update_checks = 0;

        // Even though this is a ping, we should still update the last_update_time for
        // policy to compute the next ping time.
        self.context.schedule.last_update_time = Some(self.time_source.now().into());
        co.yield_(StateMachineEvent::ScheduleChange(self.context.schedule)).await;

        let app_responses = Self::make_app_responses(response, update_check::Action::NoUpdate);
        self.app_set.lock().await.update_from_omaha(&app_responses);

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
    ) -> Result<(Parts, Vec<u8>, Option<RequestMetadata>, Option<DerSignature>), OmahaRequestError>
    {
        let (request, request_metadata) = builder.build(self.cup_handler.as_ref())?;
        let response = Self::make_request(&mut self.http, request).await?;

        let signature: Option<DerSignature> = if let (Some(handler), Some(metadata)) =
            (self.cup_handler.as_ref(), &request_metadata)
        {
            let signature = handler
                .verify_response(metadata, &response, metadata.public_key_id)
                .map_err(|e| {
                    error!("Could not verify response: {:?}", e);
                    e
                })?;
            Some(signature)
        } else {
            None
        };

        let (parts, body) = response.into_parts();

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
            Ok((parts, body, request_metadata, signature))
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
    ) -> Result<HttpResponse<Vec<u8>>, http_request::Error> {
        info!("Making http request to: {}", request.uri());
        http_client.request(request).await.map_err(|err| {
            warn!("Unable to perform request: {}", err);
            err
        })
    }

    /// This method takes the response bytes from Omaha, and converts them into a protocol::Response
    /// struct, returning all of the various errors that can occur in that process as a consolidated
    /// error enum.
    fn parse_omaha_response(data: &[u8]) -> Result<Response, ResponseParseError> {
        parse_json_response(data).map_err(ResponseParseError::Json)
    }

    /// Utility to extract pairs of app id => omaha status response, to make it easier to ask
    /// questions about the response.
    fn get_app_update_statuses(response: &Response) -> Vec<(&str, &OmahaStatus)> {
        response
            .apps
            .iter()
            .filter_map(|app| app.update_check.as_ref().map(|u| (app.id.as_str(), &u.status)))
            .collect()
    }

    /// Utility to take a set of protocol::response::Apps and then construct a set of AppResponse
    /// from the update check based on those app IDs.
    ///
    /// TODO(fxbug.dev/88997): Change the Policy and Installer to return a set of results, one for
    ///                        each app ID, then make this match that.
    fn make_app_responses(
        response: protocol::response::Response,
        action: update_check::Action,
    ) -> Vec<update_check::AppResponse> {
        let daystart = response.daystart;
        response
            .apps
            .into_iter()
            .map(|app| update_check::AppResponse {
                app_id: app.id,
                cohort: app.cohort,
                user_counting: daystart.clone().into(),
                result: action.clone(),
            })
            .collect()
    }

    /// Make an Ok result for `perform_update_check()` when update wasn't installed/failed.
    fn make_not_updated_result(
        response: protocol::response::Response,
        action: update_check::Action,
    ) -> Result<(update_check::Response, RebootAfterUpdate<IN::InstallResult>), UpdateCheckError>
    {
        Ok((
            update_check::Response { app_responses: Self::make_app_responses(response, action) },
            RebootAfterUpdate::NotNeeded,
        ))
    }

    /// Send the state to the observer.
    async fn yield_state(state: State, co: &mut async_generator::Yield<StateMachineEvent>) {
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
impl<PE, HR, IN, TM, MR, ST, AS, IR, PL, CH> StateMachine<PE, HR, IN, TM, MR, ST, AS, CH>
where
    PE: PolicyEngine<InstallResult = IR, InstallPlan = PL>,
    HR: HttpRequest,
    IN: Installer<InstallResult = IR, InstallPlan = PL>,
    TM: Timer,
    MR: MetricsReporter,
    ST: Storage,
    AS: AppSet,
    CH: Cupv2Handler,
    IR: 'static + Send,
    PL: Plan,
{
    /// Run perform_update_check once, returning the update check result.
    async fn oneshot(
        &mut self,
        request_params: RequestParams,
    ) -> Result<(update_check::Response, RebootAfterUpdate<IN::InstallResult>), UpdateCheckError>
    {
        let apps = self.app_set.lock().await.get_apps();

        async_generator::generate(move |mut co| async move {
            self.perform_update_check(request_params, apps, &mut co).await
        })
        .into_complete()
        .await
    }

    /// Run start_upate_check once, discarding its states.
    async fn run_once(&mut self) {
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
    use super::update_check::{
        Action, CONSECUTIVE_FAILED_UPDATE_CHECKS, LAST_UPDATE_TIME, SERVER_DICTATED_POLL_INTERVAL,
    };
    use super::*;
    use crate::{
        app_set::VecAppSet,
        common::{
            App, CheckOptions, PersistedApp, ProtocolState, UpdateCheckSchedule, UserCounting,
        },
        configuration::Updater,
        cup_ecdsa::test_support::{make_cup_handler_for_test, MockCupv2Handler},
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
    use assert_matches::assert_matches;
    use futures::executor::{block_on, LocalPool};
    use futures::future::LocalBoxFuture;
    use futures::task::LocalSpawnExt;
    use pretty_assertions::assert_eq;
    use serde_json::json;
    use std::cell::RefCell;
    use std::time::Duration;
    use tracing::info;
    use version::Version;

    fn make_test_app_set() -> Rc<Mutex<VecAppSet>> {
        Rc::new(Mutex::new(VecAppSet::new(vec![App::builder()
            .id("{00000000-0000-0000-0000-000000000001}")
            .version([1, 2, 3, 4])
            .cohort(Cohort::new("stable-channel"))
            .build()])))
    }

    fn make_update_available_response() -> HttpResponse<Vec<u8>> {
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
        HttpResponse::new(serde_json::to_vec(&response).unwrap())
    }

    fn make_noupdate_httpresponse() -> Vec<u8> {
        serde_json::to_vec(
            &(json!({"response":{
              "server": "prod",
              "protocol": "3.0",
              "app": [{
                "appid": "{00000000-0000-0000-0000-000000000001}",
                "status": "ok",
                "updatecheck": {
                  "status": "noupdate"
                }
              }]
            }})),
        )
        .unwrap()
    }

    // Assert that the last request made to |http| is equal to the request built by
    // |request_builder|.
    async fn assert_request<'a>(http: &MockHttpRequest, request_builder: RequestBuilder<'a>) {
        let cup_handler = make_cup_handler_for_test();
        let (request, _request_metadata) = request_builder.build(Some(&cup_handler)).unwrap();
        let body = hyper::body::to_bytes(request).await.unwrap();
        // Compare string instead of Vec<u8> for easier debugging.
        let body_str = String::from_utf8_lossy(&body);
        http.assert_body_str(&body_str).await;
    }

    #[test]
    fn run_simple_check_with_noupdate_result() {
        block_on(async {
            let http = MockHttpRequest::new(HttpResponse::new(make_noupdate_httpresponse()));

            StateMachineBuilder::new_stub()
                .http(http)
                .oneshot(RequestParams::default())
                .await
                .unwrap();

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
            let http = MockHttpRequest::new(HttpResponse::new(response));

            let (response, reboot_after_update) = StateMachineBuilder::new_stub()
                .http(http)
                .oneshot(RequestParams::default())
                .await
                .unwrap();
            assert_eq!("{00000000-0000-0000-0000-000000000001}", response.app_responses[0].app_id);
            assert_eq!(Some("1".into()), response.app_responses[0].cohort.id);
            assert_eq!(Some("stable-channel".into()), response.app_responses[0].cohort.name);
            assert_eq!(None, response.app_responses[0].cohort.hint);

            assert_matches!(reboot_after_update, RebootAfterUpdate::NotNeeded);
        });
    }

    #[test]
    fn test_cohort_returned_with_update_result() {
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
                  "status": "ok"
                }
              }]
            }});
            let response = serde_json::to_vec(&response).unwrap();
            let http = MockHttpRequest::new(HttpResponse::new(response));

            let (response, reboot_after_update) = StateMachineBuilder::new_stub()
                .http(http)
                .oneshot(RequestParams::default())
                .await
                .unwrap();
            assert_eq!("{00000000-0000-0000-0000-000000000001}", response.app_responses[0].app_id);
            assert_eq!(Some("1".into()), response.app_responses[0].cohort.id);
            assert_eq!(Some("stable-channel".into()), response.app_responses[0].cohort.name);
            assert_eq!(None, response.app_responses[0].cohort.hint);

            assert_matches!(reboot_after_update, RebootAfterUpdate::Needed(()));
        });
    }

    #[test]
    fn test_report_parse_response_error() {
        block_on(async {
            let http = MockHttpRequest::new(HttpResponse::new("invalid response".into()));

            let mut state_machine = StateMachineBuilder::new_stub().http(http).build().await;

            let response = state_machine.oneshot(RequestParams::default()).await;
            assert_matches!(response, Err(UpdateCheckError::ResponseParser(_)));

            let request_params = RequestParams::default();
            let mut request_builder = RequestBuilder::new(&state_machine.config, &request_params);
            let event = Event {
                previous_version: Some("1.2.3.4".to_string()),
                ..Event::error(EventErrorCode::ParseResponse)
            };
            let apps = state_machine.app_set.lock().await.get_apps();
            request_builder = request_builder
                .add_event(&apps[0], event)
                .session_id(GUID::from_u128(0))
                .request_id(GUID::from_u128(2));
            assert_request(&state_machine.http, request_builder).await;
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
            let http = MockHttpRequest::new(HttpResponse::new(response));

            let mut state_machine = StateMachineBuilder::new_stub().http(http).build().await;

            let response = state_machine.oneshot(RequestParams::default()).await;
            assert_matches!(response, Err(UpdateCheckError::InstallPlan(_)));

            let request_params = RequestParams::default();
            let mut request_builder = RequestBuilder::new(&state_machine.config, &request_params);
            let event = Event {
                previous_version: Some("1.2.3.4".to_string()),
                ..Event::error(EventErrorCode::ConstructInstallPlan)
            };
            let apps = state_machine.app_set.lock().await.get_apps();
            request_builder = request_builder
                .add_event(&apps[0], event)
                .session_id(GUID::from_u128(0))
                .request_id(GUID::from_u128(2));
            assert_request(&state_machine.http, request_builder).await;
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
                  "status": "ok",
                  "manifest": {
                      "version": "5.6.7.8",
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
            let http = MockHttpRequest::new(HttpResponse::new(response));

            let mut state_machine = StateMachineBuilder::new_stub()
                .http(http)
                .installer(StubInstaller { should_fail: true })
                .build()
                .await;

            let (response, reboot_after_update) =
                state_machine.oneshot(RequestParams::default()).await.unwrap();
            assert_eq!(Action::InstallPlanExecutionError, response.app_responses[0].result);
            assert_matches!(reboot_after_update, RebootAfterUpdate::NotNeeded);

            let request_params = RequestParams::default();
            let mut request_builder = RequestBuilder::new(&state_machine.config, &request_params);
            let event = Event {
                previous_version: Some("1.2.3.4".to_string()),
                next_version: Some("5.6.7.8".to_string()),
                download_time_ms: Some(0),
                ..Event::error(EventErrorCode::Installation)
            };
            let apps = state_machine.app_set.lock().await.get_apps();
            request_builder = request_builder
                .add_event(&apps[0], event)
                .session_id(GUID::from_u128(0))
                .request_id(GUID::from_u128(3));
            assert_request(&state_machine.http, request_builder).await;
        });
    }

    #[test]
    fn test_report_installation_error_multi_app() {
        block_on(async {
            // Intentionally made the app order in response and app_set different.
            let response = json!({"response":{
              "server": "prod",
              "protocol": "3.0",
              "app": [{
                "appid": "appid_3",
                "status": "ok",
                "updatecheck": {
                  "status": "ok",
                  "manifest": {
                      "version": "5.6.7.8",
                      "actions": {
                          "action": [],
                      },
                      "packages": {
                          "package": [],
                      },
                  }
                }
              },{
                "appid": "appid_1",
                "status": "ok",
                "updatecheck": {
                  "status": "ok",
                  "manifest": {
                      "version": "1.2.3.4",
                      "actions": {
                          "action": [],
                      },
                      "packages": {
                          "package": [],
                      },
                  }
                }
              },{
                "appid": "appid_2",
                "status": "ok",
                "updatecheck": {
                  "status": "noupdate",
                }
              }],
            }});
            let response = serde_json::to_vec(&response).unwrap();
            let mut http = MockHttpRequest::new(HttpResponse::new(response));
            http.add_response(HttpResponse::new(vec![]));
            let app_set = VecAppSet::new(vec![
                App::builder().id("appid_1").version([1, 2, 3, 3]).build(),
                App::builder().id("appid_2").version([9, 9, 9, 9]).build(),
                App::builder().id("appid_3").version([5, 6, 7, 7]).build(),
            ]);
            let app_set = Rc::new(Mutex::new(app_set));
            let (send_install, mut recv_install) = mpsc::channel(0);

            let mut state_machine = StateMachineBuilder::new_stub()
                .app_set(Rc::clone(&app_set))
                .http(http)
                .installer(BlockingInstaller { on_install: send_install, on_reboot: None })
                .build()
                .await;

            let recv_install_fut = async move {
                let unblock_install = recv_install.next().await.unwrap();
                unblock_install
                    .send(vec![AppInstallResult::Deferred, AppInstallResult::Installed])
                    .unwrap();
            };

            let (oneshot_result, ()) =
                future::join(state_machine.oneshot(RequestParams::default()), recv_install_fut)
                    .await;
            let (response, reboot_after_update) = oneshot_result.unwrap();

            assert_eq!("appid_3", response.app_responses[0].app_id);
            assert_eq!(Action::DeferredByPolicy, response.app_responses[0].result);
            assert_eq!("appid_1", response.app_responses[1].app_id);
            assert_eq!(Action::Updated, response.app_responses[1].result);
            assert_eq!("appid_2", response.app_responses[2].app_id);
            assert_eq!(Action::NoUpdate, response.app_responses[2].result);
            assert_matches!(reboot_after_update, RebootAfterUpdate::Needed(()));

            let request_params = RequestParams::default();
            let apps = app_set.lock().await.get_apps();

            let mut request_builder = RequestBuilder::new(&state_machine.config, &request_params);
            let event = Event {
                previous_version: Some("1.2.3.3".to_string()),
                next_version: Some("1.2.3.4".to_string()),
                download_time_ms: Some(0),
                ..Event::success(EventType::UpdateComplete)
            };
            request_builder = request_builder
                .add_event(&apps[0], event)
                .session_id(GUID::from_u128(0))
                .request_id(GUID::from_u128(4));
            assert_request(&state_machine.http, request_builder).await;

            let mut request_builder = RequestBuilder::new(&state_machine.config, &request_params);
            let event1 = Event {
                previous_version: Some("1.2.3.3".to_string()),
                next_version: Some("1.2.3.4".to_string()),
                download_time_ms: Some(0),
                ..Event::success(EventType::UpdateDownloadFinished)
            };
            let event2 = Event {
                previous_version: Some("5.6.7.7".to_string()),
                next_version: Some("5.6.7.8".to_string()),
                download_time_ms: Some(0),
                event_type: EventType::UpdateComplete,
                event_result: EventResult::UpdateDeferred,
                ..Event::default()
            };
            request_builder = request_builder
                .add_event(&apps[2], event2)
                .add_event(&apps[0], event1)
                .session_id(GUID::from_u128(0))
                .request_id(GUID::from_u128(3));
            assert_request(&state_machine.http, request_builder).await;
        });
    }

    // Test that our observer can see when there's an installation error, and that it gets
    // the right error type.
    #[test]
    fn test_observe_installation_error() {
        block_on(async {
            let http = MockHttpRequest::new(make_update_available_response());

            let actual_errors = StateMachineBuilder::new_stub()
                .http(http)
                .installer(StubInstaller { should_fail: true })
                .oneshot_check()
                .await
                .filter_map(|event| {
                    future::ready(match event {
                        StateMachineEvent::InstallerError(Some(e)) => {
                            Some(*e.downcast::<StubInstallErrors>().unwrap())
                        }
                        _ => None,
                    })
                })
                .collect::<Vec<StubInstallErrors>>()
                .await;

            let expected_errors = vec![StubInstallErrors::Failed];
            assert_eq!(actual_errors, expected_errors);
        });
    }

    #[test]
    fn test_report_deferred_by_policy() {
        block_on(async {
            let http = MockHttpRequest::new(make_update_available_response());

            let policy_engine = MockPolicyEngine {
                update_decision: UpdateDecision::DeferredByPolicy,
                ..MockPolicyEngine::default()
            };
            let mut state_machine = StateMachineBuilder::new_stub()
                .policy_engine(policy_engine)
                .http(http)
                .build()
                .await;

            let (response, reboot_after_update) =
                state_machine.oneshot(RequestParams::default()).await.unwrap();
            assert_eq!(Action::DeferredByPolicy, response.app_responses[0].result);
            assert_matches!(reboot_after_update, RebootAfterUpdate::NotNeeded);

            let request_params = RequestParams::default();
            let mut request_builder = RequestBuilder::new(&state_machine.config, &request_params);
            let event = Event {
                event_type: EventType::UpdateComplete,
                event_result: EventResult::UpdateDeferred,
                previous_version: Some("1.2.3.4".to_string()),
                ..Event::default()
            };
            let apps = state_machine.app_set.lock().await.get_apps();
            request_builder = request_builder
                .add_event(&apps[0], event)
                .session_id(GUID::from_u128(0))
                .request_id(GUID::from_u128(2));
            assert_request(&state_machine.http, request_builder).await;
        });
    }

    #[test]
    fn test_report_denied_by_policy() {
        block_on(async {
            let response = make_update_available_response();
            let http = MockHttpRequest::new(response);
            let policy_engine = MockPolicyEngine {
                update_decision: UpdateDecision::DeniedByPolicy,
                ..MockPolicyEngine::default()
            };

            let mut state_machine = StateMachineBuilder::new_stub()
                .policy_engine(policy_engine)
                .http(http)
                .build()
                .await;

            let (response, reboot_after_update) =
                state_machine.oneshot(RequestParams::default()).await.unwrap();
            assert_eq!(Action::DeniedByPolicy, response.app_responses[0].result);
            assert_matches!(reboot_after_update, RebootAfterUpdate::NotNeeded);

            let request_params = RequestParams::default();
            let mut request_builder = RequestBuilder::new(&state_machine.config, &request_params);
            let event = Event {
                previous_version: Some("1.2.3.4".to_string()),
                ..Event::error(EventErrorCode::DeniedByPolicy)
            };
            let apps = state_machine.app_set.lock().await.get_apps();
            request_builder = request_builder
                .add_event(&apps[0], event)
                .session_id(GUID::from_u128(0))
                .request_id(GUID::from_u128(2));
            assert_request(&state_machine.http, request_builder).await;
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
            let mut http = MockHttpRequest::new(HttpResponse::new(response.clone()));
            http.add_response(HttpResponse::new(response));
            let apps = make_test_app_set();

            let mut state_machine =
                StateMachineBuilder::new_stub().http(http).app_set(apps.clone()).build().await;

            // Run it the first time.
            state_machine.run_once().await;

            let apps = apps.lock().await.get_apps();
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
            assert_request(&state_machine.http, expected_request_builder).await;
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
            let http = MockHttpRequest::new(HttpResponse::new(response));

            let (response, reboot_after_update) = StateMachineBuilder::new_stub()
                .http(http)
                .oneshot(RequestParams::default())
                .await
                .unwrap();

            assert_eq!(
                UserCounting::ClientRegulatedByDate(Some(1234567)),
                response.app_responses[0].user_counting
            );
            assert_matches!(reboot_after_update, RebootAfterUpdate::NotNeeded);
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

            let expected_states = vec![
                State::CheckingForUpdates(InstallSource::ScheduledTask),
                State::ErrorCheckingForUpdate,
            ];
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
            let expected_schedule = UpdateCheckSchedule::builder()
                .last_update_time(mock_time.now())
                .last_update_check_time(mock_time.now())
                .build();

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
            let http = MockHttpRequest::new(HttpResponse::new(response));

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
    fn test_metrics_report_omaha_event_lost() {
        block_on(async {
            // This is sufficient to trigger a lost Omaha event as oneshot triggers an
            // update check, which gets the invalid response (but hasn't checked the
            // validity yet). This invalid response still contains an OK status, resulting
            // in the UpdateCheckResponseTime and RequestsPerCheck events being generated
            // reporting success.
            //
            // The response is then parsed and found to be incorrect; this parse error is
            // attempted to be sent back to Omaha as an event with the ParseResponse error
            // associated. However, the MockHttpRequest has already consumed the one
            // response it knew how to give; this event is reported via HTTP, but is "lost"
            // because the mock responds with a 500 error when it has no responses left to
            // return.
            //
            // That finally results in the OmahaEventLost.
            let http = MockHttpRequest::new(HttpResponse::new("invalid response".into()));
            let mut metrics_reporter = MockMetricsReporter::new();
            let _response = StateMachineBuilder::new_stub()
                .http(http)
                .metrics_reporter(&mut metrics_reporter)
                .oneshot(RequestParams::default())
                .await;

            // FIXME(https://github.com/rust-lang/rustfmt/issues/4530) rustfmt doesn't wrap slice
            // patterns yet.
            #[rustfmt::skip]
            assert_matches!(
                metrics_reporter.metrics.as_slice(),
                [
                    Metrics::UpdateCheckResponseTime { response_time: _, successful: true },
                    Metrics::RequestsPerCheck { count: 1, successful: true },
                    Metrics::OmahaEventLost(Event {
                        event_type: EventType::UpdateComplete,
                        event_result: EventResult::Error,
                        errorcode: Some(EventErrorCode::ParseResponse),
                        previous_version: None,
                        next_version: None,
                        download_time_ms: None,
                    })
                ]
            );
        });
    }

    #[test]
    fn test_metrics_report_update_check_response_time() {
        block_on(async {
            let mut metrics_reporter = MockMetricsReporter::new();
            let _response = StateMachineBuilder::new_stub()
                .metrics_reporter(&mut metrics_reporter)
                .oneshot(RequestParams::default())
                .await;

            // FIXME(https://github.com/rust-lang/rustfmt/issues/4530) rustfmt doesn't wrap slice
            // patterns yet.
            #[rustfmt::skip]
            assert_matches!(
                metrics_reporter.metrics.as_slice(),
                [
                    Metrics::UpdateCheckResponseTime { response_time: _, successful: true },
                    Metrics::RequestsPerCheck { count: 1, successful: true },
                ]
            );
        });
    }

    #[test]
    fn test_metrics_report_update_check_response_time_on_failure() {
        block_on(async {
            let mut metrics_reporter = MockMetricsReporter::new();
            let mut http = MockHttpRequest::default();

            for _ in 0..MAX_OMAHA_REQUEST_ATTEMPTS {
                http.add_error(http_request::mock_errors::make_transport_error());
            }

            // Note: we exit the update loop before we fetch the successful result, so we never see
            // this result.
            http.add_response(hyper::Response::default());

            let _response = StateMachineBuilder::new_stub()
                .http(http)
                .metrics_reporter(&mut metrics_reporter)
                .oneshot(RequestParams::default())
                .await;

            // FIXME(https://github.com/rust-lang/rustfmt/issues/4530) rustfmt doesn't wrap slice
            // patterns yet.
            #[rustfmt::skip]
            assert_matches!(
                metrics_reporter.metrics.as_slice(),
                [
                    Metrics::UpdateCheckResponseTime { response_time: _, successful: false },
                    Metrics::UpdateCheckResponseTime { response_time: _, successful: false },
                    Metrics::UpdateCheckResponseTime { response_time: _, successful: false },
                    Metrics::RequestsPerCheck { count: 3, successful: false },
                ]
            );
        });
    }

    #[test]
    fn test_metrics_report_update_check_response_time_on_failure_followed_by_success() {
        block_on(async {
            let mut metrics_reporter = MockMetricsReporter::new();
            let mut http = MockHttpRequest::default();

            for _ in 0..MAX_OMAHA_REQUEST_ATTEMPTS - 1 {
                http.add_error(http_request::mock_errors::make_transport_error());
            }
            http.add_response(hyper::Response::default());

            let _response = StateMachineBuilder::new_stub()
                .http(http)
                .metrics_reporter(&mut metrics_reporter)
                .oneshot(RequestParams::default())
                .await;

            // FIXME(https://github.com/rust-lang/rustfmt/issues/4530) rustfmt doesn't wrap slice
            // patterns yet.
            #[rustfmt::skip]
            assert_matches!(
                metrics_reporter.metrics.as_slice(),
                [
                    Metrics::UpdateCheckResponseTime { response_time: _, successful: false },
                    Metrics::UpdateCheckResponseTime { response_time: _, successful: false },
                    Metrics::UpdateCheckResponseTime { response_time: _, successful: true },
                    Metrics::RequestsPerCheck { count: 3, successful: true },
                    Metrics::OmahaEventLost(Event {
                        event_type: EventType::UpdateComplete,
                        event_result: EventResult::Error,
                        errorcode: Some(EventErrorCode::ParseResponse),
                        previous_version: None,
                        next_version: None,
                        download_time_ms: None
                    }),
                ]
            );
        });
    }

    #[test]
    fn test_metrics_report_requests_per_check() {
        block_on(async {
            let mut metrics_reporter = MockMetricsReporter::new();
            let _response = StateMachineBuilder::new_stub()
                .metrics_reporter(&mut metrics_reporter)
                .oneshot(RequestParams::default())
                .await;

            assert!(metrics_reporter
                .metrics
                .contains(&Metrics::RequestsPerCheck { count: 1, successful: true }));
        });
    }

    #[test]
    fn test_metrics_report_requests_per_check_on_failure_followed_by_success() {
        block_on(async {
            let mut metrics_reporter = MockMetricsReporter::new();
            let mut http = MockHttpRequest::default();

            for _ in 0..MAX_OMAHA_REQUEST_ATTEMPTS - 1 {
                http.add_error(http_request::mock_errors::make_transport_error());
            }

            http.add_response(hyper::Response::default());

            let _response = StateMachineBuilder::new_stub()
                .http(http)
                .metrics_reporter(&mut metrics_reporter)
                .oneshot(RequestParams::default())
                .await;

            assert!(!metrics_reporter.metrics.is_empty());
            assert!(metrics_reporter.metrics.contains(&Metrics::RequestsPerCheck {
                count: MAX_OMAHA_REQUEST_ATTEMPTS,
                successful: true
            }));
        });
    }

    #[test]
    fn test_metrics_report_requests_per_check_on_failure() {
        block_on(async {
            let mut metrics_reporter = MockMetricsReporter::new();
            let mut http = MockHttpRequest::default();

            for _ in 0..MAX_OMAHA_REQUEST_ATTEMPTS {
                http.add_error(http_request::mock_errors::make_transport_error());
            }

            // Note we will give up before we get this successful request.
            http.add_response(hyper::Response::default());

            let _response = StateMachineBuilder::new_stub()
                .http(http)
                .metrics_reporter(&mut metrics_reporter)
                .oneshot(RequestParams::default())
                .await;

            assert!(!metrics_reporter.metrics.is_empty());
            assert!(metrics_reporter.metrics.contains(&Metrics::RequestsPerCheck {
                count: MAX_OMAHA_REQUEST_ATTEMPTS,
                successful: false
            }));
        });
    }

    #[test]
    fn test_requests_per_check_backoff_with_mock_timer() {
        block_on(async {
            let mut timer = MockTimer::new();
            timer.expect_for_range(Duration::from_millis(500), Duration::from_millis(1500));
            timer.expect_for_range(Duration::from_millis(1500), Duration::from_millis(2500));
            let requested_waits = timer.get_requested_waits_view();
            let response = StateMachineBuilder::new_stub()
                .http(MockHttpRequest::empty())
                .timer(timer)
                .oneshot(RequestParams::default())
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
            assert!(storage.committed());
        });
    }

    #[test]
    fn test_persist_server_dictated_poll_interval() {
        block_on(async {
            let response = HttpResponse::builder()
                .header(X_RETRY_AFTER, 1234)
                .body(make_noupdate_httpresponse())
                .unwrap();
            let http = MockHttpRequest::new(response);
            let storage = Rc::new(Mutex::new(MemStorage::new()));

            let mut state_machine = StateMachineBuilder::new_stub()
                .http(http)
                .storage(Rc::clone(&storage))
                .build()
                .await;
            state_machine.oneshot(RequestParams::default()).await.unwrap();

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
            let response = HttpResponse::builder()
                .status(hyper::StatusCode::INTERNAL_SERVER_ERROR)
                .header(X_RETRY_AFTER, 1234)
                .body(vec![])
                .unwrap();
            let http = MockHttpRequest::new(response);
            let storage = Rc::new(Mutex::new(MemStorage::new()));

            let mut state_machine = StateMachineBuilder::new_stub()
                .http(http)
                .storage(Rc::clone(&storage))
                .build()
                .await;
            assert_matches!(
                state_machine.oneshot(RequestParams::default()).await,
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
            let response = HttpResponse::builder()
                .status(hyper::StatusCode::INTERNAL_SERVER_ERROR)
                .header(X_RETRY_AFTER, 123456789)
                .body(vec![])
                .unwrap();
            let http = MockHttpRequest::new(response);
            let storage = Rc::new(Mutex::new(MemStorage::new()));

            let mut state_machine = StateMachineBuilder::new_stub()
                .http(http)
                .storage(Rc::clone(&storage))
                .build()
                .await;
            assert_matches!(
                state_machine.oneshot(RequestParams::default()).await,
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
                state_machine.oneshot(RequestParams::default()).await,
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
            let apps = app_set.lock().await.get_apps();
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
            let app_set = VecAppSet::new(vec![App::builder()
                .id("{00000000-0000-0000-0000-000000000001}")
                .version([1, 2, 3, 4])
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
            let apps = app_set.get_apps();
            storage.set_string(&apps[0].id, &json).await.unwrap();

            let app_set = Rc::new(Mutex::new(app_set));

            let _state_machine = StateMachineBuilder::new_stub()
                .storage(Rc::new(Mutex::new(storage)))
                .app_set(Rc::clone(&app_set))
                .build()
                .await;

            let apps = app_set.lock().await.get_apps();
            assert_eq!(persisted_app.cohort, apps[0].cohort);
            assert_eq!(UserCounting::ClientRegulatedByDate(Some(22222)), apps[0].user_counting);
        });
    }

    #[test]
    fn test_report_check_interval_with_no_storage() {
        block_on(async {
            let mut mock_time = MockTimeSource::new_from_now();
            let mut state_machine = StateMachineBuilder::new_stub()
                .policy_engine(StubPolicyEngine::new(mock_time.clone()))
                .metrics_reporter(MockMetricsReporter::new())
                .build()
                .await;

            state_machine.report_check_interval(InstallSource::ScheduledTask).await;
            // No metrics should be reported because no LAST_UPDATE_TIME in storage.
            assert!(state_machine.metrics_reporter.metrics.is_empty());

            // A second update check should report metrics.
            let interval = Duration::from_micros(999999);
            mock_time.advance(interval);

            state_machine.report_check_interval(InstallSource::ScheduledTask).await;

            assert_eq!(
                state_machine.metrics_reporter.metrics,
                vec![Metrics::UpdateCheckInterval {
                    interval,
                    clock: ClockType::Monotonic,
                    install_source: InstallSource::ScheduledTask,
                }]
            );
        });
    }

    #[test]
    fn test_report_check_interval_mono_transition() {
        block_on(async {
            let mut mock_time = MockTimeSource::new_from_now();
            let mut state_machine = StateMachineBuilder::new_stub()
                .policy_engine(StubPolicyEngine::new(mock_time.clone()))
                .metrics_reporter(MockMetricsReporter::new())
                .build()
                .await;

            // Make sure that, provided a wall time, we get an initial report
            // using the wall time.
            let initial_duration = Duration::from_secs(999);
            let initial_time = mock_time.now_in_walltime() - initial_duration;
            state_machine.context.schedule.last_update_check_time =
                Some(PartialComplexTime::Wall(initial_time));
            state_machine.report_check_interval(InstallSource::ScheduledTask).await;

            // Advance one more time, and this time we should see a monotonic delta.
            let interval = Duration::from_micros(999999);
            mock_time.advance(interval);
            state_machine.report_check_interval(InstallSource::ScheduledTask).await;

            // One final time, to demonstrate monotonic time edges to
            // monotonic time.
            mock_time.advance(interval);
            state_machine.report_check_interval(InstallSource::ScheduledTask).await;
            assert_eq!(
                state_machine.metrics_reporter.metrics,
                vec![
                    Metrics::UpdateCheckInterval {
                        interval: initial_duration,
                        clock: ClockType::Wall,
                        install_source: InstallSource::ScheduledTask,
                    },
                    Metrics::UpdateCheckInterval {
                        interval,
                        clock: ClockType::Monotonic,
                        install_source: InstallSource::ScheduledTask,
                    },
                    Metrics::UpdateCheckInterval {
                        interval,
                        clock: ClockType::Monotonic,
                        install_source: InstallSource::ScheduledTask,
                    },
                ]
            );
        });
    }

    #[derive(Debug)]
    pub struct TestInstaller {
        reboot_called: Rc<RefCell<bool>>,
        install_fails: usize,
        mock_time: MockTimeSource,
    }
    struct TestInstallerBuilder {
        install_fails: usize,
        mock_time: MockTimeSource,
    }
    impl TestInstaller {
        fn builder(mock_time: MockTimeSource) -> TestInstallerBuilder {
            TestInstallerBuilder { install_fails: 0, mock_time }
        }
    }
    impl TestInstallerBuilder {
        fn add_install_fail(mut self) -> Self {
            self.install_fails += 1;
            self
        }
        fn build(self) -> TestInstaller {
            TestInstaller {
                reboot_called: Rc::new(RefCell::new(false)),
                install_fails: self.install_fails,
                mock_time: self.mock_time,
            }
        }
    }
    const INSTALL_DURATION: Duration = Duration::from_micros(98765433);

    impl Installer for TestInstaller {
        type InstallPlan = StubPlan;
        type Error = StubInstallErrors;
        type InstallResult = ();

        fn perform_install<'a>(
            &'a mut self,
            _install_plan: &StubPlan,
            observer: Option<&'a dyn ProgressObserver>,
        ) -> LocalBoxFuture<'a, (Self::InstallResult, Vec<AppInstallResult<Self::Error>>)> {
            if self.install_fails > 0 {
                self.install_fails -= 1;
                future::ready(((), vec![AppInstallResult::Failed(StubInstallErrors::Failed)]))
                    .boxed()
            } else {
                self.mock_time.advance(INSTALL_DURATION);
                async move {
                    if let Some(observer) = observer {
                        observer.receive_progress(None, 0.0, None, None).await;
                        observer.receive_progress(None, 0.3, None, None).await;
                        observer.receive_progress(None, 0.9, None, None).await;
                        observer.receive_progress(None, 1.0, None, None).await;
                    }
                    ((), vec![AppInstallResult::Installed])
                }
                .boxed_local()
            }
        }

        fn perform_reboot(&mut self) -> LocalBoxFuture<'_, Result<(), anyhow::Error>> {
            self.reboot_called.replace(true);
            future::ready(Ok(())).boxed_local()
        }

        fn try_create_install_plan<'a>(
            &'a self,
            _request_params: &'a RequestParams,
            _request_metadata: Option<&'a RequestMetadata>,
            _response: &'a Response,
            _response_bytes: Vec<u8>,
            _ecdsa_signature: Option<Vec<u8>>,
        ) -> LocalBoxFuture<'a, Result<Self::InstallPlan, Self::Error>> {
            future::ready(Ok(StubPlan)).boxed_local()
        }
    }

    #[test]
    fn test_report_successful_update_duration() {
        block_on(async {
            let http = MockHttpRequest::new(make_update_available_response());
            let storage = Rc::new(Mutex::new(MemStorage::new()));

            let mut mock_time = MockTimeSource::new_from_now();
            mock_time.truncate_submicrosecond_walltime();
            let now = mock_time.now();

            let update_completed_time = now + INSTALL_DURATION;
            let expected_update_duration = update_completed_time.wall_duration_since(now).unwrap();

            let first_seen_time = now - Duration::from_micros(1000);

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

            #[rustfmt::skip]
            assert_matches!(
                state_machine.metrics_reporter.metrics.as_slice(),
                [
                    Metrics::UpdateCheckResponseTime { response_time: _, successful: true },
                    Metrics::RequestsPerCheck { count: 1, successful: true },
                    Metrics::OmahaEventLost(Event { event_type: EventType::UpdateDownloadStarted, event_result: EventResult::Success, .. }),
                    Metrics::SuccessfulUpdateDuration(install_duration),
                    Metrics::OmahaEventLost(Event { event_type: EventType::UpdateDownloadFinished, event_result: EventResult::Success, .. }),
                    Metrics::OmahaEventLost(Event { event_type: EventType::UpdateComplete, event_result: EventResult::Success, .. }),
                    Metrics::SuccessfulUpdateFromFirstSeen(duration_since_first_seen),
                    Metrics::AttemptsToSuccessfulCheck(1),
                    Metrics::AttemptsToSuccessfulInstall { count: 1, successful: true },
                ]
                if
                    *install_duration == expected_update_duration &&
                    *duration_since_first_seen == expected_duration_since_first_seen
            );
        });
    }

    #[test]
    fn test_report_failed_update_duration() {
        block_on(async {
            let http = MockHttpRequest::new(make_update_available_response());
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
    fn test_report_attempts_to_successful_check() {
        block_on(async {
            let storage = Rc::new(Mutex::new(MemStorage::new()));
            let mut state_machine = StateMachineBuilder::new_stub()
                .installer(StubInstaller { should_fail: true })
                .metrics_reporter(MockMetricsReporter::new())
                .storage(Rc::clone(&storage))
                .build()
                .await;

            state_machine.report_attempts_to_successful_check(true).await;

            // consecutive_failed_update_attempts should be zero (there were no previous failures)
            // but we should record an attempt in metrics
            assert_eq!(state_machine.context.state.consecutive_failed_update_checks, 0);
            assert_eq!(
                state_machine.metrics_reporter.metrics,
                vec![Metrics::AttemptsToSuccessfulCheck(1)]
            );

            state_machine.report_attempts_to_successful_check(false).await;
            assert_eq!(state_machine.context.state.consecutive_failed_update_checks, 1);

            state_machine.report_attempts_to_successful_check(false).await;
            assert_eq!(state_machine.context.state.consecutive_failed_update_checks, 2);

            // consecutive_failed_update_attempts should be reset to zero on success
            // but we should record the previous number of failed attempts (2) + 1 in metrics
            state_machine.report_attempts_to_successful_check(true).await;
            assert_eq!(state_machine.context.state.consecutive_failed_update_checks, 0);
            assert_eq!(
                state_machine.metrics_reporter.metrics,
                vec![Metrics::AttemptsToSuccessfulCheck(1), Metrics::AttemptsToSuccessfulCheck(3)]
            );
        });
    }

    #[test]
    fn test_ping_omaha_updates_consecutive_failed_update_checks_and_persists() {
        block_on(async {
            let mut http = MockHttpRequest::empty();
            http.add_error(http_request::mock_errors::make_transport_error());
            http.add_response(HttpResponse::new(vec![]));
            let response = json!({"response":{
              "server": "prod",
              "protocol": "3.0",
              "app": [{
                "appid": "{00000000-0000-0000-0000-000000000001}",
                "status": "ok",
              }],
            }});
            let response = serde_json::to_vec(&response).unwrap();
            http.add_response(HttpResponse::new(response));

            let storage = Rc::new(Mutex::new(MemStorage::new()));

            // Start out with a value in storage...
            {
                let mut storage = storage.lock().await;
                storage.set_int(CONSECUTIVE_FAILED_UPDATE_CHECKS, 1);
                storage.commit();
            }

            let mut state_machine = StateMachineBuilder::new_stub()
                .storage(Rc::clone(&storage))
                .http(http)
                .build()
                .await;

            async_generator::generate(move |mut co| async move {
                // Failed ping increases `consecutive_failed_update_checks`, adding the value from
                // storage.
                state_machine.ping_omaha(&mut co).await;
                assert_eq!(state_machine.context.state.consecutive_failed_update_checks, 2);
                {
                    let storage = storage.lock().await;
                    assert_eq!(storage.get_int(CONSECUTIVE_FAILED_UPDATE_CHECKS).await, Some(2));
                }

                state_machine.ping_omaha(&mut co).await;
                assert_eq!(state_machine.context.state.consecutive_failed_update_checks, 3);
                {
                    let storage = storage.lock().await;
                    assert_eq!(storage.get_int(CONSECUTIVE_FAILED_UPDATE_CHECKS).await, Some(3));
                }

                // Successful ping resets `consecutive_failed_update_checks`.
                state_machine.ping_omaha(&mut co).await;
                assert_eq!(state_machine.context.state.consecutive_failed_update_checks, 0);
                {
                    let storage = storage.lock().await;
                    assert_eq!(storage.get_int(CONSECUTIVE_FAILED_UPDATE_CHECKS).await, None);
                }
            })
            .into_complete()
            .await;
        });
    }

    #[test]
    fn test_report_attempts_to_successful_install() {
        block_on(async {
            let http = MockHttpRequest::new(make_update_available_response());
            let storage = Rc::new(Mutex::new(MemStorage::new()));

            let mock_time = MockTimeSource::new_from_now();

            let mut state_machine = StateMachineBuilder::new_stub()
                .http(http)
                .installer(TestInstaller::builder(mock_time.clone()).build())
                .policy_engine(StubPolicyEngine::new(mock_time.clone()))
                .metrics_reporter(MockMetricsReporter::new())
                .storage(Rc::clone(&storage))
                .build()
                .await;

            state_machine.run_once().await;

            // FIXME(https://github.com/rust-lang/rustfmt/issues/4530) rustfmt doesn't wrap slice
            // patterns yet.
            #[rustfmt::skip]
            assert_matches!(
                state_machine.metrics_reporter.metrics.as_slice(),
                [
                    Metrics::UpdateCheckResponseTime { response_time: _, successful: true },
                    Metrics::RequestsPerCheck { count: 1, successful: true },
                    Metrics::OmahaEventLost(Event { event_type: EventType::UpdateDownloadStarted, event_result: EventResult::Success, .. }),
                    Metrics::SuccessfulUpdateDuration(_),
                    Metrics::OmahaEventLost(Event { event_type: EventType::UpdateDownloadFinished, event_result: EventResult::Success, .. }),
                    Metrics::OmahaEventLost(Event { event_type: EventType::UpdateComplete, event_result: EventResult::Success, .. }),
                    Metrics::SuccessfulUpdateFromFirstSeen(_),
                    Metrics::AttemptsToSuccessfulCheck(1),
                    Metrics::AttemptsToSuccessfulInstall { count: 1, successful: true },
                ]
            );
        });
    }

    #[test]
    fn test_report_attempts_to_successful_install_fails_then_succeeds() {
        block_on(async {
            let mut http = MockHttpRequest::new(make_update_available_response());
            // Responses to events. This first batch corresponds to the install failure, so these
            // should be the update download started, and another for a failed install.
            // `Event::error(EventErrorCode::Installation)`.
            http.add_response(HttpResponse::new(vec![]));
            http.add_response(HttpResponse::new(vec![]));

            // Respond to the next request.
            http.add_response(make_update_available_response());
            // Responses to events. This corresponds to the update download started, and the other
            // for a successful install.
            http.add_response(HttpResponse::new(vec![]));
            http.add_response(HttpResponse::new(vec![]));

            let storage = Rc::new(Mutex::new(MemStorage::new()));
            let mock_time = MockTimeSource::new_from_now();

            let mut state_machine = StateMachineBuilder::new_stub()
                .http(http)
                .installer(TestInstaller::builder(mock_time.clone()).add_install_fail().build())
                .policy_engine(StubPolicyEngine::new(mock_time.clone()))
                .metrics_reporter(MockMetricsReporter::new())
                .storage(Rc::clone(&storage))
                .build()
                .await;

            state_machine.run_once().await;
            state_machine.run_once().await;

            // FIXME(https://github.com/rust-lang/rustfmt/issues/4530) rustfmt doesn't wrap slice
            // patterns yet.
            #[rustfmt::skip]
            assert_matches!(
                state_machine.metrics_reporter.metrics.as_slice(),
                [
                    Metrics::UpdateCheckResponseTime { response_time: _, successful: true },
                    Metrics::RequestsPerCheck { count: 1, successful: true },
                    Metrics::FailedUpdateDuration(_),
                    Metrics::AttemptsToSuccessfulCheck(1),
                    Metrics::AttemptsToSuccessfulInstall { count: 1, successful: false },
                    Metrics::UpdateCheckInterval { .. },
                    Metrics::UpdateCheckResponseTime { response_time: _, successful: true },
                    Metrics::RequestsPerCheck { count: 1, successful: true },
                    Metrics::SuccessfulUpdateDuration(_),
                    Metrics::OmahaEventLost(Event { .. }),
                    Metrics::SuccessfulUpdateFromFirstSeen(_),
                    Metrics::AttemptsToSuccessfulCheck(1),
                    Metrics::AttemptsToSuccessfulInstall { count: 2, successful: true }
                ]
            );
        });
    }

    #[test]
    fn test_report_attempts_to_successful_install_does_not_report_for_no_update() {
        block_on(async {
            let response = json!({"response":{
              "server": "prod",
              "protocol": "3.0",
              "app": [{
                "appid": "{00000000-0000-0000-0000-000000000001}",
                "status": "ok",
                "updatecheck": {
                  "status": "noupdate",
                  "info": "no update for you"
                }
              }],
            }});
            let response = serde_json::to_vec(&response).unwrap();
            let http = MockHttpRequest::new(HttpResponse::new(response.clone()));

            let storage = Rc::new(Mutex::new(MemStorage::new()));
            let mock_time = MockTimeSource::new_from_now();

            let mut state_machine = StateMachineBuilder::new_stub()
                .http(http)
                .installer(TestInstaller::builder(mock_time.clone()).build())
                .policy_engine(StubPolicyEngine::new(mock_time.clone()))
                .metrics_reporter(MockMetricsReporter::new())
                .storage(Rc::clone(&storage))
                .build()
                .await;

            state_machine.run_once().await;

            // FIXME(https://github.com/rust-lang/rustfmt/issues/4530) rustfmt doesn't wrap slice
            // patterns yet.
            #[rustfmt::skip]
            assert_matches!(
                state_machine.metrics_reporter.metrics.as_slice(),
                [
                    Metrics::UpdateCheckResponseTime { response_time: _, successful: true },
                    Metrics::RequestsPerCheck { count: 1, successful: true },
                    Metrics::AttemptsToSuccessfulCheck(1),
                ]
            );
        });
    }

    #[test]
    fn test_successful_update_triggers_reboot() {
        let mut pool = LocalPool::new();
        let spawner = pool.spawner();

        let http = MockHttpRequest::new(make_update_available_response());
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
    fn test_skip_reboot_if_not_needed() {
        let mut pool = LocalPool::new();
        let spawner = pool.spawner();

        let http = MockHttpRequest::new(make_update_available_response());
        let mock_time = MockTimeSource::new_from_now();
        let next_update_time = mock_time.now();
        let reboot_check_options_received = Rc::new(RefCell::new(vec![]));
        let policy_engine = MockPolicyEngine {
            reboot_check_options_received: Rc::clone(&reboot_check_options_received),
            check_timing: Some(CheckTiming::builder().time(next_update_time).build()),
            time_source: mock_time.clone(),
            reboot_needed: Rc::new(RefCell::new(false)),
            ..MockPolicyEngine::default()
        };
        let (timer, mut timers) = BlockingTimer::new();

        let installer = TestInstaller::builder(mock_time).build();
        let reboot_called = Rc::clone(&installer.reboot_called);
        let (_ctl, state_machine) = pool.run_until(
            StateMachineBuilder::new_stub()
                .http(http)
                .installer(installer)
                .policy_engine(policy_engine)
                .timer(timer)
                .start(),
        );
        let observer = TestObserver::default();
        spawner.spawn_local(observer.observe(state_machine)).unwrap();

        let blocked_timer = pool.run_until(timers.next()).unwrap();
        assert_eq!(blocked_timer.requested_wait(), RequestedWait::Until(next_update_time.into()));
        blocked_timer.unblock();
        pool.run_until_stalled();

        assert_eq!(
            observer.take_states(),
            vec![
                State::CheckingForUpdates(InstallSource::ScheduledTask),
                State::InstallingUpdate,
                State::Idle
            ]
        );

        assert_eq!(*reboot_check_options_received.borrow(), vec![]);
        assert!(!*reboot_called.borrow());
    }

    #[test]
    fn test_failed_update_does_not_trigger_reboot() {
        let mut pool = LocalPool::new();
        let spawner = pool.spawner();

        let http = MockHttpRequest::new(make_update_available_response());
        let mock_time = MockTimeSource::new_from_now();
        let next_update_time = mock_time.now();
        let (timer, mut timers) = BlockingTimer::new();

        let installer = TestInstaller::builder(mock_time.clone()).add_install_fail().build();
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

    // Verify that if we are in the middle of checking for or applying an update, a new OnDemand
    // update check request will "upgrade" the inflight check request to behave as if it was
    // OnDemand. In particular, this should cause an immediate reboot.
    #[test]
    fn test_reboots_immediately_if_user_initiated_update_requests_occurs_during_install() {
        let mut pool = LocalPool::new();
        let spawner = pool.spawner();

        let http = MockHttpRequest::new(make_update_available_response());
        let mock_time = MockTimeSource::new_from_now();

        let (send_install, mut recv_install) = mpsc::channel(0);
        let (send_reboot, mut recv_reboot) = mpsc::channel(0);
        let reboot_check_options_received = Rc::new(RefCell::new(vec![]));
        let policy_engine = MockPolicyEngine {
            reboot_check_options_received: Rc::clone(&reboot_check_options_received),
            check_timing: Some(CheckTiming::builder().time(mock_time.now()).build()),
            ..MockPolicyEngine::default()
        };

        let (mut ctl, state_machine) = pool.run_until(
            StateMachineBuilder::new_stub()
                .http(http)
                .installer(BlockingInstaller {
                    on_install: send_install,
                    on_reboot: Some(send_reboot),
                })
                .policy_engine(policy_engine)
                .start(),
        );

        let observer = TestObserver::default();
        spawner.spawn_local(observer.observe(state_machine)).unwrap();

        let unblock_install = pool.run_until(recv_install.next()).unwrap();
        pool.run_until_stalled();
        assert_eq!(
            observer.take_states(),
            vec![State::CheckingForUpdates(InstallSource::ScheduledTask), State::InstallingUpdate]
        );

        pool.run_until(async {
            assert_eq!(
                ctl.start_update_check(CheckOptions { source: InstallSource::OnDemand }).await,
                Ok(StartUpdateCheckResponse::AlreadyRunning)
            );
        });

        pool.run_until_stalled();
        assert_eq!(observer.take_states(), vec![]);

        unblock_install.send(vec![AppInstallResult::Installed]).unwrap();
        pool.run_until_stalled();
        assert_eq!(observer.take_states(), vec![State::WaitingForReboot]);

        let unblock_reboot = pool.run_until(recv_reboot.next()).unwrap();
        pool.run_until_stalled();
        unblock_reboot.send(Ok(())).unwrap();

        // Make sure when we checked whether we could reboot, it was from an OnDemand source
        assert_eq!(
            *reboot_check_options_received.borrow(),
            vec![CheckOptions { source: InstallSource::OnDemand }]
        );
    }

    // Verifies that if the state machine is done with an install and waiting for a reboot, and a
    // user-initiated UpdateCheckRequest comes in, we reboot immediately.
    #[test]
    fn test_reboots_immediately_when_check_now_comes_in_during_wait() {
        let mut pool = LocalPool::new();
        let spawner = pool.spawner();

        let mut http = MockHttpRequest::new(make_update_available_response());
        // Responses to events.
        http.add_response(HttpResponse::new(vec![]));
        http.add_response(HttpResponse::new(vec![]));
        http.add_response(HttpResponse::new(vec![]));
        // Response to the ping.
        http.add_response(make_update_available_response());
        let mut mock_time = MockTimeSource::new_from_now();
        mock_time.truncate_submicrosecond_walltime();
        let next_update_time = mock_time.now() + Duration::from_secs(1000);
        let (timer, mut timers) = BlockingTimer::new();
        let reboot_allowed = Rc::new(RefCell::new(false));
        let reboot_check_options_received = Rc::new(RefCell::new(vec![]));
        let policy_engine = MockPolicyEngine {
            time_source: mock_time.clone(),
            reboot_allowed: Rc::clone(&reboot_allowed),
            check_timing: Some(CheckTiming::builder().time(next_update_time).build()),
            reboot_check_options_received: Rc::clone(&reboot_check_options_received),
            ..MockPolicyEngine::default()
        };
        let installer = TestInstaller::builder(mock_time.clone()).build();
        let reboot_called = Rc::clone(&installer.reboot_called);
        let storage_ref = Rc::new(Mutex::new(MemStorage::new()));
        let apps = make_test_app_set();

        let (mut ctl, state_machine) = pool.run_until(
            StateMachineBuilder::new_stub()
                .app_set(apps)
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
        let (wait_for_reboot_timer, _wait_for_next_ping_timer) =
            match blocked_timer1.requested_wait() {
                RequestedWait::For(_) => (blocked_timer1, blocked_timer2),
                RequestedWait::Until(_) => (blocked_timer2, blocked_timer1),
            };
        // This is the timer waiting for next reboot_allowed check.
        assert_eq!(
            wait_for_reboot_timer.requested_wait(),
            RequestedWait::For(CHECK_REBOOT_ALLOWED_INTERVAL)
        );

        // If we send an update check request that's from a user (source == OnDemand), we should
        // short-circuit the wait for reboot, and update immediately.
        assert!(!*reboot_called.borrow());
        *reboot_allowed.borrow_mut() = true;
        pool.run_until(async {
            assert_eq!(
                ctl.start_update_check(CheckOptions { source: InstallSource::OnDemand }).await,
                Ok(StartUpdateCheckResponse::AlreadyRunning)
            );
        });
        pool.run_until_stalled();
        assert!(*reboot_called.borrow());

        // Check that we got one check for reboot from a Scheduled Task (the start of the wait),
        // and then another came in with OnDemand, as we "upgraded it" with our OnDemand check
        // request
        assert_eq!(
            *reboot_check_options_received.borrow(),
            vec![
                CheckOptions { source: InstallSource::ScheduledTask },
                CheckOptions { source: InstallSource::OnDemand },
            ]
        );
    }

    // Verifies that if reboot is not allowed, state machine will send pings to Omaha while waiting
    // for reboot, and it will reply AlreadyRunning to any StartUpdateCheck requests, and when it's
    // finally time to reboot, it will trigger reboot.
    #[test]
    fn test_wait_for_reboot() {
        let mut pool = LocalPool::new();
        let spawner = pool.spawner();

        let mut http = MockHttpRequest::new(make_update_available_response());
        // Responses to events.
        http.add_response(HttpResponse::new(vec![]));
        http.add_response(HttpResponse::new(vec![]));
        http.add_response(HttpResponse::new(vec![]));
        // Response to the ping.
        http.add_response(make_update_available_response());
        let ping_request_viewer = MockHttpRequest::from_request_cell(http.get_request_cell());
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

        let apps = pool.run_until(apps.lock()).get_apps();
        let mut expected_request_builder = RequestBuilder::new(&config, &request_params)
            // 0: session id for update check
            // 1: request id for update check
            // 2-4: request id for events
            .session_id(GUID::from_u128(5))
            .request_id(GUID::from_u128(6));
        for app in &apps {
            expected_request_builder = expected_request_builder.add_ping(app);
        }
        pool.run_until(assert_request(&ping_request_viewer, expected_request_builder));

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
            expected_request_builder = expected_request_builder.add_ping(app);
        }
        pool.run_until(assert_request(&ping_request_viewer, expected_request_builder));

        assert!(!*reboot_called.borrow());

        // Now allow reboot.
        *reboot_called.borrow_mut() = true;
        wait_for_reboot_timer.unblock();
        pool.run_until_stalled();
        assert!(*reboot_called.borrow());
    }

    #[derive(Debug)]
    struct BlockingInstaller {
        on_install: mpsc::Sender<oneshot::Sender<Vec<AppInstallResult<StubInstallErrors>>>>,
        on_reboot: Option<mpsc::Sender<oneshot::Sender<Result<(), anyhow::Error>>>>,
    }

    impl Installer for BlockingInstaller {
        type InstallPlan = StubPlan;
        type Error = StubInstallErrors;
        type InstallResult = ();

        fn perform_install(
            &mut self,
            _install_plan: &StubPlan,
            _observer: Option<&dyn ProgressObserver>,
        ) -> LocalBoxFuture<'_, (Self::InstallResult, Vec<AppInstallResult<Self::Error>>)> {
            let (send, recv) = oneshot::channel();
            let send_fut = self.on_install.send(send);

            async move {
                send_fut.await.unwrap();
                ((), recv.await.unwrap())
            }
            .boxed_local()
        }

        fn perform_reboot(&mut self) -> LocalBoxFuture<'_, Result<(), anyhow::Error>> {
            match &mut self.on_reboot {
                Some(on_reboot) => {
                    let (send, recv) = oneshot::channel();
                    let send_fut = on_reboot.send(send);

                    async move {
                        send_fut.await.unwrap();
                        recv.await.unwrap()
                    }
                    .boxed_local()
                }
                None => future::ready(Ok(())).boxed_local(),
            }
        }

        fn try_create_install_plan<'a>(
            &'a self,
            _request_params: &'a RequestParams,
            _request_metadata: Option<&'a RequestMetadata>,
            _response: &'a Response,
            _response_bytes: Vec<u8>,
            _ecdsa_signature: Option<Vec<u8>>,
        ) -> LocalBoxFuture<'a, Result<Self::InstallPlan, Self::Error>> {
            future::ready(Ok(StubPlan)).boxed_local()
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
            std::mem::take(&mut *self.states.borrow_mut())
        }
    }

    #[test]
    fn test_start_update_during_update_replies_with_in_progress() {
        let mut pool = LocalPool::new();
        let spawner = pool.spawner();

        let http = MockHttpRequest::new(make_update_available_response());
        let (send_install, mut recv_install) = mpsc::channel(0);
        let (mut ctl, state_machine) = pool.run_until(
            StateMachineBuilder::new_stub()
                .http(http)
                .installer(BlockingInstaller { on_install: send_install, on_reboot: None })
                .start(),
        );

        let observer = TestObserver::default();
        spawner.spawn_local(observer.observe_until_terminal(state_machine)).unwrap();

        let unblock_install = pool.run_until(recv_install.next()).unwrap();
        pool.run_until_stalled();
        assert_eq!(
            observer.take_states(),
            vec![State::CheckingForUpdates(InstallSource::ScheduledTask), State::InstallingUpdate]
        );

        pool.run_until(async {
            assert_eq!(
                ctl.start_update_check(CheckOptions::default()).await,
                Ok(StartUpdateCheckResponse::AlreadyRunning)
            );
        });
        pool.run_until_stalled();
        assert_eq!(observer.take_states(), vec![]);

        unblock_install.send(vec![AppInstallResult::Installed]).unwrap();
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
            vec![
                State::CheckingForUpdates(InstallSource::ScheduledTask),
                State::ErrorCheckingForUpdate,
                State::Idle
            ]
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
            vec![
                State::CheckingForUpdates(InstallSource::ScheduledTask),
                State::ErrorCheckingForUpdate,
                State::Idle
            ]
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
            let http = MockHttpRequest::new(make_update_available_response());
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
    // A scenario in which
    // (now_in_monotonic - state_machine_start_in_monotonic) > (update_finish_time - now_in_wall)
    // should not panic.
    fn test_report_waited_for_reboot_duration_doesnt_panic_on_wrong_current_time() {
        block_on(async {
            let metrics_reporter = MockMetricsReporter::new();

            let state_machine_start_monotonic = Instant::now();
            let update_finish_time = SystemTime::now();

            // Set the monotonic increase in time larger than the wall time increase since the end
            // of the last update.
            // This can happen if we don't have a reliable current wall time.
            let now_wall = update_finish_time + Duration::from_secs(1);
            let now_monotonic = state_machine_start_monotonic + Duration::from_secs(10);

            let mut state_machine =
                StateMachineBuilder::new_stub().metrics_reporter(metrics_reporter).build().await;

            // Time has advanced monotonically since we noted the start of the state machine for
            // longer than the wall time difference between update finish time and now.
            // This computation should currently overflow.
            state_machine
                .report_waited_for_reboot_duration(
                    update_finish_time,
                    state_machine_start_monotonic,
                    ComplexTime { wall: now_wall, mono: now_monotonic },
                )
                .expect_err("should overflow and error out");

            // We should have reported no metrics
            assert!(state_machine.metrics_reporter.metrics.is_empty());
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
        let http = MockHttpRequest::new(HttpResponse::new(response));
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
                    .oneshot(RequestParams::default())
            ),
            Ok(_)
        );

        mock_time.advance(Duration::from_secs(999));

        // Execute state machine `run()`, simulating that we already rebooted.
        let config = Config {
            updater: Updater { name: "updater".to_string(), version: Version::from([0, 1]) },
            os: OS { version: "1.2.3.5".to_string(), ..OS::default() },
            service_url: "http://example.com/".to_string(),
            omaha_public_keys: None,
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
                .filter(|m| matches!(m, Metrics::WaitedForRebootDuration(_)))
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

    // The same as |run_simple_check_with_noupdate_result|, but with CUPv2 protocol validation.
    #[test]
    fn run_cup_but_decoration_error() {
        block_on(async {
            let http = MockHttpRequest::new(HttpResponse::new(make_noupdate_httpresponse()));

            let stub_cup_handler = MockCupv2Handler::new().set_decoration_error(|| {
                Some(CupDecorationError::ParseError("".parse::<http::Uri>().unwrap_err()))
            });

            assert_matches!(
                StateMachineBuilder::new_stub()
                    .http(http)
                    .cup_handler(Some(stub_cup_handler))
                    .oneshot(RequestParams::default())
                    .await,
                Err(UpdateCheckError::OmahaRequest(OmahaRequestError::CupDecoration(
                    CupDecorationError::ParseError(_)
                )))
            );

            info!("update check complete!");
        });
    }

    #[test]
    fn run_cup_but_verification_error() {
        block_on(async {
            let http = MockHttpRequest::new(HttpResponse::new(make_noupdate_httpresponse()));

            let stub_cup_handler = MockCupv2Handler::new()
                .set_verification_error(|| Some(CupVerificationError::EtagHeaderMissing));

            assert_matches!(
                StateMachineBuilder::new_stub()
                    .http(http)
                    .cup_handler(Some(stub_cup_handler))
                    .oneshot(RequestParams::default())
                    .await,
                Err(UpdateCheckError::OmahaRequest(OmahaRequestError::CupValidation(
                    CupVerificationError::EtagHeaderMissing
                )))
            );

            info!("update check complete!");
        });
    }

    #[test]
    fn run_cup_valid() {
        block_on(async {
            let http = MockHttpRequest::new(HttpResponse::new(make_noupdate_httpresponse()));

            assert_matches!(
                StateMachineBuilder::new_stub()
                    .http(http)
                    // Default stub_cup_handler, which is permissive.
                    .oneshot(RequestParams::default())
                    .await,
                Ok(_)
            );

            info!("update check complete!");
        });
    }
}
