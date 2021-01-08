// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{
        constants::{ARCHIVIST_MONIKER, ARCHIVIST_URL},
        container::{ComponentDiagnostics, ComponentIdentity},
        events::types::{ComponentEvent, ComponentIdentifier, LogSinkRequestedEvent},
        inspect::container::{InspectArtifactsContainer, UnpopulatedInspectDataContainer},
        lifecycle::container::{LifecycleArtifactsContainer, LifecycleDataContainer},
        logs::{
            buffer::{AccountedBuffer, LazyItem},
            container::LogsArtifactsContainer,
            debuglog::{DebugLog, DebugLogBridge, KERNEL_IDENTITY},
            error::LogsError,
            listener::{pretend_scary_listener_is_safe, Listener, ListenerError},
            trimmer::keep_logs_trimmed,
            Message,
        },
    },
    anyhow::{format_err, Error},
    diagnostics_hierarchy::{trie, InspectHierarchyMatcher},
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_diagnostics::{self, Selector, StreamMode},
    fidl_fuchsia_io::{DirectoryProxy, CLONE_FLAG_SAME_RIGHTS},
    fidl_fuchsia_logger::{LogInterestSelector, LogMarker, LogRequest, LogRequestStream},
    fidl_fuchsia_sys2 as fsys,
    fidl_fuchsia_sys_internal::{LogConnection, LogConnectionListenerRequest, LogConnectorProxy},
    fuchsia_async::Task,
    fuchsia_inspect as inspect,
    fuchsia_inspect_derive::WithInspect,
    fuchsia_zircon as zx,
    futures::channel::mpsc::{self, Sender},
    futures::prelude::*,
    io_util,
    parking_lot::{Mutex, RwLock},
    selectors,
    std::collections::HashMap,
    std::{
        convert::{TryFrom, TryInto},
        sync::Arc,
    },
    tracing::{debug, error, warn},
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

#[cfg(test)]
impl Default for DataRepo {
    fn default() -> Self {
        Self::new(crate::constants::LEGACY_DEFAULT_MAXIMUM_CACHED_LOGS_BYTES, &Default::default())
    }
}

impl DataRepo {
    pub fn new(logs_capacity: usize, parent: &fuchsia_inspect::Node) -> Self {
        DataRepo { inner: DataRepoState::new(logs_capacity, parent) }
    }

    /// Drain the kernel's debug log. The returned future completes once
    /// existing messages have been ingested.
    pub async fn drain_debuglog<K>(self, klog_reader: K)
    where
        K: DebugLog + Send + Sync + 'static,
    {
        debug!("Draining debuglog.");
        let container = self.write().get_log_container(KERNEL_IDENTITY.clone());
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
            container.ingest_message(message);
        }

        let res = kernel_logger
            .listen()
            .try_for_each(|message| async {
                container.ingest_message(message);
                Ok(())
            })
            .await;
        if let Err(e) = res {
            error!(%e, "failed to drain kernel log, important logs may be missing");
        }
    }

    // TODO(fxbug.dev/66950) this should be a small shim to convert this into v2 events
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
                            let identity = match ComponentIdentity::try_from(source_identity) {
                                Ok(i) => i,
                                Err(e) => {
                                    error!(%e, "error consuming SourceIdentity");
                                    continue;
                                }
                            };
                            let container = self.write().get_log_container(identity);

                            let stream = log_request
                                .into_stream()
                                .expect("getting LogSinkRequestStream from serverend");
                            let task =
                                Task::spawn(container.handle_log_sink(stream, sender.clone()));
                            sender
                                .unbounded_send(task)
                                .expect("channel is held by archivist, lasts for whole program");
                        }
                    };
                }
            }
            Ok(None) => warn!("local realm already gave out LogConnectionListener, skipping logs"),
            Err(e) => error!(%e, "error retrieving LogConnectionListener from LogConnector"),
        }
    }

    // TODO(fxbug.dev/66950) delete this
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
                    if let Err(e) = self.clone().handle_event(event, sender.clone()) {
                        error!(%e, "Unable to process event");
                    }
                }
            }
        }
    }

    // TODO(fxbug.dev/66950) delete this
    /// Handle the components v2 CapabilityRequested event for attributed logs of
    /// v2 components.
    fn handle_event(
        self,
        event: fsys::Event,
        sender: mpsc::UnboundedSender<Task<()>>,
    ) -> Result<(), LogsError> {
        let LogSinkRequestedEvent { metadata, requests } = match event.try_into()? {
            ComponentEvent::LogSinkRequested(event) => event,
            other => unreachable!("should never see {:?} here", other),
        };
        let container = self.write().get_log_container(metadata.identity);
        let task = Task::spawn(container.handle_log_sink(requests, sender.clone()));
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
                self.write().update_logs_interest(s);
            }

            sender.send(listener.spawn(logs, dump_logs)).await.ok();
        }
        Ok(())
    }

    pub fn cursor(&self, mode: StreamMode) -> impl Stream<Item = Arc<Message>> {
        self.read().logs_buffer.lock().cursor(mode).map(|item| match item {
            LazyItem::Next(m) => m,
            LazyItem::ItemsDropped(n) => Arc::new(Message::for_dropped(n)),
        })
    }

    /// Stop accepting new messages, ensuring that pending Cursors return Poll::Ready(None) after
    /// consuming any messages received before this call.
    pub fn terminate_logs(&self) {
        self.read().logs_buffer.lock().terminate();
    }
}

