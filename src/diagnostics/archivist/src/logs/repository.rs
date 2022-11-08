// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    container::ComponentDiagnostics,
    identity::ComponentIdentity,
    logs::{
        budget::BudgetManager,
        container::LogsArtifactsContainer,
        debuglog::{DebugLog, DebugLogBridge, KERNEL_IDENTITY},
        error::LogsError,
        listener::Listener,
        multiplex::{Multiplexer, MultiplexerHandle},
    },
    trie,
};
use async_lock::{Mutex, RwLock};
use diagnostics_data::LogsData;
use fidl::prelude::*;
use fidl_fuchsia_diagnostics::{
    self, LogInterestSelector, LogSettingsMarker, LogSettingsRequest, LogSettingsRequestStream,
    Selector, StreamMode,
};
use fidl_fuchsia_logger::{LogMarker, LogRequest, LogRequestStream};
use fuchsia_async as fasync;
use fuchsia_inspect as inspect;
use fuchsia_trace as ftrace;
use futures::channel::mpsc;
use futures::prelude::*;
use lazy_static::lazy_static;
use std::{
    collections::{BTreeMap, HashMap},
    sync::{
        atomic::{AtomicUsize, Ordering},
        Arc,
    },
};
use tracing::{debug, error, warn};

lazy_static! {
    static ref CONNECTION_ID: AtomicUsize = AtomicUsize::new(0);
}

/// LogsRepository holds all diagnostics data and is a singleton wrapped by multiple
/// [`pipeline::Pipeline`]s in a given Archivist instance.
pub struct LogsRepository {
    inner: RwLock<LogsRepositoryState>,
}

impl LogsRepository {
    pub async fn new(logs_budget: &BudgetManager, parent: &fuchsia_inspect::Node) -> Self {
        LogsRepository { inner: LogsRepositoryState::new(logs_budget.clone(), parent).await }
    }

    /// Drain the kernel's debug log. The returned future completes once
    /// existing messages have been ingested.
    pub async fn drain_debuglog<K>(self: Arc<Self>, klog_reader: K)
    where
        K: DebugLog + Send + Sync + 'static,
    {
        debug!("Draining debuglog.");
        let container = self.inner.write().await.get_log_container(KERNEL_IDENTITY.clone()).await;
        let mut kernel_logger = DebugLogBridge::create(klog_reader);
        let mut messages = match kernel_logger.existing_logs().await {
            Ok(messages) => messages,
            Err(e) => {
                error!(%e, "failed to read from kernel log, important logs may be missing");
                return;
            }
        };
        messages.sort_by_key(|m| m.timestamp());
        for message in messages {
            container.ingest_message(message).await;
        }

        let res = kernel_logger
            .listen()
            .try_for_each(|message| async {
                container.ingest_message(message).await;
                Ok(())
            })
            .await;
        if let Err(e) = res {
            error!(%e, "failed to drain kernel log, important logs may be missing");
        }
    }

