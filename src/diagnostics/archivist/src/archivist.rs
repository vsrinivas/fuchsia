// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    accessor::ArchiveAccessorServer,
    component_lifecycle::{self, TestControllerServer},
    error::Error,
    events::{
        router::{ConsumerConfig, EventRouter, ProducerConfig, RouterOptions},
        sources::{ComponentEventProvider, EventSource, LogConnector, UnattributedLogSinkSource},
        types::*,
    },
    identity::ComponentIdentity,
    inspect::repository::InspectRepository,
    logs::{budget::BudgetManager, repository::LogsRepository, servers::*, KernelDebugLog},
    pipeline::Pipeline,
};
use archivist_config::Config;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_sys2::EventSourceMarker;
use fidl_fuchsia_sys_internal as fsys_internal;
use fuchsia_async as fasync;
use fuchsia_component::{
    client::connect_to_protocol,
    server::{ServiceFs, ServiceObj},
};
use fuchsia_inspect::{component, health::Reporter};
use fuchsia_zircon as zx;
use futures::{
    channel::{mpsc, oneshot},
    future::{self, abortable},
    prelude::*,
};
use std::{path::Path, sync::Arc};
use tracing::{debug, error, info, warn};

/// Responsible for initializing an `Archivist` instance. Supports multiple configurations by
/// either calling or not calling methods on the builder like `serve_test_controller_protocol`.
pub struct Archivist {
    /// Handles event routing between archivist parts.
    event_router: EventRouter,

    /// Receive stop signal to kill this archivist.
    stop_recv: Option<oneshot::Receiver<()>>,

    /// Listens for lifecycle requests, to handle Stop requests.
    _lifecycle_task: Option<fasync::Task<()>>,

    /// Tasks that drains klog.
    _drain_klog_task: Option<fasync::Task<()>>,

    /// Tasks receiving external events from component manager and appmgr.
    incoming_external_event_producers: Vec<fasync::Task<()>>,

    /// The diagnostics pipelines that have been installed.
    pipelines: Vec<Arc<Pipeline>>,

    /// The repository holding Inspect data.
    _inspect_repository: Arc<InspectRepository>,

    /// The repository holding active log connections.
    logs_repository: Arc<LogsRepository>,

    /// The overall capacity we enforce for log messages across containers.
    logs_budget: BudgetManager,

    /// The server handling fuchsia.diagnostics.ArchiveAccessor
    accessor_server: Arc<ArchiveAccessorServer>,

    /// The server handling fuchsia.logger.Log
    log_server: Arc<LogServer>,

    /// The server handling fuchsia.diagnostics.LogSettings
    log_settings_server: Arc<LogSettingsServer>,

    /// The server handling fuchsia.diagnostics.test.Controller
    test_controller_server: Option<TestControllerServer>,

    /// The source that takes care of routing unattributed log sink request streams.
    unattributed_log_sink_source: Option<UnattributedLogSinkSource>,
}