pub struct DataRepoState {
    pub data_directories: trie::Trie<String, ComponentDiagnostics>,
    inspect_node: inspect::Node,
    logs_interest: Vec<LogInterestSelector>,
    logs_buffer: Arc<Mutex<AccountedBuffer<Message>>>,
    logs_notifier: Sender<()>,
    _logs_trimmer: Task<()>,
}

impl DataRepoState {
    fn new(logs_capacity: usize, parent: &fuchsia_inspect::Node) -> Arc<RwLock<Self>> {
        let logs_buffer = Arc::new(Mutex::new(
            AccountedBuffer::default()
                .with_inspect(parent, "logs_buffer")
                .expect("failed to attach inspect"),
        ));
        let (logs_notifier, on_new_messages) = mpsc::channel(1);
        let weak_buffer = Arc::downgrade(&logs_buffer);
        let _logs_trimmer =
            Task::spawn(keep_logs_trimmed(weak_buffer, logs_capacity, on_new_messages));

        Arc::new(RwLock::new(Self {
            inspect_node: parent.create_child("sources"),
            data_directories: trie::Trie::new(),
            logs_interest: vec![],
            logs_buffer,
            logs_notifier,
            _logs_trimmer,
        }))
    }

    pub fn remove(&mut self, key: &[String]) {
        self.data_directories.remove(key.to_vec());
    }

