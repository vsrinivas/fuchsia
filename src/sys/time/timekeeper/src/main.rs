// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

//! `timekeeper` is responsible for external time synchronization in Fuchsia.

mod diagnostics;
mod network;
mod rtc;

use {
    crate::{
        diagnostics::{CobaltDiagnostics, CobaltDiagnosticsImpl, InspectDiagnostics},
        network::wait_for_network_available,
        rtc::{Rtc, RtcImpl},
    },
    anyhow::{Context as _, Error},
    chrono::prelude::*,
    fidl_fuchsia_deprecatedtimezone as ftz, fidl_fuchsia_netstack as fnetstack,
    fidl_fuchsia_time as ftime,
    fuchsia_async::{self as fasync, DurationExt},
    fuchsia_component::{
        client::{launch, launcher},
        server::ServiceFs,
    },
    fuchsia_zircon as zx,
    futures::StreamExt,
    log::{debug, error, info, warn},
    parking_lot::Mutex,
    std::cmp,
    std::sync::Arc,
    time_metrics_registry::{
        self, RealTimeClockEventsMetricDimensionEventType as RtcEventType,
        TimekeeperLifecycleEventsMetricDimensionEventType as LifecycleEventType,
    },
};

/// URL of the time source. In the future, this value belongs in a config file.
const NETWORK_TIME_SERVICE: &str =
    "fuchsia-pkg://fuchsia.com/network-time-service#meta/network_time_service.cmx";

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["time"]).context("initializing logging").unwrap();
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::INFO);

    info!("retrieving UTC clock handle");
    let time_maintainer =
        fuchsia_component::client::connect_to_service::<ftime::MaintenanceMarker>().unwrap();
    let utc_clock = Arc::new(zx::Clock::from(
        time_maintainer
            .get_writable_utc_clock()
            .await
            .context("failed to get UTC clock from maintainer")?,
    ));

    let mut inspect =
        InspectDiagnostics::new(diagnostics::INSPECTOR.root(), Arc::clone(&utc_clock));
    let mut fs = ServiceFs::new();
    info!("diagnostics initialized, serving on servicefs");
    diagnostics::INSPECTOR.serve(&mut fs)?;

    info!("initializing Cobalt");
    let mut cobalt = CobaltDiagnosticsImpl::new();
    let source = initial_utc_source(&*utc_clock);
    let notifier = Notifier::new(source);

    info!("connecting to real time clock");
    let optional_rtc = RtcImpl::only_device()
        .map_err(|err| {
            warn!("failed to connect to RTC: {}", err);
            let cobalt_err = err.into();
            cobalt.log_rtc_event(cobalt_err);
            inspect.rtc_initialize(cobalt_err, None);
        })
        .ok();

    info!("connecting to external update service");
    let launcher = launcher().context("starting launcher")?;
    let time_app = launch(&launcher, NETWORK_TIME_SERVICE.to_string(), None)
        .context("launching time service")?;
    let time_service = time_app.connect_to_service::<ftz::TimeServiceMarker>().unwrap();
    let netstack_service =
        fuchsia_component::client::connect_to_service::<fnetstack::NetstackMarker>().unwrap();
    let notifier_clone = notifier.clone();

    fasync::Task::spawn(async move {
        // Keep time_app in the same scope as time_service so the app is not stopped while
        // we are still using it
        let _time_app = time_app;
        maintain_utc(
            utc_clock,
            optional_rtc,
            notifier_clone,
            time_service,
            netstack_service,
            inspect,
            cobalt,
        )
        .await;
    })
    .detach();

    info!("serving notifier on servicefs");
    fs.dir("svc").add_fidl_service(move |requests: ftime::UtcRequestStream| {
        notifier.handle_request_stream(requests);
    });

    fs.take_and_serve_directory_handle()?;
    Ok(fs.collect().await)
}

fn initial_utc_source(utc_clock: &zx::Clock) -> ftime::UtcSource {
    let clock_details = utc_clock.get_details().expect("failed to get UTC clock details");
    // When the clock is first initialized to the backstop time, its synthetic offset should
    // be identical. Once the clock is updated, this is no longer true.
    if clock_details.backstop.into_nanos() == clock_details.ticks_to_synthetic.synthetic_offset {
        ftime::UtcSource::Backstop
    } else {
        ftime::UtcSource::External
    }
}

