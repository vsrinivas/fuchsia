// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    chrono::{Datelike, TimeZone, Timelike},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_cobalt::CobaltEvent,
    fidl_fuchsia_cobalt_test::{LogMethod, LoggerQuerierMarker, LoggerQuerierProxy},
    fidl_fuchsia_hardware_rtc::{DeviceRequest, DeviceRequestStream},
    fidl_fuchsia_io as fio,
    fidl_fuchsia_testing::{
        FakeClockControlMarker, FakeClockControlProxy, FakeClockMarker, FakeClockProxy,
    },
    fidl_fuchsia_time::{MaintenanceRequest, MaintenanceRequestStream},
    fidl_fuchsia_time_external::{PushSourceMarker, Status, TimeSample},
    fidl_test_time::{TimeSourceControlRequest, TimeSourceControlRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::{
        Capability, ChildOptions, ChildRef, LocalComponentHandles, RealmBuilder, RealmInstance,
        Ref, Route,
    },
    fuchsia_zircon::{self as zx, HandleBased, Rights},
    futures::{
        channel::mpsc::Sender,
        stream::{Stream, StreamExt, TryStreamExt},
        Future, FutureExt, SinkExt,
    },
    lazy_static::lazy_static,
    parking_lot::Mutex,
    push_source::{PushSource, TestUpdateAlgorithm, Update},
    std::{ops::Deref, sync::Arc},
    time_metrics_registry::PROJECT_ID,
    vfs::{directory::entry::DirectoryEntry, execution_scope::ExecutionScope, pseudo_directory},
};

/// URL for timekeeper.
const TIMEKEEPER_URL: &str =
    "fuchsia-pkg://fuchsia.com/timekeeper-integration#meta/timekeeper_for_integration.cm";
/// URL for timekeeper with fake time.
const TIMEKEEPER_FAKE_TIME_URL: &str =
    "fuchsia-pkg://fuchsia.com/timekeeper-integration#meta/timekeeper_with_fake_time.cm";
/// URL for fake cobalt.
const COBALT_URL: &str = "#meta/mock_cobalt.cm";
/// URL for the fake clock component.
const FAKE_CLOCK_URL: &str = "#meta/fake_clock.cm";

/// A reference to a timekeeper running inside a nested environment which runs fake versions of
/// the services timekeeper requires.
pub struct NestedTimekeeper {
    _realm_instance: RealmInstance,
}

impl NestedTimekeeper {
    /// Launches an instance of timekeeper maintaining the provided |clock| in a nested
    /// environment. If |initial_rtc_time| is provided, then the environment contains a fake RTC
    /// device that reports the time as |initial_rtc_time|.
    /// If use_fake_clock is true, also launches a fake clock service.
    /// Returns a `NestedTimekeeper`, handles to the PushSource and RTC it obtains updates from,
    /// Cobalt debug querier, and a fake clock control handle if use_fake_clock is true.
    pub async fn new(
        clock: Arc<zx::Clock>,
        initial_rtc_time: Option<zx::Time>,
        use_fake_clock: bool,
    ) -> (Self, Arc<PushSourcePuppet>, RtcUpdates, LoggerQuerierProxy, Option<FakeClockController>)
    {
        let push_source_puppet = Arc::new(PushSourcePuppet::new());

        let builder = RealmBuilder::new().await.unwrap();
        let mock_cobalt =
            builder.add_child("mock_cobalt", COBALT_URL, ChildOptions::new()).await.unwrap();

        let timekeeper_url = if use_fake_clock { TIMEKEEPER_FAKE_TIME_URL } else { TIMEKEEPER_URL };
        let timekeeper = builder
            .add_child("timekeeper_test", timekeeper_url, ChildOptions::new().eager())
            .await
            .unwrap();

        let timesource_server = builder
            .add_local_child(
                "timesource_mock",
                {
                    let push_source_puppet = Arc::clone(&push_source_puppet);
                    move |handles: LocalComponentHandles| {
                        Box::pin(timesource_mock_server(handles, Arc::clone(&push_source_puppet)))
                    }
                },
                ChildOptions::new(),
            )
            .await
            .unwrap();

        let maintenance_server = builder
            .add_local_child(
                "maintenance_mock",
                move |handles: LocalComponentHandles| {
                    Box::pin(maintenance_mock_server(handles, Arc::clone(&clock)))
                },
                ChildOptions::new(),
            )
            .await
            .unwrap();

        // Launch fake clock if needed.
        if use_fake_clock {
            let fake_clock =
                builder.add_child("fake_clock", FAKE_CLOCK_URL, ChildOptions::new()).await.unwrap();

            builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol_by_name(
                            "fuchsia.testing.FakeClockControl",
                        ))
                        .from(&fake_clock)
                        .to(Ref::parent()),
                )
                .await
                .unwrap();

            builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol_by_name("fuchsia.testing.FakeClock"))
                        .from(&fake_clock)
                        .to(Ref::parent())
                        .to(&timekeeper),
                )
                .await
                .unwrap();

            builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                        .from(Ref::parent())
                        .to(&fake_clock),
                )
                .await
                .unwrap();
        };

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.time.Maintenance"))
                    .from(&maintenance_server)
                    .to(&timekeeper),
            )
            .await
            .unwrap();

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("test.time.TimeSourceControl"))
                    .from(&timesource_server)
                    .to(&timekeeper),
            )
            .await
            .unwrap();

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.cobalt.test.LoggerQuerier"))
                    .from(&mock_cobalt)
                    .to(Ref::parent()),
            )
            .await
            .unwrap();

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.cobalt.LoggerFactory"))
                    .from(&mock_cobalt)
                    .to(&timekeeper),
            )
            .await
            .unwrap();

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .from(Ref::parent())
                    .to(&mock_cobalt)
                    .to(&timekeeper)
                    .to(&timesource_server)
                    .to(&maintenance_server),
            )
            .await
            .unwrap();

        let rtc_updates = setup_rtc(initial_rtc_time, &builder, &timekeeper).await;
        let realm_instance = builder.build().await.unwrap();

        let fake_clock_control = if use_fake_clock {
            let control_proxy = realm_instance
                .root
                .connect_to_protocol_at_exposed_dir::<FakeClockControlMarker>()
                .unwrap();
            let clock_proxy = realm_instance
                .root
                .connect_to_protocol_at_exposed_dir::<FakeClockMarker>()
                .unwrap();
            Some(FakeClockController { control_proxy, clock_proxy })
        } else {
            None
        };

        let cobalt_querier = realm_instance
            .root
            .connect_to_protocol_at_exposed_dir::<LoggerQuerierMarker>()
            .unwrap();

        let nested_timekeeper = Self { _realm_instance: realm_instance };

        (nested_timekeeper, push_source_puppet, rtc_updates, cobalt_querier, fake_clock_control)
    }
}

/// A `PushSource` that allows a single client and can be controlled by a test.
pub struct PushSourcePuppet {
    /// Internal state for the current PushSource. May be dropped and replaced
    /// to clear all state.
    inner: Mutex<PushSourcePuppetInner>,
    /// The number of client connections received over the lifetime of the puppet.
    cumulative_clients: Mutex<u32>,
}

impl PushSourcePuppet {
    /// Create a new `PushSourcePuppet`.
    fn new() -> Self {
        Self { inner: Mutex::new(PushSourcePuppetInner::new()), cumulative_clients: Mutex::new(0) }
    }

    /// Serve the `PushSource` service to a client.
    fn serve_client(&self, server_end: ServerEnd<PushSourceMarker>) {
        let mut inner = self.inner.lock();
        // Timekeeper should only need to connect to a push source once, except when it is
        // restarting a time source. This case appears to the test as a second connection to the
        // puppet. Since the puppet is restarted, all its state should be cleared as well.
        if inner.served_client() {
            *inner = PushSourcePuppetInner::new();
        }
        inner.serve_client(server_end);
        *self.cumulative_clients.lock() += 1;
    }

    /// Set the next sample reported by the time source.
    pub async fn set_sample(&self, sample: TimeSample) {
        self.inner.lock().update(sample.into()).await;
    }

    /// Set the next status reported by the time source.
    pub async fn set_status(&self, status: Status) {
        self.inner.lock().update(status.into()).await;
    }

    /// Simulate a crash by closing client channels and wiping state.
    pub fn simulate_crash(&self) {
        *self.inner.lock() = PushSourcePuppetInner::new();
        // This drops the old inner and cleans up any tasks it owns.
    }

    /// Returns the number of cumulative connections served. This allows asserting
    /// behavior such as whether Timekeeper has restarted a connection.
    pub fn lifetime_served_connections(&self) -> u32 {
        *self.cumulative_clients.lock()
    }
}

/// Internal state for a PushSourcePuppet. This struct contains a PushSource and
/// all Tasks needed for it to serve requests,
struct PushSourcePuppetInner {
    push_source: Arc<PushSource<TestUpdateAlgorithm>>,
    /// Tasks serving PushSource clients.
    tasks: Vec<fasync::Task<()>>,
    /// Sink through which updates are passed to the PushSource.
    update_sink: Sender<Update>,
}

impl PushSourcePuppetInner {
    fn new() -> Self {
        let (update_algorithm, update_sink) = TestUpdateAlgorithm::new();
        let push_source = Arc::new(PushSource::new(update_algorithm, Status::Ok).unwrap());
        let push_source_clone = Arc::clone(&push_source);
        let tasks = vec![fasync::Task::spawn(async move {
            push_source_clone.poll_updates().await.unwrap();
        })];
        Self { push_source, tasks, update_sink }
    }