    pub fn add_new_component(
        &mut self,
        identity: ComponentIdentity,
        event_timestamp: zx::Time,
        component_start_time: Option<zx::Time>,
    ) -> Result<(), Error> {
        let lifecycle_artifact_container = LifecycleArtifactsContainer {
            event_timestamp: event_timestamp,
            component_start_time: component_start_time,
        };

        let key = identity.unique_key.clone();

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
                                Arc::new(identity),
                                lifecycle_artifact_container,
                                &self.inspect_node,
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
                    Arc::new(identity),
                    lifecycle_artifact_container,
                    &self.inspect_node,
                ),
            ),
        }
        Ok(())
    }

    /// Returns a container for logs artifacts, constructing one and adding it to the trie if
    /// necessary.
    pub fn get_log_container(
        &mut self,
        identity: ComponentIdentity,
    ) -> Arc<LogsArtifactsContainer> {
        let trie_key = identity.unique_key.clone();

        // we use a macro instead of a closure to avoid lifetime issues
        macro_rules! insert_component {
            () => {{
                let mut to_insert =
                    ComponentDiagnostics::empty(Arc::new(identity), &self.inspect_node);
                let logs =
                    to_insert.logs(&self.logs_buffer, &self.logs_notifier, &self.logs_interest);
                self.data_directories.insert(trie_key, to_insert);
                logs
            }};
        }

        match self.data_directories.get_mut(trie_key.clone()) {
            Some(component) => match &mut component.get_values_mut()[..] {
                [] => insert_component!(),
                [existing] => {
                    existing.logs(&self.logs_buffer, &self.logs_notifier, &self.logs_interest)
                }
                _ => unreachable!("invariant: each trie node has 0-1 entries"),
            },
            None => insert_component!(),
        }
    }

    pub fn get_own_log_container(&mut self) -> Arc<LogsArtifactsContainer> {
        self.get_log_container(ComponentIdentity::from_identifier_and_url(
            &ComponentIdentifier::Moniker(ARCHIVIST_MONIKER.to_string()),
            ARCHIVIST_URL,
        ))
    }

    pub fn update_logs_interest(&mut self, selectors: Vec<LogInterestSelector>) {
        self.logs_interest = selectors;
        for (_, dir) in self.data_directories.iter() {
            if let Some(dir) = dir {
                if let Some(logs) = &dir.logs {
                    logs.update_interest(&self.logs_interest);
                }
            }
        }
    }

    pub fn add_inspect_artifacts(
        &mut self,
        identity: ComponentIdentity,
        directory_proxy: DirectoryProxy,
        event_timestamp: zx::Time,
    ) -> Result<(), Error> {
        let inspect_container = InspectArtifactsContainer {
            component_diagnostics_proxy: Arc::new(directory_proxy),
            event_timestamp,
        };

        self.insert_inspect_artifact_container(inspect_container, identity)
    }

    // Inserts an InspectArtifactsContainer into the data repository.
    fn insert_inspect_artifact_container(
        &mut self,
        inspect_container: InspectArtifactsContainer,
        identity: ComponentIdentity,
    ) -> Result<(), Error> {
        let key = identity.unique_key.clone();

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
                                Arc::new(identity),
                                inspect_container,
                                &self.inspect_node,
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
                    Arc::new(identity),
                    inspect_container,
                    &self.inspect_node,
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
                                diagnostics_artifacts_container.identity.clone(),
                            ));
                        }

                        if let Some(inspect_artifacts) = &diagnostics_artifacts_container.inspect {
                            acc.push(LifecycleDataContainer::from_inspect_artifact(
                                inspect_artifacts,
                                diagnostics_artifacts_container.identity.clone(),
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
                        match map.get(
                            &diagnostics_artifacts_container.identity.relative_moniker.join("/"),
                        ) {
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
                            &diagnostics_artifacts_container.identity.relative_moniker,
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
                    identity: diagnostics_artifacts_container.identity.clone(),
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
        let inspect_repo = DataRepo::default();
        let mut inspect_repo = inspect_repo.write();
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path,
            component_name: "foo.cmx".into(),
        });
        let identity = ComponentIdentity::from_identifier_and_url(&component_id, TEST_URL);

        let (proxy, _) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("create directory proxy");

        inspect_repo
            .add_inspect_artifacts(identity.clone(), proxy, zx::Time::from_nanos(0))
            .expect("add to repo");

        let (proxy, _) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("create directory proxy");

        inspect_repo
            .add_inspect_artifacts(identity.clone(), proxy, zx::Time::from_nanos(0))
            .expect("add to repo");

        let key = identity.unique_key.clone();
        assert_eq!(inspect_repo.data_directories.get(key).unwrap().get_values().len(), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn data_repo_updates_existing_entry_to_hold_inspect_data() {
        let data_repo = DataRepo::default();
        let mut data_repo = data_repo.write();
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path,
            component_name: "foo.cmx".into(),
        });
        let identity = ComponentIdentity::from_identifier_and_url(&component_id, TEST_URL);

        data_repo
            .add_new_component(identity.clone(), zx::Time::from_nanos(0), None)
            .expect("instantiated new component.");

        let (proxy, _) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("create directory proxy");

        data_repo
            .add_inspect_artifacts(identity.clone(), proxy, zx::Time::from_nanos(0))
            .expect("add to repo");

        let key = &identity.unique_key;
        assert_eq!(data_repo.data_directories.get(key.clone()).unwrap().get_values().len(), 1);
        let entry = &data_repo.data_directories.get(key.clone()).unwrap().get_values()[0];
        assert!(entry.inspect.is_some());
        assert_eq!(entry.identity.url, TEST_URL);
    }

    #[fasync::run_singlethreaded(test)]
    async fn data_repo_tolerates_duplicate_new_component_insertions() {
        let data_repo = DataRepo::default();
        let mut data_repo = data_repo.write();
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path,
            component_name: "foo.cmx".into(),
        });
        let identity = ComponentIdentity::from_identifier_and_url(&component_id, TEST_URL);

        data_repo
            .add_new_component(identity.clone(), zx::Time::from_nanos(0), None)
            .expect("instantiated new component.");

        let duplicate_new_component_insertion = data_repo.add_new_component(
            identity.clone(),
            zx::Time::from_nanos(1),
            Some(zx::Time::from_nanos(0)),
        );

        assert!(duplicate_new_component_insertion.is_ok());

        let key = &identity.unique_key;
        let repo_values = data_repo.data_directories.get(key.clone()).unwrap().get_values();
        assert_eq!(repo_values.len(), 1);
        let entry = &repo_values[0];
        assert!(entry.lifecycle.is_some());
        let lifecycle_container = entry.lifecycle.as_ref().unwrap();
        assert!(lifecycle_container.component_start_time.is_none());
        assert_eq!(entry.identity.relative_moniker, component_id.relative_moniker_for_selectors());
        assert_eq!(entry.identity.url, TEST_URL);
    }

    #[fasync::run_singlethreaded(test)]
    async fn running_components_provide_start_time() {
        let data_repo = DataRepo::default();
        let mut data_repo = data_repo.write();
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path,
            component_name: "foo.cmx".into(),
        });
        let identity = ComponentIdentity::from_identifier_and_url(&component_id, TEST_URL);

        let component_insertion = data_repo.add_new_component(
            identity.clone(),
            zx::Time::from_nanos(1),
            Some(zx::Time::from_nanos(0)),
        );

        assert!(component_insertion.is_ok());

        let key = &identity.unique_key;
        let repo_values = data_repo.data_directories.get(key.clone()).unwrap().get_values();
        assert_eq!(repo_values.len(), 1);
        let entry = &repo_values[0];
        assert!(entry.lifecycle.is_some());
        let lifecycle_container = entry.lifecycle.as_ref().unwrap();
        assert!(lifecycle_container.component_start_time.is_some());
        assert_eq!(lifecycle_container.component_start_time.unwrap().into_nanos(), 0);
        assert_eq!(entry.identity.relative_moniker, component_id.relative_moniker_for_selectors());
        assert_eq!(entry.identity.url, TEST_URL);
    }

    #[fasync::run_singlethreaded(test)]
    async fn data_repo_tolerant_of_new_component_calls_if_diagnostics_ready_already_processed() {
        let data_repo = DataRepo::default();
        let mut data_repo = data_repo.write();
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path,
            component_name: "foo.cmx".into(),
        });
        let identity = ComponentIdentity::from_identifier_and_url(&component_id, TEST_URL);

        let (proxy, _) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("create directory proxy");

        data_repo
            .add_inspect_artifacts(identity.clone(), proxy, zx::Time::from_nanos(0))
            .expect("add to repo");

        let false_new_component_result =
            data_repo.add_new_component(identity.clone(), zx::Time::from_nanos(0), None);
        assert!(false_new_component_result.is_ok());

        // We shouldn't have overwritten the entry. There should still be an inspect
        // artifacts container.
        let key = &identity.unique_key;
        assert_eq!(data_repo.data_directories.get(key.clone()).unwrap().get_values().len(), 1);
        let entry = &data_repo.data_directories.get(key.clone()).unwrap().get_values()[0];
        assert_eq!(entry.identity.url, TEST_URL);
        assert!(entry.inspect.is_some());
        assert!(entry.lifecycle.is_some());
    }

    #[fasync::run_singlethreaded(test)]
    async fn diagnostics_repo_cant_have_more_than_one_diagnostics_data_container_per_component() {
        let data_repo = DataRepo::default();
        let mut data_repo = data_repo.write();
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path,
            component_name: "foo.cmx".into(),
        });
        let identity = ComponentIdentity::from_identifier_and_url(&component_id, TEST_URL);

        data_repo
            .add_new_component(identity.clone(), zx::Time::from_nanos(0), None)
            .expect("insertion will succeed.");

        let key = &identity.unique_key;
        assert_eq!(data_repo.data_directories.get(key.clone()).unwrap().get_values().len(), 1);

        let mutable_values =
            data_repo.data_directories.get_mut(key.clone()).unwrap().get_values_mut();

        mutable_values
            .push(ComponentDiagnostics::empty(Arc::new(identity.clone()), &Default::default()));

        let (proxy, _) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("create directory proxy");

        assert!(data_repo.add_inspect_artifacts(identity, proxy, zx::Time::from_nanos(0)).is_err());
    }

    #[fasync::run_singlethreaded(test)]
    async fn data_repo_filters_inspect_by_selectors() {
        let data_repo = DataRepo::default();
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path: realm_path.clone(),
            component_name: "foo.cmx".into(),
        });
        let identity = ComponentIdentity::from_identifier_and_url(&component_id, TEST_URL);

        data_repo
            .write()
            .add_new_component(identity.clone(), zx::Time::from_nanos(0), None)
            .expect("insertion will succeed.");

        data_repo
            .write()
            .add_inspect_artifacts(
                identity,
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
        let identity2 = ComponentIdentity::from_identifier_and_url(&component_id2, TEST_URL);

        data_repo
            .write()
            .add_new_component(identity2.clone(), zx::Time::from_nanos(0), None)
            .expect("insertion will succeed.");

        data_repo
            .write()
            .add_inspect_artifacts(
                identity2,
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
