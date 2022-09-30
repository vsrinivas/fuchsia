// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    install_plan::FuchsiaInstallPlan, installer::InstallResult, metrics::CobaltMetricsReporter,
    timer::FuchsiaTimer,
};
use anyhow::{anyhow, Context as _, Error};
use async_utils::hanging_get::client::HangingGetStream;
use fidl_fuchsia_input_interaction::{NotifierMarker, NotifierProxy, State};
use fidl_fuchsia_update::{CommitStatusProviderMarker, CommitStatusProviderProxy};
use fidl_fuchsia_update_config::{OptOutMarker, OptOutPreference, OptOutProxy};
use fidl_fuchsia_update_ext::{query_commit_status, CommitStatus};
use fuchsia_async::TimeoutExt;
use fuchsia_component::client::connect_to_protocol;
use futures::{future::BoxFuture, future::FutureExt, lock::Mutex, prelude::*};
use omaha_client::{
    common::{App, CheckOptions, CheckTiming, ProtocolState, UpdateCheckSchedule},
    policy::{CheckDecision, Policy, PolicyEngine, UpdateDecision},
    protocol::request::InstallSource,
    request_builder::RequestParams,
    time::{ComplexTime, PartialComplexTime, TimeSource, Timer},
    unless::Unless,
};
use std::{
    convert::{TryFrom, TryInto},
    sync::Arc,
    time::Duration,
};
use tracing::{error, info, warn};

mod rate_limiter;
use rate_limiter::UpdateCheckRateLimiter;

/// Allow reboot if it's been more than 48 hours since waiting to reboot.
const VALID_REBOOT_DURATION: Duration = Duration::from_secs(48 * 60 * 60);
const MAX_FUZZ_PERCENTAGE_RANGE: u32 = 200;

/// The policy implementation for Fuchsia.
struct FuchsiaPolicy;

impl Policy for FuchsiaPolicy {
    type ComputeNextUpdateTimePolicyData = FuchsiaComputeNextUpdateTimePolicyData;
    type UpdateCheckAllowedPolicyData = FuchsiaUpdateCheckAllowedPolicyData;
    type UpdateCanStartPolicyData = FuchsiaUpdateCanStartPolicyData;
    type RebootPolicyData = FuchsiaRebootPolicyData;
    type InstallPlan = FuchsiaInstallPlan;

    fn compute_next_update_time(
        policy_data: &Self::ComputeNextUpdateTimePolicyData,
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

        let interval = match policy_data.interval_fuzz_seed {
            Some(fuzz_seed) => {
                fuzz_interval(interval, fuzz_seed, policy_data.config.fuzz_percentage_range)
            }
            _ => interval,
        };

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
                            //      (fxbug.dev/39970)
                            // .time(last_wall_time.complete_with(policy_data.current_time) + interval)
                            .time(policy_data.current_time + policy_data.config.startup_delay)
                            .minimum_wait(policy_data.config.startup_delay)
                            .build()
                    }

                    // In all other cases (there is at least a monotonic time), add the fuzz_interval to
                    // the last time and use that.
                    last_update_time => {
                        info!("Using Standard logic.");
                        CheckTiming::builder()
                            .time(last_update_time + interval)
                            .minimum_wait(policy_data.config.retry_delay)
                            .build()
                    }
                }
            }
        }
    }

    fn update_check_allowed(
        policy_data: &Self::UpdateCheckAllowedPolicyData,
        _apps: &[App],
        scheduling: &UpdateCheckSchedule,
        _protocol_state: &ProtocolState,
        check_options: &CheckOptions,
    ) -> CheckDecision {
        info!(
            "FuchsiaPolicy::update_check_allowed with {:?} and {:?} for {:?}",
            scheduling, policy_data, check_options
        );
        if policy_data.update_check_rate_limiter.should_rate_limit(policy_data.current_time.mono) {
            return CheckDecision::ThrottledByPolicy;
        }
        let disable_updates = match policy_data.opt_out_preference {
            OptOutPreference::AllowAllUpdates => false,
            OptOutPreference::AllowOnlySecurityUpdates => true,
        };
        // Always allow update check initiated by a user.
        if check_options.source == InstallSource::OnDemand {
            CheckDecision::Ok(RequestParams {
                source: InstallSource::OnDemand,
                use_configured_proxies: true,
                disable_updates,
                offer_update_if_same_version: false,
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
                            disable_updates,
                            offer_update_if_same_version: false,
                        })
                    } else {
                        CheckDecision::TooSoon
                    }
                }
            }
        }
    }

    fn update_can_start(
        policy_data: &Self::UpdateCanStartPolicyData,
        _proposed_install_plan: &Self::InstallPlan,
    ) -> UpdateDecision {
        if policy_data.commit_status == Some(CommitStatus::Committed) {
            UpdateDecision::Ok
        } else {
            UpdateDecision::DeferredByPolicy
        }
    }

    /// Is reboot allowed right now.
    fn reboot_allowed(policy_data: &Self::RebootPolicyData, check_options: &CheckOptions) -> bool {
        if policy_data.urgent_update {
            fuchsia_syslog::fx_log_info!("Reboot reason: urgent update triggered!");
        }

        check_options.source == InstallSource::OnDemand
            || (policy_data.allow_reboot_when_idle
                && policy_data.ui_activity.state != State::Active)
            || (policy_data
                .current_time
                .is_after_or_eq_any(policy_data.last_reboot_time + VALID_REBOOT_DURATION))
            || policy_data.urgent_update
    }

    fn reboot_needed(install_plan: &Self::InstallPlan) -> bool {
        install_plan.is_system_update()
    }
}

// fuzz_interval deterministically fuzzes `interval` into the range
// [(1-pct/2)*interval, (1+pct/2)*interval]. For example, a `fuzz_percentage_range`
// value of 100 (corresponding to a pct value of 1) would fuzz a duration of 10s to
// [5s, 15s]. Where in this range it falls depends on the `interval_fuzz_seed`, which
// (in this example) would always be 5s for a value of 0, and always 15s for a value
// of u64::MAX.
//
// This was previously implemented using IEE754 floats, which proved to have
// error bounds that made reliable testing difficult. This implementation uses
// integer arithmetic, which, while imprecise, is at least predictably so. To
// avoid loss of sub-second precision, the formula here is applied to a
// nanosecond value.
//
// The tradeoff is that we cannot reliably fuzz durations longer than about 2^55.7
// nanoseconds -- a period of about 21.5 months. That seems like a fine tradeoff:
// this function aims to solve thundering herd problems -- which shouldn't occur
// on such large timescales (famous last words) -- and expects to fuzz much shorter
// intervals within smaller bounds.
fn fuzz_interval(
    interval: Duration,
    interval_fuzz_seed: u64,
    fuzz_percentage_range: u32,
) -> Duration {
    // Percentage range must be clamped as the subtraction in the numerator can result
    // in integer overflow.
    if fuzz_percentage_range > MAX_FUZZ_PERCENTAGE_RANGE {
        warn!("Supplied fuzz range too wide: {:?}", interval);
        return interval;
    }

    const M: u128 = u64::MAX as u128;
    let (d, s, p) =
        (interval.as_nanos(), interval_fuzz_seed as u128, fuzz_percentage_range as u128);

    let numerator = (MAX_FUZZ_PERCENTAGE_RANGE as u128) * M + 2 * p * s - p * M;
    if let Some(nanos) = d.checked_mul(numerator) {
        let nanos = nanos / (MAX_FUZZ_PERCENTAGE_RANGE as u128) / M;
        return Duration::new((nanos / 1_000_000_000) as u64, (nanos % 1_000_000_000) as u32);
    }

    interval
}

