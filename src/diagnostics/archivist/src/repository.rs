// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{
        constants::MAXIMUM_CACHED_LOGS_BYTES,
        container::ComponentDiagnostics,
        events::types::ComponentIdentifier,
        inspect::container::{InspectArtifactsContainer, UnpopulatedInspectDataContainer},
        lifecycle::container::{LifecycleArtifactsContainer, LifecycleDataContainer},
        logs::{
            buffer::{LazyItem, MemoryBoundedBuffer},
            debuglog::{DebugLog, DebugLogBridge},
            error::{ForwardError, LogsError, StreamError},
            interest::InterestDispatcher,
            listener::{pretend_scary_listener_is_safe, Listener, ListenerError},
            log_sink_request_stream_from_event,
            socket::{Encoding, Forwarder, LegacyEncoding, LogMessageSocket, StructuredEncoding},
            source_identity_from_event,
            stats::{LogManagerStats, LogSource},
            Message,
        },
    },
    anyhow::{format_err, Error},
    diagnostics_hierarchy::{trie, InspectHierarchyMatcher},
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_diagnostics::{self, Interest, Selector, StreamMode},
    fidl_fuchsia_io::{DirectoryProxy, CLONE_FLAG_SAME_RIGHTS},
    fidl_fuchsia_logger::{
        LogMarker, LogRequest, LogRequestStream, LogSinkControlHandle, LogSinkMarker,
        LogSinkRequest, LogSinkRequestStream,
    },
    fidl_fuchsia_sys2 as fsys,
    fidl_fuchsia_sys_internal::{
        LogConnection, LogConnectionListenerRequest, LogConnectorProxy, SourceIdentity,
    },
    fuchsia_async::Task,
    fuchsia_inspect as inspect,
    fuchsia_inspect_derive::{Inspect, WithInspect},
    fuchsia_zircon as zx,
    futures::channel::mpsc,
    futures::prelude::*,
    io_util,
    parking_lot::RwLock,
    selectors,
    std::collections::HashMap,
    std::sync::Arc,
    tracing::{debug, error, trace, warn},
};

/// DataRepo holds all diagnostics data and is a singleton wrapped by multiple
/// [`pipeline::Pipeline`]s in a given Archivist instance.
#[derive(Clone)]
pub struct DataRepo {
    inner: Arc<RwLock<DataRepoState>>,
}

impl std::ops::Deref for DataRepo {
    type Target = RwLock<DataRepoState>;
    fn deref(&self) -> &Self::Target {
        &*self.inner
    }
}

impl DataRepo {
    pub fn new() -> Self {
        DataRepo {
            inner: Arc::new(RwLock::new(DataRepoState {
                data_directories: trie::Trie::new(),
                logs: Default::default(),
            })),
        }
    }

    pub fn with_logs_inspect(parent: &fuchsia_inspect::Node, name: impl AsRef<str>) -> Self {
        let logs = LogState::default();
        DataRepo {
            inner: Arc::new(RwLock::new(DataRepoState {
                data_directories: trie::Trie::new(),
                logs: logs
                    .with_inspect(parent, name)
                    .expect("couldn't attach log stats to inspect"),
            })),
        }
    }
}

impl DataRepo {
    /// Drain the kernel's debug log. The returned future completes once
    /// existing messages have been ingested.
    pub async fn drain_debuglog<K>(self, klog_reader: K)
    where
        K: DebugLog + Send + Sync + 'static,
    {
        debug!("Draining debuglog.");
        let component_log_stats =
            { self.read().logs.stats.get_component_log_stats("fuchsia-boot://klog") };
        let mut kernel_logger = DebugLogBridge::create(klog_reader);
        let mut messages = match kernel_logger.existing_logs().await {
            Ok(messages) => messages,
            Err(e) => {
                error!(%e, "failed to read from kernel log, important logs may be missing");
                return;
            }
        };
        messages.sort_by_key(|m| m.metadata.timestamp);
        for message in messages {
            component_log_stats.record_log(&message);
            self.ingest_message(message, LogSource::Kernel);
        }

        let res = kernel_logger
            .listen()
            .try_for_each(|message| async {
                component_log_stats.clone().record_log(&message);
                self.ingest_message(message, LogSource::Kernel);
                Ok(())
            })
            .await;
        if let Err(e) = res {
            error!(%e, "failed to drain kernel log, important logs may be missing");
        }
    }

