// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use chrono::{Datelike, TimeZone, Timelike};
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_cobalt::{CobaltEvent, LoggerFactoryMarker};
use fidl_fuchsia_cobalt_test::{LogMethod, LoggerQuerierMarker, LoggerQuerierProxy};
use fidl_fuchsia_hardware_rtc::{DeviceRequest, DeviceRequestStream};
use fidl_fuchsia_io::{NodeMarker, MODE_TYPE_DIRECTORY, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE};
use fidl_fuchsia_logger::LogSinkMarker;
use fidl_fuchsia_net_interfaces::{StateRequest, StateRequestStream};
use fidl_fuchsia_time::{MaintenanceRequest, MaintenanceRequestStream};
use fidl_fuchsia_time_external::{Status, TimeSample};
use fidl_test_time::{TimeSourceControlRequest, TimeSourceControlRequestStream};
use fuchsia_async as fasync;
use fuchsia_cobalt::CobaltEventExt;
use fuchsia_component::{
    client::{launcher, App, AppBuilder},
    server::{NestedEnvironment, ServiceFs},
};
use fuchsia_zircon::{self as zx, HandleBased, Rights};
use futures::{
    channel::mpsc::Sender,
    future::join,
    stream::{Stream, StreamExt, TryStreamExt},
    Future, FutureExt, SinkExt,
};
use lazy_static::lazy_static;
use log::debug;
use parking_lot::Mutex;
use push_source::{PushSource, TestUpdateAlgorithm, Update};
use std::sync::Arc;
use test_util::{assert_geq, assert_leq};
use time_metrics_registry::{
    RealTimeClockEventsMetricDimensionEventType as RtcEventType,
    TimekeeperLifecycleEventsMetricDimensionEventType as LifecycleEventType, PROJECT_ID,
    REAL_TIME_CLOCK_EVENTS_METRIC_ID, TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID,
};
use vfs::{directory::entry::DirectoryEntry, pseudo_directory};

/// Test manifest for timekeeper.
const TIMEKEEPER_URL: &str =
    "fuchsia-pkg://fuchsia.com/timekeeper-integration#meta/timekeeper_for_integration.cmx";
/// Manifest for fake cobalt.
const COBALT_URL: &str = "fuchsia-pkg://fuchsia.com/mock_cobalt#meta/mock_cobalt.cmx";

/// A reference to a timekeeper running inside a nested environment which runs fake versions of
/// the services timekeeper requires.
struct NestedTimekeeper {
    /// Application object for timekeeper. Needs to be kept in scope to
    /// keep timekeeper alive.
    _timekeeper_app: App,

    /// Application object for Cobalt. Needs to be kept in scope to keep Cobalt alive.
    _cobalt_app: App,

    /// The nested environment timekeeper is running in. Needs to be kept
    /// in scope to keep the nested environment alive.
    _nested_envronment: NestedEnvironment,

    /// Task running fake services injected into the nested environment.
    _task: fasync::Task<()>,
}

/// Services injected and implemented by `NestedTimekeeper`.
enum InjectedServices {
    TimeSourceControl(TimeSourceControlRequestStream),
    Maintenance(MaintenanceRequestStream),
    Network(StateRequestStream),
}

/// A handle to a `PushSource` controlled by the integ test that allows setting the latest data on
/// the fly.
struct PushSourceController(Sender<Update>);

impl PushSourceController {
    async fn set_sample(&mut self, sample: TimeSample) {
        self.0.send(sample.into()).await.unwrap();
    }

    #[allow(dead_code)]
    async fn set_status(&mut self, status: Status) {
        self.0.send(status.into()).await.unwrap();
    }
}

/// The list of RTC update requests recieved by a `NestedTimekeeper`.
#[derive(Clone, Debug)]
struct RtcUpdates(Arc<Mutex<Vec<fidl_fuchsia_hardware_rtc::Time>>>);

impl RtcUpdates {
    /// Get all received RTC times as a vec.
    fn to_vec(&self) -> Vec<fidl_fuchsia_hardware_rtc::Time> {
        self.0.lock().clone()
    }
}