impl Archivist {
    /// Creates new instance, sets up inspect and adds 'archive' directory to output folder.
    /// Also installs `fuchsia.diagnostics.Archive` service.
    /// Call `install_log_services`
    pub async fn new(config: &Config) -> Self {
        // Initialize the pipelines that the archivist will expose.
        let pipelines = Self::init_pipelines(config);

        // Initialize our data repositories.
        let logs_budget = BudgetManager::new(config.logs_max_cached_original_bytes as usize);
        let logs_repo = Arc::new(LogsRepository::new(&logs_budget, component::inspector().root()));
        let inspect_repo =
            Arc::new(InspectRepository::new(pipelines.iter().map(Arc::downgrade).collect()));

        // Initialize our FIDL servers. This doesn't start serving yet.
        let accessor_server =
            Arc::new(ArchiveAccessorServer::new(inspect_repo.clone(), logs_repo.clone()));
        let log_server = Arc::new(LogServer::new(logs_repo.clone()));
        let log_settings_server = Arc::new(LogSettingsServer::new(logs_repo.clone()));

        // Initialize the respective v1 and v2 protocols in charge of telling the archivist to
        // shutdown gracefully while draining existing log sinks and ensuring existing readers get
        // all the logs.
        assert!(
            !(config.install_controller && config.listen_to_lifecycle),
            "only one shutdown mechanism can be specified."
        );
        let (test_controller_server, _lifecycle_task, stop_recv) = if config.install_controller {
            let (s, r) = TestControllerServer::new();
            (Some(s), None, Some(r))
        } else if config.listen_to_lifecycle {
            debug!("Lifecycle listener initialized.");
            let (t, r) = component_lifecycle::serve_v2();
            (None, Some(t), Some(r))
        } else {
            (None, None, None)
        };

        // Initialize the core event router and the external event providers containing incoming
        // diagnostics directories and log sink connections.
        let mut event_router =
            EventRouter::new(component::inspector().root().create_child("events"));
        let incoming_external_event_producers =
            Self::initialize_external_event_sources(&mut event_router, config).await;
        event_router.add_consumer(ConsumerConfig {
            consumer: &logs_repo,
            events: vec![EventType::LogSinkRequested],
        });
        event_router.add_consumer(ConsumerConfig {
            consumer: &inspect_repo,
            events: vec![EventType::DiagnosticsReady],
        });

        // Initialize the unattributed log sink provider.
        let unattributed_log_sink_source = if config.serve_unattributed_logs {
            let mut source = UnattributedLogSinkSource::default();
            event_router.add_producer(ProducerConfig {
                producer: &mut source,
                events: vec![EventType::LogSinkRequested],
            });
            Some(source)
        } else {
            None
        };

        // Drain klog and publish it to syslog.
        if config.enable_klog {
            match KernelDebugLog::new().await {
                Ok(klog) => logs_repo.drain_debuglog(klog).await,
                Err(err) => warn!(
                    ?err,
                    "Failed to start the kernel debug log reader. Klog won't be in syslog"
                ),
            };
        }

        // Start related services that should start once the Archivist has started.
        for name in &config.bind_services {
            info!("Connecting to service {}", name);
            let (_local, remote) = zx::Channel::create().expect("cannot create channels");
            if let Err(e) = fdio::service_connect(&format!("/svc/{}", name), remote) {
                error!("Couldn't connect to service {}: {:?}", name, e);
            }
        }

        Self {
            test_controller_server,
            accessor_server,
            log_server,
            log_settings_server,
            event_router,
            stop_recv,
            _lifecycle_task,
            _drain_klog_task: None,
            incoming_external_event_producers,
            pipelines,
            _inspect_repository: inspect_repo,
            logs_repository: logs_repo,
            logs_budget,
            unattributed_log_sink_source,
        }
    }

    fn init_pipelines(config: &Config) -> Vec<Arc<Pipeline>> {
        let pipelines_node = component::inspector().root().create_child("pipelines");
        let accessor_stats_node =
            component::inspector().root().create_child("archive_accessor_stats");
        let pipelines_path = Path::new(&config.pipelines_path);
        let pipelines = [
            Pipeline::feedback(pipelines_path, &pipelines_node, &accessor_stats_node),
            Pipeline::legacy_metrics(pipelines_path, &pipelines_node, &accessor_stats_node),
            Pipeline::lowpan(pipelines_path, &pipelines_node, &accessor_stats_node),
            Pipeline::all_access(pipelines_path, &pipelines_node, &accessor_stats_node),
        ];

        if pipelines.iter().any(|p| p.config_has_error()) {
            component::health().set_unhealthy("Pipeline config has an error");
        } else {
            component::health().set_ok();
        }
        let pipelines = pipelines.into_iter().map(Arc::new).collect::<Vec<_>>();

        component::inspector().root().record(pipelines_node);
        component::inspector().root().record(accessor_stats_node);

        pipelines
    }

    pub async fn initialize_external_event_sources(
        event_router: &mut EventRouter,
        config: &Config,
    ) -> Vec<fasync::Task<()>> {
        let mut incoming_external_event_producers = vec![];
        if config.enable_component_event_provider {
            match connect_to_protocol::<fsys_internal::ComponentEventProviderMarker>() {
                Err(err) => {
                    warn!(?err, "Failed to connect to fuchsia.sys.internal.ComponentEventProvider");
                }
                Ok(proxy) => {
                    let mut component_event_provider = ComponentEventProvider::new(proxy);
                    event_router.add_producer(ProducerConfig {
                        producer: &mut component_event_provider,
                        events: vec![EventType::DiagnosticsReady],
                    });
                    incoming_external_event_producers.push(fasync::Task::spawn(async move {
                        component_event_provider.spawn().await.unwrap_or_else(|err| {
                            warn!(?err, "Failed to run component event provider loop");
                        });
                    }));
                }
            };
        }

        if config.enable_event_source {
            match EventSource::new(connect_to_protocol::<EventSourceMarker>().unwrap()).await {
                Err(err) => warn!(?err, "Failed to create event source"),
                Ok(mut event_source) => {
                    event_router.add_producer(ProducerConfig {
                        producer: &mut event_source,
                        events: vec![EventType::LogSinkRequested, EventType::DiagnosticsReady],
                    });
                    incoming_external_event_producers.push(fasync::Task::spawn(async move {
                        // This should never exit.
                        let _ = event_source.spawn().await;
                    }));
                }
            }
        }

        if config.enable_log_connector {
            match connect_to_protocol::<fsys_internal::LogConnectorMarker>() {
                Err(err) => {
                    warn!(?err, "Failed to connect to fuchsia.sys.internal.LogConnector");
                }
                Ok(proxy) => {
                    let mut connector = LogConnector::new(proxy);
                    event_router.add_producer(ProducerConfig {
                        producer: &mut connector,
                        events: vec![EventType::LogSinkRequested],
                    });
                    incoming_external_event_producers.push(fasync::Task::spawn(async move {
                        connector.spawn().await.unwrap_or_else(|err| {
                            warn!(?err, "Failed to run event source producer loop");
                        });
                    }));
                }
            }
        }

        incoming_external_event_producers
    }