    /// Drain log sink for messages sent by the archivist itself.
    pub async fn drain_internal_log_sink(self, socket: zx::Socket, name: &str) {
        let forwarder = self.read().logs.legacy_forwarder.clone();
        // TODO(fxbug.dev/50105): Figure out how to properly populate SourceIdentity
        let mut source = SourceIdentity::EMPTY;
        source.component_name = Some(name.to_owned());
        let source = Arc::new(source);
        let log_stream = LogMessageSocket::new(socket, source, forwarder)
            .expect("failed to create internal LogMessageSocket");
        self.drain_messages(log_stream).await;
        unreachable!();
    }

    /// Handle `LogConnectionListener` for the parent realm, eventually passing
    /// `LogSink` connections into the manager.
    pub async fn handle_log_connector(
        self,
        connector: LogConnectorProxy,
        sender: mpsc::UnboundedSender<Task<()>>,
    ) {
        debug!("Handling LogSink connections from appmgr.");
        match connector.take_log_connection_listener().await {
            Ok(Some(listener)) => {
                let mut connections =
                    listener.into_stream().expect("getting request stream from server end");
                while let Ok(Some(connection)) = connections.try_next().await {
                    match connection {
                        LogConnectionListenerRequest::OnNewConnection {
                            connection: LogConnection { log_request, source_identity },
                            control_handle: _,
                        } => {
                            let stream = log_request
                                .into_stream()
                                .expect("getting LogSinkRequestStream from serverend");
                            let source = Arc::new(source_identity);
                            sender
                                .unbounded_send(Task::spawn(self.clone().handle_log_sink(
                                    stream,
                                    source,
                                    sender.clone(),
                                )))
                                .expect("channel is held by archivist, lasts for whole program");
                        }
                    };
                }
            }
            Ok(None) => warn!("local realm already gave out LogConnectionListener, skipping logs"),
            Err(e) => error!(%e, "error retrieving LogConnectionListener from LogConnector"),
        }
    }

    /// Handle `LogSink` protocol on `stream`. The future returned by this
    /// function will not complete before all messages on this connection are
    /// processed.
    pub async fn handle_log_sink(
        self,
        mut stream: LogSinkRequestStream,
        source: Arc<SourceIdentity>,
        sender: mpsc::UnboundedSender<Task<()>>,
    ) {
        if source.component_name.is_none() {
            self.read().logs.stats.record_unattributed();
        }

        while let Some(next) = stream.next().await {
            match next {
                Ok(LogSinkRequest::Connect { socket, control_handle }) => {
                    let forwarder = { self.read().logs.legacy_forwarder.clone() };
                    match LogMessageSocket::new(socket, source.clone(), forwarder) {
                        Ok(log_stream) => {
                            self.try_add_interest_listener(&source, control_handle);
                            let task = Task::spawn(self.clone().drain_messages(log_stream));
                            sender.unbounded_send(task).expect("channel alive for whole program");
                        }
                        Err(e) => {
                            control_handle.shutdown();
                            warn!(?source, %e, "error creating socket")
                        }
                    };
                }
                Ok(LogSinkRequest::ConnectStructured { socket, control_handle }) => {
                    let forwarder = { self.read().logs.structured_forwarder.clone() };
                    match LogMessageSocket::new_structured(socket, source.clone(), forwarder) {
                        Ok(log_stream) => {
                            self.try_add_interest_listener(&source, control_handle);
                            let task = Task::spawn(self.clone().drain_messages(log_stream));
                            sender.unbounded_send(task).expect("channel alive for whole program");
                        }
                        Err(e) => {
                            control_handle.shutdown();
                            warn!(?source, %e, "error creating socket")
                        }
                    };
                }
                Err(e) => error!(?source, %e, "error handling log sink"),
            }
        }
    }

    /// Drain a `LogMessageSocket` which wraps a socket from a component
    /// generating logs.
    async fn drain_messages<E>(self, mut log_stream: LogMessageSocket<E>)
    where
        E: Encoding + Unpin,
    {
        let component_log_stats =
            { self.read().logs.stats.get_component_log_stats(log_stream.source_url()) };
        loop {
            match log_stream.next().await {
                Ok(message) => {
                    component_log_stats.record_log(&message);
                    self.ingest_message(message, LogSource::LogSink);
                }
                Err(StreamError::Closed) => return,
                Err(e) => {
                    self.read().logs.stats.record_closed_stream();
                    warn!(source = ?log_stream.source_url(), %e, "closing socket");
                    return;
                }
            }
        }
    }

