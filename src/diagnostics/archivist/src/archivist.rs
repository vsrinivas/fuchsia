// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        component_lifecycle, diagnostics,
        error::Error,
        events::{
            router::{
                ConsumerConfig, EventConsumer, EventRouter, ProducerConfig, ProducerType,
                RouterOptions,
            },
            sources::{
                ComponentEventProvider, EventSource, LogConnector, UnattributedLogSinkSource,
            },
            types::*,
        },
        identity::ComponentIdentity,
        logs::{budget::BudgetManager, KernelDebugLog},
        pipeline::Pipeline,
        repository::DataRepo,
    },
    archivist_config::Config,
    async_lock::RwLock,
    async_trait::async_trait,
    fidl_fuchsia_io as fio, fidl_fuchsia_logger as flogger,
    fidl_fuchsia_sys2::EventSourceMarker,
    fidl_fuchsia_sys_internal as fsys_internal,
    fuchsia_async::{self as fasync, Task},
    fuchsia_component::{
        client::connect_to_protocol,
        server::{ServiceFs, ServiceObj},
    },
    fuchsia_inspect::{component, health::Reporter},
    futures::{
        channel::mpsc,
        future::{self, abortable},
        prelude::*,
    },
    std::{path::Path, sync::Arc},
    tracing::{debug, error, info, warn},
};

/// Responsible for initializing an `Archivist` instance. Supports multiple configurations by
/// either calling or not calling methods on the builder like `serve_test_controller_protocol`.
pub struct Archivist {
    /// Archive state, including the diagnostics repo which currently stores all logs.
    archivist_state: Arc<ArchivistState>,

    /// ServiceFs object to server outgoing directory.
    fs: ServiceFs<ServiceObj<'static, ()>>,

    /// Receiver for stream which will process LogSink connections.
    log_receiver: mpsc::UnboundedReceiver<Task<()>>,

    /// Sender which is used to close the stream of LogSink connections.
    ///
    /// Clones of the sender keep the receiver end of the channel open. As soon
    /// as all clones are dropped or disconnected, the receiver will close. The
    /// receiver must close for `Archivist::run` to return gracefully.
    log_sender: mpsc::UnboundedSender<Task<()>>,

    /// Sender which is used to close the stream of Log connections after log_sender
    /// completes.
    ///
    /// Clones of the sender keep the receiver end of the channel open. As soon
    /// as all clones are dropped or disconnected, the receiver will close. The
    /// receiver must close for `Archivist::run` to return gracefully.
    listen_sender: mpsc::UnboundedSender<Task<()>>,

    /// Handles event routing between archivist parts.
    event_router: EventRouter,

    /// Recieve stop signal to kill this archivist.
    stop_recv: Option<mpsc::Receiver<()>>,

    /// Listens for lifecycle requests, to handle Stop requests.
    _lifecycle_task: Option<fasync::Task<()>>,

    /// Tasks that drains klog.
    _drain_klog_task: Option<fasync::Task<()>>,

    /// Task draining the receiver for the `listen_sender`s.
    drain_listeners_task: fasync::Task<()>,

    incoming_external_event_producers: Vec<fasync::Task<()>>,
}