    /// Returns true if this puppet has or is currently serving a client.
    fn served_client(&self) -> bool {
        self.tasks.len() > 1
    }

    /// Serve the `PushSource` service to a client.
    fn serve_client(&mut self, server_end: ServerEnd<PushSourceMarker>) {
        let push_source_clone = Arc::clone(&self.push_source);
        self.tasks.push(fasync::Task::spawn(async move {
            push_source_clone
                .handle_requests_for_stream(server_end.into_stream().unwrap())
                .await
                .unwrap();
        }));
    }

    /// Send an update to the PushSource.
    async fn update(&mut self, update: Update) {
        self.update_sink.send(update).await.unwrap()
    }
}

/// The list of RTC update requests received by a `NestedTimekeeper`.
#[derive(Clone, Debug)]
pub struct RtcUpdates(Arc<Mutex<Vec<fidl_fuchsia_hardware_rtc::Time>>>);

impl RtcUpdates {
    /// Get all received RTC times as a vec.
    pub fn to_vec(&self) -> Vec<fidl_fuchsia_hardware_rtc::Time> {
        self.0.lock().clone()
    }
}

/// A wrapper around a `FakeClockControlProxy` that also allows a client to read the current fake
/// time.
pub struct FakeClockController {
    control_proxy: FakeClockControlProxy,
    clock_proxy: FakeClockProxy,
}

impl Deref for FakeClockController {
    type Target = FakeClockControlProxy;

    fn deref(&self) -> &Self::Target {
        &self.control_proxy
    }
}

impl FakeClockController {
    pub async fn get_monotonic(&self) -> Result<i64, fidl::Error> {
        self.clock_proxy.get().await
    }
}

