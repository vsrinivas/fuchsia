// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        accessor::ArchiveAccessorServer,
        component_lifecycle::{self, TestControllerServer},
        diagnostics,
        error::Error,
        events::{
            router::{ConsumerConfig, EventRouter, ProducerConfig, RouterOptions},
            sources::{
                ComponentEventProvider, EventSource, LogConnector, UnattributedLogSinkSource,
            },
            types::*,
        },
        identity::ComponentIdentity,
        inspect::repository::InspectRepository,
        logs::{budget::BudgetManager, repository::LogsRepository, KernelDebugLog},
        pipeline::Pipeline,
    },
    archivist_config::Config,
    fidl_fuchsia_io as fio,
    fidl_fuchsia_sys2::EventSourceMarker,
    fidl_fuchsia_sys_internal as fsys_internal,
    fuchsia_async::{self as fasync, Task},
    fuchsia_component::{
        client::connect_to_protocol,
        server::{ServiceFs, ServiceObj},
    },
    fuchsia_inspect::{component, health::Reporter},
    futures::{
        channel::{mpsc, oneshot},
        future::{self, abortable},
        prelude::*,
    },
    std::{path::Path, sync::Arc},
    tracing::{debug, error, info, warn},
};

/// Responsible for initializing an `Archivist` instance. Supports multiple configurations by
/// either calling or not calling methods on the builder like `serve_test_controller_protocol`.
pub struct Archivist {
    /// ServiceFs object to server outgoing directory.
    fs: ServiceFs<ServiceObj<'static, ()>>,

    /// Sender which is used to close the stream of Log connections after ingestion of logsink
    /// completes.
    ///
    /// Clones of the sender keep the receiver end of the channel open. As soon
    /// as all clones are dropped or disconnected, the receiver will close. The
    /// receiver must close for `Archivist::run` to return gracefully.
    listen_sender: mpsc::UnboundedSender<Task<()>>,

    /// Handles event routing between archivist parts.
    event_router: EventRouter,

    /// Receive stop signal to kill this archivist.
    stop_recv: Option<oneshot::Receiver<()>>,

    /// Listens for lifecycle requests, to handle Stop requests.
    _lifecycle_task: Option<fasync::Task<()>>,

    /// Tasks that drains klog.
    _drain_klog_task: Option<fasync::Task<()>>,

    /// Task draining the receiver for the `listen_sender`s.
    drain_listeners_task: fasync::Task<()>,

    /// Tasks receiving external events from component manager and appmgr.
    incoming_external_event_producers: Vec<fasync::Task<()>>,

    /// The diagnostics pipelines that have been installed.
    pipelines: Vec<Arc<Pipeline>>,

    /// The repository holding Inspect data.
    inspect_repository: Arc<InspectRepository>,

    /// The repository holding active log connections.
    logs_repository: Arc<LogsRepository>,

    /// The overall capacity we enforce for log messages across containers.
    logs_budget: BudgetManager,

    /// The server handling fuchsia.diagnostics.ArchiveAccessor
    accessor_server: Arc<ArchiveAccessorServer>,

    /// The server handling fuchsia.diagnostics.test.Controller
    test_controller_server: Option<TestControllerServer>,

    /// The source that takes care of routing unattributed log sink request streams.
    unattributed_log_sink_source: Option<UnattributedLogSinkSource>,
}