/// The top-level control loop for time synchronization.
///
/// Checks for network connectivity before attempting any time updates.
///
/// Actual updates are performed by calls to  `fuchsia.deprecatedtimezone.TimeService` which we
/// plan to deprecate.
async fn maintain_utc<C: CobaltDiagnostics, R: Rtc>(
    utc_clock: Arc<zx::Clock>,
    optional_rtc: Option<R>,
    notifs: Notifier,
    time_service: ftz::TimeServiceProxy,
    netstack_service: fnetstack::NetstackProxy,
    mut inspect: InspectDiagnostics,
    mut cobalt: C,
) {
    info!("record the state at initialization.");
    match initial_utc_source(&*utc_clock) {
        ftime::UtcSource::Backstop => {
            cobalt.log_lifecycle_event(LifecycleEventType::InitializedBeforeUtcStart)
        }
        ftime::UtcSource::External => {
            cobalt.log_lifecycle_event(LifecycleEventType::InitializedAfterUtcStart)
        }
    }

    if let Some(rtc) = optional_rtc {
        info!("reading initial RTC time.");
        match rtc.get().await {
            Err(err) => {
                warn!("failed to read RTC time: {}", err);
                inspect.rtc_initialize(RtcEventType::ReadFailed, None);
                cobalt.log_rtc_event(RtcEventType::ReadFailed);
            }
            Ok(time) => {
                info!("initial RTC time: {}", Utc.timestamp_nanos(time.into_nanos()));
                let backstop =
                    utc_clock.get_details().expect("failed to get UTC clock details").backstop;
                let status = if time < backstop {
                    RtcEventType::ReadInvalidBeforeBackstop
                } else {
                    RtcEventType::ReadSucceeded
                };
                inspect.rtc_initialize(status, Some(time));
                cobalt.log_rtc_event(status);
            }
        }
    }

    info!("waiting for network connectivity before attempting network time sync...");
    match wait_for_network_available(netstack_service.take_event_stream()).await {
        Ok(_) => inspect.network_available(),
        Err(why) => warn!("failed to wait for network, attempted to sync time anyway: {:?}", why),
    }

    for i in 0.. {
        info!("requesting roughtime service update the system time...");
        match time_service.update(1).await {
            Ok(Some(updated_time)) => {
                let updated_time = zx::Time::from_nanos(updated_time.utc_time);
                if let Err(status) = utc_clock.update(zx::ClockUpdate::new().value(updated_time)) {
                    error!("failed to update UTC clock to time {:?}: {}", updated_time, status);
                }
                inspect.update_clock();
                info!("adjusted UTC time to {}", Utc.timestamp_nanos(updated_time.into_nanos()));
                let monotonic_before = zx::Time::get(zx::ClockId::Monotonic).into_nanos();
                let utc_now = Utc::now().timestamp_nanos();
                let monotonic_after = zx::Time::get(zx::ClockId::Monotonic).into_nanos();
                info!(
                    "CF-884:monotonic_before={}:utc={}:monotonic_after={}",
                    monotonic_before, utc_now, monotonic_after,
                );
                if notifs.0.lock().set_source(ftime::UtcSource::External, monotonic_before) {
                    cobalt.log_lifecycle_event(LifecycleEventType::StartedUtcFromTimeSource);
                }
                break;
            }
            Ok(None) => {
                debug!(
                    "failed to update time, probably a network error. retrying in {}s.",
                    backoff_duration(i).into_seconds()
                );
            }
            Err(why) => {
                error!("couldn't make request to update time: {:?}", why);
            }
        }
        fasync::Timer::new(backoff_duration(i).after_now()).await;
    }
}

