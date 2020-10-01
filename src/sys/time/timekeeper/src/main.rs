// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

//! `timekeeper` is responsible for external time synchronization in Fuchsia.

mod diagnostics;
mod enums;
mod network;
mod notifier;
mod rtc;
mod time_source;
mod time_source_manager;

use {
    crate::{
        diagnostics::{
            CobaltDiagnostics, CompositeDiagnostics, Diagnostics, Event, InspectDiagnostics,
        },
        enums::{
            InitialClockState, InitializeRtcOutcome, Role, StartClockSource, Track, WriteRtcOutcome,
        },
        network::{event_stream, wait_for_network_available},
        notifier::Notifier,
        rtc::{Rtc, RtcImpl},
        time_source::{PushTimeSource, Sample, TimeSource},
        time_source_manager::TimeSourceManager,
    },
    anyhow::{Context as _, Error},
    chrono::prelude::*,
    fidl_fuchsia_net_interfaces as finterfaces, fidl_fuchsia_time as ftime,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::StreamExt as _,
    log::{error, info, warn},
    std::sync::Arc,
};

/// URL of the primary time source. In the future, this value belongs in a config file.
const PRIMARY_SOURCE: &str =
    "fuchsia-pkg://fuchsia.com/network-time-service#meta/network_time_service.cmx";