    /// Run archivist to completion.
    /// # Arguments:
    /// * `outgoing_channel`- channel to serve outgoing directory on.
    pub async fn run(
        mut self,
        outgoing_channel: fidl::endpoints::ServerEnd<fio::DirectoryMarker>,
        router_opts: RouterOptions,
    ) -> Result<(), Error> {
        debug!("Running Archivist.");

        // Start servicing all outgoing services.
        let mut fs = ServiceFs::new();
        self.serve_protocols(&mut fs).await;
        fs.serve_connection(outgoing_channel).map_err(Error::ServeOutgoing)?;
        let run_outgoing = fs.collect::<()>();

        // Start the log budget component removal task.
        // TODO(fxbug.dev/92015): move this logic and the logs budget to the logs repository.
        let logs_repository = self.logs_repository.clone();
        let (snd, rcv) = mpsc::unbounded::<Arc<ComponentIdentity>>();
        self.logs_budget.set_remover(snd).await;
        let component_removal_task =
            fasync::Task::spawn(Self::process_removal_of_components(rcv, logs_repository.clone()));

        // Start ingesting events.
        let (terminate_handle, drain_events_fut) = self
            .event_router
            .start(router_opts)
            // panic: can only panic if we didn't register event producers and consumers correctly.
            .expect("Failed to start event router");
        let _event_routing_task = fasync::Task::spawn(async move {
            drain_events_fut.await;
        });

        let accessor_server = self.accessor_server.clone();
        let log_server = self.log_server.clone();
        let logs_repo = logs_repository.clone();
        let logs_budget = self.logs_budget.handle();
        let all_msg = async {
            logs_repo.wait_for_termination().await;
            debug!("Terminated logs");
            logs_budget.terminate().await;
            debug!("Flushing to listeners.");
            accessor_server.wait_for_servers_to_complete().await;
            log_server.wait_for_servers_to_complete().await;
            debug!("Log listeners and batch iterators stopped.");
            component_removal_task.cancel().await;
            debug!("Not processing more component removal requests.");
        };

        let (abortable_fut, abort_handle) = abortable(run_outgoing);

        let log_server = self.log_server;
        let accessor_server = self.accessor_server;
        let incoming_external_event_producers = self.incoming_external_event_producers;
        let stop_fut = match self.stop_recv {
            Some(stop_recv) => async move {
                stop_recv.into_future().await.ok();
                terminate_handle.terminate().await;
                for task in incoming_external_event_producers {
                    task.cancel().await;
                }
                log_server.stop().await;
                accessor_server.stop().await;
                logs_repository.stop_accepting_new_log_sinks().await;
                abort_handle.abort()
            }
            .left_future(),
            None => future::ready(()).right_future(),
        };

        info!("archivist: Entering core loop.");
        // Combine all three futures into a main future.
        future::join3(abortable_fut, stop_fut, all_msg).map(|_| Ok(())).await
    }