/// Returns the duration for which time synchronization should wait after failing to synchronize
/// time. `attempt_index` is a zero-based index of the failed attempt, i.e. after the third failed
/// attempt `attempt_index` = 2.
fn backoff_duration(attempt_index: u32) -> zx::Duration {
    // We make three tries at each interval before doubling, but never exceed 8 seconds (ie 2^3).
    const TRIES_PER_EXPONENT: u32 = 3;
    const MAX_EXPONENT: u32 = 3;
    let exponent = cmp::min(attempt_index / TRIES_PER_EXPONENT, MAX_EXPONENT);
    zx::Duration::from_seconds(2i64.pow(exponent))
}

/// Notifies waiting clients when the clock has been updated, wrapped in a lock to allow
/// sharing between tasks.
#[derive(Clone, Debug)]
struct Notifier(Arc<Mutex<NotifyInner>>);

impl Notifier {
    fn new(source: ftime::UtcSource) -> Self {
        Notifier(Arc::new(Mutex::new(NotifyInner { source, clients: Vec::new() })))
    }

    /// Spawns an async task to handle requests on this channel.
    fn handle_request_stream(&self, requests: ftime::UtcRequestStream) {
        let notifier = self.clone();
        fasync::Task::spawn(async move {
            let mut counted_requests = requests.enumerate();
            let mut last_seen_state = notifier.0.lock().source;
            while let Some((request_count, Ok(ftime::UtcRequest::WatchState { responder }))) =
                counted_requests.next().await
            {
                let mut n = notifier.0.lock();
                // we return immediately if this is the first request on this channel, or if there
                // has been a new update since the last request.
                if request_count == 0 || last_seen_state != n.source {
                    n.reply(responder, zx::Time::get(zx::ClockId::Monotonic).into_nanos());
                } else {
                    n.register(responder);
                }
                last_seen_state = n.source;
            }
        })
        .detach();
    }
}

/// Notifies waiting clients when the clock has been updated.
#[derive(Debug)]
struct NotifyInner {
    /// The current source for our UTC approximation.
    source: ftime::UtcSource,
    /// All clients waiting for an update to UTC's time.
    clients: Vec<ftime::UtcWatchStateResponder>,
}

impl NotifyInner {
    /// Reply to a client with the current UtcState.
    fn reply(&self, responder: ftime::UtcWatchStateResponder, update_time: i64) {
        if let Err(why) = responder
            .send(ftime::UtcState { timestamp: Some(update_time), source: Some(self.source) })
        {
            warn!("failed to notify a client of an update: {:?}", why);
        }
    }

    /// Registers a client to be later notified that a clock update has occurred.
    fn register(&mut self, responder: ftime::UtcWatchStateResponder) {
        info!("registering a client for notifications");
        self.clients.push(responder);
    }