    /// Add 'Interest' listener to connect the interest dispatcher to the
    /// LogSinkControlHandle (weak reference) associated with the given source.
    /// Interest listeners are only supported for log connections where the
    /// SourceIdentity includes an attributed component name. If no component
    /// name is present, this function will exit without adding any listener.
    fn try_add_interest_listener(
        &self,
        source: &Arc<SourceIdentity>,
        control_handle: LogSinkControlHandle,
    ) {
        if source.component_name.is_none() {
            return;
        }

        let control_handle = Arc::new(control_handle);
        let event_listener = control_handle.clone();
        self.write()
            .logs
            .interest_dispatcher
            .add_interest_listener(source, Arc::downgrade(&event_listener));

        // ack successful connections with 'empty' interest
        // for async clients
        let _ = control_handle.send_on_register_interest(Interest::EMPTY);
    }

    /// Handle the components v2 EventStream for attributed logs of v2
    /// components.
    pub async fn handle_event_stream(
        self,
        mut stream: fsys::EventStreamRequestStream,
        sender: mpsc::UnboundedSender<Task<()>>,
    ) {
        while let Ok(Some(request)) = stream.try_next().await {
            match request {
                fsys::EventStreamRequest::OnEvent { event, .. } => {
                    if let Err(e) = self.handle_event(event, sender.clone()) {
                        error!(%e, "Unable to process event");
                    }
                }
            }
        }
    }

    /// Handle the components v2 CapabilityRequested event for attributed logs of
    /// v2 components.
    fn handle_event(
        &self,
        event: fsys::Event,
        sender: mpsc::UnboundedSender<Task<()>>,
    ) -> Result<(), LogsError> {
        let identity = source_identity_from_event(&event)?;
        let stream = log_sink_request_stream_from_event(event)?;
        let task = Task::spawn(self.clone().handle_log_sink(stream, identity, sender.clone()));
        sender.unbounded_send(task).expect("channel is alive for whole program");
        Ok(())
    }

    /// Spawn a task to handle requests from components reading the shared log.
    pub fn handle_log(self, stream: LogRequestStream, sender: mpsc::UnboundedSender<Task<()>>) {
        if let Err(e) = sender.clone().unbounded_send(Task::spawn(async move {
            if let Err(e) = self.handle_log_requests(stream, sender).await {
                warn!("error handling Log requests: {}", e);
            }
        })) {
            warn!("Couldn't queue listener task: {:?}", e);
        }
    }

    /// Handle requests to `fuchsia.logger.Log`. All request types read the
    /// whole backlog from memory, `DumpLogs(Safe)` stops listening after that.
    async fn handle_log_requests(
        self,
        mut stream: LogRequestStream,
        mut sender: mpsc::UnboundedSender<Task<()>>,
    ) -> Result<(), LogsError> {
        while let Some(request) = stream.next().await {
            let request = request.map_err(|source| LogsError::HandlingRequests {
                protocol: LogMarker::NAME,
                source,
            })?;

            let (listener, options, dump_logs, selectors) = match request {
                LogRequest::ListenSafe { log_listener, options, .. } => {
                    (log_listener, options, false, None)
                }
                LogRequest::DumpLogsSafe { log_listener, options, .. } => {
                    (log_listener, options, true, None)
                }

                LogRequest::ListenSafeWithSelectors {
                    log_listener, options, selectors, ..
                } => (log_listener, options, false, Some(selectors)),

                // TODO(fxbug.dev/48758) delete these methods!
                LogRequest::Listen { log_listener, options, .. } => {
                    warn!("Use of fuchsia.logger.Log.Listen. Use ListenSafe.");
                    let listener = pretend_scary_listener_is_safe(log_listener)
                        .map_err(|source| ListenerError::AsbestosIo { source })?;
                    (listener, options, false, None)
                }
                LogRequest::DumpLogs { log_listener, options, .. } => {
                    warn!("Use of fuchsia.logger.Log.DumpLogs. Use DumpLogsSafe.");
                    let listener = pretend_scary_listener_is_safe(log_listener)
                        .map_err(|source| ListenerError::AsbestosIo { source })?;
                    (listener, options, true, None)
                }
            };

            let listener = Listener::new(listener, options)?;
            let mode =
                if dump_logs { StreamMode::Snapshot } else { StreamMode::SnapshotThenSubscribe };
            let logs = self.cursor(mode);
            if let Some(s) = selectors {
                self.write().logs.interest_dispatcher.update_selectors(s);
            }

            sender.send(listener.spawn(logs, dump_logs)).await.ok();
        }
        Ok(())
    }

