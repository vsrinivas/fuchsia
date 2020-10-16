// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

//! `timekeeper` is responsible for external time synchronization in Fuchsia.

mod clock_manager;
mod diagnostics;
mod enums;
mod estimator;
mod network;
mod notifier;
mod rtc;
mod time_source;
mod time_source_manager;

use {
    crate::{
        clock_manager::ClockManager,
        diagnostics::{
            CobaltDiagnostics, CompositeDiagnostics, Diagnostics, Event, InspectDiagnostics,
        },
        enums::{InitialClockState, InitializeRtcOutcome, Role, StartClockSource, Track},
        network::{event_stream, wait_for_network_available},
        notifier::Notifier,
        rtc::{Rtc, RtcImpl},
        time_source::{PushTimeSource, TimeSource},
        time_source_manager::TimeSourceManager,
    },
    anyhow::{Context as _, Error},
    chrono::prelude::*,
    fidl_fuchsia_net_interfaces as finterfaces, fidl_fuchsia_time as ftime,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::{
        future::{self, OptionFuture},
        StreamExt as _,
    },
    log::{info, warn},
    std::sync::Arc,
};

/// A definition which time sources to install, along with the URL for each.
struct TimeSourceUrls {
    primary: &'static str,
    monitor: Option<&'static str>,
}

/// The time sources to install in the default (i.e. non-test) case. In the future, these values
/// belong in a config file.
const DEFAULT_SOURCES: TimeSourceUrls = TimeSourceUrls {
    primary: "fuchsia-pkg://fuchsia.com/network-time-service#meta/network_time_service.cmx",
    monitor: Some("fuchsia-pkg://fuchsia.com/httpsdate-time-source#meta/httpsdate_time_source.cmx"),
};

/// The time sources to install when the dev_time_sources flag is set. In the future, these values
/// belong in a config file.
const DEV_TEST_SOURCES: TimeSourceUrls = TimeSourceUrls {
    primary: "fuchsia-pkg://fuchsia.com/timekeeper-integration#meta/dev_time_source.cmx",
    monitor: None,
};

/// The information required to maintain UTC for the primary track.
struct PrimaryTrack<T: TimeSource> {
    time_source: T,
    clock: zx::Clock,
    notifier: Notifier,
}

/// The information required to maintain UTC for the monitor track.
struct MonitorTrack<T: TimeSource> {
    time_source: T,
    clock: zx::Clock,
}