    /// Increases the revision counter by 1 and notifies any clients waiting on updates from
    /// previous revisions, returning true iff the source changed as a result of the call.
    fn set_source(&mut self, source: ftime::UtcSource, update_time: i64) -> bool {
        if self.source != source {
            self.source = source;
            let clients = std::mem::replace(&mut self.clients, vec![]);
            info!("UTC source changed to {:?}, notifying {} clients", source, clients.len());
            for responder in clients {
                self.reply(responder, update_time);
            }
            true
        } else {
            info!("received UTC source update but the actual source didn't change.");
            false
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::rtc::FakeRtc,
        fuchsia_inspect::Inspector,
        fuchsia_zircon as zx,
        lazy_static::lazy_static,
        matches::assert_matches,
        std::{
            future::Future,
            task::{Context, Poll},
        },
    };

    lazy_static! {
        static ref BACKSTOP_TIME: zx::Time = zx::Time::from_nanos(111111);
        static ref RTC_TIME: zx::Time = zx::Time::from_nanos(222222);
        static ref UPDATE_TIME: zx::Time = zx::Time::from_nanos(333333);
    }

    #[fasync::run_singlethreaded(test)]
    async fn single_client() {
        let clock =
            Arc::new(zx::Clock::create(zx::ClockOpts::empty(), Some(*BACKSTOP_TIME)).unwrap());
        clock.update(zx::ClockUpdate::new().value(*BACKSTOP_TIME)).unwrap();
        let initial_update_ticks = clock.get_details().unwrap().last_value_update_ticks;

        let inspector = Inspector::new();
        let inspect_diagnostics =
            diagnostics::InspectDiagnostics::new(inspector.root(), Arc::clone(&clock));
        let (cobalt_diagnostics, mut cobalt_monitor) = diagnostics::FakeCobaltDiagnostics::new();
        info!("starting single notification test");

        let (utc, utc_requests) =
            fidl::endpoints::create_proxy_and_stream::<ftime::UtcMarker>().unwrap();
        let (time_service, mut time_requests) =
            fidl::endpoints::create_proxy_and_stream::<ftz::TimeServiceMarker>().unwrap();

        let netstack_service = network::create_event_service_with_valid_interface();

        let notifier = Notifier::new(ftime::UtcSource::Backstop);
        let (mut allow_update, mut wait_for_update) = futures::channel::mpsc::channel(1);
        info!("spawning test notifier");
        notifier.handle_request_stream(utc_requests);

        fasync::Task::spawn(maintain_utc(
            Arc::clone(&clock),
            Some(FakeRtc::valid(*RTC_TIME)),
            notifier.clone(),
            time_service,
            netstack_service,
            inspect_diagnostics,
            cobalt_diagnostics,
        ))
        .detach();

        fasync::Task::spawn(async move {
            while let Some(Ok(ftz::TimeServiceRequest::Update { responder, .. })) =
                time_requests.next().await
            {
                let () = wait_for_update.next().await.unwrap();
                responder
                    .send(Some(&mut ftz::UpdatedTime { utc_time: UPDATE_TIME.into_nanos() }))
                    .unwrap();
            }
        })
        .detach();

        info!("checking that the time source has not been externally initialized yet");
        assert_eq!(utc.watch_state().await.unwrap().source.unwrap(), ftime::UtcSource::Backstop);

        info!("checking that the clock has not been updated yet");
        assert_eq!(initial_update_ticks, clock.get_details().unwrap().last_value_update_ticks);

        info!("checking that the initial state was logged to Cobalt");
        cobalt_monitor.assert_lifecycle_events(&[LifecycleEventType::InitializedBeforeUtcStart]);
        cobalt_monitor.reset();

        let task_waker = futures::future::poll_fn(|cx| Poll::Ready(cx.waker().clone())).await;
        let mut cx = Context::from_waker(&task_waker);

        let mut hanging = Box::pin(utc.watch_state());
        assert!(
            hanging.as_mut().poll(&mut cx).is_pending(),
            "hanging get should not return before time updated event has been emitted"
        );

        info!("sending network update event");
        allow_update.try_send(()).unwrap();

        info!("waiting for time source update");
        assert_eq!(hanging.await.unwrap().source.unwrap(), ftime::UtcSource::External);
        assert!(clock.get_details().unwrap().last_value_update_ticks > initial_update_ticks);

        info!("checking that the started clock was logged to Cobalt");
        cobalt_monitor.assert_lifecycle_events(&[LifecycleEventType::StartedUtcFromTimeSource]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn initial_utc_source_initialized() {
        let clock =
            zx::Clock::create(zx::ClockOpts::empty(), Some(zx::Time::from_nanos(1_000))).unwrap();
        // The clock must be started with an initial value.
        clock.update(zx::ClockUpdate::new().value(zx::Time::from_nanos(1_000))).unwrap();
        let source = initial_utc_source(&clock);
        assert_matches!(source, ftime::UtcSource::Backstop);

        // Update the clock, which is already running.
        clock.update(zx::ClockUpdate::new().value(zx::Time::from_nanos(1_000_000))).unwrap();
        let source = initial_utc_source(&clock);
        assert_matches!(source, ftime::UtcSource::External);
    }

    #[test]
    fn backoff_sequence_matches_expectation() {
        let expectation = vec![1, 1, 1, 2, 2, 2, 4, 4, 4, 8, 8, 8, 8, 8];
        for i in 0..expectation.len() {
            let expected = zx::Duration::from_seconds(expectation[i]);
            let actual = backoff_duration(i as u32);

            assert_eq!(
                actual, expected,
                "backoff after iteration {} should be {:?} but was {:?}",
                i, expected, actual
            );
        }
    }
}