impl NestedTimekeeper {
    /// Launches an instance of timekeeper maintaining the provided |clock| in a nested
    /// environment. If |initial_rtc_time| is provided, then the environment contains a fake RTC
    /// device that reports the time as |initial_rtc_time|.
    /// Returns a `NestedTimekeeper`, handles to the PushSource and RTC it obtains updates from,
    /// and a connection to a fake cobalt instance.
    fn new(
        clock: Arc<zx::Clock>,
        initial_rtc_time: Option<zx::Time>,
    ) -> (Self, PushSourceController, RtcUpdates, LoggerQuerierProxy) {
        let mut service_fs = ServiceFs::new();
        // Route logs for components in nested env to the same logsink as the test.
        service_fs.add_proxy_service::<LogSinkMarker, _>();
        // Launch a new instance of cobalt for each environment. This allows verifying
        // the events cobalt receives for each test case.
        let cobalt_app = AppBuilder::new(COBALT_URL).spawn(&launcher().unwrap()).unwrap();
        service_fs.add_proxy_service_to::<LoggerFactoryMarker, _>(Arc::clone(
            cobalt_app.directory_request(),
        ));
        // Inject test control and maintenence services.
        service_fs.add_fidl_service(InjectedServices::TimeSourceControl);
        service_fs.add_fidl_service(InjectedServices::Maintenance);
        service_fs.add_fidl_service(InjectedServices::Network);
        // Inject fake devfs.
        let rtc_updates = RtcUpdates(Arc::new(Mutex::new(vec![])));
        let rtc_update_clone = rtc_updates.clone();
        let (devmgr_client, devmgr_server) = create_endpoints::<NodeMarker>().unwrap();
        let fake_devfs = match initial_rtc_time {
            Some(initial_time) => pseudo_directory! {
                "class" => pseudo_directory! {
                    "rtc" => pseudo_directory! {
                        "000" => vfs::service::host(move |stream| {
                            debug!("Fake RTC connected.");
                            Self::serve_fake_rtc(initial_time, rtc_update_clone.clone(), stream)
                        })
                    }
                }
            },
            None => pseudo_directory! {
                "class" => pseudo_directory! {
                    "rtc" => pseudo_directory! {
                    }
                }
            },
        };
        fake_devfs.open(
            vfs::execution_scope::ExecutionScope::new(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            vfs::path::Path::empty(),
            devmgr_server,
        );

        let nested_environment =
            service_fs.create_salted_nested_environment("timekeeper_test").unwrap();
        let timekeeper_app = AppBuilder::new(TIMEKEEPER_URL)
            .add_handle_to_namespace("/dev".to_string(), devmgr_client.into_handle())
            .spawn(nested_environment.launcher())
            .unwrap();

        let (update_algorithm, update_sink) = TestUpdateAlgorithm::new();
        let push_source = Arc::new(PushSource::new(update_algorithm, Status::Ok).unwrap());
        let push_source_clone = Arc::clone(&push_source);
        let push_source_update_fut = async move {
            push_source_clone.poll_updates().await.unwrap();
        };

        let injected_service_fut = async move {
            service_fs
                .for_each_concurrent(None, |conn_req| async {
                    match conn_req {
                        InjectedServices::TimeSourceControl(stream) => {
                            debug!("Time source control service connected.");
                            Self::serve_test_control(&*push_source, stream).await;
                        }
                        InjectedServices::Maintenance(stream) => {
                            debug!("Maintenance service connected.");
                            Self::serve_maintenance(Arc::clone(&clock), stream).await;
                        }
                        // Timekeeper uses the network state service to wait util the network is
                        // available. Since this isn't a hard dependency, timekeeper continues on
                        // to poll samples anyway even if the network service fails after
                        // connecting. Therefore, the fake injected by the test accepts
                        // connections, holds the stream long enough for the single required
                        // request to occur, then drops the channel. This provides the minimal
                        // implentation needed to bypass the network check.
                        // This can be removed once timekeeper is not responsible for the network
                        // check.
                        InjectedServices::Network(mut stream) => {
                            debug!("Network state service connected.");
                            if let Some(req) = stream.try_next().await.unwrap() {
                                let StateRequest::GetWatcher { watcher: _watcher, .. } = req;
                                debug!("Network watcher service connected.");
                            }
                        }
                    }
                })
                .await;
        };

        let cobalt_querier = cobalt_app.connect_to_service::<LoggerQuerierMarker>().unwrap();

        let nested_timekeeper = NestedTimekeeper {
            _timekeeper_app: timekeeper_app,
            _cobalt_app: cobalt_app,
            _nested_envronment: nested_environment,
            _task: fasync::Task::spawn(async {
                join(injected_service_fut, push_source_update_fut).await;
            }),
        };

        (nested_timekeeper, PushSourceController(update_sink), rtc_updates, cobalt_querier)
    }

    async fn serve_test_control(
        push_source: &PushSource<TestUpdateAlgorithm>,
        stream: TimeSourceControlRequestStream,
    ) {
        stream
            .try_for_each_concurrent(None, |req| async move {
                let TimeSourceControlRequest::ConnectPushSource { push_source: client_end, .. } =
                    req;
                push_source
                    .handle_requests_for_stream(client_end.into_stream().unwrap())
                    .await
                    .unwrap();
                Ok(())
            })
            .await
            .unwrap();
    }

    async fn serve_fake_rtc(
        initial_time: zx::Time,
        rtc_updates: RtcUpdates,
        mut stream: DeviceRequestStream,
    ) {
        while let Some(req) = stream.try_next().await.unwrap() {
            match req {
                DeviceRequest::Get { responder } => {
                    // Since timekeeper only pulls a time off of the RTC device once on startup, we
                    // don't attempt to update the sent time.
                    responder.send(&mut zx_time_to_rtc_time(initial_time)).unwrap();
                }
                DeviceRequest::Set { rtc, responder } => {
                    rtc_updates.0.lock().push(rtc);
                    responder.send(zx::Status::OK.into_raw()).unwrap();
                }
            }
        }
    }

    async fn serve_maintenance(clock_handle: Arc<zx::Clock>, mut stream: MaintenanceRequestStream) {
        while let Some(req) = stream.try_next().await.unwrap() {
            let MaintenanceRequest::GetWritableUtcClock { responder } = req;
            responder.send(clock_handle.duplicate_handle(Rights::SAME_RIGHTS).unwrap()).unwrap();
        }
    }

    /// Cleanly tear down the timekeeper and fakes. This is done manually so that timekeeper is
    /// always torn down first, avoiding the situation where timekeeper sees a dependency close
    /// its channel and log an error in response.
    async fn teardown(self) {
        let mut app = self._timekeeper_app;
        app.kill().unwrap();
        app.wait().await.unwrap();
        let _ = self._task.cancel();
    }
}

fn zx_time_to_rtc_time(zx_time: zx::Time) -> fidl_fuchsia_hardware_rtc::Time {
    let date = chrono::Utc.timestamp_nanos(zx_time.into_nanos());
    fidl_fuchsia_hardware_rtc::Time {
        seconds: date.second() as u8,
        minutes: date.minute() as u8,
        hours: date.hour() as u8,
        day: date.day() as u8,
        month: date.month() as u8,
        year: date.year() as u16,
    }
}

fn rtc_time_to_zx_time(rtc_time: fidl_fuchsia_hardware_rtc::Time) -> zx::Time {
    let date = chrono::Utc
        .ymd(rtc_time.year as i32, rtc_time.month as u32, rtc_time.day as u32)
        .and_hms(rtc_time.hours as u32, rtc_time.minutes as u32, rtc_time.seconds as u32);
    zx::Time::from_nanos(date.timestamp_nanos())
}

/// Run a test against an instance of timekeeper. Timekeeper will maintain the provided clock.
/// If `initial_rtc_time` is provided, a fake RTC device that reports the time as
/// `initial_rtc_time` is injected into timekeeper's environment. The provided `test_fn` is
/// provided with handles to manipulate the time source and observe changes to the RTC and cobalt.
fn timekeeper_test<F, Fut>(clock: Arc<zx::Clock>, initial_rtc_time: Option<zx::Time>, test_fn: F)
where
    F: FnOnce(PushSourceController, RtcUpdates, LoggerQuerierProxy) -> Fut,
    Fut: Future,
{
    let _ = fuchsia_syslog::init();
    let mut executor = fasync::Executor::new().unwrap();
    executor.run_singlethreaded(async move {
        let clock_arc = Arc::new(clock);
        let (timekeeper, push_source_controller, rtc, cobalt) =
            NestedTimekeeper::new(Arc::clone(&clock_arc), initial_rtc_time);
        test_fn(push_source_controller, rtc, cobalt).await;
        timekeeper.teardown().await;
    });
}

lazy_static! {
    static ref BACKSTOP_TIME: zx::Time = zx::Time::from_nanos(
        chrono::DateTime::parse_from_rfc2822("Sun, 20 Sep 2020 01:01:01 GMT")
            .unwrap()
            .timestamp_nanos()
    );
    static ref VALID_RTC_TIME: zx::Time = zx::Time::from_nanos(
        chrono::DateTime::parse_from_rfc2822("Sun, 20 Sep 2020 02:02:02 GMT")
            .unwrap()
            .timestamp_nanos()
    );
    static ref BEFORE_BACKSTOP_TIME: zx::Time = zx::Time::from_nanos(
        chrono::DateTime::parse_from_rfc2822("Fri, 06 Mar 2020 04:04:04 GMT")
            .unwrap()
            .timestamp_nanos()
    );
    static ref VALID_TIME: zx::Time = zx::Time::from_nanos(
        chrono::DateTime::parse_from_rfc2822("Tue, 29 Sep 2020 02:19:01 GMT")
            .unwrap()
            .timestamp_nanos()
    );
}

fn new_clock() -> Arc<zx::Clock> {
    Arc::new(zx::Clock::create(zx::ClockOpts::empty(), Some(*BACKSTOP_TIME)).unwrap())
}

/// Retry an async `poll_fn` until it returns Some. Returns the contents of the `Some` value
/// produced by `poll_fn`.
async fn poll_until<T, F, Fut>(poll_fn: F) -> T
where
    F: Fn() -> Fut,
    Fut: Future<Output = Option<T>>,
{
    const RETRY_WAIT_DURATION: zx::Duration = zx::Duration::from_millis(10);
    loop {
        match poll_fn().await {
            Some(value) => return value,
            None => fasync::Timer::new(fasync::Time::after(RETRY_WAIT_DURATION)).await,
        }
    }
}

/// Poll `poll_fn` until it returns true.
async fn wait_until<F: Fn() -> bool>(poll_fn: F) {
    poll_until(|| async {
        match poll_fn() {
            false => None,
            true => Some(()),
        }
    })
    .await;
}

/// Create a stream of CobaltEvents from a proxy.
fn create_cobalt_event_stream(
    proxy: Arc<LoggerQuerierProxy>,
    log_method: LogMethod,
) -> std::pin::Pin<Box<dyn Stream<Item = CobaltEvent>>> {
    // mock_cobalt contains a race condition where it returns an error if timekeeper hasn't
    // connected yet (fxbug.dev/45615). Therefore, we retry the first watch.
    let proxy_clone = Arc::clone(&proxy);
    let first_poll_stream = poll_until(move || {
        let clone = Arc::clone(&proxy_clone);
        clone
            .watch_logs(PROJECT_ID, log_method)
            .map(|res| res.unwrap().ok().map(|(events, _)| events))
    })
    .into_stream();

    let subsequent_polls_stream =
        async_utils::hanging_get::client::GeneratedFutureStream::new(Box::new(move || {
            Some(proxy.watch_logs(PROJECT_ID, log_method).map(|res| res.unwrap().unwrap().0))
        }));

    first_poll_stream.chain(subsequent_polls_stream).map(futures::stream::iter).flatten().boxed()
}

#[test]
fn test_no_rtc_start_clock_from_time_source() {
    let clock = new_clock();
    timekeeper_test(Arc::clone(&clock), None, |mut push_source_controller, _, cobalt| async move {
        let before_update_ticks = clock.get_details().unwrap().last_value_update_ticks;

        let sample_monotonic = zx::Time::get_monotonic();
        push_source_controller
            .set_sample(TimeSample {
                utc: Some(VALID_TIME.into_nanos()),
                monotonic: Some(sample_monotonic.into_nanos()),
                standard_deviation: None,
            })
            .await;

        fasync::OnSignals::new(&*clock, zx::Signals::CLOCK_STARTED).await.unwrap();
        let after_update_ticks = clock.get_details().unwrap().last_value_update_ticks;
        assert!(after_update_ticks > before_update_ticks);

        // UTC time reported by the clock should be at least the time in the sample and no
        // more than the UTC time in the sample + time elapsed since the sample was created.
        let reported_utc = clock.read().unwrap();
        let monotonic_after_update = zx::Time::get_monotonic();
        assert_geq!(reported_utc, *VALID_TIME);
        assert_leq!(reported_utc, *VALID_TIME + (monotonic_after_update - sample_monotonic));

        let cobalt_event_stream =
            create_cobalt_event_stream(Arc::new(cobalt), LogMethod::LogCobaltEvent);
        assert_eq!(
            cobalt_event_stream.take(3).collect::<Vec<_>>().await,
            vec![
                CobaltEvent::builder(REAL_TIME_CLOCK_EVENTS_METRIC_ID)
                    .with_event_codes(vec![RtcEventType::NoDevices])
                    .as_event(),
                CobaltEvent::builder(TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID)
                    .with_event_codes(vec![LifecycleEventType::InitializedBeforeUtcStart])
                    .as_event(),
                CobaltEvent::builder(TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID)
                    .with_event_codes(vec![LifecycleEventType::StartedUtcFromTimeSource])
                    .as_event(),
            ]
        );
    });
}

#[test]
fn test_invalid_rtc_start_clock_from_time_source() {
    let clock = new_clock();
    timekeeper_test(
        Arc::clone(&clock),
        Some(*BEFORE_BACKSTOP_TIME),
        |mut push_source_controller, rtc_updates, cobalt| async move {
            let mut cobalt_event_stream =
                create_cobalt_event_stream(Arc::new(cobalt), LogMethod::LogCobaltEvent);
            // Timekeeper should reject the RTC time.
            assert_eq!(
                cobalt_event_stream.by_ref().take(2).collect::<Vec<CobaltEvent>>().await,
                vec![
                    CobaltEvent::builder(TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID)
                        .with_event_codes(vec![LifecycleEventType::InitializedBeforeUtcStart])
                        .as_event(),
                    CobaltEvent::builder(REAL_TIME_CLOCK_EVENTS_METRIC_ID)
                        .with_event_codes(vec![RtcEventType::ReadInvalidBeforeBackstop])
                        .as_event()
                ]
            );

            let sample_monotonic = zx::Time::get_monotonic();
            push_source_controller
                .set_sample(TimeSample {
                    utc: Some(VALID_TIME.into_nanos()),
                    monotonic: Some(sample_monotonic.into_nanos()),
                    standard_deviation: None,
                })
                .await;

            // Timekeeper should accept the time from the time source.
            fasync::OnSignals::new(&*clock, zx::Signals::CLOCK_STARTED).await.unwrap();
            // UTC time reported by the clock should be at least the time reported by the time
            // source, and no more than the UTC time reported by the time source + time elapsed
            // since the time was read.
            let reported_utc = clock.read().unwrap();
            let monotonic_after = zx::Time::get_monotonic();
            assert_geq!(reported_utc, *VALID_TIME);
            assert_leq!(reported_utc, *VALID_TIME + (monotonic_after - sample_monotonic));
            // RTC should also be set.
            let rtc_update = poll_until(|| async { rtc_updates.to_vec().pop() }).await;
            let monotonic_after_rtc_set = zx::Time::get(zx::ClockId::Monotonic);
            let rtc_reported_utc = rtc_time_to_zx_time(rtc_update);
            assert_geq!(rtc_reported_utc, *VALID_TIME);
            assert_leq!(
                rtc_reported_utc,
                *VALID_TIME + (monotonic_after_rtc_set - sample_monotonic)
            );
            assert_eq!(
                cobalt_event_stream.take(2).collect::<Vec<_>>().await,
                vec![
                    CobaltEvent::builder(TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID)
                        .with_event_codes(vec![LifecycleEventType::StartedUtcFromTimeSource])
                        .as_event(),
                    CobaltEvent::builder(REAL_TIME_CLOCK_EVENTS_METRIC_ID)
                        .with_event_codes(vec![RtcEventType::WriteSucceeded])
                        .as_event()
                ]
            );
        },
    );
}

#[test]
fn test_start_clock_from_rtc() {
    let clock = new_clock();
    let monotonic_before = zx::Time::get_monotonic();
    timekeeper_test(
        Arc::clone(&clock),
        Some(*VALID_RTC_TIME),
        |mut push_source_controller, rtc_updates, cobalt| async move {
            let mut cobalt_event_stream =
                create_cobalt_event_stream(Arc::new(cobalt), LogMethod::LogCobaltEvent);

            // Clock should start from the time read off the RTC.
            fasync::OnSignals::new(&*clock, zx::Signals::CLOCK_STARTED).await.unwrap();

            // UTC time reported by the clock should be at least the time reported by the RTC, and no
            // more than the UTC time reported by the RTC + time elapsed since Timekeeper was launched.
            let reported_utc = clock.read().unwrap();
            let monotonic_after = zx::Time::get_monotonic();
            assert_geq!(reported_utc, *VALID_RTC_TIME);
            assert_leq!(reported_utc, *VALID_RTC_TIME + (monotonic_after - monotonic_before));

            assert_eq!(
                cobalt_event_stream.by_ref().take(3).collect::<Vec<CobaltEvent>>().await,
                vec![
                    CobaltEvent::builder(TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID)
                        .with_event_codes(vec![LifecycleEventType::InitializedBeforeUtcStart])
                        .as_event(),
                    CobaltEvent::builder(REAL_TIME_CLOCK_EVENTS_METRIC_ID)
                        .with_event_codes(vec![RtcEventType::ReadSucceeded])
                        .as_event(),
                    CobaltEvent::builder(TIMEKEEPER_LIFECYCLE_EVENTS_METRIC_ID)
                        .with_event_codes(vec![LifecycleEventType::StartedUtcFromRtc])
                        .as_event(),
                ]
            );

            // Clock should be updated again when the push source reports another time.
            let clock_last_set_ticks = clock.get_details().unwrap().last_value_update_ticks;
            let sample_monotonic = zx::Time::get_monotonic();
            push_source_controller
                .set_sample(TimeSample {
                    utc: Some(VALID_TIME.into_nanos()),
                    monotonic: Some(sample_monotonic.into_nanos()),
                    standard_deviation: None,
                })
                .await;
            wait_until(|| {
                clock.get_details().unwrap().last_value_update_ticks != clock_last_set_ticks
            })
            .await;
            let clock_utc = clock.read().unwrap();
            let monotonic_after_read = zx::Time::get_monotonic();
            assert_geq!(clock_utc, *VALID_TIME);
            assert_leq!(clock_utc, *VALID_TIME + (monotonic_after_read - sample_monotonic));
            // RTC should be set too.
            let rtc_update = poll_until(|| async { rtc_updates.to_vec().pop() }).await;
            let monotonic_after_rtc_set = zx::Time::get(zx::ClockId::Monotonic);
            let rtc_reported_utc = rtc_time_to_zx_time(rtc_update);
            assert_geq!(rtc_reported_utc, *VALID_TIME);
            assert_leq!(
                rtc_reported_utc,
                *VALID_TIME + (monotonic_after_rtc_set - sample_monotonic)
            );

            assert_eq!(
                cobalt_event_stream.by_ref().take(1).collect::<Vec<CobaltEvent>>().await,
                vec![CobaltEvent::builder(REAL_TIME_CLOCK_EVENTS_METRIC_ID)
                    .with_event_codes(vec![RtcEventType::WriteSucceeded])
                    .as_event(),]
            );
        },
    );
}