impl Archivist {
    /// Creates new instance, sets up inspect and adds 'archive' directory to output folder.
    /// Also installs `fuchsia.diagnostics.Archive` service.
    /// Call `install_log_services`
    pub async fn new(archivist_configuration: &Config) -> Result<Self, Error> {
        let mut fs = ServiceFs::new();
        diagnostics::serve(&mut fs)?;

        let (log_sender, log_receiver) = mpsc::unbounded();
        let (listen_sender, listen_receiver) = mpsc::unbounded();

        let logs_budget =
            BudgetManager::new(archivist_configuration.logs_max_cached_original_bytes as usize);
        let diagnostics_repo = DataRepo::new(&logs_budget, component::inspector().root()).await;

        let pipelines_node = component::inspector().root().create_child("pipelines");
        let pipelines_path = Path::new(&archivist_configuration.pipelines_path);
        let pipelines = vec![
            Pipeline::feedback(diagnostics_repo.clone(), pipelines_path, &pipelines_node),
            Pipeline::legacy_metrics(diagnostics_repo.clone(), pipelines_path, &pipelines_node),
            Pipeline::lowpan(diagnostics_repo.clone(), pipelines_path, &pipelines_node),
            Pipeline::all_access(diagnostics_repo.clone(), pipelines_path, &pipelines_node),
        ];
        component::inspector().root().record(pipelines_node);

        if pipelines.iter().any(|p| p.config_has_error()) {
            component::health().set_unhealthy("Pipeline config has an error");
        } else {
            component::health().set_ok();
        }

        let stats_node = component::inspector().root().create_child("archive_accessor_stats");
        let pipelines = pipelines
            .into_iter()
            .map(|pipeline| pipeline.serve(&mut fs, listen_sender.clone(), &stats_node))
            .collect();
        component::inspector().root().record(stats_node);

        let archivist_state = Arc::new(ArchivistState::new(
            pipelines,
            diagnostics_repo,
            logs_budget,
            log_sender.clone(),
        )?);

        Ok(Self {
            fs,
            archivist_state,
            log_receiver,
            log_sender,
            listen_sender,
            event_router: EventRouter::new(component::inspector().root().create_child("events")),
            stop_recv: None,
            _lifecycle_task: None,
            _drain_klog_task: None,
            drain_listeners_task: fasync::Task::spawn(async move {
                listen_receiver.for_each_concurrent(None, |rx| async move { rx.await }).await;
            }),
            incoming_external_event_producers: vec![],
        })
    }

    pub fn data_repo(&self) -> &DataRepo {
        &self.archivist_state.diagnostics_repo
    }

    pub fn log_sender(&self) -> &mpsc::UnboundedSender<Task<()>> {
        &self.log_sender
    }

    /// Install controller protocol.
    pub fn serve_test_controller_protocol(&mut self) -> &mut Self {
        let (stop_sender, stop_recv) = mpsc::channel(0);
        self.fs.dir("svc").add_fidl_service(move |stream| {
            fasync::Task::spawn(component_lifecycle::serve_test_controller(
                stream,
                stop_sender.clone(),
            ))
            .detach()
        });
        self.stop_recv = Some(stop_recv);
        debug!("Controller services initialized.");
        self
    }

    /// Listen for v2 lifecycle requests.
    pub fn serve_lifecycle_protocol(&mut self) -> &mut Self {
        let (task, stop_recv) = component_lifecycle::serve_v2();
        self._lifecycle_task = Some(task);
        self.stop_recv = Some(stop_recv);
        debug!("Lifecycle listener initialized.");
        self
    }