    pub fn cursor(&self, mode: StreamMode) -> impl Stream<Item = Arc<Message>> {
        self.read().logs.log_msg_buffer.cursor(mode).map(|item| match item {
            LazyItem::Next(m) => m,
            LazyItem::ItemsDropped(n) => Arc::new(Message::for_dropped(n)),
        })
    }

    /// Ingest an individual log message.
    fn ingest_message(&self, log_msg: Message, source: LogSource) {
        let mut inner = self.write();
        trace!("Ingesting {:?}", log_msg.id);

        // We always record the log before pushing onto the buffer and waking listeners because
        // we want to be able to see that stats are updated as soon as we receive messages in tests.
        inner.logs.stats.record_log(&log_msg, source);
        inner.logs.log_msg_buffer.push(log_msg);
    }

    /// Stop accepting new messages, ensuring that pending Cursors return Poll::Ready(None) after
    /// consuming any messages received before this call.
    pub fn terminate_logs(&self) {
        self.write().logs.log_msg_buffer.terminate();
    }

    /// Initializes internal log forwarders.
    pub fn forward_logs(self) {
        if let Err(e) = self.init_forwarders() {
            error!(%e, "couldn't forward logs");
        } else {
            debug!("Log forwarding initialized.");
        }
    }

    fn init_forwarders(&self) -> Result<(), LogsError> {
        let sink =
            fuchsia_component::client::connect_to_service::<LogSinkMarker>().map_err(|source| {
                LogsError::ConnectingToService { protocol: LogSinkMarker::NAME, source }
            })?;
        let mut state = self.write();

        let (send, recv) = zx::Socket::create(zx::SocketOpts::DATAGRAM)
            .map_err(|source| ForwardError::Create { source })?;
        sink.connect(recv).map_err(|source| ForwardError::Connect { source })?;
        state.logs.legacy_forwarder.init(send);

        let (send, recv) = zx::Socket::create(zx::SocketOpts::DATAGRAM)
            .map_err(|source| ForwardError::Create { source })?;
        sink.connect_structured(recv).map_err(|source| ForwardError::Connect { source })?;
        state.logs.structured_forwarder.init(send);

        Ok(())
    }
}

pub struct DataRepoState {
    pub data_directories: trie::Trie<String, ComponentDiagnostics>,
    logs: LogState,
}

#[derive(Inspect)]
struct LogState {
    #[inspect(skip)]
    interest_dispatcher: InterestDispatcher,
    #[inspect(skip)]
    legacy_forwarder: Forwarder<LegacyEncoding>,
    #[inspect(skip)]
    structured_forwarder: Forwarder<StructuredEncoding>,
    #[inspect(rename = "buffer_stats")]
    log_msg_buffer: MemoryBoundedBuffer<Message>,
    stats: LogManagerStats,
    inspect_node: inspect::Node,
}

impl Default for LogState {
    fn default() -> Self {
        LogState {
            interest_dispatcher: InterestDispatcher::default(),
            log_msg_buffer: MemoryBoundedBuffer::new(MAXIMUM_CACHED_LOGS_BYTES),
            stats: LogManagerStats::new_detached(),
            inspect_node: inspect::Node::default(),
            legacy_forwarder: Forwarder::new(),
            structured_forwarder: Forwarder::new(),
        }
    }
}

impl DataRepoState {
    pub fn remove(&mut self, component_id: &ComponentIdentifier) {
        self.data_directories.remove(component_id.unique_key());
    }