/// FuchsiaPolicyEngine gathers the current time and other current system state, handing it off to
/// the FuchsiaPolicy as the PolicyData.
#[derive(Debug)]
pub struct FuchsiaPolicyEngine<T> {
    time_source: T,
    metrics: CobaltMetricsReporter,
    // Whether the device is in active use.
    ui_activity: Arc<Mutex<UiActivityState>>,
    config: PolicyConfig,
    waiting_for_reboot_time: Option<ComplexTime>,
    update_check_rate_limiter: UpdateCheckRateLimiter,
    commit_status: Arc<Mutex<Option<CommitStatus>>>,
}

pub struct FuchsiaPolicyEngineBuilder {
    config: PolicyConfig,
}
pub struct FuchsiaPolicyEngineBuilderWithTime<T> {
    config: PolicyConfig,
    time_source: T,
}
pub struct FuchsiaPolicyEngineBuilderWithTimeAndMetrics<T> {
    config: PolicyConfig,
    time_source: T,
    metrics: CobaltMetricsReporter,
}

impl FuchsiaPolicyEngineBuilder {
    pub fn new(config: impl Into<PolicyConfig>) -> FuchsiaPolicyEngineBuilder {
        FuchsiaPolicyEngineBuilder { config: config.into() }
    }
    pub fn new_from_args() -> FuchsiaPolicyEngineBuilder {
        let policy_config: PolicyConfig =
            omaha_client_structured_config::Config::take_from_startup_handle()
                .try_into()
                .expect("Invalid config arguments.");
        FuchsiaPolicyEngineBuilder::new(policy_config)
    }
    pub fn time_source<T>(self, time_source: T) -> FuchsiaPolicyEngineBuilderWithTime<T>
    where
        T: TimeSource + Clone,
    {
        FuchsiaPolicyEngineBuilderWithTime { config: self.config, time_source }
    }
}
impl<T> FuchsiaPolicyEngineBuilderWithTime<T> {
    pub fn metrics_reporter(
        self,
        metrics: CobaltMetricsReporter,
    ) -> FuchsiaPolicyEngineBuilderWithTimeAndMetrics<T> {
        FuchsiaPolicyEngineBuilderWithTimeAndMetrics {
            config: self.config,
            time_source: self.time_source,
            metrics,
        }
    }
    #[cfg(test)]
    fn nop_metrics_reporter(self) -> FuchsiaPolicyEngineBuilderWithTimeAndMetrics<T> {
        let (metrics, receiver) = CobaltMetricsReporter::new_mock();

        // Send the metrics to an open receiver to avoid logspam.
        fuchsia_async::Task::spawn(async move { receiver.for_each(|_| async move {}).await })
            .detach();

        self.metrics_reporter(metrics)
    }
}
impl<T> FuchsiaPolicyEngineBuilderWithTimeAndMetrics<T> {
    /// Override the PolicyConfig periodic interval with a different value.
    pub fn periodic_interval(mut self, periodic_interval: Duration) -> Self {
        self.config.periodic_interval = periodic_interval;
        self
    }

    pub fn build(self) -> FuchsiaPolicyEngine<T> {
        FuchsiaPolicyEngine {
            time_source: self.time_source,
            metrics: self.metrics,
            ui_activity: Arc::new(Mutex::new(UiActivityState::new())),
            config: self.config,
            waiting_for_reboot_time: None,
            update_check_rate_limiter: UpdateCheckRateLimiter::new(),
            commit_status: Arc::new(Mutex::new(None)),
        }
    }
}

impl<T> PolicyEngine for FuchsiaPolicyEngine<T>
where
    T: TimeSource + Clone + Send + Sync,
{
    type TimeSource = T;
    type InstallResult = InstallResult;
    type InstallPlan = FuchsiaInstallPlan;

    fn time_source(&self) -> &Self::TimeSource {
        &self.time_source
    }

    fn compute_next_update_time<'a>(
        &'a mut self,
        apps: &'a [App],
        scheduling: &'a UpdateCheckSchedule,
        protocol_state: &'a ProtocolState,
    ) -> BoxFuture<'a, CheckTiming> {
        async move {
            let policy_data =
                FuchsiaComputeNextUpdateTimePolicyData::from_policy_engine(self).await;

            FuchsiaPolicy::compute_next_update_time(&policy_data, apps, scheduling, protocol_state)
        }
        .boxed()
    }

    fn update_check_allowed<'a>(
        &'a mut self,
        apps: &'a [App],
        scheduling: &'a UpdateCheckSchedule,
        protocol_state: &'a ProtocolState,
        check_options: &'a CheckOptions,
    ) -> BoxFuture<'a, CheckDecision> {
        async move {
            let policy_data = FuchsiaUpdateCheckAllowedPolicyData::from_policy_engine(self).await;

            let decision = FuchsiaPolicy::update_check_allowed(
                &policy_data,
                apps,
                scheduling,
                protocol_state,
                check_options,
            );
            if let CheckDecision::Ok(_) = &decision {
                self.update_check_rate_limiter.add_time(policy_data.current_time.mono);

                self.metrics.report_update_check_opt_out_preference(policy_data.opt_out_preference);
            }
            decision
        }
        .boxed()
    }

    fn update_can_start<'a>(
        &'a mut self,
        proposed_install_plan: &'a Self::InstallPlan,
    ) -> BoxFuture<'a, UpdateDecision> {
        async move {
            let data = FuchsiaUpdateCanStartPolicyData::from_policy_engine(self).await;

            FuchsiaPolicy::update_can_start(&data, proposed_install_plan)
        }
        .boxed()
    }

    fn reboot_allowed<'a>(
        &'a mut self,
        check_options: &'a CheckOptions,
        install_result: &'a Self::InstallResult,
    ) -> BoxFuture<'a, bool> {
        async move {
            if self.waiting_for_reboot_time.is_none() {
                self.waiting_for_reboot_time = Some(self.time_source.now());
            }

            FuchsiaPolicy::reboot_allowed(
                &FuchsiaRebootPolicyData::new(
                    *self.ui_activity.lock().await,
                    self.time_source.now(),
                    self.waiting_for_reboot_time.unwrap(),
                    self.config.allow_reboot_when_idle,
                    install_result.urgent_update,
                ),
                check_options,
            )
        }
        .boxed()
    }

    fn reboot_needed<'a>(&'a mut self, install_plan: &'a Self::InstallPlan) -> BoxFuture<'a, bool> {
        let decision = FuchsiaPolicy::reboot_needed(install_plan);
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
        if !self.config.allow_reboot_when_idle {
            info!("not watching ui.activity since the product will handle reboots");
            return futures::future::ready(()).left_future();
        }

        let ui_activity = Arc::clone(&self.ui_activity);
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
        .right_future()
    }

    pub fn get_config(&self) -> &PolicyConfig {
        &self.config
    }
}

