// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

//! `timekeeper` is responsible for external time synchronization in Fuchsia.

mod diagnostics;
mod network;
mod rtc;
mod time_source;

use {
    crate::{
        diagnostics::{CobaltDiagnostics, CobaltDiagnosticsImpl, InspectDiagnostics},
        network::wait_for_network_available,
        rtc::{Rtc, RtcImpl},
        time_source::{Event, PushTimeSource, TimeSource},
    },
    anyhow::{Context as _, Error},
    chrono::prelude::*,
    fidl_fuchsia_netstack as fnetstack, fidl_fuchsia_time as ftime, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::StreamExt,
    log::{error, info, warn},
    parking_lot::Mutex,
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

    // TODO(jsankey): When moving to the updated diagnostics design use interior mutability to
    // avoid the mutex here.
    let inspect = Arc::new(Mutex::new(InspectDiagnostics::new(
        diagnostics::INSPECTOR.root(),
        Arc::clone(&utc_clock),
    )));
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
            warn!("failed to connect to RTC, ZX_CLOCK_UTC won't be updated: {}", err);
            let cobalt_err = err.into();
            cobalt.log_rtc_event(cobalt_err);
            inspect.lock().rtc_initialize(cobalt_err, None);
        })
        .map(|rtc| Arc::new(rtc))
        .ok();

    let time_source = PushTimeSource::new(NETWORK_TIME_SERVICE.to_string());
    let netstack_service =
        fuchsia_component::client::connect_to_service::<fnetstack::NetstackMarker>().unwrap();
    let notifier_clone = notifier.clone();

    fasync::Task::spawn(async move {
        maintain_utc(
            utc_clock,
            optional_rtc,
            notifier_clone,
            time_source,
            netstack_service,
            Arc::clone(&inspect),
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
async fn maintain_utc<C: CobaltDiagnostics, R: Rtc, T: TimeSource>(
    utc_clock: Arc<zx::Clock>,
    mut optional_rtc: Option<Arc<R>>,
    notifs: Notifier,
    time_source: T,
    netstack_service: fnetstack::NetstackProxy,
    inspect: Arc<Mutex<InspectDiagnostics>>,
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

    if let Some(rtc) = &optional_rtc.as_mut() {
        info!("reading initial RTC time.");
        match rtc.get().await {
            Err(err) => {
                warn!("failed to read RTC time: {}", err);
                inspect.lock().rtc_initialize(RtcEventType::ReadFailed, None);
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
                inspect.lock().rtc_initialize(status, Some(time));
                cobalt.log_rtc_event(status);
            }
        }
    }

    info!("launching time source...");
    let mut time_source_events = match time_source.launch() {
        Err(err) => {
            error!("failed to launch time source, aborting: {}", err);
            inspect.lock().failed("Time source did not launch");
            return;
        }
        Ok(events) => events,
    };

    info!("waiting for network connectivity before attempting network time sync...");
    match wait_for_network_available(netstack_service.take_event_stream()).await {
        Ok(_) => inspect.lock().network_available(),
        Err(why) => warn!("failed to wait for network, attempted to sync time anyway: {:?}", why),
    }

    let mut received_valid_update: bool = false;
    while let Some(outcome) = time_source_events.next().await {
        match outcome {
            Err(err) => {
                // With the hanging get pattern failures are likely to be repeated in fast
                // succession so we don't want to stay in the loop reporting them.
                // TODO(jsankey): Count failures and trigger a reset after some number of
                // consecutive failures rather then abandoning on the first attempt.
                error!("error waiting for time events, aborting: {}", err);
                inspect.lock().failed("Time source failure");
                return;
            }
            Ok(Event::StatusChange { status }) => {
                info!("time source changed state to {:?}", status);
            }
            Ok(Event::TimeSample { utc, .. }) => {
                let utc_chrono = Utc.timestamp_nanos(utc.into_nanos());
                if received_valid_update {
                    // Until we can handle blending clock updates we only use the first valid
                    // sample we receive, but we still log additional updates.
                    info!("received time update to {}", utc_chrono);
                    continue;
                }

                if let Err(status) = utc_clock.update(zx::ClockUpdate::new().value(utc)) {
                    error!("failed to update UTC clock to {}: {}", utc_chrono, status);
                } else {
                    inspect.lock().update_clock();
                    info!("adjusted UTC time to {}", utc_chrono);
                }
                if let Some(ref rtc) = optional_rtc {
                    match rtc.set(utc).await {
                        Err(err) => {
                            error!(
                                "failed to update RTC and ZX_CLOCK_UTC to {}: {}",
                                utc_chrono, err
                            );
                            inspect.lock().rtc_write(false);
                            cobalt.log_rtc_event(RtcEventType::WriteFailed);
                        }
                        Ok(()) => {
                            info!("updated RTC to {}", utc_chrono);
                            inspect.lock().rtc_write(true);
                            cobalt.log_rtc_event(RtcEventType::WriteSucceeded);
                        }
                    }
                }
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
                received_valid_update = true;
            }
        };
    }

    error!("time source event stream closed, time synchronization has stopped.");
    inspect.lock().failed("Time source closed");
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
        crate::time_source::FakeTimeSource,
        fidl_fuchsia_time_external as ftexternal,
        fuchsia_inspect::{assert_inspect_tree, Inspector},
        fuchsia_zircon as zx,
        futures::FutureExt,
        lazy_static::lazy_static,
        matches::assert_matches,
        std::task::Poll,
    };

    lazy_static! {
        static ref BACKSTOP_TIME: zx::Time = zx::Time::from_nanos(111111);
        static ref RTC_TIME: zx::Time = zx::Time::from_nanos(222222);
        static ref UPDATE_TIME: zx::Time = zx::Time::from_nanos(333333);
        static ref UPDATE_TIME_2: zx::Time = zx::Time::from_nanos(444444);
        static ref CLOCK_OPTS: zx::ClockOpts = zx::ClockOpts::empty();
    }

    #[fasync::run_singlethreaded(test)]
    async fn successful_update_single_notify_client() {
        let clock = Arc::new(zx::Clock::create(*CLOCK_OPTS, Some(*BACKSTOP_TIME)).unwrap());
        clock.update(zx::ClockUpdate::new().value(*BACKSTOP_TIME)).unwrap();
        let initial_update_ticks = clock.get_details().unwrap().last_value_update_ticks;

        let rtc = Arc::new(FakeRtc::valid(*RTC_TIME));

        let inspector = Inspector::new();
        let inspect_diagnostics = Arc::new(Mutex::new(diagnostics::InspectDiagnostics::new(
            inspector.root(),
            Arc::clone(&clock),
        )));
        let (cobalt_diagnostics, cobalt_monitor) = diagnostics::FakeCobaltDiagnostics::new();

        let (utc, utc_requests) =
            fidl::endpoints::create_proxy_and_stream::<ftime::UtcMarker>().unwrap();
        let time_source = FakeTimeSource::events_then_pending(vec![
            Event::StatusChange { status: ftexternal::Status::Ok },
            Event::TimeSample { utc: *UPDATE_TIME, monotonic: zx::Time::from_nanos(0) },
            Event::TimeSample { utc: *UPDATE_TIME_2, monotonic: zx::Time::from_nanos(1) },
        ]);

        let netstack_service = network::create_event_service_with_valid_interface();

        let notifier = Notifier::new(ftime::UtcSource::Backstop);
        // Spawning test notifier and verifying initial state
        notifier.handle_request_stream(utc_requests);
        assert_eq!(utc.watch_state().await.unwrap().source.unwrap(), ftime::UtcSource::Backstop);

        let _task = fasync::Task::spawn(maintain_utc(
            Arc::clone(&clock),
            Some(Arc::clone(&rtc)),
            notifier.clone(),
            time_source,
            netstack_service,
            Arc::clone(&inspect_diagnostics),
            cobalt_diagnostics,
        ));

        // Checking that the reported time source has been updated to the first input
        assert_eq!(utc.watch_state().await.unwrap().source.unwrap(), ftime::UtcSource::External);
        assert!(clock.get_details().unwrap().last_value_update_ticks > initial_update_ticks);
        assert_eq!(rtc.last_set(), Some(*UPDATE_TIME));

        // Checking that correct events were logged to Cobalt
        cobalt_monitor.assert_lifecycle_events(&[
            LifecycleEventType::InitializedBeforeUtcStart,
            LifecycleEventType::StartedUtcFromTimeSource,
        ]);
        cobalt_monitor
            .assert_rtc_events(&[RtcEventType::ReadSucceeded, RtcEventType::WriteSucceeded]);
    }

    #[test]
    fn no_update_single_notify_client() {
        let mut executor = fasync::Executor::new().unwrap();
        let clock = Arc::new(zx::Clock::create(*CLOCK_OPTS, Some(*BACKSTOP_TIME)).unwrap());
        clock.update(zx::ClockUpdate::new().value(*BACKSTOP_TIME)).unwrap();
        let initial_update_ticks = clock.get_details().unwrap().last_value_update_ticks;

        let rtc = Arc::new(FakeRtc::valid(*RTC_TIME));

        let inspector = Inspector::new();
        let inspect_diagnostics = Arc::new(Mutex::new(diagnostics::InspectDiagnostics::new(
            inspector.root(),
            Arc::clone(&clock),
        )));
        let (cobalt_diagnostics, cobalt_monitor) = diagnostics::FakeCobaltDiagnostics::new();

        let (utc, utc_requests) =
            fidl::endpoints::create_proxy_and_stream::<ftime::UtcMarker>().unwrap();
        let time_source = FakeTimeSource::events_then_pending(vec![Event::StatusChange {
            status: ftexternal::Status::Network,
        }]);

        let netstack_service = network::create_event_service_with_valid_interface();

        // Spawning test notifier and verifying the initial state
        let notifier = Notifier::new(ftime::UtcSource::Backstop);
        notifier.handle_request_stream(utc_requests);
        let mut fut1 = async { utc.watch_state().await.unwrap().source.unwrap() }.boxed();
        assert_eq!(executor.run_until_stalled(&mut fut1), Poll::Ready(ftime::UtcSource::Backstop));

        // Maintain UTC until no more work remains
        let mut fut2 = maintain_utc(
            Arc::clone(&clock),
            Some(Arc::clone(&rtc)),
            notifier.clone(),
            time_source,
            netstack_service,
            Arc::clone(&inspect_diagnostics),
            cobalt_diagnostics,
        )
        .boxed();
        let _ = executor.run_until_stalled(&mut fut2);

        // Checking that the reported time source has not been updated
        let mut fut3 = async { utc.watch_state().await.unwrap().source.unwrap() }.boxed();
        assert_eq!(executor.run_until_stalled(&mut fut3), Poll::Pending);

        // Checking that the clock has not been updated yet
        assert_eq!(initial_update_ticks, clock.get_details().unwrap().last_value_update_ticks);
        assert_eq!(rtc.last_set(), None);

        // Checking that correct events were logged to Cobalt
        cobalt_monitor.assert_lifecycle_events(&[LifecycleEventType::InitializedBeforeUtcStart]);
        cobalt_monitor.assert_rtc_events(&[RtcEventType::ReadSucceeded]);
    }

    #[fasync::run_until_stalled(test)]
    async fn failed_time_source() {
        let clock = Arc::new(zx::Clock::create(*CLOCK_OPTS, Some(*BACKSTOP_TIME)).unwrap());
        clock.update(zx::ClockUpdate::new().value(*BACKSTOP_TIME)).unwrap();
        let initial_update_ticks = clock.get_details().unwrap().last_value_update_ticks;

        let rtc = Arc::new(FakeRtc::valid(*RTC_TIME));

        let inspector = Inspector::new();
        let inspect_diagnostics = Arc::new(Mutex::new(diagnostics::InspectDiagnostics::new(
            inspector.root(),
            Arc::clone(&clock),
        )));
        let (cobalt_diagnostics, cobalt_monitor) = diagnostics::FakeCobaltDiagnostics::new();

        // Create a time source that will close the channel after the first status.
        let time_source = FakeTimeSource::events_then_terminate(vec![Event::StatusChange {
            status: ftexternal::Status::Hardware,
        }]);

        let netstack_service = network::create_event_service_with_valid_interface();
        let notifier = Notifier::new(ftime::UtcSource::Backstop);

        maintain_utc(
            Arc::clone(&clock),
            Some(Arc::clone(&rtc)),
            notifier.clone(),
            time_source,
            netstack_service,
            Arc::clone(&inspect_diagnostics),
            cobalt_diagnostics,
        )
        .await;

        // Checking that the clock has not been updated yet
        assert_eq!(initial_update_ticks, clock.get_details().unwrap().last_value_update_ticks);
        assert_eq!(rtc.last_set(), None);

        // Checking that correct events were logged to Cobalt
        cobalt_monitor.assert_lifecycle_events(&[LifecycleEventType::InitializedBeforeUtcStart]);
        cobalt_monitor.assert_rtc_events(&[RtcEventType::ReadSucceeded]);

        // Checking the inspect data is available and unhealthy
        assert_inspect_tree!(
            inspector,
            root: contains {
                "fuchsia.inspect.Health": contains {
                    status: "UNHEALTHY",
                },
            }
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn unlaunchable_time_source() {
        let clock = Arc::new(zx::Clock::create(*CLOCK_OPTS, Some(*BACKSTOP_TIME)).unwrap());
        clock.update(zx::ClockUpdate::new().value(*BACKSTOP_TIME)).unwrap();
        let initial_update_ticks = clock.get_details().unwrap().last_value_update_ticks;

        let rtc = Arc::new(FakeRtc::valid(*RTC_TIME));

        let inspector = Inspector::new();
        let inspect_diagnostics = Arc::new(Mutex::new(diagnostics::InspectDiagnostics::new(
            inspector.root(),
            Arc::clone(&clock),
        )));
        let (cobalt_diagnostics, cobalt_monitor) = diagnostics::FakeCobaltDiagnostics::new();

        let time_source = FakeTimeSource::failing();
        let netstack_service = network::create_event_service_with_valid_interface();
        let notifier = Notifier::new(ftime::UtcSource::Backstop);

        maintain_utc(
            Arc::clone(&clock),
            Some(Arc::clone(&rtc)),
            notifier.clone(),
            time_source,
            netstack_service,
            Arc::clone(&inspect_diagnostics),
            cobalt_diagnostics,
        )
        .await;

        // Checking that the clock has not been updated yet
        assert_eq!(initial_update_ticks, clock.get_details().unwrap().last_value_update_ticks);
        assert_eq!(rtc.last_set(), None);

        // Checking that correct events were logged to Cobalt
        cobalt_monitor.assert_lifecycle_events(&[LifecycleEventType::InitializedBeforeUtcStart]);
        cobalt_monitor.assert_rtc_events(&[RtcEventType::ReadSucceeded]);

        // Checking the inspect data is available and unhealthy
        assert_inspect_tree!(
            inspector,
            root: contains {
                "fuchsia.inspect.Health": contains {
                    status: "UNHEALTHY",
                },
            }
        );
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
}