impl Archivist {
    /// Creates new instance, sets up inspect and adds 'archive' directory to output folder.
    /// Also installs `fuchsia.diagnostics.Archive` service.
    /// Call `install_log_services`
    pub fn new(archivist_configuration: &Config) -> Self {
        let fs = ServiceFs::new();

        let (listen_sender, listen_receiver) = mpsc::unbounded();

        let pipelines = Self::init_pipelines(archivist_configuration);

        let logs_budget =
            BudgetManager::new(archivist_configuration.logs_max_cached_original_bytes as usize);
        let logs_repo = Arc::new(LogsRepository::new(&logs_budget, component::inspector().root()));
        let inspect_repo =
            Arc::new(InspectRepository::new(pipelines.iter().map(Arc::downgrade).collect()));

        let accessor_server =
            Arc::new(ArchiveAccessorServer::new(inspect_repo.clone(), logs_repo.clone()));

        let (test_controller_server, _lifecycle_task, stop_recv) =
            if archivist_configuration.install_controller {
                let (s, r) = TestControllerServer::new();
                (Some(s), None, Some(r))
            } else if archivist_configuration.listen_to_lifecycle {
                debug!("Lifecycle listener initialized.");
                let (t, r) = component_lifecycle::serve_v2();
                (None, Some(t), Some(r))
            } else {
                (None, None, None)
            };

        let mut event_router =
            EventRouter::new(component::inspector().root().create_child("events"));
        let unattributed_log_sink_source = if archivist_configuration.serve_unattributed_logs {
            let mut source = UnattributedLogSinkSource::default();
            event_router.add_producer(ProducerConfig {
                producer: &mut source,
                events: vec![EventType::LogSinkRequested],
            });
            Some(source)
        } else {
            None
        };

        Self {
            fs,
            test_controller_server,
            accessor_server,
            listen_sender,
            event_router,
            stop_recv,
            _lifecycle_task,
            _drain_klog_task: None,
            drain_listeners_task: fasync::Task::spawn(async move {
                listen_receiver.for_each_concurrent(None, |rx| async move { rx.await }).await;
            }),
            incoming_external_event_producers: vec![],
            pipelines,
            inspect_repository: inspect_repo,
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

    /// Installs `LogSink` and `Log` services. Panics if called twice.
    pub async fn install_log_services(&mut self) -> &mut Self {
        let data_repo_1 = self.logs_repository.clone();
        let data_repo_2 = self.logs_repository.clone();
        let listen_sender = self.listen_sender.clone();

        self.fs
            .dir("svc")
            .add_fidl_service(move |stream| {
                debug!("fuchsia.logger.Log connection");
                data_repo_1.clone().handle_log(stream, listen_sender.clone());
            })
            .add_fidl_service(move |stream| {
                debug!("fuchsia.diagnostics.LogSettings connection");
                let data_repo_for_task = data_repo_2.clone();
                fasync::Task::spawn(async move {
                    data_repo_for_task.handle_log_settings(stream).await.unwrap_or_else(|err| {
                        error!(?err, "Failed to handle LogSettings");
                    });
                })
                .detach();
            });
        debug!("Log services initialized.");
        self
    }

    pub async fn install_event_source(&mut self) {
        debug!("fuchsia.sys.EventStream connection");
        match EventSource::new(connect_to_protocol::<EventSourceMarker>().unwrap()).await {
            Err(err) => error!(?err, "Failed to create event source"),
            Ok(mut event_source) => {
                self.event_router.add_producer(ProducerConfig {
                    producer: &mut event_source,
                    events: vec![EventType::LogSinkRequested, EventType::DiagnosticsReady],
                });
                self.incoming_external_event_producers.push(fasync::Task::spawn(async move {
                    // This should never exit.
                    // If it does, it will print an error informing users why it did exit.
                    let _ = event_source.spawn().await;
                }));
            }
        }
    }

    pub async fn install_component_event_provider(&mut self) {
        let proxy = match connect_to_protocol::<fsys_internal::ComponentEventProviderMarker>() {
            Ok(proxy) => proxy,
            Err(err) => {
                error!(?err, "Failed to connect to fuchsia.sys.internal.ComponentEventProvider");
                return;
            }
        };
        let mut component_event_provider = ComponentEventProvider::new(proxy);
        self.event_router.add_producer(ProducerConfig {
            producer: &mut component_event_provider,
            events: vec![EventType::DiagnosticsReady],
        });
        self.incoming_external_event_producers.push(fasync::Task::spawn(async move {
            component_event_provider.spawn().await.unwrap_or_else(|err| {
                error!(?err, "Failed to run component event provider loop");
            });
        }));
    }

    pub fn install_log_connector(&mut self) {
        let proxy = match connect_to_protocol::<fsys_internal::LogConnectorMarker>() {
            Ok(proxy) => proxy,
            Err(err) => {
                error!(?err, "Failed to connect to fuchsia.sys.internal.LogConnector");
                return;
            }
        };
        let mut connector = LogConnector::new(proxy);
        self.event_router.add_producer(ProducerConfig {
            producer: &mut connector,
            events: vec![EventType::LogSinkRequested],
        });
        self.incoming_external_event_producers.push(fasync::Task::spawn(async move {
            connector.spawn().await.unwrap_or_else(|err| {
                warn!(?err, "Failed to run event source producer loop");
            });
        }));
    }

    /// Spawns a task that will drain klog as another log source.
    pub async fn start_draining_klog(&mut self) -> Result<(), Error> {
        let debuglog = KernelDebugLog::new().await?;
        self._drain_klog_task =
            Some(fasync::Task::spawn(self.logs_repository.clone().drain_debuglog(debuglog)));
        Ok(())
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

        self.serve_protocols().await;

        let logs_repository = self.logs_repository.clone();
        let inspect_repository = self.inspect_repository.clone();
        self.fs.serve_connection(outgoing_channel).map_err(Error::ServeOutgoing)?;
        // Start servicing all outgoing services.
        let run_outgoing = self.fs.collect::<()>();

        let (snd, rcv) = mpsc::unbounded::<Arc<ComponentIdentity>>();
        self.logs_budget.set_remover(snd).await;
        let component_removal_task =
            fasync::Task::spawn(Self::process_removal_of_components(rcv, logs_repository.clone()));

        let logs_budget = self.logs_budget.handle();
        let mut event_router = self.event_router;
        event_router.add_consumer(ConsumerConfig {
            consumer: &logs_repository,
            events: vec![EventType::LogSinkRequested],
        });
        event_router.add_consumer(ConsumerConfig {
            consumer: &inspect_repository,
            events: vec![EventType::DiagnosticsReady],
        });
        // panic: can only panic if we didn't register event producers and consumers correctly.
        let (terminate_handle, drain_events_fut) =
            event_router.start(router_opts).expect("Failed to start event router");
        let _event_routing_task = fasync::Task::spawn(async move {
            drain_events_fut.await;
        });

        let drain_listeners_task = self.drain_listeners_task;
        let accessor_server = self.accessor_server.clone();
        let logs_repo = logs_repository.clone();
        let all_msg = async {
            logs_repo.wait_for_termination().await;
            debug!("Terminated logs");
            logs_budget.terminate().await;
            debug!("Flushing to listeners.");
            accessor_server.wait_for_servers_to_complete().await;
            drain_listeners_task.await;
            debug!("Log listeners and batch iterators stopped.");
            component_removal_task.cancel().await;
            debug!("Not processing more component removal requests.");
        };

        let (abortable_fut, abort_handle) = abortable(run_outgoing);

        let mut listen_sender = self.listen_sender;
        let accessor_server = self.accessor_server;
        let incoming_external_event_producers = self.incoming_external_event_producers;
        let stop_fut = match self.stop_recv {
            Some(stop_recv) => async move {
                stop_recv.into_future().await.ok();
                terminate_handle.terminate().await;
                for task in incoming_external_event_producers {
                    task.cancel().await;
                }
                listen_sender.disconnect();
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

    async fn serve_protocols(&mut self) {
        diagnostics::serve(&mut self.fs)
            .unwrap_or_else(|err| warn!(?err, "failed to serve diagnostics"));

        let mut svc_dir = self.fs.dir("svc");

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

        if let Some(mut unattributed_log_sink_source) = self.unattributed_log_sink_source.take() {
            svc_dir.add_fidl_service(move |stream| {
                debug!("unattributed fuchsia.logger.LogSink connection");
                futures::executor::block_on(unattributed_log_sink_source.new_connection(stream));
            });
        }
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

    fn init_archivist() -> Archivist {
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

        let mut archivist = Archivist::new(&config);
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
        let mut archivist = init_archivist();
        archivist.install_log_services().await;
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
        let mut archivist = init_archivist();
        archivist.install_log_services().await;
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
