// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::timer::FuchsiaTimer;
use anyhow::{Context as _, Error};
use fidl_fuchsia_ui_activity::{
    ListenerMarker, ListenerRequest, ProviderMarker, ProviderProxy, State,
};
use fuchsia_component::client::connect_to_service;
use fuchsia_zircon as zx;
use futures::{future::BoxFuture, prelude::*};
use log::{error, info, warn};
use omaha_client::{
    common::{App, CheckOptions, CheckTiming, ProtocolState, UpdateCheckSchedule},
    installer::Plan,
    policy::{CheckDecision, Policy, PolicyEngine, UpdateDecision},
    protocol::request::InstallSource,
    request_builder::RequestParams,
    time::{ComplexTime, PartialComplexTime, TimeSource, Timer},
    unless::Unless,
};
use serde::Deserialize;
use std::cell::Cell;
use std::path::Path;
use std::rc::Rc;
use std::time::Duration;

/// We do periodic update check roughly every hour.
const PERIODIC_INTERVAL: Duration = Duration::from_secs(1 * 60 * 60);
/// Wait at least one minute before checking for updates after startup.
const STARTUP_DELAY: Duration = Duration::from_secs(60);
/// Wait 5 minutes before retrying after failed update checks.
const RETRY_DELAY: Duration = Duration::from_secs(5 * 60);
/// If the UI activity is active but is > 48 hours old, we should still allow reboot.
const UI_ACTIVITY_VALID_DURATION: zx::Duration = zx::Duration::from_seconds(48 * 60);

/// The policy implementation for Fuchsia.
struct FuchsiaPolicy;

impl Policy for FuchsiaPolicy {
    type UpdatePolicyData = FuchsiaUpdatePolicyData;
    type RebootPolicyData = FuchsiaRebootPolicyData;

    fn compute_next_update_time(
        policy_data: &Self::UpdatePolicyData,
        _apps: &[App],
        scheduling: &UpdateCheckSchedule,
        protocol_state: &ProtocolState,
    ) -> CheckTiming {
        info!(
            "FuchsiaPolicy::compute_next_update_time with {:?} and {:?} in state {:?}",
            scheduling, policy_data, protocol_state
        );

        // Error conditions are handled separately.
        //
        // For the first few consecutive errors, retry quickly using only the monotonic clock.
        // Afterwards, only use the monotonic clock, but the normal delay, until such time
        // as omaha has been reached again.
        let consecutive_failed_checks = protocol_state.consecutive_failed_update_checks;
        if consecutive_failed_checks > 0 {
            warn!("Using Retry Mode logic: {:?}", protocol_state);
            let error_duration = if consecutive_failed_checks < 4 {
                policy_data.config.retry_delay
            } else {
                policy_data.config.periodic_interval
            };
            return CheckTiming::builder()
                .time(PartialComplexTime::Monotonic(
                    (policy_data.current_time + error_duration).into(),
                ))
                .build();
        }

        // Normal operation, use the standard poll interval, unless a server-dictated interval
        // has been set by the server.
        let interval = policy_data
            .config
            .periodic_interval
            .unless(protocol_state.server_dictated_poll_interval);

        // The `CheckTiming` to return is primarily based on the state of the `last_update_time`.
        //
        // If this is the first attempt to talk to Omaha since starting, `last_update_time` will
        // be `None`, or will not have an Instant component, and so there needs to be a minimum wait
        // of STARTUP_DELAY, so that no automatic background polls are performed too soon after
        // startup.
        match scheduling.last_update_time {
            // There is no previous time at all, so this looks like the very first time (or after
            // say a factory reset), so go immediately after the startup delay from now.
            None => {
                warn!("Using FDR Startup Mode logic.");
                CheckTiming::builder()
                    .time(policy_data.current_time + policy_data.config.startup_delay)
                    .minimum_wait(policy_data.config.startup_delay)
                    .build()
            }

            // If there's a `last_update_time`, then the time for the next check is at least
            // partially based on that.
            Some(last_update_time) => {
                match last_update_time {
                    // If only a wall time is known, then it's likely that this is a startup
                    // condition, and bound on the monotonic timeline needs to be added to the
                    // bound based on the last update wall time, as well as a minimum delay.
                    //
                    // PartialComplexTime::complete_with(ComplexTime) accomplises this by adding
                    // the missing bound on the monotonic timeline.
                    _last_wall_time @ PartialComplexTime::Wall(_) => {
                        info!("Using Startup Mode logic.");
                        CheckTiming::builder()
                            // TODO Switch back the line below after channel is in vbmeta
                            //      (fxb/39970)
                            // .time(last_wall_time.complete_with(policy_data.current_time) + interval)
                            .time(policy_data.current_time + policy_data.config.startup_delay)
                            .minimum_wait(policy_data.config.startup_delay)
                            .build()
                    }

                    // In all other cases (there is at least a monotonic time), add the interval to
                    // the last time and use that.
                    last_update_time @ _ => {
                        info!("Using Standard logic.");
                        CheckTiming::builder().time(last_update_time + interval).build()
                    }
                }
            }
        }
    }