    /// Spawn a task to handle requests from components reading the shared log.
    pub fn handle_log(
        self: Arc<LogsRepository>,
        stream: LogRequestStream,
        sender: mpsc::UnboundedSender<fasync::Task<()>>,
    ) {
        if let Err(e) = sender.clone().unbounded_send(fasync::Task::spawn(async move {
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
        self: Arc<LogsRepository>,
        mut stream: LogRequestStream,
        mut sender: mpsc::UnboundedSender<fasync::Task<()>>,
    ) -> Result<(), LogsError> {
        let connection_id = CONNECTION_ID.fetch_add(1, Ordering::Relaxed);
        while let Some(request) = stream.next().await {
            let request = request.map_err(|source| LogsError::HandlingRequests {
                protocol: LogMarker::PROTOCOL_NAME,
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
            };

            let listener = Listener::new(listener, options)?;
            let mode =
                if dump_logs { StreamMode::Snapshot } else { StreamMode::SnapshotThenSubscribe };
            // NOTE: The LogListener code path isn't instrumented for tracing at the moment.
            let logs = self.logs_cursor(mode, None, ftrace::Id::random()).await;
            if let Some(s) = selectors {
                self.inner.write().await.update_logs_interest(connection_id, s).await;
            }

            sender.send(listener.spawn(logs, dump_logs)).await.ok();
        }
        self.inner.write().await.finish_interest_connection(connection_id).await;
        Ok(())
    }

    pub async fn handle_log_settings(
        self: Arc<Self>,
        mut stream: LogSettingsRequestStream,
    ) -> Result<(), LogsError> {
        let connection_id = CONNECTION_ID.fetch_add(1, Ordering::Relaxed);
        while let Some(request) = stream.next().await {
            let request = request.map_err(|source| LogsError::HandlingRequests {
                protocol: LogSettingsMarker::PROTOCOL_NAME,
                source,
            })?;
            match request {
                LogSettingsRequest::RegisterInterest { selectors, .. } => {
                    self.inner.write().await.update_logs_interest(connection_id, selectors).await;
                }
            }
        }
        self.inner.write().await.finish_interest_connection(connection_id).await;

        Ok(())
    }

    pub async fn logs_cursor(
        &self,
        mode: StreamMode,
        selectors: Option<Vec<Selector>>,
        parent_trace_id: ftrace::Id,
    ) -> impl Stream<Item = Arc<LogsData>> + Send + 'static {
        let mut repo = self.inner.write().await;
        let (mut merged, mpx_handle) = Multiplexer::new(parent_trace_id);
        if let Some(selectors) = selectors {
            merged.set_selectors(selectors);
        }
        repo.data_directories
            .iter()
            .filter_map(|(_, c)| c)
            .filter_map(|c| {
                c.logs_cursor(mode, parent_trace_id)
                    .map(|cursor| (c.identity.relative_moniker.clone(), cursor))
            })
            .for_each(|(n, c)| {
                mpx_handle.send(n, c);
            });
        repo.logs_multiplexers.add(mode, mpx_handle).await;
        merged.set_on_drop_id_sender(repo.logs_multiplexers.cleanup_sender());

        merged
    }

    /// Returns `true` if a container exists for the requested `identity` and that container either
    /// corresponds to a running component or we've decided to still retain it.
    pub async fn is_live(&self, identity: &ComponentIdentity) -> bool {
        let this = self.inner.read().await;
        match this.data_directories.get(&identity.unique_key()) {
            Some(container) => container.should_retain().await,
            None => false,
        }
    }

    pub async fn get_log_container(
        &self,
        identity: ComponentIdentity,
    ) -> Arc<LogsArtifactsContainer> {
        self.inner.write().await.get_log_container(identity).await
    }

    pub async fn remove(&self, identity: &ComponentIdentity) {
        self.inner.write().await.remove(identity);
    }
    /// Stop accepting new messages, ensuring that pending Cursors return Poll::Ready(None) after
    /// consuming any messages received before this call.
    pub async fn terminate_logs(&self) {
        let mut repo = self.inner.write().await;
        for container in repo.data_directories.iter().filter_map(|(_, v)| v) {
            container.terminate_logs();
        }
        repo.logs_multiplexers.terminate().await;
    }

    #[cfg(test)]
    pub(crate) async fn default() -> Self {
        let budget = BudgetManager::new(crate::constants::LEGACY_DEFAULT_MAXIMUM_CACHED_LOGS_BYTES);
        LogsRepository { inner: LogsRepositoryState::new(budget, &Default::default()).await }
    }
}

pub struct LogsRepositoryState {
    data_directories: trie::Trie<String, ComponentDiagnostics>,
    inspect_node: inspect::Node,

    /// A reference to the budget manager, kept to be passed to containers.
    logs_budget: BudgetManager,
    /// The current global interest in logs, as defined by the last client to send us selectors.
    logs_interest: Vec<LogInterestSelector>,
    /// BatchIterators for logs need to be made aware of new components starting and their logs.
    logs_multiplexers: MultiplexerBroker,

    /// Interest registrations that we have received through fuchsia.logger.Log/ListWithSelectors
    /// or through fuchsia.logger.LogSettings/RegisterInterest.
    interest_registrations: BTreeMap<usize, Vec<LogInterestSelector>>,
}

impl LogsRepositoryState {
    async fn new(logs_budget: BudgetManager, parent: &fuchsia_inspect::Node) -> RwLock<Self> {
        RwLock::new(Self {
            inspect_node: parent.create_child("sources"),
            data_directories: trie::Trie::new(),
            logs_budget,
            logs_interest: vec![],
            logs_multiplexers: MultiplexerBroker::new(),
            interest_registrations: BTreeMap::new(),
        })
    }

    /// Returns a container for logs artifacts, constructing one and adding it to the trie if
    /// necessary.
    pub async fn get_log_container(
        &mut self,
        identity: ComponentIdentity,
    ) -> Arc<LogsArtifactsContainer> {
        let trie_key: Vec<_> = identity.unique_key().into();

        // we use a macro instead of a closure to avoid lifetime issues
        macro_rules! insert_component {
            () => {{
                let (to_insert, logs) = ComponentDiagnostics::new_with_logs(
                    Arc::new(identity),
                    &self.inspect_node,
                    &self.logs_budget,
                    &self.logs_interest,
                    &mut self.logs_multiplexers,
                )
                .await;
                self.data_directories.set(trie_key, to_insert);
                logs
            }};
        }

        match self.data_directories.get_mut(&trie_key) {
            None => insert_component!(),
            Some(existing) => {
                existing
                    .logs(&self.logs_budget, &self.logs_interest, &mut self.logs_multiplexers)
                    .await
            }
        }
    }

    async fn update_logs_interest(
        &mut self,
        connection_id: usize,
        selectors: Vec<LogInterestSelector>,
    ) {
        let previous_selectors =
            self.interest_registrations.insert(connection_id, selectors).unwrap_or_default();
        // unwrap safe, we just inserted.
        let new_selectors = self.interest_registrations.get(&connection_id).unwrap();
        for (_, dir) in self.data_directories.iter() {
            if let Some(dir) = dir {
                if let Some(logs) = &dir.logs {
                    logs.update_interest(new_selectors, &previous_selectors).await;
                }
            }
        }
    }

    pub async fn finish_interest_connection(&mut self, connection_id: usize) {
        let selectors = self.interest_registrations.remove(&connection_id);
        if let Some(selectors) = selectors {
            for (_, dir) in self.data_directories.iter() {
                if let Some(dir) = dir {
                    if let Some(logs) = &dir.logs {
                        logs.reset_interest(&selectors).await;
                    }
                }
            }
        }
    }

    pub fn remove(&mut self, identity: &ComponentIdentity) {
        self.data_directories.remove(&identity.unique_key());
    }
}

type LiveIteratorsMap = HashMap<usize, (StreamMode, MultiplexerHandle<Arc<LogsData>>)>;

/// Ensures that BatchIterators get access to logs from newly started components.
pub struct MultiplexerBroker {
    live_iterators: Arc<Mutex<LiveIteratorsMap>>,
    cleanup_sender: mpsc::UnboundedSender<usize>,
    _live_iterators_cleanup_task: fasync::Task<()>,
}

impl MultiplexerBroker {
    fn new() -> Self {
        let (cleanup_sender, mut receiver) = mpsc::unbounded();
        let live_iterators = Arc::new(Mutex::new(HashMap::new()));
        let live_iterators_clone = live_iterators.clone();
        Self {
            live_iterators,
            cleanup_sender,
            _live_iterators_cleanup_task: fasync::Task::spawn(async move {
                while let Some(id) = receiver.next().await {
                    live_iterators_clone.lock().await.remove(&id);
                }
            }),
        }
    }

    fn cleanup_sender(&self) -> mpsc::UnboundedSender<usize> {
        self.cleanup_sender.clone()
    }

    /// A new BatchIterator has been created and must be notified when future log containers are
    /// created.
    async fn add(&mut self, mode: StreamMode, recipient: MultiplexerHandle<Arc<LogsData>>) {
        match mode {
            // snapshot streams only want to know about what's currently available
            StreamMode::Snapshot => recipient.close(),
            StreamMode::SnapshotThenSubscribe | StreamMode::Subscribe => {
                self.live_iterators
                    .lock()
                    .await
                    .insert(recipient.multiplexer_id(), (mode, recipient));
            }
        }
    }

    /// Notify existing BatchIterators of a new logs container so they can include its messages
    /// in their results.
    pub async fn send(&mut self, container: &Arc<LogsArtifactsContainer>) {
        self.live_iterators.lock().await.retain(|_, (mode, recipient)| {
            recipient.send(
                container.identity.relative_moniker.clone(),
                container.cursor(*mode, recipient.parent_trace_id()),
            )
        });
    }

    /// Notify all multiplexers to terminate their streams once sub streams have terminated.
    async fn terminate(&mut self) {
        for (_, (_, recipient)) in self.live_iterators.lock().await.drain() {
            recipient.close();
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{events::types::ComponentIdentifier, logs::stored_message::StoredMessage},
        diagnostics_log_encoding::{
            encode::Encoder, Argument, Record, Severity as StreamSeverity, Value,
        },
        selectors::{self, FastError},
        std::{io::Cursor, time::Duration},
    };

    #[fuchsia::test]
    async fn data_repo_filters_logs_by_selectors() {
        let repo = LogsRepository::default().await;
        let foo_container = repo
            .get_log_container(ComponentIdentity::from_identifier_and_url(
                ComponentIdentifier::parse_from_moniker("./foo").unwrap(),
                "fuchsia-pkg://foo",
            ))
            .await;
        let bar_container = repo
            .get_log_container(ComponentIdentity::from_identifier_and_url(
                ComponentIdentifier::parse_from_moniker("./bar").unwrap(),
                "fuchsia-pkg://bar",
            ))
            .await;

        foo_container.ingest_message(make_message("a", 1)).await;
        bar_container.ingest_message(make_message("b", 2)).await;
        foo_container.ingest_message(make_message("c", 3)).await;

        let stream = repo.logs_cursor(StreamMode::Snapshot, None, ftrace::Id::random()).await;

        let results =
            stream.map(|value| value.msg().unwrap().to_string()).collect::<Vec<_>>().await;
        assert_eq!(results, vec!["a".to_string(), "b".to_string(), "c".to_string()]);

        let filtered_stream = repo
            .logs_cursor(
                StreamMode::Snapshot,
                Some(vec![selectors::parse_selector::<FastError>("foo:root").unwrap()]),
                ftrace::Id::random(),
            )
            .await;

        let results =
            filtered_stream.map(|value| value.msg().unwrap().to_string()).collect::<Vec<_>>().await;
        assert_eq!(results, vec!["a".to_string(), "c".to_string()]);
    }

    #[fuchsia::test]
    async fn multiplexer_broker_cleanup() {
        let repo = LogsRepository::default().await;
        let stream =
            repo.logs_cursor(StreamMode::SnapshotThenSubscribe, None, ftrace::Id::random()).await;

        assert_eq!(repo.inner.read().await.logs_multiplexers.live_iterators.lock().await.len(), 1);

        // When the multiplexer goes away it must be forgotten by the broker.
        drop(stream);
        loop {
            fasync::Timer::new(Duration::from_millis(100)).await;
            if repo.inner.read().await.logs_multiplexers.live_iterators.lock().await.len() == 0 {
                break;
            }
        }
    }

    fn make_message(msg: &str, timestamp: i64) -> StoredMessage {
        let record = Record {
            timestamp,
            severity: StreamSeverity::Debug,
            arguments: vec![
                Argument { name: "pid".to_string(), value: Value::UnsignedInt(1) },
                Argument { name: "tid".to_string(), value: Value::UnsignedInt(2) },
                Argument { name: "message".to_string(), value: Value::Text(msg.to_string()) },
            ],
        };
        let mut buffer = Cursor::new(vec![0u8; 1024]);
        let mut encoder = Encoder::new(&mut buffer);
        encoder.write_record(&record).unwrap();
        let encoded = &buffer.get_ref()[..buffer.position() as usize];
        StoredMessage::structured(encoded, Default::default()).unwrap()
    }
}