async fn watch_ui_activity(ui_activity: &Arc<Mutex<UiActivityState>>) -> Result<(), Error> {
    let notifier_proxy = connect_to_protocol::<NotifierMarker>()?;
    watch_ui_activity_impl(ui_activity, notifier_proxy).await
}

async fn watch_ui_activity_impl(
    ui_activity: &Arc<Mutex<UiActivityState>>,
    notifier_proxy: NotifierProxy,
) -> Result<(), Error> {
    let mut watch_activity_state_stream =
        HangingGetStream::new(notifier_proxy, NotifierProxy::watch_state);

    while let Some(state) = watch_activity_state_stream.try_next().await? {
        *ui_activity.lock().await = UiActivityState { state };
    }
    Ok(())
}

/// Queries the user's update opt-out preference, defaulting to
/// [`OptOutPreference::AllowAllUpdates`] if not set or on error.
async fn query_opt_out_preference<ProviderFn>(provider_fn: ProviderFn) -> OptOutPreference
where
    ProviderFn: FnOnce() -> Result<OptOutProxy, Error>,
{
    // Area owners suggested 1 second as a reasonable time to expect an answer by.
    // Wait a few times that before assuming the default to be extra sure the request is stuck.
    const TIMEOUT: Duration = Duration::from_secs(5);

    async move { provider_fn().ok()?.get().await.ok() }
        .on_timeout(TIMEOUT, || {
            // Prevent unexpected hangs from blocking updates.
            error!("Timed out reading update opt-out preference.");
            None
        })
        .await
        .unwrap_or_else(|| {
            info!("fuchsia.update.config.OptOut provider not present on this product. Using default auto-update preference.");

            OptOutPreference::AllowAllUpdates
        })
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
struct UiActivityState {
    state: State,
}

impl UiActivityState {
    fn new() -> Self {
        Self { state: State::Invalid }
    }
}

#[derive(Clone, Debug)]
struct FuchsiaComputeNextUpdateTimePolicyData {
    current_time: ComplexTime,
    config: PolicyConfig,
    interval_fuzz_seed: Option<u64>,
}

impl FuchsiaComputeNextUpdateTimePolicyData {
    async fn from_policy_engine<T: TimeSource>(policy_engine: &FuchsiaPolicyEngine<T>) -> Self {
        Self {
            current_time: policy_engine.time_source.now(),
            config: policy_engine.config.clone(),
            interval_fuzz_seed: Some(rand::random()),
        }
    }
}

#[derive(Clone, Debug)]
struct FuchsiaUpdateCheckAllowedPolicyData {
    current_time: ComplexTime,
    update_check_rate_limiter: UpdateCheckRateLimiter,
    opt_out_preference: OptOutPreference,
}

impl FuchsiaUpdateCheckAllowedPolicyData {
    async fn from_policy_engine<T: TimeSource>(policy_engine: &FuchsiaPolicyEngine<T>) -> Self {
        let opt_out_preference =
            query_opt_out_preference(connect_to_protocol::<OptOutMarker>).await;

        Self {
            current_time: policy_engine.time_source.now(),
            update_check_rate_limiter: policy_engine.update_check_rate_limiter.clone(),
            opt_out_preference,
        }
    }
}

struct FuchsiaUpdateCanStartPolicyData {
    commit_status: Option<CommitStatus>,
}

impl FuchsiaUpdateCanStartPolicyData {
    fn from_policy_engine<T: TimeSource>(
        policy_engine: &FuchsiaPolicyEngine<T>,
    ) -> impl Future<Output = Self> + 'static {
        let engine_commit_status = Arc::clone(&policy_engine.commit_status);

        async move {
            let commit_status = match query_commit_status_and_update_status(
                connect_to_protocol::<CommitStatusProviderMarker>,
                &mut *engine_commit_status.lock().await,
            )
            .await
            {
                Ok(status) => Some(status),
                Err(e) => {
                    error!("got error with query_commit_status: {:#}", anyhow!(e));
                    None
                }
            };
            Self { commit_status }
        }
    }
}

/// Queries the commit status and updates the value in `engine_commit_status`.
async fn query_commit_status_and_update_status<ProviderFn>(
    provider_fn: ProviderFn,
    engine_commit_status: &mut Option<CommitStatus>,
) -> Result<CommitStatus, Error>
where
    ProviderFn: FnOnce() -> Result<CommitStatusProviderProxy, Error>,
{
    // If we're already committed, no need to do additional work.
    if engine_commit_status.as_ref() == Some(&CommitStatus::Committed) {
        return Ok(CommitStatus::Committed);
    }

    let provider = provider_fn().context("while connecting to commit status provider")?;
    query_commit_status(&provider).await.map(|status| {
        engine_commit_status.replace(status);
        status
    })
}

#[derive(Clone, Debug)]
struct FuchsiaRebootPolicyData {
    ui_activity: UiActivityState,
    current_time: ComplexTime,
    last_reboot_time: ComplexTime,
    allow_reboot_when_idle: bool,
    urgent_update: bool,
}