/// Command line arguments supplied to Timekeeper.
#[derive(argh::FromArgs)]
struct Options {
    /// flag indicating to use the dev time sources.
    #[argh(switch)]
    dev_time_sources: bool,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["time"]).context("initializing logging").unwrap();
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::INFO);
    let options = argh::from_env::<Options>();

    info!("retrieving UTC clock handle");
    let time_maintainer =
        fuchsia_component::client::connect_to_service::<ftime::MaintenanceMarker>().unwrap();
    let utc_clock = zx::Clock::from(
        time_maintainer
            .get_writable_utc_clock()
            .await
            .context("failed to get UTC clock from maintainer")?,
    );

    info!("constructing time sources");
    let notifier = Notifier::new(match initial_clock_state(&utc_clock) {
        InitialClockState::NotSet => ftime::UtcSource::Backstop,
        InitialClockState::PreviouslySet => ftime::UtcSource::Unverified,
    });
    let time_source_urls = match options.dev_time_sources {
        true => DEV_TEST_SOURCES,
        false => DEFAULT_SOURCES,
    };
    let primary_track = PrimaryTrack {
        time_source: PushTimeSource::new(time_source_urls.primary.to_string()),
        clock: utc_clock,
        notifier: notifier.clone(),
    };
    let monitor_track = time_source_urls.monitor.map(|url| MonitorTrack {
        time_source: PushTimeSource::new(url.to_string()),
        clock: create_monitor_clock(&primary_track.clock),
    });

    info!("initializing diagnostics and serving inspect on servicefs");
    let diagnostics = Arc::new(CompositeDiagnostics::new(
        InspectDiagnostics::new(diagnostics::INSPECTOR.root(), &primary_track, &monitor_track),
        CobaltDiagnostics::new(),
    ));
    let mut fs = ServiceFs::new();
    diagnostics::INSPECTOR.serve(&mut fs)?;

    info!("connecting to real time clock");
    let optional_rtc = match RtcImpl::only_device() {
        Ok(rtc) => Some(rtc),
        Err(err) => {
            warn!("failed to connect to RTC, ZX_CLOCK_UTC won't be updated: {}", err);
            diagnostics.record(Event::InitializeRtc { outcome: err.into(), time: None });
            None
        }
    };

    let interface_state_service =
        fuchsia_component::client::connect_to_service::<finterfaces::StateMarker>()
            .context("failed to connect to fuchsia.net.interfaces/State")?;
    let interface_event_stream = event_stream(&interface_state_service)?;

    fasync::Task::spawn(async move {
        maintain_utc(
            primary_track,
            monitor_track,
            optional_rtc,
            interface_event_stream,
            diagnostics,
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

/// Creates and starts a new userspace clock for use in the monitor track, set to the same
/// backstop time as the supplied primary clock.
fn create_monitor_clock(primary_clock: &zx::Clock) -> zx::Clock {
    // Note: Failure should not be possible from a valid zx::Clock.
    let backstop = primary_clock.get_details().expect("failed to get UTC clock details").backstop;
    // Note: Only failure mode is an OOM which we handle via panic.
    let clock = zx::Clock::create(zx::ClockOpts::empty(), Some(backstop))
        .expect("failed to create new monitor clock");
    // Note: Failure should not be possible from a freshly created zx::Clock.
    clock.update(zx::ClockUpdate::new().value(backstop)).expect("failed to start monitor clock");
    clock
}

/// Determines whether the supplied clock has previously been set.
fn initial_clock_state(utc_clock: &zx::Clock) -> InitialClockState {
    // Note: Failure should not be possible from a valid zx::Clock.
    let clock_details = utc_clock.get_details().expect("failed to get UTC clock details");
    // When the clock is first initialized to the backstop time, its synthetic offset should
    // be identical. Once the clock is updated, this is no longer true.
    if clock_details.backstop.into_nanos() == clock_details.ticks_to_synthetic.synthetic_offset {
        InitialClockState::NotSet
    } else {
        InitialClockState::PreviouslySet
    }
}

/// Attempts to initialize a userspace clock from the current value of the real time clock.
/// sending progress to diagnosistics and a notifier as appropriate.
async fn set_clock_from_rtc<R: Rtc, D: Diagnostics>(
    rtc: &R,
    clock: &mut zx::Clock,
    notifier: &mut Notifier,
    diagnostics: Arc<D>,
) {
    info!("reading initial RTC time.");
    let time = match rtc.get().await {
        Err(err) => {
            warn!("failed to read RTC time: {}", err);
            diagnostics.record(Event::InitializeRtc {
                outcome: InitializeRtcOutcome::ReadFailed,
                time: None,
            });
            return;
        }
        Ok(time) => time,
    };

    let utc_chrono = Utc.timestamp_nanos(time.into_nanos());
    let backstop = clock.get_details().expect("failed to get UTC clock details").backstop;
    if time < backstop {
        warn!("initial RTC time before backstop: {}", utc_chrono);
        diagnostics.record(Event::InitializeRtc {
            outcome: InitializeRtcOutcome::InvalidBeforeBackstop,
            time: Some(time),
        });
        return;
    }

    diagnostics.record(Event::InitializeRtc {
        outcome: InitializeRtcOutcome::Succeeded,
        time: Some(time),
    });
    if let Err(status) = clock.update(zx::ClockUpdate::new().value(time)) {
        warn!("failed to start UTC clock from RTC at time {}: {}", utc_chrono, status);
    } else {
        notifier.set_source(ftime::UtcSource::Unverified).await;
        diagnostics
            .record(Event::StartClock { track: Track::Primary, source: StartClockSource::Rtc });
        info!("started UTC clock from RTC at time: {}", utc_chrono);
    }
}

/// The top-level control loop for time synchronization.
///
/// Checks for network connectivity before attempting any time updates.
///
/// Maintains the utc clock using updates received over the `fuchsia.time.external` protocols.
async fn maintain_utc<R: 'static, T: 'static, S: 'static, D: 'static>(
    mut primary: PrimaryTrack<T>,
    optional_monitor: Option<MonitorTrack<T>>,
    optional_rtc: Option<R>,
    interface_event_stream: S,
    diagnostics: Arc<D>,
) where
    R: Rtc,
    T: TimeSource,
    S: futures::Stream<Item = Result<finterfaces::Event, Error>>,
    D: Diagnostics,
{
    info!("record the state at initialization.");
    let initial_clock_state = initial_clock_state(&primary.clock);
    diagnostics.record(Event::Initialized { clock_state: initial_clock_state });

    match initial_clock_state {
        InitialClockState::NotSet => {
            if let Some(rtc) = optional_rtc.as_ref() {
                set_clock_from_rtc(
                    rtc,
                    &mut primary.clock,
                    &mut primary.notifier,
                    Arc::clone(&diagnostics),
                )
                .await;
            }
        }
        InitialClockState::PreviouslySet => {
            if optional_rtc.is_some() {
                diagnostics.record(Event::InitializeRtc {
                    outcome: InitializeRtcOutcome::ReadNotAttempted,
                    time: None,
                });
            }
            info!("clock was already running at intialization, reporting source as unverified");
            primary.notifier.set_source(ftime::UtcSource::Unverified).await;
        }
    }

    info!("launching time source managers...");
    let backstop = primary.clock.get_details().expect("failed to get UTC clock details").backstop;
    let mut primary_source_manager = TimeSourceManager::new(
        backstop,
        Role::Primary,
        primary.time_source,
        Arc::clone(&diagnostics),
    );
    primary_source_manager.warm_up();
    let monitor_source_manager_and_clock = optional_monitor.map(|monitor| {
        let mut source_manager = TimeSourceManager::new(
            backstop,
            Role::Monitor,
            monitor.time_source,
            Arc::clone(&diagnostics),
        );
        source_manager.warm_up();
        (source_manager, monitor.clock)
    });

    info!("waiting for network connectivity before attempting network time sync...");
    match wait_for_network_available(interface_event_stream).await {
        Ok(()) => diagnostics.record(Event::NetworkAvailable),
        Err(why) => warn!("failed to wait for network, attempted to sync time anyway: {:?}", why),
    }

    info!("launching clock managers...");
    let fut1 = ClockManager::execute(
        primary.clock,
        primary_source_manager,
        optional_rtc,
        Some(primary.notifier),
        Arc::clone(&diagnostics),
        Track::Primary,
    );
    let fut2: OptionFuture<_> = monitor_source_manager_and_clock
        .map(|(source_manager, clock)| {
            ClockManager::<T, R, D>::execute(
                clock,
                source_manager,
                None,
                None,
                diagnostics,
                Track::Monitor,
            )
        })
        .into();
    future::join(fut1, fut2).await;
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            diagnostics::FakeDiagnostics,
            enums::{InitialClockState, InitializeRtcOutcome, WriteRtcOutcome},
            rtc::FakeRtc,
            time_source::{Event as TimeSourceEvent, FakeTimeSource, Sample},
        },
        fidl_fuchsia_time_external as ftexternal, fuchsia_zircon as zx,
        fuchsia_zircon::HandleBased as _,
        futures::FutureExt,
        lazy_static::lazy_static,
        std::task::Poll,
    };

    const NANOS_PER_SECOND: i64 = 1_000_000_000;
    const OFFSET: zx::Duration = zx::Duration::from_seconds(1111_000);
    const OFFSET_2: zx::Duration = zx::Duration::from_seconds(1111_333);
    const STD_DEV: zx::Duration = zx::Duration::from_millis(44);

    lazy_static! {
        static ref INVALID_RTC_TIME: zx::Time = zx::Time::from_nanos(111111 * NANOS_PER_SECOND);
        static ref BACKSTOP_TIME: zx::Time = zx::Time::from_nanos(222222 * NANOS_PER_SECOND);
        static ref VALID_RTC_TIME: zx::Time = zx::Time::from_nanos(333333 * NANOS_PER_SECOND);
        static ref CLOCK_OPTS: zx::ClockOpts = zx::ClockOpts::empty();
    }

    /// Creates and starts a new clock with default options, returning a tuple of the clock and its
    /// initial update time in ticks.
    fn create_clock() -> (zx::Clock, i64) {
        let clock = zx::Clock::create(*CLOCK_OPTS, Some(*BACKSTOP_TIME)).unwrap();
        clock.update(zx::ClockUpdate::new().value(*BACKSTOP_TIME)).unwrap();
        let initial_update_ticks = clock.get_details().unwrap().last_value_update_ticks;
        (clock, initial_update_ticks)
    }

    fn duplicate_clock(clock: &zx::Clock) -> zx::Clock {
        clock.duplicate_handle(zx::Rights::SAME_RIGHTS).unwrap()
    }

    #[fasync::run_singlethreaded(test)]
    async fn successful_update_single_notify_client_with_monitor() {
        let (primary_clock, primary_ticks) = create_clock();
        let (monitor_clock, monitor_ticks) = create_clock();
        let rtc = FakeRtc::valid(*INVALID_RTC_TIME);
        let diagnostics = Arc::new(FakeDiagnostics::new());

        let (utc, utc_requests) =
            fidl::endpoints::create_proxy_and_stream::<ftime::UtcMarker>().unwrap();
        let monotonic_ref = zx::Time::get_monotonic();
        let interface_event_stream = network::create_stream_with_valid_interface();

        // Spawning test notifier and verifying initial state
        let notifier = Notifier::new(ftime::UtcSource::Backstop);
        notifier.handle_request_stream(utc_requests);
        assert_eq!(utc.watch_state().await.unwrap().source.unwrap(), ftime::UtcSource::Backstop);

        let _task = fasync::Task::spawn(maintain_utc(
            PrimaryTrack {
                clock: duplicate_clock(&primary_clock),
                time_source: FakeTimeSource::events(vec![
                    TimeSourceEvent::StatusChange { status: ftexternal::Status::Ok },
                    TimeSourceEvent::from(Sample::new(
                        monotonic_ref + OFFSET,
                        monotonic_ref,
                        STD_DEV,
                    )),
                ]),
                notifier: notifier.clone(),
            },
            Some(MonitorTrack {
                clock: duplicate_clock(&monitor_clock),
                time_source: FakeTimeSource::events(vec![
                    TimeSourceEvent::StatusChange { status: ftexternal::Status::Network },
                    TimeSourceEvent::StatusChange { status: ftexternal::Status::Ok },
                    TimeSourceEvent::from(Sample::new(
                        monotonic_ref + OFFSET_2,
                        monotonic_ref,
                        STD_DEV,
                    )),
                ]),
            }),
            Some(rtc.clone()),
            interface_event_stream,
            Arc::clone(&diagnostics),
        ));

        // Checking that the reported time source has been updated and the clocks are set.
        assert_eq!(utc.watch_state().await.unwrap().source.unwrap(), ftime::UtcSource::External);
        assert!(primary_clock.get_details().unwrap().last_value_update_ticks > primary_ticks);
        assert!(monitor_clock.get_details().unwrap().last_value_update_ticks > monitor_ticks);
        assert!(rtc.last_set().is_some());

        // Checking that the correct diagnostic events were logged.
        diagnostics.assert_events(&[
            Event::Initialized { clock_state: InitialClockState::NotSet },
            Event::InitializeRtc {
                outcome: InitializeRtcOutcome::InvalidBeforeBackstop,
                time: Some(*INVALID_RTC_TIME),
            },
            Event::NetworkAvailable,
            Event::TimeSourceStatus { role: Role::Primary, status: ftexternal::Status::Ok },
            Event::StartClock {
                track: Track::Primary,
                source: StartClockSource::External(Role::Primary),
            },
            Event::WriteRtc { outcome: WriteRtcOutcome::Succeeded },
            Event::TimeSourceStatus { role: Role::Monitor, status: ftexternal::Status::Network },
            Event::TimeSourceStatus { role: Role::Monitor, status: ftexternal::Status::Ok },
            Event::StartClock {
                track: Track::Monitor,
                source: StartClockSource::External(Role::Monitor),
            },
        ]);
    }

    #[test]
    fn no_update_invalid_rtc_single_notify_client() {
        let mut executor = fasync::Executor::new().unwrap();
        let (clock, initial_update_ticks) = create_clock();
        let rtc = FakeRtc::valid(*INVALID_RTC_TIME);
        let diagnostics = Arc::new(FakeDiagnostics::new());

        let (utc, utc_requests) =
            fidl::endpoints::create_proxy_and_stream::<ftime::UtcMarker>().unwrap();
        let time_source = FakeTimeSource::events(vec![TimeSourceEvent::StatusChange {
            status: ftexternal::Status::Network,
        }]);

        let interface_event_stream = network::create_stream_with_valid_interface();

        // Spawning test notifier and verifying the initial state
        let notifier = Notifier::new(ftime::UtcSource::Backstop);
        notifier.handle_request_stream(utc_requests);
        let mut fut1 = async { utc.watch_state().await.unwrap().source.unwrap() }.boxed();
        assert_eq!(executor.run_until_stalled(&mut fut1), Poll::Ready(ftime::UtcSource::Backstop));

        // Maintain UTC until no more work remains
        let mut fut2 = maintain_utc(
            PrimaryTrack {
                clock: duplicate_clock(&clock),
                time_source,
                notifier: notifier.clone(),
            },
            None,
            Some(rtc.clone()),
            interface_event_stream,
            Arc::clone(&diagnostics),
        )
        .boxed();
        let _ = executor.run_until_stalled(&mut fut2);

        // Checking that the reported time source has not been updated
        let mut fut3 = async { utc.watch_state().await.unwrap().source.unwrap() }.boxed();
        assert_eq!(executor.run_until_stalled(&mut fut3), Poll::Pending);

        // Checking that the clock has not been updated yet
        assert_eq!(initial_update_ticks, clock.get_details().unwrap().last_value_update_ticks);
        assert_eq!(rtc.last_set(), None);

        // Checking that the correct diagnostic events were logged.
        diagnostics.assert_events(&[
            Event::Initialized { clock_state: InitialClockState::NotSet },
            Event::InitializeRtc {
                outcome: InitializeRtcOutcome::InvalidBeforeBackstop,
                time: Some(*INVALID_RTC_TIME),
            },
            Event::NetworkAvailable,
            Event::TimeSourceStatus { role: Role::Primary, status: ftexternal::Status::Network },
        ]);
    }

    #[test]
    fn no_update_valid_rtc_single_notify_client() {
        let mut executor = fasync::Executor::new().unwrap();
        let (clock, initial_update_ticks) = create_clock();
        let rtc = FakeRtc::valid(*VALID_RTC_TIME);
        let diagnostics = Arc::new(FakeDiagnostics::new());

        let (utc, utc_requests) =
            fidl::endpoints::create_proxy_and_stream::<ftime::UtcMarker>().unwrap();
        let time_source = FakeTimeSource::events(vec![TimeSourceEvent::StatusChange {
            status: ftexternal::Status::Network,
        }]);

        let interface_event_stream = network::create_stream_with_valid_interface();

        // Spawning test notifier and verifying the initial state
        let notifier = Notifier::new(ftime::UtcSource::Backstop);
        notifier.handle_request_stream(utc_requests);
        let mut fut1 = async { utc.watch_state().await.unwrap().source.unwrap() }.boxed();
        assert_eq!(executor.run_until_stalled(&mut fut1), Poll::Ready(ftime::UtcSource::Backstop));

        // Maintain UTC until no more work remains
        let mut fut2 = maintain_utc(
            PrimaryTrack {
                clock: duplicate_clock(&clock),
                time_source,
                notifier: notifier.clone(),
            },
            None,
            Some(rtc.clone()),
            interface_event_stream,
            Arc::clone(&diagnostics),
        )
        .boxed();
        let _ = executor.run_until_stalled(&mut fut2);

        // Checking that the reported time source has been updated to reflect the use of RTC
        let mut fut3 = async { utc.watch_state().await.unwrap().source.unwrap() }.boxed();
        assert_eq!(
            executor.run_until_stalled(&mut fut3),
            Poll::Ready(ftime::UtcSource::Unverified)
        );

        // Checking that the clock was updated to use the valid RTC time.
        assert!(clock.get_details().unwrap().last_value_update_ticks > initial_update_ticks);
        assert!(clock.read().unwrap() >= *VALID_RTC_TIME);
        assert_eq!(rtc.last_set(), None);

        // Checking that the correct diagnostic events were logged.
        diagnostics.assert_events(&[
            Event::Initialized { clock_state: InitialClockState::NotSet },
            Event::InitializeRtc {
                outcome: InitializeRtcOutcome::Succeeded,
                time: Some(*VALID_RTC_TIME),
            },
            Event::StartClock { track: Track::Primary, source: StartClockSource::Rtc },
            Event::NetworkAvailable,
            Event::TimeSourceStatus { role: Role::Primary, status: ftexternal::Status::Network },
        ]);
    }

    #[test]
    fn no_update_clock_already_running() {
        let mut executor = fasync::Executor::new().unwrap();

        // Create a clock and set it slightly after backstop
        let (clock, _) = create_clock();
        clock
            .update(zx::ClockUpdate::new().value(*BACKSTOP_TIME + zx::Duration::from_millis(1)))
            .unwrap();
        let initial_update_ticks = clock.get_details().unwrap().last_value_update_ticks;
        let rtc = FakeRtc::valid(*VALID_RTC_TIME);
        let diagnostics = Arc::new(FakeDiagnostics::new());

        let (utc, utc_requests) =
            fidl::endpoints::create_proxy_and_stream::<ftime::UtcMarker>().unwrap();
        let time_source = FakeTimeSource::events(vec![TimeSourceEvent::StatusChange {
            status: ftexternal::Status::Network,
        }]);

        let interface_event_stream = network::create_stream_with_valid_interface();

        // Spawning test notifier and verifying the initial state
        let notifier = Notifier::new(ftime::UtcSource::Backstop);
        notifier.handle_request_stream(utc_requests);
        let mut fut1 = async { utc.watch_state().await.unwrap().source.unwrap() }.boxed();
        assert_eq!(executor.run_until_stalled(&mut fut1), Poll::Ready(ftime::UtcSource::Backstop));

        // Maintain UTC until no more work remains
        let mut fut2 = maintain_utc(
            PrimaryTrack {
                clock: duplicate_clock(&clock),
                time_source,
                notifier: notifier.clone(),
            },
            None,
            Some(rtc.clone()),
            interface_event_stream,
            Arc::clone(&diagnostics),
        )
        .boxed();
        let _ = executor.run_until_stalled(&mut fut2);

        // Checking that the reported time source has been updated to reflect the fact we're not
        // using backstop but can't verify whatever the previous source was.
        let mut fut3 = async { utc.watch_state().await.unwrap().source.unwrap() }.boxed();
        assert_eq!(
            executor.run_until_stalled(&mut fut3),
            Poll::Ready(ftime::UtcSource::Unverified)
        );
        // Checking that neither the clock nor the RTC were updated.
        assert_eq!(clock.get_details().unwrap().last_value_update_ticks, initial_update_ticks);
        assert_eq!(rtc.last_set(), None);

        // Checking that the correct diagnostic events were logged.
        diagnostics.assert_events(&[
            Event::Initialized { clock_state: InitialClockState::PreviouslySet },
            Event::InitializeRtc { outcome: InitializeRtcOutcome::ReadNotAttempted, time: None },
            Event::NetworkAvailable,
            Event::TimeSourceStatus { role: Role::Primary, status: ftexternal::Status::Network },
        ]);
    }

    #[test]
    fn test_initial_clock_state() {
        let clock =
            zx::Clock::create(zx::ClockOpts::empty(), Some(zx::Time::from_nanos(1_000))).unwrap();
        // The clock must be started with an initial value.
        clock.update(zx::ClockUpdate::new().value(zx::Time::from_nanos(1_000))).unwrap();
        assert_eq!(initial_clock_state(&clock), InitialClockState::NotSet);

        // Update the clock, which is already running.
        clock.update(zx::ClockUpdate::new().value(zx::Time::from_nanos(1_000_000))).unwrap();
        assert_eq!(initial_clock_state(&clock), InitialClockState::PreviouslySet);
    }
}
