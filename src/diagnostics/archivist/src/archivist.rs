// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        component_lifecycle, configs,
        container::ComponentIdentity,
        diagnostics,
        events::{
            source_registry::EventSourceRegistry,
            sources::{StaticEventStream, UnattributedLogSinkSource},
            types::{ComponentEvent, ComponentEventStream, DiagnosticsReadyEvent, EventSource},
        },
        logs::{budget::BudgetManager, socket::LogMessageSocket, KernelDebugLog},
        pipeline::Pipeline,
        repository::DataRepo,
    },
    anyhow::{Context, Error},
    fidl_fuchsia_sys2::EventSourceMarker,
    fuchsia_async::{self as fasync, Task},
    fuchsia_component::{
        client::connect_to_protocol,
        server::{ServiceFs, ServiceObj},
    },
    fuchsia_inspect::{component, health::Reporter},
    fuchsia_zircon as zx,
    futures::{
        channel::mpsc,
        future::{self, abortable},
        prelude::*,
    },
    parking_lot::RwLock,
    std::sync::Arc,
    tracing::{debug, error, warn},
};

/// Options for ingesting logs.
pub struct LogOpts {
    /// Whether or not logs coming from v2 components should be ingested.
    pub ingest_v2_logs: bool,
}

/// Responsible for initializing an `Archivist` instance. Supports multiple configurations by
/// either calling or not calling methods on the builder like `serve_test_controller_protocol`.
pub struct Archivist {
    /// Archive state, including the diagnostics repo which currently stores all logs.
    archivist_state: ArchivistState,

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

    /// Receiver for stream which will process Log connections.
    listen_receiver: mpsc::UnboundedReceiver<Task<()>>,

    /// Sender which is used to close the stream of Log connections after log_sender
    /// completes.
    ///
    /// Clones of the sender keep the receiver end of the channel open. As soon
    /// as all clones are dropped or disconnected, the receiver will close. The
    /// receiver must close for `Archivist::run` to return gracefully.
    listen_sender: mpsc::UnboundedSender<Task<()>>,

    /// Listes for events coming from v1 and v2.
    event_source_registry: EventSourceRegistry,

    /// Recieve stop signal to kill this archivist.
    stop_recv: Option<mpsc::Receiver<()>>,

    /// Listens for lifecycle requests, to handle Stop requests.
    _lifecycle_task: Option<fasync::Task<()>>,

    /// When the archivist is consuming its own logs, this task drains the archivist log stream
    /// socket.
    _consume_own_logs_task: Option<fasync::Task<()>>,

    /// Tasks that drains klog.
    _drain_klog_task: Option<fasync::Task<()>>,
}