    fn update_check_allowed(
        policy_data: &Self::UpdatePolicyData,
        _apps: &[App],
        scheduling: &UpdateCheckSchedule,
        _protocol_state: &ProtocolState,
        check_options: &CheckOptions,
    ) -> CheckDecision {
        info!(
            "FuchsiaPolicy::update_check_allowed with {:?} and {:?} for {:?}",
            scheduling, policy_data, check_options
        );
        // Always allow update check initiated by a user.
        if check_options.source == InstallSource::OnDemand {
            CheckDecision::Ok(RequestParams {
                source: InstallSource::OnDemand,
                use_configured_proxies: true,
            })
        } else {
            match scheduling.next_update_time {
                // Cannot check without first scheduling a next update time.
                None => CheckDecision::TooSoon,
                Some(next_update_time) => {
                    if policy_data.current_time.is_after_or_eq_any(next_update_time.time) {
                        CheckDecision::Ok(RequestParams {
                            source: InstallSource::ScheduledTask,
                            use_configured_proxies: true,
                        })
                    } else {
                        CheckDecision::TooSoon
                    }
                }
            }
        }
    }

    fn update_can_start(
        _policy_data: &Self::UpdatePolicyData,
        _proposed_install_plan: &impl Plan,
    ) -> UpdateDecision {
        UpdateDecision::Ok
    }

    /// Is reboot allowed right now.
    fn reboot_allowed(policy_data: &Self::RebootPolicyData, check_options: &CheckOptions) -> bool {
        check_options.source == InstallSource::OnDemand
            || policy_data.ui_activity.state != State::Active
            || policy_data.ui_activity.transition_time + UI_ACTIVITY_VALID_DURATION
                < policy_data.current_monotonic_zx_time
    }
}

/// FuchsiaPolicyEngine just gathers the current time and hands it off to the FuchsiaPolicy as the
/// PolicyData.
#[derive(Debug)]
pub struct FuchsiaPolicyEngine<T> {
    time_source: T,
    // Whether the device is in active use.
    ui_activity: Rc<Cell<UiActivityState>>,
    config: PolicyConfig,
}
pub struct FuchsiaPolicyEngineBuilder;
pub struct FuchsiaPolicyEngineBuilderWithTime<T> {
    time_source: T,
    config: PolicyConfig,
}
impl FuchsiaPolicyEngineBuilder {
    pub fn time_source<T>(self, time_source: T) -> FuchsiaPolicyEngineBuilderWithTime<T>
    where
        T: TimeSource + Clone,
    {
        FuchsiaPolicyEngineBuilderWithTime { time_source, config: PolicyConfig::default() }
    }
}
impl<T> FuchsiaPolicyEngineBuilderWithTime<T> {
    pub fn load_config_from(self, path: impl AsRef<Path>) -> Self {
        Self { time_source: self.time_source, config: PolicyConfigJson::load_from(path).into() }
    }

    pub fn build(self) -> FuchsiaPolicyEngine<T> {
        FuchsiaPolicyEngine {
            time_source: self.time_source,
            ui_activity: Rc::new(Cell::new(UiActivityState::new())),
            config: self.config,
        }
    }
}

impl<T> PolicyEngine for FuchsiaPolicyEngine<T>
where
    T: TimeSource + Clone,
{
    type TimeSource = T;

    fn time_source(&self) -> &Self::TimeSource {
        &self.time_source
    }

    fn compute_next_update_time(
        &mut self,
        apps: &[App],
        scheduling: &UpdateCheckSchedule,
        protocol_state: &ProtocolState,
    ) -> BoxFuture<'_, CheckTiming> {
        let timing = FuchsiaPolicy::compute_next_update_time(
            &self.build_update_policy_data(),
            apps,
            scheduling,
            protocol_state,
        );
        future::ready(timing).boxed()
    }

    fn update_check_allowed(
        &mut self,
        apps: &[App],
        scheduling: &UpdateCheckSchedule,
        protocol_state: &ProtocolState,
        check_options: &CheckOptions,
    ) -> BoxFuture<'_, CheckDecision> {
        let decision = FuchsiaPolicy::update_check_allowed(
            &self.build_update_policy_data(),
            apps,
            scheduling,
            protocol_state,
            check_options,
        );
        future::ready(decision).boxed()
    }

    fn update_can_start(
        &mut self,
        proposed_install_plan: &impl Plan,
    ) -> BoxFuture<'_, UpdateDecision> {
        let decision = FuchsiaPolicy::update_can_start(
            &self.build_update_policy_data(),
            proposed_install_plan,
        );
        future::ready(decision).boxed()
    }

    fn reboot_allowed(&mut self, check_options: &CheckOptions) -> BoxFuture<'_, bool> {
        let decision = FuchsiaPolicy::reboot_allowed(
            &FuchsiaRebootPolicyData::new(self.ui_activity.get()),
            check_options,
        );
        future::ready(decision).boxed()
    }
}