impl FuchsiaRebootPolicyData {
    fn new(
        ui_activity: UiActivityState,
        current_time: ComplexTime,
        last_reboot_time: ComplexTime,
        allow_reboot_when_idle: bool,
        urgent_update: bool,
    ) -> Self {
        Self { ui_activity, current_time, last_reboot_time, allow_reboot_when_idle, urgent_update }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PolicyConfig {
    pub periodic_interval: Duration,
    pub startup_delay: Duration,
    pub retry_delay: Duration,
    pub allow_reboot_when_idle: bool,
    pub fuzz_percentage_range: u32,
}

impl TryFrom<omaha_client_structured_config::Config> for PolicyConfig {
    type Error = &'static str;
    fn try_from(c: omaha_client_structured_config::Config) -> Result<Self, Self::Error> {
        let policy_config = Self {
            periodic_interval: Duration::from_secs((c.periodic_interval_minutes * 60).into()),
            startup_delay: Duration::from_secs(c.startup_delay_seconds.into()),
            retry_delay: Duration::from_secs(c.retry_delay_seconds.into()),
            allow_reboot_when_idle: c.allow_reboot_when_idle,
            fuzz_percentage_range: c.fuzz_percentage_range.into(),
        };
        if policy_config.fuzz_percentage_range >= MAX_FUZZ_PERCENTAGE_RANGE {
            return Err("fuzz_percentage_range must be < 200%");
        }
        Ok(policy_config)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;
    use cobalt_client::traits::AsEventCode;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_input_interaction::NotifierRequest;
    use fidl_fuchsia_update::{CommitStatusProviderMarker, CommitStatusProviderRequest};
    use fidl_fuchsia_update_config::OptOutRequest;
    use fuchsia_async as fasync;
    use fuchsia_backoff::retry_or_last_error;
    use fuchsia_zircon::{self as zx, Peered};
    use omaha_client::time::{ComplexTime, MockTimeSource, StandardTimeSource, TimeSource};
    use proptest::prelude::*;
    use std::iter::repeat;
    use std::sync::atomic::{AtomicU8, Ordering};
    use std::{collections::VecDeque, time::Instant};
    use zx::HandleBased;

    // We do periodic update check roughly every hour.
    const PERIODIC_INTERVAL_FOR_TEST: Duration = Duration::from_secs(60 * 60);
    // Wait at least one minute before checking for updates after startup.
    const STARTUP_DELAY_FOR_TEST: Duration = Duration::from_secs(60);
    // Wait 5 minutes before retrying after failed update checks.
    const RETRY_DELAY_FOR_TEST: Duration = Duration::from_secs(5 * 60);
    // Default value for `fuzz_percentage_range`, see `fuzz_interval` for details.
    const DEFAULT_FUZZ_PERCENTAGE_RANGE_FOR_TEST: u32 = 25;

    // test-only default.
    impl Default for PolicyConfig {
        fn default() -> Self {
            Self {
                periodic_interval: PERIODIC_INTERVAL_FOR_TEST,
                startup_delay: STARTUP_DELAY_FOR_TEST,
                retry_delay: RETRY_DELAY_FOR_TEST,
                allow_reboot_when_idle: true,
                fuzz_percentage_range: DEFAULT_FUZZ_PERCENTAGE_RANGE_FOR_TEST,
            }
        }
    }

    #[derive(Debug)]
    struct ComputeNextUpdateTimePolicyDataBuilder {
        current_time: ComplexTime,
        config: PolicyConfig,
        interval_fuzz_seed: Option<u64>,
    }

    impl ComputeNextUpdateTimePolicyDataBuilder {
        fn new(current_time: ComplexTime) -> Self {
            Self { current_time, config: PolicyConfig::default(), interval_fuzz_seed: None }
        }

        /// Set the `config` explicitly from a given PolicyConfig.
        fn config(self, config: PolicyConfig) -> Self {
            Self { config, ..self }
        }

        /// Set the `interval_fuzz_seed` explicitly from a given number.
        fn interval_fuzz_seed(self, interval_fuzz_seed: Option<u64>) -> Self {
            Self { interval_fuzz_seed, ..self }
        }
        fn build(self) -> FuchsiaComputeNextUpdateTimePolicyData {
            FuchsiaComputeNextUpdateTimePolicyData {
                current_time: self.current_time,
                config: self.config,
                interval_fuzz_seed: self.interval_fuzz_seed,
            }
        }
    }

    #[derive(Debug)]
    struct UpdateCheckAllowedPolicyDataBuilder {
        current_time: ComplexTime,
        recent_update_check_times: VecDeque<Instant>,
        opt_out_preference: OptOutPreference,
    }

    impl UpdateCheckAllowedPolicyDataBuilder {
        fn new(current_time: ComplexTime) -> Self {
            Self {
                current_time,
                recent_update_check_times: VecDeque::new(),
                opt_out_preference: OptOutPreference::AllowAllUpdates,
            }
        }
        /// Set the `recent_update_check_times` explicitly from a given VecDeque<Instant>.
        fn recent_update_check_times(self, recent_update_check_times: VecDeque<Instant>) -> Self {
            Self { recent_update_check_times, ..self }
        }

        /// Set the `opt_out_preference` explicitly from a given OptOutPreference.
        fn opt_out_preference(self, opt_out_preference: OptOutPreference) -> Self {
            Self { opt_out_preference, ..self }
        }

        fn build(self) -> FuchsiaUpdateCheckAllowedPolicyData {
            FuchsiaUpdateCheckAllowedPolicyData {
                current_time: self.current_time,
                update_check_rate_limiter: UpdateCheckRateLimiter::with_recent_update_check_times(
                    self.recent_update_check_times,
                ),
                opt_out_preference: self.opt_out_preference,
            }
        }
    }

    prop_compose! {
        // N.B. not using Arbitrary impl for duration here due to a small potential for
        // flake issues. Cf https://github.com/AltSysrq/proptest/issues/221.
        fn arb_fuzzable_duration()(secs: u64, nsec in 0..1_000_000_000u32) -> Duration {
            Duration::new(secs, nsec)
        }
    }

    proptest! {
        #[test]
        fn test_fuchsia_update_policy_data_builder_doesnt_panic(interval_fuzz_seed: u64) {
            let mock_time = MockTimeSource::new_from_now();
            let now = mock_time.now();
            ComputeNextUpdateTimePolicyDataBuilder::new(now)
                .interval_fuzz_seed(Some(interval_fuzz_seed))
                .build();
        }


        #[test]
        fn test_compute_next_update_time(interval_fuzz_seed: u64) {
            // TODO(fxbug.dev/58338) derive arbitrary on UpdateCheckSchedule, FuchsiaUpdatePolicyData
            let mock_time = MockTimeSource::new_from_now();
            let now = mock_time.now();
            // The current context:
            //   - the last update was recently in the past
            let last_update_time = now - Duration::from_secs(1234);
            let schedule = UpdateCheckSchedule::builder().last_update_time(last_update_time).build();
            // Set up the state for this check:
            //  - the time is "now"
            let policy_data = ComputeNextUpdateTimePolicyDataBuilder::new(now)
                .interval_fuzz_seed(Some(interval_fuzz_seed))
                .build();
            // Execute the policy check.
            FuchsiaPolicy::compute_next_update_time(
                &policy_data,
                &[],
                &schedule,
                &ProtocolState::default(),
            );
        }

        #[test]
        fn test_fuzz_interval_lower_bounds(interval in arb_fuzzable_duration(),
            interval_fuzz_seed: u64,
            fuzz_percentage_range in 0u32..200u32) {
            let fuzzed_interval = fuzz_interval(interval, interval_fuzz_seed, fuzz_percentage_range).as_nanos();

            let nanos = interval.as_nanos();
            let lower_bound = nanos - (nanos * fuzz_percentage_range as u128 / (MAX_FUZZ_PERCENTAGE_RANGE as u128) );
            assert!(
                fuzzed_interval >= lower_bound,
                "bound exceeded: {} <= {} for interval {:?}, seed {}, and range {}",
                lower_bound,
                fuzzed_interval,
                interval,
                interval_fuzz_seed,
                fuzz_percentage_range,
            );
        }

        #[test]
        fn test_fuzz_interval_upper_bounds(interval in arb_fuzzable_duration(),
            interval_fuzz_seed: u64,
            fuzz_percentage_range in 0u32..200u32) {
            let fuzzed_interval = fuzz_interval(interval, interval_fuzz_seed, fuzz_percentage_range).as_nanos();

            let nanos = interval.as_nanos();
            let upper_bound = nanos + (nanos * fuzz_percentage_range as u128 / (MAX_FUZZ_PERCENTAGE_RANGE as u128) );

            // The upper bound may overflow u64, but this doesn't mean that the interval itself
            // would fuzz above u64. To avoid issues with duplicating the fuzz_interval function
            // into this test, we compare nanos here -- if the function would've overflowed, it
            // returns the original interval, which would still be within the calculated bounds
            // after conversion to nanos.
            assert!(
                fuzzed_interval <= upper_bound,
                "bounds exceeded: {} <= {} for interval {:?}, seed {}, and range {}",
                upper_bound,
                fuzzed_interval,
                interval,
                interval_fuzz_seed,
                fuzz_percentage_range,
            );
        }
    }

    /// The math performed in fuzz_interval can overflow for duration inputs that
    /// approach 2^64 nanoseconds; this test checks to see that's detected and that
    /// the function returns its input unmodified in that case.
    #[test]
    fn test_fuzz_interval_yields_input_for_large_durations() {
        let interval = Duration::from_secs(u64::MAX);
        let fuzzed_interval = fuzz_interval(interval, u64::MAX, 100);
        assert!(
            fuzzed_interval == interval,
            "invariant failed: {:?} != {:?}",
            fuzzed_interval,
            interval
        );
    }

    /// The fuzz_interval function fuzzes a value to a range around itself such that
    /// a range of 100% would return a value bounded to within +-50%. Durations don't
    /// represent negative values, so fuzzing across ranges larger than 200% is
    /// nonsensical. This test checks that invariant.
    #[test]
    fn test_fuzz_interval_rejects_wide_ranges() {
        let interval = Duration::from_secs(100);
        let fuzzed_interval = fuzz_interval(interval, 1, 201);
        assert!(
            interval == fuzzed_interval,
            "invariant failed: {:?} != {:?}",
            fuzzed_interval,
            interval
        );
    }

    /// Ensure that fuzz_interval behaves as expected given deterministic inputs.
    #[test]
    fn test_fuzz_interval_determinism() {
        let d = Duration::from_secs(100);
        assert!(fuzz_interval(d, 0, 0) == d);
        assert!(fuzz_interval(d, 0, 50) == Duration::from_secs(75));
        assert!(fuzz_interval(d, 0, 100) == Duration::from_secs(50));
        assert!(fuzz_interval(d, 0, 200) == Duration::from_secs(0));
        assert!(fuzz_interval(d, u64::MAX, 0) == d);
        assert!(fuzz_interval(d, u64::MAX, 50) == Duration::from_secs(125));
        assert!(fuzz_interval(d, u64::MAX, 100) == Duration::from_secs(150));
        assert!(fuzz_interval(d, u64::MAX, 200) == Duration::from_secs(200));
    }

    /// Test that the correct next update time is calculated for the normal case where a check was
    /// recently done and the next needs to be scheduled.
    #[test]
    fn test_compute_next_update_time_for_normal_operation() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        // The current context:
        //   - the last update was recently in the past
        let last_update_time = now - Duration::from_secs(1234);
        let schedule = UpdateCheckSchedule::builder().last_update_time(last_update_time).build();
        // Set up the state for this check:
        //  - the time is "now"
        let policy_data = ComputeNextUpdateTimePolicyDataBuilder::new(now).build();
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
            .time(schedule.last_update_time.unwrap() + PERIODIC_INTERVAL_FOR_TEST)
            .minimum_wait(RETRY_DELAY_FOR_TEST)
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
        let policy_data = ComputeNextUpdateTimePolicyDataBuilder::new(now).build();
        // Execute the policy check.
        let result = FuchsiaPolicy::compute_next_update_time(
            &policy_data,
            &[],
            &schedule,
            &ProtocolState::default(),
        );
        // Confirm that:
        //  - the policy-computed next update time is a startup delay poll interval from now.
        let expected = CheckTiming::builder()
            .time(now + STARTUP_DELAY_FOR_TEST)
            .minimum_wait(STARTUP_DELAY_FOR_TEST)
            .build();
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
        let schedule = UpdateCheckSchedule::builder().last_update_time(last_update_time).build();
        // Set up the state for this check:
        //  - the time is "now"
        let policy_data = ComputeNextUpdateTimePolicyDataBuilder::new(now).build();
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
            //      (fxbug.dev/39970)
            // .time((
            //     last_update_time.checked_to_system_time().unwrap() + PERIODIC_INTERVAL,
            //     Instant::from(now) + PERIODIC_INTERVAL,
            // ))
            .time(now + STARTUP_DELAY_FOR_TEST)
            .minimum_wait(STARTUP_DELAY_FOR_TEST)
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
        let schedule = UpdateCheckSchedule::builder().last_update_time(last_update_time).build();
        // Set up the state for this check:
        //  - the time is "now"
        let policy_data = ComputeNextUpdateTimePolicyDataBuilder::new(now).build();
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
            //      (fxbug.dev/39970)
            // .time((
            //     last_update_time.checked_to_system_time().unwrap() + PERIODIC_INTERVAL,
            //     Instant::from(now) + PERIODIC_INTERVAL,
            // ))
            .time(now + STARTUP_DELAY_FOR_TEST)
            .minimum_wait(STARTUP_DELAY_FOR_TEST)
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
        let schedule = UpdateCheckSchedule::builder().last_update_time(last_update_time).build();
        let protocol_state =
            ProtocolState { consecutive_failed_update_checks: 1, ..Default::default() };
        // Set up the state for this check:
        //  - the time is "now"
        let policy_data = ComputeNextUpdateTimePolicyDataBuilder::new(now).build();
        // Execute the policy check
        let result =
            FuchsiaPolicy::compute_next_update_time(&policy_data, &[], &schedule, &protocol_state);
        // Confirm that:
        //  - the computed next update time is a retry poll interval from now, on the monotonic
        //    timeline only.
        let expected = CheckTiming::builder().time(now.mono + RETRY_DELAY_FOR_TEST).build();
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
        let schedule = UpdateCheckSchedule::builder().last_update_time(last_update_time).build();
        let protocol_state =
            ProtocolState { consecutive_failed_update_checks: 4, ..Default::default() };
        // Set up the state for this check:
        //  - the time is "now"
        let policy_data = ComputeNextUpdateTimePolicyDataBuilder::new(now).build();
        // Execute the policy check
        let result =
            FuchsiaPolicy::compute_next_update_time(&policy_data, &[], &schedule, &protocol_state);
        // Confirm that:
        //  - the computed next update time is a standard poll interval from now (only monotonic).
        let expected = CheckTiming::builder().time(now.mono + PERIODIC_INTERVAL_FOR_TEST).build();
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
        let schedule = UpdateCheckSchedule::builder()
            .last_update_time(now - Duration::from_secs(1234))
            .build();
        let server_dictated_poll_interval = Duration::from_secs(5678);
        let protocol_state = ProtocolState {
            server_dictated_poll_interval: Some(server_dictated_poll_interval),
            ..Default::default()
        };
        // Set up the state for this check:
        //  - the time is "now"
        let policy_data = ComputeNextUpdateTimePolicyDataBuilder::new(now).build();
        // Execute the policy check
        let result =
            FuchsiaPolicy::compute_next_update_time(&policy_data, &[], &schedule, &protocol_state);
        // Confirm that:
        //  - the last update check time is unchanged.
        //  - the computed next update time is a server-dictated poll interval from now.
        let expected = CheckTiming::builder()
            .time(schedule.last_update_time.unwrap() + server_dictated_poll_interval)
            .minimum_wait(RETRY_DELAY_FOR_TEST)
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
        let schedule = UpdateCheckSchedule::builder().last_update_time(last_update_time).build();
        // Set up the state for this check:
        //  - custom policy config
        let periodic_interval = Duration::from_secs(9999);
        let retry_delay = Duration::from_secs(50);
        let policy_config =
            PolicyConfig { periodic_interval, retry_delay, ..PolicyConfig::default() };
        //  - the time is "now"
        let policy_data =
            ComputeNextUpdateTimePolicyDataBuilder::new(now).config(policy_config).build();
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
            .minimum_wait(retry_delay)
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
        let last_update_time = now - PERIODIC_INTERVAL_FOR_TEST - Duration::from_secs(1);
        let next_update_time = last_update_time + PERIODIC_INTERVAL_FOR_TEST;
        let schedule = UpdateCheckSchedule::builder()
            .last_update_time(last_update_time)
            .next_update_time(CheckTiming::builder().time(next_update_time).build())
            .build();
        // Set up the state for this check:
        //  - the time is "now"
        //  - the check options are at normal defaults (scheduled background check)
        let policy_data = UpdateCheckAllowedPolicyDataBuilder::new(now).build();
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
        //  - disable_updates is set to false (the default)
        let expected = CheckDecision::Ok(RequestParams {
            source: check_options.source,
            use_configured_proxies: true,
            disable_updates: false,
            offer_update_if_same_version: false,
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
        let last_update_time = now - PERIODIC_INTERVAL_FOR_TEST + Duration::from_secs(1);
        let next_update_time = last_update_time + PERIODIC_INTERVAL_FOR_TEST;
        let schedule = UpdateCheckSchedule::builder()
            .last_update_time(last_update_time)
            .next_update_time(CheckTiming::builder().time(next_update_time).build())
            .build();
        // Set up the state for this check:
        //  - the time is "now"
        //  - the check options are at normal defaults (scheduled background check)
        let policy_data = UpdateCheckAllowedPolicyDataBuilder::new(now).build();
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

    // Test that an update check is throttled if update check times exceeds limit in short period.
    #[test]
    fn test_update_check_allowed_throttled() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        // The current context:
        //   - the last update was far in the past
        //   - the next update time is just in the past
        let last_update_time = now - PERIODIC_INTERVAL_FOR_TEST - Duration::from_secs(1);
        let next_update_time = last_update_time + PERIODIC_INTERVAL_FOR_TEST;
        let schedule = UpdateCheckSchedule::builder()
            .last_update_time(last_update_time)
            .next_update_time(CheckTiming::builder().time(next_update_time).build())
            .build();
        // Set up the state for this check:
        //  - the time is "now"
        //  - the check options are at normal defaults (scheduled background check)
        //  - the recent update check interval is shorter than the short period limit
        let recent_update_check_times = [1, 10, 20, 30, 60, 100, 150, 200, 250, 299, 1000]
            .iter()
            .map(|&i| now.mono - Duration::from_secs(i))
            .collect();
        let policy_data = UpdateCheckAllowedPolicyDataBuilder::new(now)
            .recent_update_check_times(recent_update_check_times)
            .build();
        let check_options = CheckOptions::default();
        // Execute the policy check
        let result = FuchsiaPolicy::update_check_allowed(
            &policy_data,
            &[],
            &schedule,
            &ProtocolState::default(),
            &check_options,
        );
        assert_eq!(result, CheckDecision::ThrottledByPolicy);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_check_allowed_update_recent_update_check_times() {
        let mut mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        // The current context:
        //   - the last update was far in the past
        //   - the next update time is just in the past
        let last_update_time = now - PERIODIC_INTERVAL_FOR_TEST - Duration::from_secs(1);
        let next_update_time = last_update_time + PERIODIC_INTERVAL_FOR_TEST;
        let schedule = UpdateCheckSchedule::builder()
            .last_update_time(last_update_time)
            .next_update_time(CheckTiming::builder().time(next_update_time).build())
            .build();
        let mut policy_engine = FuchsiaPolicyEngineBuilder::new(PolicyConfig::default())
            .time_source(mock_time.clone())
            .nop_metrics_reporter()
            .build();
        let check_options = CheckOptions::default();

        for _ in 0..10 {
            let decision = policy_engine
                .update_check_allowed(&[], &schedule, &ProtocolState::default(), &check_options)
                .await;
            assert_eq!(
                decision,
                CheckDecision::Ok(RequestParams {
                    source: check_options.source,
                    use_configured_proxies: true,
                    disable_updates: false,
                    offer_update_if_same_version: false,
                })
            );

            mock_time.advance(Duration::from_secs(10));
        }

        let decision = policy_engine
            .update_check_allowed(&[], &schedule, &ProtocolState::default(), &check_options)
            .await;
        assert_eq!(decision, CheckDecision::ThrottledByPolicy);
        assert_eq!(
            policy_engine.update_check_rate_limiter.get_recent_update_check_times(),
            (0..10)
                .rev()
                .map(|i| now.mono + Duration::from_secs(10 * i))
                .collect::<VecDeque::<_>>()
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_check_allowed_do_not_update_recent_update_check_times_when_not_ok() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        // The current context:
        //   - the last update was recently in the past
        //   - the next update time is in the future
        let last_update_time = now - Duration::from_secs(1);
        let next_update_time = last_update_time + PERIODIC_INTERVAL_FOR_TEST;
        let schedule = UpdateCheckSchedule::builder()
            .last_update_time(last_update_time)
            .next_update_time(CheckTiming::builder().time(next_update_time).build())
            .build();
        let mut policy_engine = FuchsiaPolicyEngineBuilder::new(PolicyConfig::default())
            .time_source(mock_time)
            .nop_metrics_reporter()
            .build();
        let check_options = CheckOptions::default();
        let decision = policy_engine
            .update_check_allowed(&[], &schedule, &ProtocolState::default(), &check_options)
            .await;
        assert_eq!(decision, CheckDecision::TooSoon);
        assert_eq!(
            policy_engine.update_check_rate_limiter.get_recent_update_check_times(),
            VecDeque::from(vec![])
        );
    }

    // Test that an update check sets the disable_updates request param when the user's opt-out
    // preference asks to do so.
    #[test]
    fn test_update_check_allowed_opt_out_disables_updates() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        // The current context:
        //   - the last update was far in the past
        //   - the next update time is just in the past
        let last_update_time = now - PERIODIC_INTERVAL_FOR_TEST - Duration::from_secs(1);
        let next_update_time = last_update_time + PERIODIC_INTERVAL_FOR_TEST;
        let schedule = UpdateCheckSchedule::builder()
            .last_update_time(last_update_time)
            .next_update_time(CheckTiming::builder().time(next_update_time).build())
            .build();
        // Set up the state for this check:
        //  - the time is "now"
        //  - the check options are at normal defaults (scheduled background check)
        let policy_data = UpdateCheckAllowedPolicyDataBuilder::new(now)
            .opt_out_preference(OptOutPreference::AllowOnlySecurityUpdates)
            .build();
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
        //  - disable_updates is set to true
        let expected = CheckDecision::Ok(RequestParams {
            source: check_options.source,
            use_configured_proxies: true,
            disable_updates: true,
            offer_update_if_same_version: false,
        });
        assert_eq!(result, expected);
    }

    // Test that update_check_allowed emits the update_check_opt_out_preference metric when it
    // determines that an update check is allowed.
    #[fasync::run_singlethreaded(test)]
    async fn test_update_check_allowed_emits_opt_out_preference_metric_when_decision_is_ok() {
        let (metrics_reporter, mut metrics) = CobaltMetricsReporter::new_mock();
        let mock_time = MockTimeSource::new_from_now();

        let mut policy_engine = FuchsiaPolicyEngineBuilder::new(PolicyConfig::default())
            .time_source(mock_time.clone())
            .metrics_reporter(metrics_reporter)
            .build();

        // The current context:
        //  - the time is "now"
        //  - the last update was far in the past
        //  - the next update time is just in the past
        //  - the check options are at normal defaults (scheduled background check)
        let now = mock_time.now();
        let last_update_time = now - PERIODIC_INTERVAL_FOR_TEST - Duration::from_secs(1);
        let next_update_time = last_update_time + PERIODIC_INTERVAL_FOR_TEST;
        let schedule = UpdateCheckSchedule::builder()
            .last_update_time(last_update_time)
            .next_update_time(CheckTiming::builder().time(next_update_time).build())
            .build();

        // Execute the policy check
        let result = policy_engine
            .update_check_allowed(
                &[],
                &schedule,
                &ProtocolState::default(),
                &CheckOptions::default(),
            )
            .await;

        // Confirm that:
        //  - the decision is Ok
        //  - the expected metric was logged to cobalt
        assert_matches!(result, CheckDecision::Ok(_));

        let expected_metric = fidl_fuchsia_metrics::MetricEvent {
            metric_id: mos_metrics_registry::UPDATE_CHECK_OPT_OUT_PREFERENCE_MIGRATED_METRIC_ID,
            event_codes: vec![
                mos_metrics_registry::UpdateCheckOptOutPreferenceMigratedMetricDimensionPreference::AllowAllUpdates.as_event_code(),
            ],
            payload: fidl_fuchsia_metrics::MetricEventPayload::Count(1),
        };

        let metric = metrics.next().await.unwrap();
        assert_eq!(metric, expected_metric);
    }

    // Test that update_check_allowed does not emit the update_check_opt_out_preference metric when
    // it determines that an update check is not allowed yet.
    #[fasync::run_singlethreaded(test)]
    async fn test_update_check_allowed_does_not_emit_opt_out_preference_metric_when_not_time() {
        let (metrics_reporter, mut metrics) = CobaltMetricsReporter::new_mock();

        let mut policy_engine = FuchsiaPolicyEngineBuilder::new(PolicyConfig::default())
            .time_source(MockTimeSource::new_from_now())
            .metrics_reporter(metrics_reporter)
            .build();

        // Execute the policy check
        let result = policy_engine
            .update_check_allowed(
                &[],
                &UpdateCheckSchedule::default(),
                &ProtocolState::default(),
                &CheckOptions::default(),
            )
            .await;

        // Confirm that:
        //  - the decision is TooSoon
        //  - no metrics were logged to cobalt
        assert_matches!(result, CheckDecision::TooSoon);

        drop(policy_engine);
        assert_eq!(metrics.next().await, None);
    }

    // Test that update_can_start returns Ok when the system is committed.
    #[test]
    fn test_update_can_start_ok() {
        let policy_data =
            FuchsiaUpdateCanStartPolicyData { commit_status: Some(CommitStatus::Committed) };
        let result = FuchsiaPolicy::update_can_start(&policy_data, &FuchsiaInstallPlan::new_test());
        assert_eq!(result, UpdateDecision::Ok);
    }

    // Test that update_can_start returns Deferred when the system is pending commit (or if we
    // have no information on commit status).
    #[test]
    fn test_update_can_start_deferred() {
        let policy_data = FuchsiaUpdateCanStartPolicyData { commit_status: None };
        let result = FuchsiaPolicy::update_can_start(&policy_data, &FuchsiaInstallPlan::new_test());
        assert_eq!(result, UpdateDecision::DeferredByPolicy);

        let policy_data =
            FuchsiaUpdateCanStartPolicyData { commit_status: Some(CommitStatus::Pending) };
        let result = FuchsiaPolicy::update_can_start(&policy_data, &FuchsiaInstallPlan::new_test());
        assert_eq!(result, UpdateDecision::DeferredByPolicy);
    }

    // Verifies that query_commit_status_and_update_status updates the commit status and stops
    // calling the FIDL server once the system is committed.
    #[fasync::run_singlethreaded(test)]
    async fn test_query_commit_status_and_update_status() {
        let (proxy, mut stream) = create_proxy_and_stream::<CommitStatusProviderMarker>().unwrap();
        let provider_fn = || Ok(proxy.clone());
        let (p0, p1) = zx::EventPair::create().unwrap();
        let fidl_call_count = Arc::new(AtomicU8::new(0));
        let mut commit_status = None;

        let fidl_call_count_clone = Arc::clone(&fidl_call_count);
        let _fidl_server = fasync::Task::local(async move {
            while let Some(Ok(req)) = stream.next().await {
                fidl_call_count_clone.fetch_add(1, Ordering::SeqCst);
                let CommitStatusProviderRequest::IsCurrentSystemCommitted { responder } = req;
                let () = responder.send(p1.duplicate_handle(zx::Rights::BASIC).unwrap()).unwrap();
            }
        });

        // When no signals are asserted, we should update commit status to Pending.
        query_commit_status_and_update_status(provider_fn, &mut commit_status).await.unwrap();
        assert_eq!(commit_status, Some(CommitStatus::Pending));
        assert_eq!(fidl_call_count.load(Ordering::SeqCst), 1);

        // When USER_0 is asserted, we should update commit status to Committed.
        let () = p0.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).unwrap();
        query_commit_status_and_update_status(provider_fn, &mut commit_status).await.unwrap();
        assert_eq!(commit_status, Some(CommitStatus::Committed));
        assert_eq!(fidl_call_count.load(Ordering::SeqCst), 2);

        // Now that we're committed, additional calls should not query the FIDL server.
        query_commit_status_and_update_status(provider_fn, &mut commit_status).await.unwrap();
        assert_eq!(commit_status, Some(CommitStatus::Committed));
        assert_eq!(fidl_call_count.load(Ordering::SeqCst), 2);
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
        let policy_engine = FuchsiaPolicyEngineBuilder::new(PolicyConfig::default())
            .time_source(StandardTimeSource)
            .nop_metrics_reporter()
            .build();
        let ui_activity = *policy_engine.ui_activity.lock().await;
        assert_eq!(ui_activity.state, State::Invalid);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_ui_activity_watch_state() {
        let policy_engine = FuchsiaPolicyEngineBuilder::new(PolicyConfig::default())
            .time_source(StandardTimeSource)
            .nop_metrics_reporter()
            .build();
        let ui_activity = Arc::clone(&policy_engine.ui_activity);
        assert_eq!(ui_activity.lock().await.state, State::Invalid);

        let (notifier_proxy, mut stream) = create_proxy_and_stream::<NotifierMarker>().unwrap();
        fasync::Task::local(async move {
            let _ = &policy_engine;
            watch_ui_activity_impl(&policy_engine.ui_activity, notifier_proxy).await.unwrap();
        })
        .detach();

        {
            let NotifierRequest::WatchState { responder } = stream.next().await.unwrap().unwrap();
            responder.send(State::Idle).unwrap();
            assert_matches!(
                retry_or_last_error(repeat(Duration::from_millis(50)).take(20), || async {
                    let UiActivityState { state } = *ui_activity.lock().await;
                    (state == State::Idle).then(|| ()).ok_or(state)
                })
                .await,
                Ok(())
            );
        }

        {
            let NotifierRequest::WatchState { responder } = stream.next().await.unwrap().unwrap();
            responder.send(State::Active).unwrap();
            assert_matches!(
                retry_or_last_error(repeat(Duration::from_millis(50)).take(20), || async {
                    let UiActivityState { state } = *ui_activity.lock().await;
                    (state == State::Active).then(|| ()).ok_or(state)
                })
                .await,
                Ok(())
            );
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_query_opt_out_preference_ok_responses() {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<OptOutMarker>().unwrap();

        let ((), ()) = futures::join!(
            async move {
                let OptOutRequest::Get { responder } = stream.next().await.unwrap().unwrap();
                responder.send(OptOutPreference::AllowAllUpdates).unwrap();

                let OptOutRequest::Get { responder } = stream.next().await.unwrap().unwrap();
                responder.send(OptOutPreference::AllowOnlySecurityUpdates).unwrap();
            },
            async move {
                let provider = || Ok(proxy.clone());

                assert_eq!(
                    query_opt_out_preference(provider).await,
                    OptOutPreference::AllowAllUpdates
                );
                assert_eq!(
                    query_opt_out_preference(provider).await,
                    OptOutPreference::AllowOnlySecurityUpdates
                );
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_query_opt_out_preference_no_response_is_allow_all_updates() {
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<OptOutMarker>().unwrap();
        drop(stream);

        let provider = || Ok(proxy.clone());

        assert_eq!(query_opt_out_preference(provider).await, OptOutPreference::AllowAllUpdates);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_query_opt_out_preference_connect_error_is_allow_all_updates() {
        let provider = || Err(anyhow!("oops"));

        assert_eq!(query_opt_out_preference(provider).await, OptOutPreference::AllowAllUpdates);
    }

    #[test]
    fn test_reboot_allowed_interactive() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        let policy_data = FuchsiaRebootPolicyData::new(
            UiActivityState { state: State::Active },
            now,
            now,
            true,
            false,
        );
        assert!(FuchsiaPolicy::reboot_allowed(
            &policy_data,
            &CheckOptions { source: InstallSource::OnDemand },
        ),);
    }
    #[test]
    fn test_reboot_allowed_when_urgent_update_true() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        let policy_data = FuchsiaRebootPolicyData::new(
            UiActivityState { state: State::Active },
            now,
            now,
            false,
            true,
        );
        assert!(FuchsiaPolicy::reboot_allowed(
            &policy_data,
            &CheckOptions { source: InstallSource::ScheduledTask },
        ),);
    }

    #[test]
    fn test_reboot_allowed_unknown() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        let policy_data =
            FuchsiaRebootPolicyData::new(UiActivityState::new(), now, now, true, false);
        assert!(FuchsiaPolicy::reboot_allowed(&policy_data, &CheckOptions::default()));
    }

    #[test]
    fn test_reboot_allowed_idle() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        let policy_data = FuchsiaRebootPolicyData::new(
            UiActivityState { state: State::Idle },
            now,
            now,
            true,
            false,
        );
        assert!(FuchsiaPolicy::reboot_allowed(&policy_data, &CheckOptions::default()));
    }

    #[test]
    fn test_reboot_allowed_state_too_old() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        let policy_data = FuchsiaRebootPolicyData::new(
            UiActivityState { state: State::Active },
            now,
            now - VALID_REBOOT_DURATION,
            true,
            false,
        );
        assert!(FuchsiaPolicy::reboot_allowed(&policy_data, &CheckOptions::default()));
    }

    #[test]
    fn test_reboot_allowed_state_too_old_allow_rebot_false() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        let policy_data = FuchsiaRebootPolicyData::new(
            UiActivityState { state: State::Active },
            now,
            now - VALID_REBOOT_DURATION,
            false,
            false,
        );
        assert!(FuchsiaPolicy::reboot_allowed(&policy_data, &CheckOptions::default()));
    }

    #[test]
    fn test_reboot_not_allowed_when_active() {
        let mock_time = MockTimeSource::new_from_now();
        let now = mock_time.now();
        let policy_data = FuchsiaRebootPolicyData::new(
            UiActivityState { state: State::Active },
            now,
            now,
            true,
            false,
        );
        assert!(!FuchsiaPolicy::reboot_allowed(&policy_data, &CheckOptions::default()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_policy_engine_builder_interval_override() {
        let policy_config = FuchsiaPolicyEngineBuilder::new(PolicyConfig::default())
            .time_source(MockTimeSource::new_from_now())
            .nop_metrics_reporter()
            .periodic_interval(Duration::from_secs(345678))
            .build()
            .config;
        assert_eq!(Duration::from_secs(345678), policy_config.periodic_interval);
    }

    #[test]
    fn policy_config_try_from_failure() {
        use std::convert::TryInto;
        let manifest_config = omaha_client_structured_config::Config {
            allow_reboot_when_idle: true,
            fuzz_percentage_range: 201,
            periodic_interval_minutes: 1,
            retry_delay_seconds: 1,
            startup_delay_seconds: 1,
        };
        let maybe_policy_config: Result<PolicyConfig, _> = manifest_config.try_into();
        assert!(maybe_policy_config.is_err());
    }
}
