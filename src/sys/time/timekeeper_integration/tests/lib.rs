// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod timekeeper_integration;

use chrono::{Datelike, TimeZone, Timelike};
use fidl::endpoints::{create_endpoints, ServerEnd};
use fidl_fuchsia_cobalt::{CobaltEvent, LoggerFactoryMarker};
use fidl_fuchsia_cobalt_test::{LogMethod, LoggerQuerierMarker, LoggerQuerierProxy};
use fidl_fuchsia_hardware_rtc::{DeviceRequest, DeviceRequestStream};
use fidl_fuchsia_io::{NodeMarker, MODE_TYPE_DIRECTORY, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE};
use fidl_fuchsia_logger::LogSinkMarker;
use fidl_fuchsia_time::{MaintenanceRequest, MaintenanceRequestStream};
use fidl_fuchsia_time_external::{PushSourceMarker, Status, TimeSample};
use fidl_test_time::{TimeSourceControlRequest, TimeSourceControlRequestStream};
use fuchsia_async as fasync;
use fuchsia_component::{
    client::{launcher, App, AppBuilder},
    server::{NestedEnvironment, ServiceFs},
};
use fuchsia_zircon::{self as zx, HandleBased, Rights};
use futures::{
    channel::mpsc::{channel, Receiver, Sender},
    stream::{Stream, StreamExt, TryStreamExt},
    FutureExt, SinkExt,
};
use lazy_static::lazy_static;
use log::{debug, info};
use parking_lot::Mutex;
use push_source::{PushSource, TestUpdateAlgorithm, Update};
use std::sync::Arc;
use time_metrics_registry::PROJECT_ID;
use vfs::{directory::entry::DirectoryEntry, pseudo_directory};

/// URL for timekeeper.
const TIMEKEEPER_URL: &str =
    "fuchsia-pkg://fuchsia.com/timekeeper-integration#meta/timekeeper_for_integration.cmx";
/// URL for fake cobalt.
const COBALT_URL: &str = "fuchsia-pkg://fuchsia.com/mock_cobalt#meta/mock_cobalt.cmx";

/// A reference to a timekeeper running inside a nested environment which runs fake versions of
/// the services timekeeper requires.
pub struct NestedTimekeeper {
    /// Application object for timekeeper. Kept in memory to keep the component alive.
    _timekeeper_app: App,

    /// Application objects for additional launched components. Kept in memory to keep components
    /// alive.
    _launched_apps: Vec<App>,

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
}

/// A `PushSource` that allows a single client and can be controlled by a test.
pub struct PushSourcePuppet {
    /// Channel through which connection requests are received.
    client_recv: Receiver<ServerEnd<PushSourceMarker>>,
    /// Push source implementation.
    push_source: Arc<PushSource<TestUpdateAlgorithm>>,
    /// Sender to push updates to `push_source`.
    update_sink: Sender<Update>,
    /// Task for retrieving updates in `push_source`.
    /// Kept in memory to ensure that the push source serves requests.
    _update_task: fasync::Task<()>,
    /// Task serving the client.
    client_task: Option<fasync::Task<()>>,
}

impl PushSourcePuppet {
    /// Create a new `PushSourcePuppet` that receives new client channels through `client_recv`.
    fn new(client_recv: Receiver<ServerEnd<PushSourceMarker>>) -> Self {
        let (update_algorithm, update_sink) = TestUpdateAlgorithm::new();
        let push_source = Arc::new(PushSource::new(update_algorithm, Status::Ok).unwrap());
        let push_source_clone = Arc::clone(&push_source);
        let update_task = fasync::Task::spawn(async move {
            push_source_clone.poll_updates().await.unwrap();
        });
        Self { client_recv, push_source, update_sink, _update_task: update_task, client_task: None }
    }

    /// Set the next sample reported by the time source.
    pub async fn set_sample(&mut self, sample: TimeSample) {
        self.ensure_client().await;
        self.update_sink.send(sample.into()).await.unwrap();
    }

    /// Set the next status reported by the time source.
    #[allow(dead_code)]
    pub async fn set_status(&mut self, status: Status) {
        self.ensure_client().await;
        self.update_sink.send(status.into()).await.unwrap();
    }

    /// Wait for a client to connect if there's no existing client.
    pub async fn ensure_client(&mut self) {
        if self.client_task.is_none() {
            let server_end = self.client_recv.next().await.unwrap();
            let push_source_clone = Arc::clone(&self.push_source);
            self.client_task.replace(fasync::Task::spawn(async move {
                push_source_clone
                    .handle_requests_for_stream(server_end.into_stream().unwrap())
                    .await
                    .unwrap();
            }));
        }
    }