impl Archivist {
    /// Creates new instance, sets up inspect and adds 'archive' directory to output folder.
    /// Also installs `fuchsia.diagnostics.Archive` service.
    /// Call `install_log_services`, `add_event_source`.
    pub fn new(archivist_configuration: configs::Config) -> Result<Self, Error> {
        let mut fs = ServiceFs::new();
        diagnostics::serve(&mut fs)?;

        let (log_sender, log_receiver) = mpsc::unbounded();
        let (listen_sender, listen_receiver) = mpsc::unbounded();

        let logs_budget =
            BudgetManager::new(archivist_configuration.logs.max_cached_original_bytes);
        let diagnostics_repo = DataRepo::new(&logs_budget, component::inspector().root());

        let pipelines_node = component::inspector().root().create_child("pipelines");
        let pipelines_path = archivist_configuration.pipelines_path;
        let pipelines = vec![
            Pipeline::feedback(diagnostics_repo.clone(), &pipelines_path, &pipelines_node),
            Pipeline::legacy_metrics(diagnostics_repo.clone(), &pipelines_path, &pipelines_node),
            Pipeline::all_access(diagnostics_repo.clone(), &pipelines_path, &pipelines_node),
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

        let archivist_state = ArchivistState::new(pipelines, diagnostics_repo, logs_budget)?;

        let events_node = component::inspector().root().create_child("event_stats");
        Ok(Self {
            fs,
            archivist_state,
            log_receiver,
            log_sender,
            listen_receiver,
            listen_sender,
            event_source_registry: EventSourceRegistry::new(events_node),
            stop_recv: None,
            _consume_own_logs_task: None,
            _lifecycle_task: None,
            _drain_klog_task: None,
        })
    }

    pub fn data_repo(&self) -> &DataRepo {
        &self.archivist_state.diagnostics_repo
    }

    pub fn log_sender(&self) -> &mpsc::UnboundedSender<Task<()>> {
        &self.log_sender
    }

    // TODO(fxbug.dev/72046) delete when netemul no longer using
    pub fn consume_own_logs(&mut self, socket: zx::Socket) {
        let container = self.data_repo().write().get_own_log_container();
        self._consume_own_logs_task = Some(fasync::Task::spawn(async move {
            let log_stream =
                LogMessageSocket::new(socket, container.identity.clone(), container.stats.clone())
                    .expect("failed to create internal LogMessageSocket");
            container.drain_messages(log_stream).await;
            unreachable!();
        }));
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
    /// # Arguments:
    /// * `log_connector` - If provided, install log connector.
    pub async fn install_log_services(&mut self, opts: LogOpts) -> &mut Self {
        let data_repo_1 = self.data_repo().clone();
        let listen_sender = self.listen_sender.clone();

        let unattributed_log_sink_source = UnattributedLogSinkSource::new();
        let unattributed_log_sink_publisher = unattributed_log_sink_source.get_publisher();
        self.event_source_registry
            .add_source("unattributed_log_sink", Box::new(unattributed_log_sink_source))
            .await;

        self.fs
            .dir("svc")
            .add_fidl_service(move |stream| {
                debug!("fuchsia.logger.Log connection");
                data_repo_1.clone().handle_log(stream, listen_sender.clone());
            })
            .add_fidl_service(move |stream| {
                debug!("unattributed fuchsia.logger.LogSink connection");
                let mut publisher = unattributed_log_sink_publisher.clone();
                // TODO(fxbug.dev/67769): get rid of this Task spawn since it introduces a small
                // window in which we might lose LogSinks.
                fasync::Task::spawn(async move {
                    publisher.send(stream).await.unwrap_or_else(|err| {
                        error!(?err, "Failed to add unattributed LogSink connection")
                    })
                })
                .detach();
            });
        if opts.ingest_v2_logs {
            debug!("fuchsia.sys.EventStream connection");
            let event_source = connect_to_protocol::<EventSourceMarker>().unwrap();
            match event_source.take_static_event_stream("EventStream").await {
                Ok(Ok(event_stream)) => {
                    let event_stream = event_stream.into_stream().unwrap();
                    self.event_source_registry
                        .add_source(
                            "v2_static_event_stream",
                            Box::new(StaticEventStream::new(event_stream)),
                        )
                        .await;
                }
                Ok(Err(err)) => debug!(?err, "Failed to open event stream"),
                Err(err) => debug!(?err, "Failed to send request to take event stream"),
            }
        }
        debug!("Log services initialized.");
        self
    }

    // Sets event provider which is used to collect component events, Panics if called twice.
    pub async fn add_event_source(
        &mut self,
        name: impl Into<String>,
        source: Box<dyn EventSource>,
    ) -> &mut Self {
        let name = name.into();
        debug!("{} event source initialized", &name);
        self.event_source_registry.add_source(name, source).await;
        self
    }

    /// Spawns a task that will drain klog as another log source.
    pub async fn start_draining_klog(&mut self) -> Result<(), Error> {
        let debuglog = KernelDebugLog::new().await.context("Failed to read kernel logs")?;
        self._drain_klog_task =
            Some(fasync::Task::spawn(self.data_repo().clone().drain_debuglog(debuglog)));
        Ok(())
    }

    /// Run archivist to completion.
    /// # Arguments:
    /// * `outgoing_channel`- channel to serve outgoing directory on.
    pub async fn run(mut self, outgoing_channel: zx::Channel) -> Result<(), Error> {
        debug!("Running Archivist.");

        let data_repo = { self.data_repo().clone() };
        self.fs.serve_connection(outgoing_channel)?;
        // Start servcing all outgoing services.
        let run_outgoing = self.fs.collect::<()>();
        // collect events.
        let events = self.event_source_registry.take_stream().await.expect("Created event stream");

        let (snd, rcv) = mpsc::unbounded::<Arc<ComponentIdentity>>();
        self.archivist_state.logs_budget.set_remover(snd);
        let component_removal_task = fasync::Task::spawn(Self::process_removal_of_components(
            rcv,
            data_repo.clone(),
            self.archivist_state.diagnostics_pipelines.clone(),
        ));

        let logs_budget = self.archivist_state.logs_budget.handle();
        let run_event_collection_task = fasync::Task::spawn(
            self.archivist_state.process_events(events, self.log_sender.clone()),
        );

        // Process messages from log sink.
        let log_receiver = self.log_receiver;
        let listen_receiver = self.listen_receiver;
        let all_msg = async {
            log_receiver.for_each_concurrent(None, |rx| async move { rx.await }).await;
            debug!("Log ingestion stopped.");
            data_repo.terminate_logs();
            logs_budget.terminate();
            debug!("Flushing to listeners.");
            listen_receiver.for_each_concurrent(None, |rx| async move { rx.await }).await;
            debug!("Log listeners and batch iterators stopped.");
            component_removal_task.cancel().await;
            debug!("Not processing more component removal requests.");
        };

        let (abortable_fut, abort_handle) = abortable(run_outgoing);

        let mut listen_sender = self.listen_sender;
        let mut log_sender = self.log_sender;
        let mut event_source_registry = self.event_source_registry;
        let stop_fut = match self.stop_recv {
            Some(stop_recv) => async move {
                stop_recv.into_future().await;
                event_source_registry.terminate();
                run_event_collection_task.await;
                listen_sender.disconnect();
                log_sender.disconnect();
                abort_handle.abort()
            }
            .left_future(),
            None => future::ready(()).right_future(),
        };

        debug!("Entering core loop.");
        // Combine all three futures into a main future.
        future::join3(abortable_fut, stop_fut, all_msg).map(|_| Ok(())).await
    }

    async fn process_removal_of_components(
        mut removal_requests: mpsc::UnboundedReceiver<Arc<ComponentIdentity>>,
        diagnostics_repo: DataRepo,
        diagnostics_pipelines: Arc<Vec<Arc<RwLock<Pipeline>>>>,
    ) {
        while let Some(identity) = removal_requests.next().await {
            maybe_remove_component(&identity, &diagnostics_repo, &diagnostics_pipelines);
        }
    }
}

fn maybe_remove_component(
    identity: &ComponentIdentity,
    diagnostics_repo: &DataRepo,
    diagnostics_pipelines: &[Arc<RwLock<Pipeline>>],
) {
    if !diagnostics_repo.is_live(&identity) {
        debug!(%identity, "Removing component from repository.");
        diagnostics_repo.write().data_directories.remove(&identity.unique_key);

        // TODO(fxbug.dev/55736): The pipeline specific updates should be happening asynchronously.
        for pipeline in diagnostics_pipelines {
            pipeline.write().remove(&identity.relative_moniker);
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
}

impl ArchivistState {
    pub fn new(
        diagnostics_pipelines: Vec<Arc<RwLock<Pipeline>>>,
        diagnostics_repo: DataRepo,
        logs_budget: BudgetManager,
    ) -> Result<Self, Error> {
        Ok(Self {
            diagnostics_pipelines: Arc::new(diagnostics_pipelines),
            diagnostics_repo,
            logs_budget,
        })
    }

    pub async fn process_events(
        mut self,
        mut events: ComponentEventStream,
        log_sender: mpsc::UnboundedSender<Task<()>>,
    ) {
        while let Some(event) = events.next().await {
            self.process_event(event, &log_sender).await;
        }
    }

    async fn populate_inspect_repo(&self, diagnostics_ready_data: DiagnosticsReadyEvent) {
        // The DiagnosticsReadyEvent should always contain a directory_proxy. Its existence
        // as an Option is only to support mock objects for equality in tests.
        let diagnostics_proxy = diagnostics_ready_data.directory.unwrap();

        let identity = diagnostics_ready_data.metadata.identity.clone();

        // Update the central repository to reference the new diagnostics source.
        self.diagnostics_repo
            .write()
            .add_inspect_artifacts(
                identity.clone(),
                diagnostics_proxy,
                diagnostics_ready_data.metadata.timestamp.clone(),
            )
            .unwrap_or_else(|e| {
                warn!(%identity, ?e, "Failed to add inspect artifacts to repository");
            });

        // Let each pipeline know that a new component arrived, and allow the pipeline
        // to eagerly bucket static selectors based on that component's moniker.
        // TODO(fxbug.dev/55736): The pipeline specific updates should be happening asynchronously.
        for pipeline in self.diagnostics_pipelines.iter() {
            pipeline.write().add_inspect_artifacts(&identity.relative_moniker).unwrap_or_else(
                |e| {
                    warn!(%identity, ?e, "Failed to add inspect artifacts to pipeline wrapper");
                },
            );
        }
    }

    /// Updates the central repository to reference the new diagnostics source.
    fn add_new_component(
        &self,
        identity: ComponentIdentity,
        event_timestamp: zx::Time,
        component_start_time: Option<zx::Time>,
    ) {
        if let Err(e) = self.diagnostics_repo.write().add_new_component(
            identity.clone(),
            event_timestamp,
            component_start_time,
        ) {
            error!(%identity, ?e, "Failed to add new component to repository");
        }
    }

    fn mark_component_stopped(&self, identity: &ComponentIdentity) {
        // TODO(fxbug.dev/53939): Get inspect data from repository before removing
        // for post-mortem inspection.
        self.diagnostics_repo.write().mark_stopped(&identity.unique_key);
        maybe_remove_component(identity, &self.diagnostics_repo, &self.diagnostics_pipelines);
    }

    async fn process_event(
        &mut self,
        event: ComponentEvent,
        log_sender: &mpsc::UnboundedSender<Task<()>>,
    ) {
        match event {
            ComponentEvent::Start(start) => {
                debug!(identity = %start.metadata.identity, "Adding new component.");
                self.add_new_component(start.metadata.identity, start.metadata.timestamp, None);
            }
            ComponentEvent::Running(running) => {
                debug!(identity = %running.metadata.identity, "Component is running.");
                self.add_new_component(
                    running.metadata.identity,
                    running.metadata.timestamp,
                    Some(running.component_start_time),
                );
            }
            ComponentEvent::Stop(stop) => {
                debug!(identity = %stop.metadata.identity, "Component stopped");
                self.mark_component_stopped(&stop.metadata.identity);
            }
            ComponentEvent::DiagnosticsReady(diagnostics_ready) => {
                debug!(
                    identity = %diagnostics_ready.metadata.identity,
                    "Diagnostics directory is ready.",
                );
                self.populate_inspect_repo(diagnostics_ready).await;
            }
            ComponentEvent::LogSinkRequested(event) => {
                let data_repo = &self.diagnostics_repo;
                let container = data_repo.write().get_log_container(event.metadata.identity);
                container.handle_log_sink(event.requests, log_sender.clone());
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{constants::*, logs::testing::*};
    use fidl::endpoints::create_proxy;
    use fidl_fuchsia_diagnostics_test::ControllerMarker;
    use fidl_fuchsia_io as fio;
    use fio::DirectoryProxy;
    use fuchsia_async as fasync;
    use fuchsia_component::client::connect_to_protocol_at_dir_svc;
    use futures::channel::oneshot;

    fn init_archivist() -> Archivist {
        let config = configs::Config {
            num_threads: 1,
            logs: configs::LogsConfig {
                max_cached_original_bytes: LEGACY_DEFAULT_MAXIMUM_CACHED_LOGS_BYTES,
            },
            pipelines_path: DEFAULT_PIPELINES_PATH.into(),
        };

        Archivist::new(config).unwrap()
    }

    // run archivist and send signal when it dies.
    async fn run_archivist_and_signal_on_exit() -> (DirectoryProxy, oneshot::Receiver<()>) {
        let (directory, server_end) = create_proxy::<fio::DirectoryMarker>().unwrap();
        let mut archivist = init_archivist();
        archivist
            .install_log_services(LogOpts { ingest_v2_logs: false })
            .await
            .serve_test_controller_protocol();
        let (signal_send, signal_recv) = oneshot::channel();
        fasync::Task::spawn(async move {
            archivist.run(server_end.into_channel()).await.expect("Cannot run archivist");
            signal_send.send(()).unwrap();
        })
        .detach();
        (directory, signal_recv)
    }

    // runs archivist and returns its directory.
    async fn run_archivist() -> DirectoryProxy {
        let (directory, server_end) = create_proxy::<fio::DirectoryMarker>().unwrap();
        let mut archivist = init_archivist();
        archivist.install_log_services(LogOpts { ingest_v2_logs: false }).await;
        fasync::Task::spawn(async move {
            archivist.run(server_end.into_channel()).await.expect("Cannot run archivist");
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