async fn setup_rtc(
    initial_rtc_time: Option<zx::Time>,
    builder: &RealmBuilder,
    timekeeper: &ChildRef,
) -> RtcUpdates {
    let rtc_updates = RtcUpdates(Arc::new(Mutex::new(vec![])));

    let rtc_dir = match initial_rtc_time {
        Some(initial_time) => pseudo_directory! {
            "class" => pseudo_directory! {
                "rtc" => pseudo_directory! {
                    "000" => vfs::service::host({
                        let rtc_updates = rtc_updates.clone();
                        move |stream| {
                            serve_fake_rtc(initial_time, rtc_updates.clone(), stream)
                        }
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

    let fake_rtc_server = builder
        .add_local_child(
            "fake_rtc",
            {
                move |handles| {
                    let rtc_dir = rtc_dir.clone();
                    async move {
                        let _ = &handles;
                        let scope = ExecutionScope::new();
                        let (client_end, server_end) =
                            fidl::endpoints::create_endpoints::<fio::DirectoryMarker>().unwrap();
                        let () = rtc_dir.open(
                            scope.clone(),
                            fio::OpenFlags::RIGHT_READABLE
                                | fio::OpenFlags::RIGHT_WRITABLE
                                | fio::OpenFlags::RIGHT_EXECUTABLE,
                            0,
                            vfs::path::Path::dot(),
                            ServerEnd::new(server_end.into_channel()),
                        );
                        let mut fs = ServiceFs::new();
                        fs.add_remote("dev", client_end.into_proxy().unwrap());
                        fs.serve_connection(handles.outgoing_dir)
                            .expect("failed to serve fake RTC ServiceFs");
                        fs.collect::<()>().await;
                        Ok(())
                    }
                    .boxed()
                }
            },
            ChildOptions::new().eager(),
        )
        .await
        .unwrap();

    builder
        .add_route(
            Route::new()
                .capability(Capability::directory("dev").path("/dev").rights(fio::RW_STAR_DIR))
                .from(&fake_rtc_server)
                .to(&*timekeeper),
        )
        .await
        .unwrap();

    rtc_updates
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

async fn serve_test_control(puppet: &PushSourcePuppet, stream: TimeSourceControlRequestStream) {
    stream
        .try_for_each_concurrent(None, |req| async {
            let _ = &req;
            let TimeSourceControlRequest::ConnectPushSource { push_source, .. } = req;
            puppet.serve_client(push_source);
            Ok(())
        })
        .await
        .unwrap();
}

async fn serve_maintenance(clock_handle: Arc<zx::Clock>, mut stream: MaintenanceRequestStream) {
    while let Some(req) = stream.try_next().await.unwrap() {
        let MaintenanceRequest::GetWritableUtcClock { responder } = req;
        responder.send(clock_handle.duplicate_handle(Rights::SAME_RIGHTS).unwrap()).unwrap();
    }
}

async fn timesource_mock_server(
    handles: LocalComponentHandles,
    push_source_puppet: Arc<PushSourcePuppet>,
) -> Result<(), anyhow::Error> {
    let mut fs = ServiceFs::new();
    let mut tasks = vec![];

    fs.dir("svc").add_fidl_service(move |stream: TimeSourceControlRequestStream| {
        let puppet_clone = Arc::clone(&push_source_puppet);

        tasks.push(fasync::Task::local(async move {
            serve_test_control(&*puppet_clone, stream).await;
        }));
    });

    fs.serve_connection(handles.outgoing_dir)?;
    fs.collect::<()>().await;

    Ok(())
}

async fn maintenance_mock_server(
    handles: LocalComponentHandles,
    clock: Arc<zx::Clock>,
) -> Result<(), anyhow::Error> {
    let mut fs = ServiceFs::new();
    let mut tasks = vec![];

    fs.dir("svc").add_fidl_service(move |stream: MaintenanceRequestStream| {
        let clock_clone = Arc::clone(&clock);

        tasks.push(fasync::Task::local(async move {
            serve_maintenance(clock_clone, stream).await;
        }));
    });

    fs.serve_connection(handles.outgoing_dir)?;
    fs.collect::<()>().await;

    Ok(())
}

fn from_rfc2822(date: &str) -> zx::Time {
    zx::Time::from_nanos(chrono::DateTime::parse_from_rfc2822(date).unwrap().timestamp_nanos())
}

lazy_static! {
    pub static ref BACKSTOP_TIME: zx::Time = from_rfc2822("Sun, 20 Sep 2020 01:01:01 GMT");
    pub static ref VALID_RTC_TIME: zx::Time = from_rfc2822("Sun, 20 Sep 2020 02:02:02 GMT");
    pub static ref BEFORE_BACKSTOP_TIME: zx::Time = from_rfc2822("Fri, 06 Mar 2020 04:04:04 GMT");
    pub static ref VALID_TIME: zx::Time = from_rfc2822("Tue, 29 Sep 2020 02:19:01 GMT");
    pub static ref VALID_TIME_2: zx::Time = from_rfc2822("Wed, 30 Sep 2020 14:59:59 GMT");
}

/// Time between each reported sample.
pub const BETWEEN_SAMPLES: zx::Duration = zx::Duration::from_seconds(5);

/// The standard deviation to report on valid time samples.
pub const STD_DEV: zx::Duration = zx::Duration::from_millis(50);

/// Create a new clock with backstop time set to `BACKSTOP_TIME`.
pub fn new_clock() -> Arc<zx::Clock> {
    Arc::new(zx::Clock::create(zx::ClockOpts::empty(), Some(*BACKSTOP_TIME)).unwrap())
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

pub fn rtc_time_to_zx_time(rtc_time: fidl_fuchsia_hardware_rtc::Time) -> zx::Time {
    let date = chrono::Utc
        .ymd(rtc_time.year as i32, rtc_time.month as u32, rtc_time.day as u32)
        .and_hms(rtc_time.hours as u32, rtc_time.minutes as u32, rtc_time.seconds as u32);
    zx::Time::from_nanos(date.timestamp_nanos())
}

/// Create a stream of CobaltEvents from a proxy.
pub fn create_cobalt_event_stream(
    proxy: Arc<LoggerQuerierProxy>,
    log_method: LogMethod,
) -> std::pin::Pin<Box<dyn Stream<Item = CobaltEvent>>> {
    async_utils::hanging_get::client::HangingGetStream::new(proxy, move |p| {
        p.watch_logs2(PROJECT_ID, log_method)
    })
    .map(|res| futures::stream::iter(res.unwrap().0))
    .flatten()
    .boxed()
}

const RETRY_WAIT_DURATION: zx::Duration = zx::Duration::from_millis(10);

/// Poll `poll_fn` until it returns Some() and return the returned value.
pub async fn poll_until_some<T, F>(poll_fn: F) -> T
where
    F: Fn() -> Option<T>,
{
    loop {
        match poll_fn() {
            Some(value) => return value,
            None => fasync::Timer::new(fasync::Time::after(RETRY_WAIT_DURATION)).await,
        }
    }
}

/// Poll `poll_fn` until it returns true.
pub async fn poll_until_async<F, Fut>(poll_fn: F)
where
    F: Fn() -> Fut,
    Fut: Future<Output = bool>,
{
    while !poll_fn().await {
        fasync::Timer::new(fasync::Time::after(RETRY_WAIT_DURATION)).await
    }
}

/// Poll `poll_fn` until it returns true.
pub async fn poll_until<F: Fn() -> bool>(poll_fn: F) {
    poll_until_async(|| futures::future::ready(poll_fn())).await
}