    /// Simulate a crash by closing client channels and wiping state.
    pub async fn simulate_crash(&mut self) {
        let (update_algorithm, update_sink) = TestUpdateAlgorithm::new();
        self.update_sink = update_sink;
        self.push_source = Arc::new(PushSource::new(update_algorithm, Status::Ok).unwrap());
        self.client_task.take();
        let push_source_clone = Arc::clone(&self.push_source);
        self._update_task = fasync::Task::spawn(async move {
            push_source_clone.poll_updates().await.unwrap();
        });
    }
}

/// The list of RTC update requests recieved by a `NestedTimekeeper`.
#[derive(Clone, Debug)]
pub struct RtcUpdates(Arc<Mutex<Vec<fidl_fuchsia_hardware_rtc::Time>>>);

impl RtcUpdates {
    /// Get all received RTC times as a vec.
    pub fn to_vec(&self) -> Vec<fidl_fuchsia_hardware_rtc::Time> {
        self.0.lock().clone()
    }
}

impl NestedTimekeeper {
    /// Launches an instance of timekeeper maintaining the provided |clock| in a nested
    /// environment. If |initial_rtc_time| is provided, then the environment contains a fake RTC
    /// device that reports the time as |initial_rtc_time|.
    /// Returns a `NestedTimekeeper`, handles to the PushSource and RTC it obtains updates from,
    /// and Cobalt debug querier.
    pub fn new(
        clock: Arc<zx::Clock>,
        initial_rtc_time: Option<zx::Time>,
    ) -> (Self, PushSourcePuppet, RtcUpdates, LoggerQuerierProxy) {
        let mut service_fs = ServiceFs::new();
        // Route logs for components in nested env to the same logsink as the test.
        service_fs.add_proxy_service::<LogSinkMarker, _>();

        let mut launched_apps = vec![];
        // Launch a new instance of cobalt for each environment. This allows verifying
        // the events cobalt receives for each test case.
        let cobalt_app = AppBuilder::new(COBALT_URL).spawn(&launcher().unwrap()).unwrap();
        service_fs.add_proxy_service_to::<LoggerFactoryMarker, _>(Arc::clone(
            cobalt_app.directory_request(),
        ));
        let cobalt_querier = cobalt_app.connect_to_service::<LoggerQuerierMarker>().unwrap();
        launched_apps.push(cobalt_app);

        // Inject test control and maintenence services.
        service_fs.add_fidl_service(InjectedServices::TimeSourceControl);
        service_fs.add_fidl_service(InjectedServices::Maintenance);
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

        let (server_end_send, server_end_recv) = channel(0);

        let injected_service_fut = async move {
            service_fs
                .for_each_concurrent(None, |conn_req| async {
                    match conn_req {
                        InjectedServices::TimeSourceControl(stream) => {
                            debug!("Time source control service connected.");
                            Self::serve_test_control(server_end_send.clone(), stream).await;
                        }
                        InjectedServices::Maintenance(stream) => {
                            debug!("Maintenance service connected.");
                            Self::serve_maintenance(Arc::clone(&clock), stream).await;
                        }
                    }
                })
                .await;
        };

        let nested_timekeeper = NestedTimekeeper {
            _timekeeper_app: timekeeper_app,
            _launched_apps: launched_apps,
            _nested_envronment: nested_environment,
            _task: fasync::Task::spawn(injected_service_fut),
        };

        (nested_timekeeper, PushSourcePuppet::new(server_end_recv), rtc_updates, cobalt_querier)
    }

    async fn serve_test_control(
        server_end_sender: Sender<ServerEnd<PushSourceMarker>>,
        stream: TimeSourceControlRequestStream,
    ) {
        stream
            .try_for_each_concurrent(None, |req| async {
                let TimeSourceControlRequest::ConnectPushSource { push_source, .. } = req;
                server_end_sender.clone().send(push_source).await.unwrap();
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
                    info!("Sent response from fake RTC.");
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
    pub async fn teardown(self) {
        let mut app = self._timekeeper_app;
        app.kill().unwrap();
        app.wait().await.unwrap();
        let _ = self._task.cancel();
    }
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
    async_utils::hanging_get::client::GeneratedFutureStream::new(Box::new(move || {
        Some(proxy.watch_logs2(PROJECT_ID, log_method).map(|res| res.unwrap().0))
    }))
    .map(futures::stream::iter)
    .flatten()
    .boxed()
}