    pub fn add_new_component(
        &mut self,
        identifier: ComponentIdentifier,
        component_url: impl Into<String>,
        event_timestamp: zx::Time,
        component_start_time: Option<zx::Time>,
    ) -> Result<(), Error> {
        let relative_moniker = identifier.relative_moniker_for_selectors();

        let lifecycle_artifact_container = LifecycleArtifactsContainer {
            event_timestamp: event_timestamp,
            component_start_time: component_start_time,
        };

        let key = identifier.unique_key();

        let diag_repo_entry_opt = self.data_directories.get_mut(key.clone());
        match diag_repo_entry_opt {
            Some(diag_repo_entry) => {
                let diag_repo_entry_values: &mut [ComponentDiagnostics] =
                    diag_repo_entry.get_values_mut();

                match &mut diag_repo_entry_values[..] {
                    [] => {
                        // An entry with no values implies that the somehow we observed the
                        // creation of a component lower in the topology before observing this
                        // one. If this is the case, just instantiate as though it's our first
                        // time encountering this moniker segment.
                        self.data_directories.insert(
                            key,
                            ComponentDiagnostics::new_with_lifecycle(
                                relative_moniker,
                                component_url.into(),
                                lifecycle_artifact_container,
                            ),
                        )
                    }
                    [existing_diagnostics_artifact_container] => {
                        // Races may occur between seeing diagnostics ready and seeing
                        // creation lifecycle events. Handle this here.
                        // TODO(fxbug.dev/52047): Remove once caching handles ordering issues.
                        if existing_diagnostics_artifact_container.lifecycle.is_none() {
                            existing_diagnostics_artifact_container.lifecycle =
                                Some(lifecycle_artifact_container);
                        }
                    }
                    _ => {
                        return Err(format_err!(
                            concat!(
                                "Encountered a diagnostics data repository node with more",
                                "than one artifact container, moniker: {:?}."
                            ),
                            key
                        ));
                    }
                }
            }
            // This case is expected to be the most common case. We've seen a creation
            // lifecycle event and it promotes the instantiation of a new data repository entry.
            None => self.data_directories.insert(
                key,
                ComponentDiagnostics::new_with_lifecycle(
                    relative_moniker,
                    component_url.into(),
                    lifecycle_artifact_container,
                ),
            ),
        }
        Ok(())
    }

    pub fn add_inspect_artifacts(
        &mut self,
        identifier: ComponentIdentifier,
        component_url: impl Into<String>,
        directory_proxy: DirectoryProxy,
        event_timestamp: zx::Time,
    ) -> Result<(), Error> {
        let relative_moniker = identifier.relative_moniker_for_selectors();
        let key = identifier.unique_key();

        let inspect_container = InspectArtifactsContainer {
            component_diagnostics_proxy: Arc::new(directory_proxy),
            event_timestamp,
        };

        self.insert_inspect_artifact_container(
            inspect_container,
            key,
            relative_moniker,
            component_url.into(),
        )
    }

    // Inserts an InspectArtifactsContainer into the data repository.
    fn insert_inspect_artifact_container(
        &mut self,
        inspect_container: InspectArtifactsContainer,
        key: Vec<String>,
        relative_moniker: Vec<String>,
        component_url: String,
    ) -> Result<(), Error> {
        let diag_repo_entry_opt = self.data_directories.get_mut(key.clone());
        match diag_repo_entry_opt {
            Some(diag_repo_entry) => {
                let diag_repo_entry_values: &mut [ComponentDiagnostics] =
                    diag_repo_entry.get_values_mut();

                match &mut diag_repo_entry_values[..] {
                    [] => {
                        // An entry with no values implies that the somehow we observed the
                        // creation of a component lower in the topology before observing this
                        // one. If this is the case, just instantiate as though it's our first
                        // time encountering this moniker segment.
                        self.data_directories.insert(
                            key,
                            ComponentDiagnostics::new_with_inspect(
                                relative_moniker,
                                component_url,
                                inspect_container,
                            ),
                        )
                    }
                    [existing_diagnostics_artifact_container] => {
                        // Races may occur between synthesized and real diagnostics_ready
                        // events, so we must handle de-duplication here.
                        // TODO(fxbug.dev/52047): Remove once caching handles ordering issues.
                        if existing_diagnostics_artifact_container.inspect.is_none() {
                            // This is expected to be the most common case. We've encountered the
                            // diagnostics_ready event for a component that has already been
                            // observed to be started/existing. We now must update the diagnostics
                            // artifact container with the inspect artifacts that accompanied the
                            // diagnostics_ready event.
                            existing_diagnostics_artifact_container.inspect =
                                Some(inspect_container);
                        }
                    }
                    _ => {
                        return Err(format_err!(
                            concat!(
                                "Encountered a diagnostics data repository node with more",
                                "than one artifact container, moniker: {:?}."
                            ),
                            key
                        ));
                    }
                }
            }
            // This case is expected to be uncommon; we've encountered a diagnostics_ready
            // event before a start or existing event!
            None => self.data_directories.insert(
                key,
                ComponentDiagnostics::new_with_inspect(
                    relative_moniker,
                    component_url,
                    inspect_container,
                ),
            ),
        }
        Ok(())
    }