    async fn serve_protocols(&mut self, fs: &mut ServiceFs<ServiceObj<'static, ()>>) {
        component::serve_inspect_stats();
        inspect_runtime::serve(component::inspector(), fs)
            .unwrap_or_else(|err| warn!(?err, "failed to serve diagnostics"));

        let mut svc_dir = fs.dir("svc");

        // Serve fuchsia.diagnostics.ArchiveAccessors backed by a pipeline.
        for pipeline in &self.pipelines {
            let accessor_server = self.accessor_server.clone();
            let accessor_pipeline = pipeline.clone();
            svc_dir.add_fidl_service_at(pipeline.protocol_name(), move |stream| {
                accessor_server.spawn_server(accessor_pipeline.clone(), stream);
            });
        }

        // Serevr fuchsia.diagnostics.test.Controller.
        if let Some(mut server) = self.test_controller_server.take() {
            svc_dir.add_fidl_service(move |stream| {
                server.spawn(stream);
            });
        }

        // Ingest unattributed fuchsia.logger.LogSink connections.
        if let Some(mut unattributed_log_sink_source) = self.unattributed_log_sink_source.take() {
            svc_dir.add_fidl_service(move |stream| {
                debug!("unattributed fuchsia.logger.LogSink connection");
                futures::executor::block_on(unattributed_log_sink_source.new_connection(stream));
            });
        }

        // Server fuchsia.logger.Log
        let log_server = self.log_server.clone();
        svc_dir.add_fidl_service(move |stream| {
            debug!("fuchsia.logger.Log connection");
            log_server.spawn(stream);
        });

        // Server fuchsia.diagnostics.LogSettings
        let log_settings_server = self.log_settings_server.clone();
        svc_dir.add_fidl_service(move |stream| {
            debug!("fuchsia.diagnostics.LogSettings connection");
            log_settings_server.spawn(stream);
        });
    }