impl<T> FuchsiaPolicyEngine<T>
where
    T: TimeSource + Clone,
{
    /// Returns a future that watches the UI activity state and updates the value within
    /// FuchsiaPolicyEngine.
    pub fn start_watching_ui_activity(&self) -> impl Future<Output = ()> {
        let ui_activity = Rc::clone(&self.ui_activity);
        async move {
            let mut backoff = Duration::from_secs(1);
            loop {
                if let Err(e) = watch_ui_activity(&ui_activity).await {
                    error!("watch_ui_activity failed, retry in {}s: {:?}", backoff.as_secs(), e);
                }
                FuchsiaTimer.wait_for(backoff).await;
                if backoff.as_secs() < 512 {
                    backoff *= 2;
                }
            }
        }
    }

    pub fn get_config(&self) -> &PolicyConfig {
        &self.config
    }

    fn build_update_policy_data(&self) -> FuchsiaUpdatePolicyData {
        FuchsiaUpdatePolicyData::builder()
            .use_timesource(&self.time_source)
            .config(self.config.clone())
            .build()
    }
}

/// Watches the UI activity state and updates the value in `ui_activity`.
async fn watch_ui_activity(ui_activity: &Rc<Cell<UiActivityState>>) -> Result<(), Error> {
    let provider = connect_to_service::<ProviderMarker>()?;
    watch_ui_activity_impl(ui_activity, provider).await
}