/// URL of the dev time source used as the primary source for integ tests.
const INTEG_DEV_TIME_SOURCE: &str =
    "fuchsia-pkg://fuchsia.com/timekeeper-integration#meta/dev_time_source.cmx";

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
    let utc_clock = Arc::new(zx::Clock::from(
        time_maintainer
            .get_writable_utc_clock()
            .await
            .context("failed to get UTC clock from maintainer")?,
    ));

    info!("initializing diagnostics and serving inspect on servicefs");
    let diagnostics = Arc::new(CompositeDiagnostics::new(
        InspectDiagnostics::new(diagnostics::INSPECTOR.root(), Arc::clone(&utc_clock)),
        CobaltDiagnostics::new(),
    ));
    let mut fs = ServiceFs::new();
    diagnostics::INSPECTOR.serve(&mut fs)?;

    let notifier = Notifier::new(match initial_clock_state(&*utc_clock) {
        InitialClockState::NotSet => ftime::UtcSource::Backstop,
        InitialClockState::PreviouslySet => ftime::UtcSource::Unverified,
    });

    info!("connecting to real time clock");
    let optional_rtc = match RtcImpl::only_device() {
        Ok(rtc) => Some(rtc),
        Err(err) => {
            warn!("failed to connect to RTC, ZX_CLOCK_UTC won't be updated: {}", err);
            diagnostics.record(Event::InitializeRtc { outcome: err.into(), time: None });
            None
        }
    };

    let primary_source_url = match options.dev_time_sources {
        true => INTEG_DEV_TIME_SOURCE,
        false => PRIMARY_SOURCE,
    };
    let primary_source = PushTimeSource::new(primary_source_url.to_string());
    let interface_state_service =
        fuchsia_component::client::connect_to_service::<finterfaces::StateMarker>()
            .context("failed to connect to fuchsia.net.interfaces/State")?;
    let interface_event_stream = event_stream(&interface_state_service)?;
    let notifier_clone = notifier.clone();

    fasync::Task::spawn(async move {
        maintain_utc(
            utc_clock,
            optional_rtc,
            notifier_clone,
            primary_source,
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

fn initial_clock_state(utc_clock: &zx::Clock) -> InitialClockState {
    let clock_details = utc_clock.get_details().expect("failed to get UTC clock details");
    // When the clock is first initialized to the backstop time, its synthetic offset should
    // be identical. Once the clock is updated, this is no longer true.
    if clock_details.backstop.into_nanos() == clock_details.ticks_to_synthetic.synthetic_offset {
        InitialClockState::NotSet
    } else {
        InitialClockState::PreviouslySet
    }
}

/// The top-level control loop for time synchronization.
///
/// Checks for network connectivity before attempting any time updates.
///
/// Maintains the utc clock using updates received over the `fuchsia.time.external` protocols.
async fn maintain_utc<R, T, S, D>(
    utc_clock: Arc<zx::Clock>,
    mut optional_rtc: Option<R>,
    notifs: Notifier,
    primary_source: T,
    interface_event_stream: S,
    diagnostics: Arc<D>,
) where
    R: Rtc,
    T: TimeSource,
    S: futures::Stream<Item = Result<finterfaces::Event, Error>>,
    D: Diagnostics,
{
    info!("record the state at initialization.");
    diagnostics.record(Event::Initialized { clock_state: initial_clock_state(&*utc_clock) });

    // At the moment, for the purpose of diagnostics, we consider "starting the clock" to be setting
    // our first value, even if looks like the clock was already running when we came to life. We
    // should reconsider this decision (and the use of RTC in this case) when Timekeeper is
    // responsibile for starting the clock ticking.
    let mut clock_started = false;
    let backstop = utc_clock.get_details().expect("failed to get UTC clock details").backstop;
    if let Some(rtc) = &optional_rtc.as_mut() {
        info!("reading initial RTC time.");
        match rtc.get().await {
            Err(err) => {
                warn!("failed to read RTC time: {}", err);
                diagnostics.record(Event::InitializeRtc {
                    outcome: InitializeRtcOutcome::ReadFailed,
                    time: None,
                });
            }
            Ok(time) => {
                let utc_chrono = Utc.timestamp_nanos(time.into_nanos());
                if time < backstop {
                    warn!("initial RTC time before backstop: {}", utc_chrono);
                    diagnostics.record(Event::InitializeRtc {
                        outcome: InitializeRtcOutcome::InvalidBeforeBackstop,
                        time: Some(time),
                    });
                } else {
                    diagnostics.record(Event::InitializeRtc {
                        outcome: InitializeRtcOutcome::Succeeded,
                        time: Some(time),
                    });
                    if let Err(status) = utc_clock.update(zx::ClockUpdate::new().value(time)) {
                        warn!(
                            "failed to start UTC clock from RTC at time {}: {}",
                            utc_chrono, status
                        );
                    } else {
                        notifs.set_source(ftime::UtcSource::Unverified).await;
                        diagnostics.record(Event::StartClock {
                            track: Track::Primary,
                            source: StartClockSource::Rtc,
                        });
                        info!("started UTC clock from RTC at time: {}", utc_chrono);
                        clock_started = true;
                    }
                };
            }
        }
    }

    info!("launching time source manager...");
    let mut primary_source_manager =
        TimeSourceManager::new(backstop, Role::Primary, primary_source, Arc::clone(&diagnostics));
    primary_source_manager.warm_up();

    info!("waiting for network connectivity before attempting network time sync...");
    match wait_for_network_available(interface_event_stream).await {
        Ok(()) => diagnostics.record(Event::NetworkAvailable),
        Err(why) => warn!("failed to wait for network, attempted to sync time anyway: {:?}", why),
    }

    let mut received_valid_update: bool = false;
    loop {
        let Sample { utc, .. } = primary_source_manager.next_sample().await;
        let utc_chrono = Utc.timestamp_nanos(utc.into_nanos());
        if received_valid_update {
            // Until we can handle blending clock updates we only use the first valid
            // sample we receive, but we still log additional updates.
            info!("received time update to {}", utc_chrono);
            continue;
        }

        if let Err(status) = utc_clock.update(zx::ClockUpdate::new().value(utc)) {
            error!("failed to update UTC clock to {}: {}", utc_chrono, status);
        } else if !clock_started {
            diagnostics.record(Event::StartClock {
                track: Track::Primary,
                source: StartClockSource::External(Role::Primary),
            });
            info!("started UTC time from external source at time {}", utc_chrono);
            clock_started = true;
        } else {
            diagnostics.record(Event::UpdateClock { track: Track::Primary });
            info!("adjusted UTC time to {}", utc_chrono);
        }
        if let Some(ref rtc) = optional_rtc {
            let outcome = match rtc.set(utc).await {
                Err(err) => {
                    error!("failed to update RTC and ZX_CLOCK_UTC to {}: {}", utc_chrono, err);
                    WriteRtcOutcome::Failed
                }
                Ok(()) => {
                    info!("updated RTC to {}", utc_chrono);
                    WriteRtcOutcome::Succeeded
                }
            };
            diagnostics.record(Event::WriteRtc { outcome });
        }
        let monotonic_before = zx::Time::get(zx::ClockId::Monotonic).into_nanos();
        let utc_now = Utc::now().timestamp_nanos();
        let monotonic_after = zx::Time::get(zx::ClockId::Monotonic).into_nanos();
        info!(
            "fxbug.dev/4753:monotonic_before={}:utc={}:monotonic_after={}",
            monotonic_before, utc_now, monotonic_after,
        );
        notifs.set_source(ftime::UtcSource::External).await;
        received_valid_update = true;
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::diagnostics::FakeDiagnostics,
        crate::enums::{
            InitialClockState, InitializeRtcOutcome, StartClockSource, WriteRtcOutcome,
        },
        crate::rtc::FakeRtc,
        crate::time_source::Event as TimeSourceEvent,
        crate::time_source::FakeTimeSource,
        fidl_fuchsia_time_external as ftexternal, fuchsia_zircon as zx,
        futures::FutureExt,
        lazy_static::lazy_static,
        std::task::Poll,
    };

    lazy_static! {
        static ref INVALID_RTC_TIME: zx::Time = zx::Time::from_nanos(111111);
        static ref BACKSTOP_TIME: zx::Time = zx::Time::from_nanos(222222);
        static ref VALID_RTC_TIME: zx::Time = zx::Time::from_nanos(333333);
        static ref UPDATE_TIME: zx::Time = zx::Time::from_nanos(444444);
        static ref UPDATE_TIME_2: zx::Time = zx::Time::from_nanos(555555);
        static ref CLOCK_OPTS: zx::ClockOpts = zx::ClockOpts::empty();
    }

    /// Creates and starts a new clock with default options, returning a tuple of the clock and its
    /// initial update time in ticks.
    fn create_clock() -> (Arc<zx::Clock>, i64) {
        let clock = Arc::new(zx::Clock::create(*CLOCK_OPTS, Some(*BACKSTOP_TIME)).unwrap());
        clock.update(zx::ClockUpdate::new().value(*BACKSTOP_TIME)).unwrap();
        let initial_update_ticks = clock.get_details().unwrap().last_value_update_ticks;
        (clock, initial_update_ticks)
    }

    #[fasync::run_singlethreaded(test)]
    async fn successful_update_single_notify_client() {
        let (clock, initial_update_ticks) = create_clock();
        let rtc = FakeRtc::valid(*INVALID_RTC_TIME);
        let diagnostics = Arc::new(FakeDiagnostics::new());

        let (utc, utc_requests) =
            fidl::endpoints::create_proxy_and_stream::<ftime::UtcMarker>().unwrap();
        let time_source = FakeTimeSource::events(vec![
            TimeSourceEvent::StatusChange { status: ftexternal::Status::Ok },
            TimeSourceEvent::from(Sample {
                utc: *UPDATE_TIME,
                monotonic: zx::Time::get(zx::ClockId::Monotonic),
            }),
        ]);

        let interface_event_stream = network::create_stream_with_valid_interface();

        // Spawning test notifier and verifying initial state
        let notifier = Notifier::new(ftime::UtcSource::Backstop);
        notifier.handle_request_stream(utc_requests);
        assert_eq!(utc.watch_state().await.unwrap().source.unwrap(), ftime::UtcSource::Backstop);

        let _task = fasync::Task::spawn(maintain_utc(
            Arc::clone(&clock),
            Some(rtc.clone()),
            notifier.clone(),
            time_source,
            interface_event_stream,
            Arc::clone(&diagnostics),
        ));

        // Checking that the reported time source has been updated to the first input
        assert_eq!(utc.watch_state().await.unwrap().source.unwrap(), ftime::UtcSource::External);
        assert!(clock.get_details().unwrap().last_value_update_ticks > initial_update_ticks);
        assert_eq!(rtc.last_set(), Some(*UPDATE_TIME));

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
            Arc::clone(&clock),
            Some(rtc.clone()),
            notifier.clone(),
            time_source,
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
            Arc::clone(&clock),
            Some(rtc.clone()),
            notifier.clone(),
            time_source,
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