    pub fn fetch_lifecycle_event_data(&self) -> Vec<LifecycleDataContainer> {
        self.data_directories.iter().fold(
            Vec::new(),
            |mut acc, (_, diagnostics_artifacts_container_opt)| {
                match diagnostics_artifacts_container_opt {
                    None => acc,
                    Some(diagnostics_artifacts_container) => {
                        if let Some(lifecycle_artifacts) =
                            &diagnostics_artifacts_container.lifecycle
                        {
                            acc.push(LifecycleDataContainer::from_lifecycle_artifact(
                                lifecycle_artifacts,
                                diagnostics_artifacts_container.relative_moniker.clone(),
                                diagnostics_artifacts_container.component_url.clone(),
                            ));
                        }

                        if let Some(inspect_artifacts) = &diagnostics_artifacts_container.inspect {
                            acc.push(LifecycleDataContainer::from_inspect_artifact(
                                inspect_artifacts,
                                diagnostics_artifacts_container.relative_moniker.clone(),
                                diagnostics_artifacts_container.component_url.clone(),
                            ));
                        }

                        acc
                    }
                }
            },
        )
    }

    /// Return all of the DirectoryProxies that contain Inspect hierarchies
    /// which contain data that should be selected from.
    pub fn fetch_inspect_data(
        &self,
        component_selectors: &Option<Vec<Arc<Selector>>>,
        moniker_to_static_matcher_map: Option<&HashMap<String, InspectHierarchyMatcher>>,
    ) -> Vec<UnpopulatedInspectDataContainer> {
        return self
            .data_directories
            .iter()
            .filter_map(|(_, diagnostics_artifacts_container_opt)| {
                let (diagnostics_artifacts_container, inspect_artifacts) =
                    match &diagnostics_artifacts_container_opt {
                        Some(diagnostics_artifacts_container) => {
                            match &diagnostics_artifacts_container.inspect {
                                Some(inspect_artifacts) => {
                                    (diagnostics_artifacts_container, inspect_artifacts)
                                }
                                None => return None,
                            }
                        }
                        None => return None,
                    };

                let optional_hierarchy_matcher = match moniker_to_static_matcher_map {
                    Some(map) => {
                        match map.get(&diagnostics_artifacts_container.relative_moniker.join("/")) {
                            Some(inspect_matcher) => Some(inspect_matcher),
                            // Return early if there were static selectors, and none were for this
                            // moniker.
                            None => return None,
                        }
                    }
                    None => None,
                };

                // Verify that the dynamic selectors contain an entry that applies to
                // this moniker as well.
                if !match component_selectors {
                    Some(component_selectors) => component_selectors.iter().any(|s| {
                        selectors::match_component_moniker_against_selector(
                            &diagnostics_artifacts_container.relative_moniker,
                            s,
                        )
                        .ok()
                        .unwrap_or(false)
                    }),
                    None => true,
                } {
                    return None;
                }

                // This artifact contains inspect and matches a passed selector.
                io_util::clone_directory(
                    &inspect_artifacts.component_diagnostics_proxy,
                    CLONE_FLAG_SAME_RIGHTS,
                )
                .ok()
                .map(|directory| UnpopulatedInspectDataContainer {
                    relative_moniker: diagnostics_artifacts_container.relative_moniker.clone(),
                    component_url: diagnostics_artifacts_container.component_url.clone(),
                    component_diagnostics_proxy: directory,
                    inspect_matcher: optional_hierarchy_matcher.cloned(),
                })
            })
            .collect();
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::events::types::{ComponentIdentifier, LegacyIdentifier, RealmPath},
        diagnostics_hierarchy::trie::TrieIterableNode,
        fidl_fuchsia_io::DirectoryMarker,
        fuchsia_async as fasync, fuchsia_zircon as zx,
    };