    /// Installs `LogSink` and `Log` services. Panics if called twice.
    pub async fn install_log_services(&mut self) -> &mut Self {
        let data_repo_1 = self.data_repo().clone();
        let data_repo_2 = self.data_repo().clone();
        let listen_sender = self.listen_sender.clone();

        let mut unattributed_log_sink_source = UnattributedLogSinkSource::default();
        let unattributed_sender = unattributed_log_sink_source.publisher();
        self.event_router.add_producer(ProducerConfig {
            producer: &mut unattributed_log_sink_source,
            producer_type: ProducerType::External,
            events: vec![],
            singleton_events: vec![SingletonEventType::LogSinkRequested],
        });
        self.incoming_external_event_producers.push(fasync::Task::spawn(async move {
            unattributed_log_sink_source.spawn().await.unwrap_or_else(|err| {
                error!(?err, "Failed to run unattributed log sink producer loop");
            });
        }));

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
            })
            .add_fidl_service(move |stream| {
                debug!("unattributed fuchsia.logger.LogSink connection");
                let mut sender = unattributed_sender.clone();
                // TODO(fxbug.dev/67769): get rid of this Task spawn since it introduces a small
                // window in which we might lose LogSinks.
                fasync::Task::spawn(async move {
                    sender.send(stream).await.unwrap_or_else(|err| {
                        error!(?err, "Failed to add unattributed LogSink connection")
                    })
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
                    producer_type: ProducerType::External,
                    events: vec![],
                    singleton_events: vec![
                        SingletonEventType::LogSinkRequested,
                        SingletonEventType::DiagnosticsReady,
                    ],
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
            producer_type: ProducerType::External,
            events: vec![],
            singleton_events: vec![SingletonEventType::DiagnosticsReady],
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
            producer_type: ProducerType::External,
            events: vec![],
            singleton_events: vec![SingletonEventType::LogSinkRequested],
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
            Some(fasync::Task::spawn(self.data_repo().clone().drain_debuglog(debuglog)));
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

        let data_repo = { self.data_repo().clone() };
        self.fs.serve_connection(outgoing_channel).map_err(Error::ServeOutgoing)?;
        // Start servicing all outgoing services.
        let run_outgoing = self.fs.collect::<()>();

        let (snd, rcv) = mpsc::unbounded::<Arc<ComponentIdentity>>();
        self.archivist_state.logs_budget.set_remover(snd).await;
        let component_removal_task = fasync::Task::spawn(Self::process_removal_of_components(
            rcv,
            data_repo.clone(),
            self.archivist_state.diagnostics_pipelines.clone(),
        ));

        let logs_budget = self.archivist_state.logs_budget.handle();
        let mut event_router = self.event_router;
        let archivist_state = self.archivist_state;
        let archivist_state_log_sender = archivist_state.log_sender.clone();
        event_router.add_consumer(ConsumerConfig {
            consumer: &archivist_state,
            events: vec![],
            singleton_events: vec![
                SingletonEventType::DiagnosticsReady,
                SingletonEventType::LogSinkRequested,
            ],
        });
        // panic: can only panic if we didn't register event producers and consumers correctly.
        let (terminate_handle, drain_events_fut) =
            event_router.start(router_opts).expect("Failed to start event router");
        let _event_routing_task = fasync::Task::spawn(async move {
            drain_events_fut.await;
        });

        // Process messages from log sink.
        let log_receiver = self.log_receiver;
        let drain_listeners_task = self.drain_listeners_task;
        let all_msg = async {
            log_receiver.for_each_concurrent(None, |rx| async move { rx.await }).await;
            debug!("Log ingestion stopped.");
            data_repo.terminate_logs().await;
            logs_budget.terminate().await;
            debug!("Flushing to listeners.");
            drain_listeners_task.await;
            debug!("Log listeners and batch iterators stopped.");
            component_removal_task.cancel().await;
            debug!("Not processing more component removal requests.");
        };

        let (abortable_fut, abort_handle) = abortable(run_outgoing);

        let mut listen_sender = self.listen_sender;
        let mut log_sender = self.log_sender;
        let incoming_external_event_producers = self.incoming_external_event_producers;
        let stop_fut = match self.stop_recv {
            Some(stop_recv) => async move {
                stop_recv.into_future().await;
                terminate_handle.terminate().await;
                for task in incoming_external_event_producers {
                    task.cancel().await;
                }
                listen_sender.disconnect();
                log_sender.disconnect();
                archivist_state_log_sender.write().await.disconnect();
                abort_handle.abort()
            }
            .left_future(),
            None => future::ready(()).right_future(),
        };

        info!("archivist: Entering core loop.");
        // Combine all three futures into a main future.
        future::join3(abortable_fut, stop_fut, all_msg).map(|_| Ok(())).await
    }

    async fn process_removal_of_components(
        mut removal_requests: mpsc::UnboundedReceiver<Arc<ComponentIdentity>>,
        diagnostics_repo: DataRepo,
        diagnostics_pipelines: Arc<Vec<Arc<RwLock<Pipeline>>>>,
    ) {
        while let Some(identity) = removal_requests.next().await {
            maybe_remove_component(&identity, &diagnostics_repo, &diagnostics_pipelines).await;
        }
    }
}

async fn maybe_remove_component(
    identity: &ComponentIdentity,
    diagnostics_repo: &DataRepo,
    diagnostics_pipelines: &[Arc<RwLock<Pipeline>>],
) {
    if !diagnostics_repo.is_live(identity).await {
        debug!(%identity, "Removing component from repository.");
        diagnostics_repo.write().await.remove(identity);

        // TODO(fxbug.dev/55736): The pipeline specific updates should be happening asynchronously.
        for pipeline in diagnostics_pipelines {
            pipeline.write().await.remove(&identity.relative_moniker);
        }
    }
}

/// Archivist owns the tools needed to persist data
/// to the archive, as well as the service-specific repositories
/// that are populated by the archivist server and exposed in the
/// service sessions.
pub struct ArchivistState {
    diagnostics_pipelines: Arc<Vec<Arc<RwLock<Pipeline>>>>,
    pub diagnostics_repo: DataRepo,

    /// The overall capacity we enforce for log messages across containers.
    logs_budget: BudgetManager,

    log_sender: Arc<RwLock<mpsc::UnboundedSender<Task<()>>>>,
}

impl ArchivistState {
    pub fn new(
        diagnostics_pipelines: Vec<Arc<RwLock<Pipeline>>>,
        diagnostics_repo: DataRepo,
        logs_budget: BudgetManager,
        log_sender: mpsc::UnboundedSender<Task<()>>,
    ) -> Result<Self, Error> {
        Ok(Self {
            diagnostics_pipelines: Arc::new(diagnostics_pipelines),
            diagnostics_repo,
            logs_budget,
            log_sender: Arc::new(RwLock::new(log_sender)),
        })
    }

    async fn handle_diagnostics_ready(
        &self,
        component: ComponentIdentity,
        directory: Option<fio::DirectoryProxy>,
    ) {
        debug!(identity = %component, "Diagnostics directory is ready.");
        if let Some(directory) = directory {
            // Update the central repository to reference the new diagnostics source.
            self.diagnostics_repo
                .add_inspect_artifacts(component.clone(), directory)
                .await
                .unwrap_or_else(|err| {
                    warn!(identity = %component, ?err, "Failed to add inspect artifacts to repository");
                });

            // Let each pipeline know that a new component arrived, and allow the pipeline
            // to eagerly bucket static selectors based on that component's moniker.
            // TODO(fxbug.dev/55736): The pipeline specific updates should be happening
            // asynchronously.
            for pipeline in self.diagnostics_pipelines.iter() {
                pipeline
                    .write()
                    .await
                    .add_inspect_artifacts(&component.relative_moniker)
                    .unwrap_or_else(|e| {
                        warn!(identity = %component, ?e,
                            "Failed to add inspect artifacts to pipeline wrapper");
                    });
            }
        }
    }

    async fn handle_log_sink_requested(
        &self,
        component: ComponentIdentity,
        request_stream: Option<flogger::LogSinkRequestStream>,
    ) {
        debug!(identity = %component, "LogSink requested.");
        if let Some(request_stream) = request_stream {
            let container = self.diagnostics_repo.write().await.get_log_container(component).await;
            container.handle_log_sink(request_stream, self.log_sender.read().await.clone()).await;
        }
    }
}

#[async_trait]
impl EventConsumer for ArchivistState {
    async fn handle(self: Arc<Self>, event: Event) {
        match event.payload {
            EventPayload::DiagnosticsReady(DiagnosticsReadyPayload { component, directory }) => {
                self.handle_diagnostics_ready(component, directory).await;
            }
            EventPayload::LogSinkRequested(LogSinkRequestedPayload {
                component,
                request_stream,
            }) => {
                self.handle_log_sink_requested(component, request_stream).await;
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
            install_controller: false,
            listen_to_lifecycle: false,
            log_to_debuglog: false,
            logs_max_cached_original_bytes: LEGACY_DEFAULT_MAXIMUM_CACHED_LOGS_BYTES as u64,
            num_threads: 1,
            pipelines_path: DEFAULT_PIPELINES_PATH.into(),
            bind_services: vec![],
        };

        let mut archivist = Archivist::new(&config).await.unwrap();
        // Install a fake producer that allows all incoming events. This allows skipping
        // validation for the purposes of the tests here.
        let mut fake_producer = FakeProducer {};
        archivist.event_router.add_producer(ProducerConfig {
            producer: &mut fake_producer,
            producer_type: ProducerType::External,
            events: vec![],
            singleton_events: vec![
                SingletonEventType::LogSinkRequested,
                SingletonEventType::DiagnosticsReady,
            ],
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
        let mut archivist = init_archivist().await;
        archivist.install_log_services().await.serve_test_controller_protocol();
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
        let mut archivist = init_archivist().await;
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