    async fn process_removal_of_components(
        mut removal_requests: mpsc::UnboundedReceiver<Arc<ComponentIdentity>>,
        logs_repo: Arc<LogsRepository>,
    ) {
        while let Some(identity) = removal_requests.next().await {
            if !logs_repo.is_live(&identity).await {
                debug!(%identity, "Removing component from repository.");
                logs_repo.remove(&identity).await;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        constants::*,
        events::router::{Dispatcher, EventProducer},
        logs::testing::*,
    };
    use fidl::endpoints::create_proxy;
    use fidl_fuchsia_diagnostics_test::ControllerMarker;
    use fidl_fuchsia_io as fio;
    use fuchsia_async as fasync;
    use fuchsia_component::client::connect_to_protocol_at_dir_svc;
    use futures::channel::oneshot;

    async fn init_archivist() -> Archivist {
        let config = Config {
            enable_component_event_provider: false,
            enable_klog: false,
            enable_event_source: false,
            enable_log_connector: false,
            install_controller: true,
            listen_to_lifecycle: false,
            log_to_debuglog: false,
            logs_max_cached_original_bytes: LEGACY_DEFAULT_MAXIMUM_CACHED_LOGS_BYTES as u64,
            serve_unattributed_logs: true,
            num_threads: 1,
            pipelines_path: DEFAULT_PIPELINES_PATH.into(),
            bind_services: vec![],
        };

        let mut archivist = Archivist::new(&config).await;
        // Install a fake producer that allows all incoming events. This allows skipping
        // validation for the purposes of the tests here.
        let mut fake_producer = FakeProducer {};
        archivist.event_router.add_producer(ProducerConfig {
            producer: &mut fake_producer,
            events: vec![EventType::LogSinkRequested, EventType::DiagnosticsReady],
        });

        archivist
    }

    struct FakeProducer {}

    impl EventProducer for FakeProducer {
        fn set_dispatcher(&mut self, _dispatcher: Dispatcher) {}
    }

    // run archivist and send signal when it dies.
    async fn run_archivist_and_signal_on_exit() -> (fio::DirectoryProxy, oneshot::Receiver<()>) {
        let (directory, server_end) = create_proxy::<fio::DirectoryMarker>().unwrap();
        let archivist = init_archivist().await;
        let (signal_send, signal_recv) = oneshot::channel();
        fasync::Task::spawn(async move {
            archivist
                .run(server_end, RouterOptions::default())
                .await
                .expect("Cannot run archivist");
            signal_send.send(()).unwrap();
        })
        .detach();
        (directory, signal_recv)
    }

    // runs archivist and returns its directory.
    async fn run_archivist() -> fio::DirectoryProxy {
        let (directory, server_end) = create_proxy::<fio::DirectoryMarker>().unwrap();
        let archivist = init_archivist().await;
        fasync::Task::spawn(async move {
            archivist
                .run(server_end, RouterOptions::default())
                .await
                .expect("Cannot run archivist");
        })
        .detach();
        directory
    }

    #[fuchsia::test]
    async fn can_log_and_retrive_log() {
        let directory = run_archivist().await;
        let mut recv_logs = start_listener(&directory);

        let mut log_helper = LogSinkHelper::new(&directory);
        log_helper.write_log("my msg1");
        log_helper.write_log("my msg2");

        assert_eq!(
            vec! {Some("my msg1".to_owned()),Some("my msg2".to_owned())},
            vec! {recv_logs.next().await,recv_logs.next().await}
        );

        // new client can log
        let mut log_helper2 = LogSinkHelper::new(&directory);
        log_helper2.write_log("my msg1");
        log_helper.write_log("my msg2");

        let mut expected = vec!["my msg1".to_owned(), "my msg2".to_owned()];
        expected.sort();

        let mut actual = vec![recv_logs.next().await.unwrap(), recv_logs.next().await.unwrap()];
        actual.sort();

        assert_eq!(expected, actual);

        // can log after killing log sink proxy
        log_helper.kill_log_sink();
        log_helper.write_log("my msg1");
        log_helper.write_log("my msg2");

        assert_eq!(
            expected,
            vec! {recv_logs.next().await.unwrap(),recv_logs.next().await.unwrap()}
        );

        // can log from new socket cnonnection
        log_helper2.add_new_connection();
        log_helper2.write_log("my msg1");
        log_helper2.write_log("my msg2");

        assert_eq!(
            expected,
            vec! {recv_logs.next().await.unwrap(),recv_logs.next().await.unwrap()}
        );
    }

    /// Makes sure that implementaion can handle multiple sockets from same
    /// log sink.
    #[fuchsia::test]
    async fn log_from_multiple_sock() {
        let directory = run_archivist().await;
        let mut recv_logs = start_listener(&directory);

        let log_helper = LogSinkHelper::new(&directory);
        let sock1 = log_helper.connect();
        let sock2 = log_helper.connect();
        let sock3 = log_helper.connect();

        LogSinkHelper::write_log_at(&sock1, "msg sock1-1");
        LogSinkHelper::write_log_at(&sock2, "msg sock2-1");
        LogSinkHelper::write_log_at(&sock1, "msg sock1-2");
        LogSinkHelper::write_log_at(&sock3, "msg sock3-1");
        LogSinkHelper::write_log_at(&sock2, "msg sock2-2");

        let mut expected = vec![
            "msg sock1-1".to_owned(),
            "msg sock1-2".to_owned(),
            "msg sock2-1".to_owned(),
            "msg sock2-2".to_owned(),
            "msg sock3-1".to_owned(),
        ];
        expected.sort();

        let mut actual = vec![
            recv_logs.next().await.unwrap(),
            recv_logs.next().await.unwrap(),
            recv_logs.next().await.unwrap(),
            recv_logs.next().await.unwrap(),
            recv_logs.next().await.unwrap(),
        ];
        actual.sort();

        assert_eq!(expected, actual);
    }

    /// Stop API works
    #[fuchsia::test]
    async fn stop_works() {
        let (directory, signal_recv) = run_archivist_and_signal_on_exit().await;
        let mut recv_logs = start_listener(&directory);

        {
            // make sure we can write logs
            let log_sink_helper = LogSinkHelper::new(&directory);
            let sock1 = log_sink_helper.connect();
            LogSinkHelper::write_log_at(&sock1, "msg sock1-1");
            log_sink_helper.write_log("msg sock1-2");
            let mut expected = vec!["msg sock1-1".to_owned(), "msg sock1-2".to_owned()];
            expected.sort();
            let mut actual = vec![recv_logs.next().await.unwrap(), recv_logs.next().await.unwrap()];
            actual.sort();
            assert_eq!(expected, actual);

            //  Start new connections and sockets
            let log_sink_helper1 = LogSinkHelper::new(&directory);
            let sock2 = log_sink_helper.connect();
            // Write logs before calling stop
            log_sink_helper1.write_log("msg 1");
            log_sink_helper1.write_log("msg 2");
            let log_sink_helper2 = LogSinkHelper::new(&directory);

            let controller = connect_to_protocol_at_dir_svc::<ControllerMarker>(&directory)
                .expect("cannot connect to log proxy");
            controller.stop().unwrap();

            // make more socket connections and write to them and old ones.
            let sock3 = log_sink_helper2.connect();
            log_sink_helper2.write_log("msg 3");
            log_sink_helper2.write_log("msg 4");

            LogSinkHelper::write_log_at(&sock3, "msg 5");
            LogSinkHelper::write_log_at(&sock2, "msg 6");
            log_sink_helper.write_log("msg 7");
            LogSinkHelper::write_log_at(&sock1, "msg 8");

            LogSinkHelper::write_log_at(&sock2, "msg 9");
        } // kills all sockets and log_sink connections
        let mut expected = vec![];
        let mut actual = vec![];
        for i in 1..=9 {
            expected.push(format!("msg {}", i));
            actual.push(recv_logs.next().await.unwrap());
        }
        expected.sort();
        actual.sort();

        // make sure archivist is dead.
        signal_recv.await.unwrap();

        assert_eq!(expected, actual);
    }
}