    const TEST_URL: &'static str = "fuchsia-pkg://test";

    #[fasync::run_singlethreaded(test)]
    async fn inspect_repo_disallows_duplicated_dirs() {
        let inspect_repo = DataRepo::new();
        let mut inspect_repo = inspect_repo.write();
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path,
            component_name: "foo.cmx".into(),
        });
        let (proxy, _) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("create directory proxy");

        inspect_repo
            .add_inspect_artifacts(component_id.clone(), TEST_URL, proxy, zx::Time::from_nanos(0))
            .expect("add to repo");

        let (proxy, _) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("create directory proxy");

        inspect_repo
            .add_inspect_artifacts(component_id.clone(), TEST_URL, proxy, zx::Time::from_nanos(0))
            .expect("add to repo");

        let key = component_id.unique_key();
        assert_eq!(inspect_repo.data_directories.get(key).unwrap().get_values().len(), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn data_repo_updates_existing_entry_to_hold_inspect_data() {
        let data_repo = DataRepo::new();
        let mut data_repo = data_repo.write();
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path,
            component_name: "foo.cmx".into(),
        });

        data_repo
            .add_new_component(component_id.clone(), TEST_URL, zx::Time::from_nanos(0), None)
            .expect("instantiated new component.");

        let (proxy, _) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("create directory proxy");

        data_repo
            .add_inspect_artifacts(component_id.clone(), TEST_URL, proxy, zx::Time::from_nanos(0))
            .expect("add to repo");

        let key = component_id.unique_key();
        assert_eq!(data_repo.data_directories.get(key.clone()).unwrap().get_values().len(), 1);
        let entry = &data_repo.data_directories.get(key.clone()).unwrap().get_values()[0];
        assert!(entry.inspect.is_some());
        assert_eq!(entry.component_url, TEST_URL);
    }

    #[fasync::run_singlethreaded(test)]
    async fn data_repo_tolerates_duplicate_new_component_insertions() {
        let data_repo = DataRepo::new();
        let mut data_repo = data_repo.write();
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path,
            component_name: "foo.cmx".into(),
        });

        data_repo
            .add_new_component(component_id.clone(), TEST_URL, zx::Time::from_nanos(0), None)
            .expect("instantiated new component.");

        let duplicate_new_component_insertion = data_repo.add_new_component(
            component_id.clone(),
            TEST_URL,
            zx::Time::from_nanos(1),
            Some(zx::Time::from_nanos(0)),
        );

        assert!(duplicate_new_component_insertion.is_ok());

        let key = component_id.unique_key();
        let repo_values = data_repo.data_directories.get(key.clone()).unwrap().get_values();
        assert_eq!(repo_values.len(), 1);
        let entry = &repo_values[0];
        assert!(entry.lifecycle.is_some());
        let lifecycle_container = entry.lifecycle.as_ref().unwrap();
        assert!(lifecycle_container.component_start_time.is_none());
        assert_eq!(entry.relative_moniker, component_id.relative_moniker_for_selectors());
        assert_eq!(entry.component_url, TEST_URL);
    }

    #[fasync::run_singlethreaded(test)]
    async fn running_components_provide_start_time() {
        let data_repo = DataRepo::new();
        let mut data_repo = data_repo.write();
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path,
            component_name: "foo.cmx".into(),
        });

        let component_insertion = data_repo.add_new_component(
            component_id.clone(),
            TEST_URL,
            zx::Time::from_nanos(1),
            Some(zx::Time::from_nanos(0)),
        );

        assert!(component_insertion.is_ok());

        let key = component_id.unique_key();
        let repo_values = data_repo.data_directories.get(key.clone()).unwrap().get_values();
        assert_eq!(repo_values.len(), 1);
        let entry = &repo_values[0];
        assert!(entry.lifecycle.is_some());
        let lifecycle_container = entry.lifecycle.as_ref().unwrap();
        assert!(lifecycle_container.component_start_time.is_some());
        assert_eq!(lifecycle_container.component_start_time.unwrap().into_nanos(), 0);
        assert_eq!(entry.relative_moniker, component_id.relative_moniker_for_selectors());
        assert_eq!(entry.component_url, TEST_URL);
    }

    #[fasync::run_singlethreaded(test)]
    async fn data_repo_tolerant_of_new_component_calls_if_diagnostics_ready_already_processed() {
        let data_repo = DataRepo::new();
        let mut data_repo = data_repo.write();
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path,
            component_name: "foo.cmx".into(),
        });

        let (proxy, _) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("create directory proxy");

        data_repo
            .add_inspect_artifacts(component_id.clone(), TEST_URL, proxy, zx::Time::from_nanos(0))
            .expect("add to repo");

        let false_new_component_result = data_repo.add_new_component(
            component_id.clone(),
            TEST_URL,
            zx::Time::from_nanos(0),
            None,
        );
        assert!(false_new_component_result.is_ok());

        // We shouldn't have overwritten the entry. There should still be an inspect
        // artifacts container.
        let key = component_id.unique_key();
        assert_eq!(data_repo.data_directories.get(key.clone()).unwrap().get_values().len(), 1);
        let entry = &data_repo.data_directories.get(key.clone()).unwrap().get_values()[0];
        assert_eq!(entry.component_url, TEST_URL);
        assert!(entry.inspect.is_some());
        assert!(entry.lifecycle.is_some());
    }

    #[fasync::run_singlethreaded(test)]
    async fn diagnostics_repo_cant_have_more_than_one_diagnostics_data_container_per_component() {
        let data_repo = DataRepo::new();
        let mut data_repo = data_repo.write();
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path,
            component_name: "foo.cmx".into(),
        });

        data_repo
            .add_new_component(component_id.clone(), TEST_URL, zx::Time::from_nanos(0), None)
            .expect("insertion will succeed.");

        let key = component_id.unique_key();
        assert_eq!(data_repo.data_directories.get(key.clone()).unwrap().get_values().len(), 1);

        let mutable_values =
            data_repo.data_directories.get_mut(key.clone()).unwrap().get_values_mut();
        mutable_values.push(ComponentDiagnostics::empty(
            component_id.relative_moniker_for_selectors(),
            TEST_URL.to_string(),
        ));

        let (proxy, _) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("create directory proxy");

        assert!(data_repo
            .add_inspect_artifacts(component_id.clone(), TEST_URL, proxy, zx::Time::from_nanos(0))
            .is_err());
    }

    #[fasync::run_singlethreaded(test)]
    async fn data_repo_filters_inspect_by_selectors() {
        let data_repo = DataRepo::new();
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path: realm_path.clone(),
            component_name: "foo.cmx".into(),
        });

        data_repo
            .write()
            .add_new_component(component_id.clone(), TEST_URL, zx::Time::from_nanos(0), None)
            .expect("insertion will succeed.");

        data_repo
            .write()
            .add_inspect_artifacts(
                component_id.clone(),
                TEST_URL,
                io_util::open_directory_in_namespace("/tmp", io_util::OPEN_RIGHT_READABLE)
                    .expect("open root"),
                zx::Time::from_nanos(0),
            )
            .expect("add inspect artifacts");

        let component_id2 = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id: "12345".to_string(),
            realm_path,
            component_name: "foo2.cmx".into(),
        });

        data_repo
            .write()
            .add_new_component(component_id2.clone(), TEST_URL, zx::Time::from_nanos(0), None)
            .expect("insertion will succeed.");

        data_repo
            .write()
            .add_inspect_artifacts(
                component_id2.clone(),
                TEST_URL,
                io_util::open_directory_in_namespace("/tmp", io_util::OPEN_RIGHT_READABLE)
                    .expect("open root"),
                zx::Time::from_nanos(0),
            )
            .expect("add inspect artifacts");

        assert_eq!(2, data_repo.read().fetch_inspect_data(&None, None).len());

        let selectors = Some(vec![Arc::new(
            selectors::parse_selector("a/b/foo.cmx:root").expect("parse selector"),
        )]);
        assert_eq!(1, data_repo.read().fetch_inspect_data(&selectors, None).len());

        let selectors = Some(vec![Arc::new(
            selectors::parse_selector("a/b/f*.cmx:root").expect("parse selector"),
        )]);
        assert_eq!(2, data_repo.read().fetch_inspect_data(&selectors, None).len());

        let selectors = Some(vec![Arc::new(
            selectors::parse_selector("foo.cmx:root").expect("parse selector"),
        )]);
        assert_eq!(0, data_repo.read().fetch_inspect_data(&selectors, None).len());
    }
}