/// Watches the UI activity state using `provider` proxy and updates the value in `ui_activity`.
async fn watch_ui_activity_impl(
    ui_activity: &Rc<Cell<UiActivityState>>,
    provider: ProviderProxy,
) -> Result<(), Error> {
    let (listener, mut stream) = fidl::endpoints::create_request_stream::<ListenerMarker>()?;
    provider.watch_state(listener).context("watch_state")?;
    while let Some(event) = stream.try_next().await? {
        let ListenerRequest::OnStateChanged { state, transition_time, responder } = event;
        ui_activity
            .set(UiActivityState { state, transition_time: zx::Time::from_nanos(transition_time) });
        responder.send()?;
    }
    Ok(())
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
struct UiActivityState {
    state: State,
    // Monotonic time of last state transition.
    transition_time: zx::Time,
}

impl UiActivityState {
    fn new() -> Self {
        Self { state: State::Unknown, transition_time: zx::Time::get(zx::ClockId::Monotonic) }
    }
}

#[derive(Clone, Debug)]
struct FuchsiaUpdatePolicyData {
    current_time: ComplexTime,
    config: PolicyConfig,
}

impl FuchsiaUpdatePolicyData {
    /// Create and return a new builder for FuchsiaUpdatePolicyData.
    fn builder() -> UpdatePolicyDataBuilder {
        UpdatePolicyDataBuilder::default()
    }
}

/// The UpdatePolicyDataBuilder uses the typestate pattern.  The builder cannot be built until the
/// time has been specified (which changes the type of the builder).
#[derive(Debug, Default)]
struct UpdatePolicyDataBuilder {
    config: PolicyConfig,
}

/// The UpdatePolicyDataBuilder, once it has time set.
struct UpdatePolicyDataBuilderWithTime {
    current_time: ComplexTime,
    config: PolicyConfig,
}

impl UpdatePolicyDataBuilder {
    /// Use a `TimeSource` to set the `current_time`.
    fn use_timesource<T: TimeSource>(self, timesource: &T) -> UpdatePolicyDataBuilderWithTime {
        UpdatePolicyDataBuilderWithTime { current_time: timesource.now(), config: self.config }
    }

    /// Set the `current_time` explicitly from a given ComplexTime.
    #[cfg(test)]
    fn time(self, current_time: ComplexTime) -> UpdatePolicyDataBuilderWithTime {
        UpdatePolicyDataBuilderWithTime { current_time, config: self.config }
    }

    /// Set the `config` explicitly from a given PolicyConfig.
    #[cfg(test)]
    fn config(self, config: PolicyConfig) -> UpdatePolicyDataBuilder {
        UpdatePolicyDataBuilder { config }
    }
}

/// These are the operations that can be performed once the time has been set.
impl UpdatePolicyDataBuilderWithTime {
    /// Set the `config` explicitly from a given PolicyConfig.
    fn config(self, config: PolicyConfig) -> UpdatePolicyDataBuilderWithTime {
        UpdatePolicyDataBuilderWithTime { current_time: self.current_time, config }
    }

    /// Construct the FuchsiaUpdatePolicyData.
    fn build(self) -> FuchsiaUpdatePolicyData {
        FuchsiaUpdatePolicyData { current_time: self.current_time, config: self.config }
    }
}

#[derive(Clone, Debug)]
struct FuchsiaRebootPolicyData {
    ui_activity: UiActivityState,
    current_monotonic_zx_time: zx::Time,
}

impl FuchsiaRebootPolicyData {
    fn new(ui_activity: UiActivityState) -> Self {
        Self {
            ui_activity,
            // We use zx::Time::get instead of time_source because there's no easy way to
            // convert Instant to zx::Time.
            current_monotonic_zx_time: zx::Time::get(zx::ClockId::Monotonic),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PolicyConfig {
    pub periodic_interval: Duration,
    pub startup_delay: Duration,
    pub retry_delay: Duration,
}

impl From<Option<PolicyConfigJson>> for PolicyConfig {
    fn from(config: Option<PolicyConfigJson>) -> Self {
        Self {
            periodic_interval: config
                .as_ref()
                .and_then(|c| c.periodic_interval_minutes)
                .map(|m| Duration::from_secs(m * 60))
                .unwrap_or(PERIODIC_INTERVAL),
            startup_delay: config
                .as_ref()
                .and_then(|c| c.startup_delay_seconds)
                .map(Duration::from_secs)
                .unwrap_or(STARTUP_DELAY),
            retry_delay: config
                .as_ref()
                .and_then(|c| c.retry_delay_seconds)
                .map(Duration::from_secs)
                .unwrap_or(RETRY_DELAY),
        }
    }
}

impl Default for PolicyConfig {
    fn default() -> Self {
        PolicyConfig::from(None)
    }
}

#[derive(Clone, Debug, Default, Deserialize)]
struct PolicyConfigJson {
    periodic_interval_minutes: Option<u64>,
    startup_delay_seconds: Option<u64>,
    retry_delay_seconds: Option<u64>,
}

impl PolicyConfigJson {
    fn load_from(path: impl AsRef<Path>) -> Option<Self> {
        let config = std::fs::read_to_string(path.as_ref().join("policy_config.json")).ok()?;
        serde_json::from_str(&config)
            .map_err(|e| warn!("Failed to parse policy config: {:?}", e))
            .ok()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_ui_activity::ProviderRequest;
    use fuchsia_async as fasync;
    use omaha_client::installer::stub::StubPlan;
    use omaha_client::time::{ComplexTime, MockTimeSource, StandardTimeSource, TimeSource};

    /// Test that the correct next update time is calculated for the normal case where a check was
    /// recently done and the next needs to be scheduled.
    #[test]
    fn test_compute_next_update_time_for_normal_operation() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        // The current context:
        //   - the last update was recently in the past
        let last_update_time = now - Duration::from_secs(1234);
        let schedule = UpdateCheckSchedule::builder().last_time(last_update_time).build();
        // Set up the state for this check:
        //  - the time is "now"
        let policy_data = FuchsiaUpdatePolicyData::builder().time(now).build();
        // Execute the policy check.
        let result = FuchsiaPolicy::compute_next_update_time(
            &policy_data,
            &[],
            &schedule,
            &ProtocolState::default(),
        );
        // Confirm that:
        //  - the policy-computed next update time is a standard poll interval from the last.
        let expected = CheckTiming::builder()
            .time(schedule.last_update_time.unwrap() + PERIODIC_INTERVAL)
            .build();
        debug_print_check_timing_test_data(
            "normal operation",
            expected,
            result,
            last_update_time,
            now,
        );
        assert_eq!(result, expected);
    }

    /// Test that the correct next update time is calculated at first startup, when there is no
    /// stored `last_update_time`.
    ///
    /// This is the case of a bootup right after a factory reset.
    #[test]
    fn test_compute_next_update_time_at_first_startup() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        // The current context:
        //   - there is no last update time
        let schedule = UpdateCheckSchedule::builder().build();
        // Set up the state for this check:
        //  - the time is "now"
        let policy_data = FuchsiaUpdatePolicyData::builder().time(now).build();
        // Execute the policy check.
        let result = FuchsiaPolicy::compute_next_update_time(
            &policy_data,
            &[],
            &schedule,
            &ProtocolState::default(),
        );
        // Confirm that:
        //  - the policy-computed next update time is a startup delay poll interval from now.
        let expected =
            CheckTiming::builder().time(now + STARTUP_DELAY).minimum_wait(STARTUP_DELAY).build();
        debug_print_check_timing_test_data("first startup", expected, result, None, now);
        assert_eq!(result, expected);
    }

    /// Test that the correct next update time is calculated at startup when the persisted last
    /// update time is "far" into the past.  This is an unlikely corner case.  But it could happen
    /// if there was a persisted `last_update_time` that was from an update check that succeeded
    /// while on the previous build's backstop time.
    #[test]
    fn test_compute_next_update_time_at_startup_with_past_last_update_time() {
        let mut mock_time = MockTimeSource::new_from_now();
        mock_time.truncate_submicrosecond_walltime();
        let now = mock_time.now();
        // The current context:
        //   - the last update was far in the past (a bit over a day)
        //   - persisted times will not have monotonic components.
        let last_update_time = PartialComplexTime::Wall(now.wall - Duration::from_secs(100000));
        let schedule = UpdateCheckSchedule::builder().last_time(last_update_time).build();
        // Set up the state for this check:
        //  - the time is "now"
        let policy_data = FuchsiaUpdatePolicyData::builder().time(now).build();
        // Execute the policy check.
        let result = FuchsiaPolicy::compute_next_update_time(
            &policy_data,
            &[],
            &schedule,
            &ProtocolState::default(),
        );
        // Confirm that:
        //  - The policy-computed next update time is
        //       1) a normal poll interval from now (monotonic)
        //       2) a normal poll interval from the persisted last update time (wall)
        //  - There's a minimum wait of STARTUP_DELAY
        let expected = CheckTiming::builder()
            // TODO Switch back the line below after channel is in vbmeta
            //      (fxb/39970)
            // .time((
            //     last_update_time.checked_to_system_time().unwrap() + PERIODIC_INTERVAL,
            //     Instant::from(now) + PERIODIC_INTERVAL,
            // ))
            .time(now + STARTUP_DELAY)
            .minimum_wait(STARTUP_DELAY)
            .build();
        debug_print_check_timing_test_data(
            "startup with past last_update_time",
            expected,
            result,
            last_update_time,
            now,
        );
        assert_eq!(result, expected);
    }

    /// Test that the correct next update time is calculated at startup when the persisted last
    /// update time is in the future.
    ///
    /// This specifically catches the case when starting on the backstop time, and the persisted
    /// time is "ahead" of now, as the current time hasn't been sync'd with UTC.  This is the normal
    /// startup case for most boots of a device.
    #[test]
    fn test_compute_next_update_time_at_startup_with_future_last_update_time() {
        let mut mock_time = MockTimeSource::new_from_now();
        mock_time.truncate_submicrosecond_walltime();
        let now = mock_time.now();
        // The current context:
        //   - the last update was far in the future (a bit over 12 days)
        //   - persisted times will not have monotonic components.
        let last_update_time = PartialComplexTime::Wall(now.wall - Duration::from_secs(100000));
        let schedule = UpdateCheckSchedule::builder().last_time(last_update_time).build();
        // Set up the state for this check:
        //  - the time is "now"
        let policy_data = FuchsiaUpdatePolicyData::builder().time(now).build();
        // Execute the policy check.
        let result = FuchsiaPolicy::compute_next_update_time(
            &policy_data,
            &[],
            &schedule,
            &ProtocolState::default(),
        );
        // Confirm that:
        //  - The policy-computed next update time is
        //       1) a normal interval from now (monotonic)
        //       2) a normal interval from the persisted last update time (wall)
        //  - There's a minimum wait of STARTUP_DELAY
        let expected = CheckTiming::builder()
            // TODO Switch back the line below after channel is in vbmeta
            //      (fxb/39970)
            // .time((
            //     last_update_time.checked_to_system_time().unwrap() + PERIODIC_INTERVAL,
            //     Instant::from(now) + PERIODIC_INTERVAL,
            // ))
            .time(now + STARTUP_DELAY)
            .minimum_wait(STARTUP_DELAY)
            .build();
        debug_print_check_timing_test_data(
            "startup with future last_update_time",
            expected,
            result,
            last_update_time,
            now,
        );
        assert_eq!(result, expected);
    }

    /// Test that the correct next update time is calculated for the case when there is a single
    /// update check failure.
    #[test]
    fn test_compute_next_update_time_after_a_single_failure() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        // The current context:
        //   - the last update was in the past
        //   - there is 1 failed update check, which moves the policy to "fast" retries
        let last_update_time = now - Duration::from_secs(10);
        let schedule = UpdateCheckSchedule::builder().last_time(last_update_time).build();
        let protocol_state =
            ProtocolState { consecutive_failed_update_checks: 1, ..Default::default() };
        // Set up the state for this check:
        //  - the time is "now"
        let policy_data = FuchsiaUpdatePolicyData::builder().time(now).build();
        // Execute the policy check
        let result =
            FuchsiaPolicy::compute_next_update_time(&policy_data, &[], &schedule, &protocol_state);
        // Confirm that:
        //  - the computed next update time is a retry poll interval from now, on the monotonic
        //    timeline only.
        let expected = CheckTiming::builder().time(now.mono + RETRY_DELAY).build();
        debug_print_check_timing_test_data(
            "single failure",
            expected,
            result,
            last_update_time,
            now,
        );
        assert_eq!(result, expected);
    }

    /// Test that the correct next update time is calculated for the case when there are multiple,
    /// consecutive update check failures.
    ///
    /// TODO:  The current test setup is for a first-attempt-after-startup, but that doesn't align
    ///        with the consecutive test failures (there would need to be a next_update_time)
    #[test]
    fn test_compute_next_update_time_after_many_consecutive_failures() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        // The current context:
        //   - the last update was in the past
        //   - there are 4 failed update checks, which moves the policy from fast retries to slow
        let last_update_time = now - Duration::from_secs(10);
        let schedule = UpdateCheckSchedule::builder().last_time(last_update_time).build();
        let protocol_state =
            ProtocolState { consecutive_failed_update_checks: 4, ..Default::default() };
        // Set up the state for this check:
        //  - the time is "now"
        let policy_data = FuchsiaUpdatePolicyData::builder().time(now).build();
        // Execute the policy check
        let result =
            FuchsiaPolicy::compute_next_update_time(&policy_data, &[], &schedule, &protocol_state);
        // Confirm that:
        //  - the computed next update time is a standard poll interval from now (only monotonic).
        let expected = CheckTiming::builder().time(now.mono + PERIODIC_INTERVAL).build();
        debug_print_check_timing_test_data(
            "many failures",
            expected,
            result,
            last_update_time,
            now,
        );
        assert_eq!(result, expected);
    }

    /// Test that the correct next update time is calculated when there is a server-dictated poll
    /// interval in effect.
    #[test]
    fn test_compute_next_update_time_uses_server_dictated_poll_interval_if_present() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        // The current context:
        //   - the last update was recently in the past
        //   - there is no planned next update time
        // TODO: This should be changed to there being a next_update_time (which is in the past, and
        //       likely to be "before" the last_update_check by the duration it takes to perform an
        //       update check)
        let schedule =
            UpdateCheckSchedule::builder().last_time(now - Duration::from_secs(1234)).build();
        let server_dictated_poll_interval = Duration::from_secs(5678);
        let protocol_state = ProtocolState {
            server_dictated_poll_interval: Some(server_dictated_poll_interval),
            ..Default::default()
        };
        // Set up the state for this check:
        //  - the time is "now"
        let policy_data = FuchsiaUpdatePolicyData::builder().time(now).build();
        // Execute the policy check
        let result =
            FuchsiaPolicy::compute_next_update_time(&policy_data, &[], &schedule, &protocol_state);
        // Confirm that:
        //  - the last update check time is unchanged.
        //  - the computed next update time is a server-dictated poll interval from now.
        let expected = CheckTiming::builder()
            .time(schedule.last_update_time.unwrap() + server_dictated_poll_interval)
            .build();
        debug_print_check_timing_test_data("server dictated", expected, result, None, now);
        assert_eq!(result, expected);
    }

    /// Test that the correct next update time is calculated using policy config.
    #[test]
    fn test_compute_next_update_time_uses_policy_config() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        // The current context:
        //   - the last update was recently in the past
        let last_update_time = now - Duration::from_secs(1234);
        let schedule = UpdateCheckSchedule::builder().last_time(last_update_time).build();
        // Set up the state for this check:
        //  - custom policy config
        let periodic_interval = Duration::from_secs(9999);
        let policy_config = PolicyConfig { periodic_interval, ..PolicyConfig::default() };
        //  - the time is "now"
        let policy_data =
            FuchsiaUpdatePolicyData::builder().config(policy_config).time(now).build();
        // Execute the policy check.
        let result = FuchsiaPolicy::compute_next_update_time(
            &policy_data,
            &[],
            &schedule,
            &ProtocolState::default(),
        );
        // Confirm that:
        //  - the policy-computed next update time is `periodic_interval` from the last.
        let expected = CheckTiming::builder()
            .time(schedule.last_update_time.unwrap() + periodic_interval)
            .build();
        debug_print_check_timing_test_data(
            "policy config",
            expected,
            result,
            last_update_time,
            now,
        );
        assert_eq!(result, expected);
    }

    // Test that an update check is allowed after the next_update_time has passed.
    #[test]
    fn test_update_check_allowed_after_next_update_time_is_ok() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        // The current context:
        //   - the last update was far in the past
        //   - the next update time is just in the past
        let last_update_time = now - PERIODIC_INTERVAL - Duration::from_secs(1);
        let next_update_time = last_update_time + PERIODIC_INTERVAL;
        let schedule = UpdateCheckSchedule::builder()
            .last_time(last_update_time)
            .next_timing(CheckTiming::builder().time(next_update_time).build())
            .build();
        // Set up the state for this check:
        //  - the time is "now"
        //  - the check options are at normal defaults (scheduled background check)
        let policy_data = FuchsiaUpdatePolicyData::builder().time(now).build();
        let check_options = CheckOptions::default();
        // Execute the policy check
        let result = FuchsiaPolicy::update_check_allowed(
            &policy_data,
            &[],
            &schedule,
            &ProtocolState::default(),
            &check_options,
        );
        // Confirm that:
        //  - the decision is Ok
        //  - the source is the same as the check_options' source
        let expected = CheckDecision::Ok(RequestParams {
            source: check_options.source,
            use_configured_proxies: true,
        });
        assert_eq!(result, expected);
    }

    // Test that an update check is not allowed before the next_update_time.
    #[test]
    fn test_update_check_allowed_before_next_update_time_is_too_soon() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        // The current context:
        //   - the last update was far in the past
        //   - the next update time is in the future
        let last_update_time = now - PERIODIC_INTERVAL + Duration::from_secs(1);
        let next_update_time = last_update_time + PERIODIC_INTERVAL;
        let schedule = UpdateCheckSchedule::builder()
            .last_time(last_update_time)
            .next_timing(CheckTiming::builder().time(next_update_time).build())
            .build();
        // Set up the state for this check:
        //  - the time is "now"
        //  - the check options are at normal defaults (scheduled background check)
        let policy_data = FuchsiaUpdatePolicyData::builder().time(now).build();
        // Execute the policy check
        let result = FuchsiaPolicy::update_check_allowed(
            &policy_data,
            &[],
            &schedule,
            &ProtocolState::default(),
            &CheckOptions::default(),
        );
        // Confirm that:
        //  - the decision is "TooSoon"
        assert_eq!(result, CheckDecision::TooSoon);
    }

    // Test that update_can_start returns Ok (always)
    #[test]
    fn test_update_can_start_always_ok() {
        let policy_data = FuchsiaUpdatePolicyData::builder()
            .use_timesource(&MockTimeSource::new_from_now())
            .build();
        let result = FuchsiaPolicy::update_can_start(&policy_data, &StubPlan);
        assert_eq!(result, UpdateDecision::Ok);
    }

    /// Prints a bunch of context about a test for proper CheckTiming generation, to stderr, for
    /// capturing as part of a failed test.
    fn debug_print_check_timing_test_data<T>(
        log_tag: &str,
        expected: CheckTiming,
        result: CheckTiming,
        last_update_time: T,
        now: ComplexTime,
    ) where
        T: Into<Option<PartialComplexTime>>,
    {
        if result != expected {
            let last_update_time = last_update_time.into();
            eprintln!(
                "[{}] last_update_time: {}",
                log_tag,
                omaha_client::common::PrettyOptionDisplay(last_update_time)
            );
            eprintln!("[{}]              now: {}", log_tag, now);
            eprintln!("[{}] expected: {}", log_tag, expected);
            eprintln!("[{}]   result: {}", log_tag, result);
            if let Some(last_update_time) = last_update_time {
                if let Some(last_update_time) = last_update_time.checked_to_system_time() {
                    eprintln!(
                        "[{}] expected wall duration (from last_update_time): {:?}",
                        log_tag,
                        expected
                            .time
                            .checked_to_system_time()
                            .map(|t| t.duration_since(last_update_time))
                    );
                    eprintln!(
                        "[{}]   result wall duration (from last_update_time): {:?}",
                        log_tag,
                        result
                            .time
                            .checked_to_system_time()
                            .map(|t| t.duration_since(last_update_time))
                    );
                }
                eprintln!(
                    "[{}]                    result wall duration (from now): {:?}",
                    log_tag,
                    result.time.checked_to_system_time().map(|t| t.duration_since(now.into()))
                );
                if let Some(last_update_time) = last_update_time.checked_to_instant() {
                    eprintln!(
                        "[{}] expected mono duration (from last_update_time): {:?}",
                        log_tag,
                        expected
                            .time
                            .checked_to_instant()
                            .map(|t| t.duration_since(last_update_time))
                    );
                    eprintln!(
                        "[{}]   result mono duration (from last_update_time): {:?}",
                        log_tag,
                        result
                            .time
                            .checked_to_instant()
                            .map(|t| t.duration_since(last_update_time))
                    );
                }
            }
            eprintln!(
                "[{}]                    result mono duration (from now): {:?}",
                log_tag,
                result.time.checked_to_instant().map(|t| t.duration_since(now.into()))
            );
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_ui_activity_state_default_unknown() {
        let policy_engine = FuchsiaPolicyEngineBuilder.time_source(StandardTimeSource).build();
        let ui_activity = policy_engine.ui_activity.get();
        assert_eq!(ui_activity.state, State::Unknown);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_ui_activity_watch_state() {
        let policy_engine = FuchsiaPolicyEngineBuilder.time_source(StandardTimeSource).build();
        let ui_activity = Rc::clone(&policy_engine.ui_activity);
        assert_eq!(ui_activity.get().state, State::Unknown);

        let (proxy, mut stream) = create_proxy_and_stream::<ProviderMarker>().unwrap();
        fasync::Task::local(async move {
            watch_ui_activity_impl(&policy_engine.ui_activity, proxy).await.unwrap();
        })
        .detach();

        let ProviderRequest::WatchState { listener, control_handle: _ } =
            stream.next().await.unwrap().unwrap();
        let listener = listener.into_proxy().unwrap();
        listener.on_state_changed(State::Active, 123).await.unwrap();
        assert_eq!(
            ui_activity.get(),
            UiActivityState { state: State::Active, transition_time: zx::Time::from_nanos(123) }
        );
        listener.on_state_changed(State::Idle, 456).await.unwrap();
        assert_eq!(
            ui_activity.get(),
            UiActivityState { state: State::Idle, transition_time: zx::Time::from_nanos(456) }
        );
    }

    #[test]
    fn test_reboot_allowed_interactive() {
        let policy_data = FuchsiaRebootPolicyData::new(UiActivityState {
            state: State::Active,
            transition_time: zx::Time::get(zx::ClockId::Monotonic),
        });
        assert_eq!(
            FuchsiaPolicy::reboot_allowed(
                &policy_data,
                &CheckOptions { source: InstallSource::OnDemand },
            ),
            true
        );
    }

    #[test]
    fn test_reboot_allowed_unknown() {
        let policy_data = FuchsiaRebootPolicyData::new(UiActivityState::new());
        assert_eq!(FuchsiaPolicy::reboot_allowed(&policy_data, &CheckOptions::default()), true);
    }

    #[test]
    fn test_reboot_allowed_idle() {
        let policy_data = FuchsiaRebootPolicyData::new(UiActivityState {
            state: State::Idle,
            transition_time: zx::Time::get(zx::ClockId::Monotonic),
        });
        assert_eq!(FuchsiaPolicy::reboot_allowed(&policy_data, &CheckOptions::default()), true);
    }

    #[test]
    fn test_reboot_allowed_state_too_old() {
        let policy_data = FuchsiaRebootPolicyData::new(UiActivityState {
            state: State::Active,
            transition_time: zx::Time::get(zx::ClockId::Monotonic) - zx::Duration::from_hours(49),
        });
        assert_eq!(FuchsiaPolicy::reboot_allowed(&policy_data, &CheckOptions::default()), true);
    }

    #[test]
    fn test_reboot_not_allowed_when_active() {
        let policy_data = FuchsiaRebootPolicyData::new(UiActivityState {
            state: State::Active,
            transition_time: zx::Time::get(zx::ClockId::Monotonic),
        });
        assert_eq!(FuchsiaPolicy::reboot_allowed(&policy_data, &CheckOptions::default()), false);
    }

    #[test]
    fn test_omaha_client_policy_config_load_from_config_data() {
        let policy_engine = FuchsiaPolicyEngineBuilder
            .time_source(StandardTimeSource)
            .load_config_from("/config/data")
            .build();
        assert_eq!(
            policy_engine.config,
            PolicyConfig {
                periodic_interval: Duration::from_secs(42 * 60),
                startup_delay: Duration::from_secs(43),
                retry_delay: Duration::from_secs(301),
            }
        );
    }

    #[test]
    fn test_policy_config_default() {
        let default_policy_config = PolicyConfig {
            periodic_interval: PERIODIC_INTERVAL,
            startup_delay: STARTUP_DELAY,
            retry_delay: RETRY_DELAY,
        };
        assert_eq!(PolicyConfig::default(), default_policy_config);
        assert_eq!(PolicyConfig::from(None), default_policy_config);
        assert_eq!(PolicyConfig::from(Some(PolicyConfigJson::default())), default_policy_config);
    }

    #[test]
    fn test_policy_config_partial_default() {
        assert_eq!(
            PolicyConfig::from(Some(PolicyConfigJson {
                startup_delay_seconds: Some(123),
                ..PolicyConfigJson::default()
            })),
            PolicyConfig {
                periodic_interval: PERIODIC_INTERVAL,
                startup_delay: Duration::from_secs(123),
                retry_delay: RETRY_DELAY,
            }
        );
    }
}
