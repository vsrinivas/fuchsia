// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_cobalt::LoggerFactoryMarker;
use fidl_fuchsia_logger::LogSinkMarker;
use fidl_fuchsia_net_interfaces::{StateRequest, StateRequestStream};
use fidl_fuchsia_time::{MaintenanceRequest, MaintenanceRequestStream};
use fidl_fuchsia_time_external::{Status, TimeSample};
use fidl_test_time::{TimeSourceControlRequest, TimeSourceControlRequestStream};
use fuchsia_async as fasync;
use fuchsia_component::{
    client::{App, AppBuilder},
    server::{NestedEnvironment, ServiceFs},
};
use fuchsia_zircon::{self as zx, HandleBased, Rights};
use futures::{
    channel::mpsc::Sender,
    future::join,
    stream::{StreamExt, TryStreamExt},
    Future, SinkExt,
};
use lazy_static::lazy_static;
use log::debug;
use push_source::{PushSource, TestUpdateAlgorithm, Update};
use std::sync::Arc;

/// Test manifest for timekeeper.
const TIMEKEEPER_URL: &str =
    "fuchsia-pkg://fuchsia.com/timekeeper-integration#meta/timekeeper_for_integration.cmx";

/// A reference to a timekeeper running inside a nested environment which runs fake versions of
/// the services timekeeper requires.
struct NestedTimekeeper {
    /// Application object for timekeeper. Needs to be kept in scope to
    /// keep timekeeper alive.
    _app: App,

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

impl NestedTimekeeper {
    /// Launches timekeeper in a nested environment alongside fake services it needs.
    /// Returns a `NestedTimekeeper`, and handles to the clock it maintains and the PushSource
    /// it obtains updates from.
    fn new() -> (Self, Arc<zx::Clock>, PushSourceController) {
        let mut service_fs = ServiceFs::new();
        // Route logs for components in nested env to the same logsink as the test.
        service_fs.add_proxy_service::<LogSinkMarker, _>();
        service_fs.add_proxy_service::<LoggerFactoryMarker, _>();
        // Inject test control and maintenence services.
        service_fs.add_fidl_service(InjectedServices::TimeSourceControl);
        service_fs.add_fidl_service(InjectedServices::Maintenance);
        service_fs.add_fidl_service(InjectedServices::Network);
        // TODO(satsukiu): add fake devfs.

        let nested_environment =
            service_fs.create_salted_nested_environment("timekeeper_test").unwrap();
        let app = AppBuilder::new(TIMEKEEPER_URL).spawn(nested_environment.launcher()).unwrap();

        let clock = Arc::new(zx::Clock::create(zx::ClockOpts::empty(), None).unwrap());
        let clock_clone = Arc::clone(&clock);
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
                            Self::serve_maintenance(Arc::clone(&clock_clone), stream).await;
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

        let nested_timekeeper = NestedTimekeeper {
            _app: app,
            _nested_envronment: nested_environment,
            _task: fasync::Task::spawn(async {
                join(injected_service_fut, push_source_update_fut).await;
            }),
        };
        (nested_timekeeper, clock, PushSourceController(update_sink))
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
        let mut app = self._app;
        app.kill().unwrap();
        app.wait().await.unwrap();
        let _ = self._task.cancel();
    }
}

/// Run a test against an instance of timekeeper. The provided `test_fn` is provided with handles
/// to inject samples and observe changes to the managed clock.
fn timekeeper_test<F, Fut>(test_fn: F)
where
    F: Fn(Arc<zx::Clock>, PushSourceController) -> Fut,
    Fut: Future,
{
    let _ = fuchsia_syslog::init();
    let mut executor = fasync::Executor::new().unwrap();
    executor.run_singlethreaded(async {
        let (timekeeper, clock, push_source_controller) = NestedTimekeeper::new();
        test_fn(clock, push_source_controller).await;
        timekeeper.teardown().await;
    });
}

lazy_static! {
    static ref TEST_UTC_TIMESTAMP: i64 =
        chrono::DateTime::parse_from_rfc2822("Tue, 29 Sep 2020 02:19:01 GMT")
            .unwrap()
            .timestamp_nanos();
}

#[test]
fn test_initial_update() {
    timekeeper_test(|clock, mut push_source_controller| async move {
        let before_update_ticks = clock.get_details().unwrap().last_value_update_ticks;

        let sample_monotonic = zx::Time::get(zx::ClockId::Monotonic).into_nanos();
        push_source_controller
            .set_sample(TimeSample {
                utc: Some(*TEST_UTC_TIMESTAMP),
                monotonic: Some(sample_monotonic),
            })
            .await;

        fasync::OnSignals::new(&*clock, zx::Signals::CLOCK_STARTED).await.unwrap();
        let after_update_ticks = clock.get_details().unwrap().last_value_update_ticks;
        assert!(after_update_ticks > before_update_ticks);

        // UTC time reported by the clock should be at least the time in the sample and no
        // more than the UTC time in the sample + time elapsed since the sample was created.
        let reported_utc = clock.read().unwrap().into_nanos();
        let monotonic_after_update = zx::Time::get(zx::ClockId::Monotonic).into_nanos();
        assert!(reported_utc >= *TEST_UTC_TIMESTAMP);
        assert!(reported_utc <= *TEST_UTC_TIMESTAMP + monotonic_after_update - sample_monotonic);
    });
}
