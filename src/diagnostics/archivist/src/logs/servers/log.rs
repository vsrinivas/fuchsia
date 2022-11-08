// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::logs::{error::LogsError, listener::Listener, repository::LogsRepository};
use async_lock::Mutex;
use fidl::endpoints::DiscoverableProtocolMarker;
use fidl_fuchsia_diagnostics::StreamMode;
use fidl_fuchsia_logger as flogger;
use fuchsia_async as fasync;
use fuchsia_trace as ftrace;
use futures::{channel::mpsc, StreamExt};
use std::sync::{Arc, Mutex as SyncMutex};
use tracing::warn;

pub struct LogServer {
    /// The repository holding the logs.
    logs_repo: Arc<LogsRepository>,

    /// Sender which is used to close the stream of Log connections after ingestion of logsink
    /// completes.
    task_sender: Arc<SyncMutex<mpsc::UnboundedSender<fasync::Task<()>>>>,

    /// Task draining the receiver for the `task_sender`s.
    drain_listeners_task: Mutex<Option<fasync::Task<()>>>,
}

impl LogServer {
    pub fn new(logs_repo: Arc<LogsRepository>) -> Self {
        let (task_sender, rcv) = mpsc::unbounded();
        Self {
            logs_repo,
            task_sender: Arc::new(SyncMutex::new(task_sender)),
            drain_listeners_task: Mutex::new(Some(fasync::Task::spawn(async move {
                rcv.for_each_concurrent(None, |rx| async move { rx.await }).await;
            }))),
        }
    }

    /// Spawn a task to handle requests from components reading the shared log.
    pub fn spawn(&self, stream: flogger::LogRequestStream) {
        let logs_repo = self.logs_repo.clone();
        let sender = self.task_sender.clone();
        let Ok(task_guard) = self.task_sender.lock() else { return };
        if let Err(e) = task_guard.unbounded_send(fasync::Task::spawn(async move {
            if let Err(e) = Self::handle_requests(logs_repo, stream, sender).await {
                warn!("error handling Log requests: {}", e);
            }
        })) {
            warn!("Couldn't queue listener task: {:?}", e);
        }
    }

    /// Instructs the server to stop accepting new connections.
    pub async fn stop(&self) {
        if let Ok(mut task_guard) = self.task_sender.lock() {
            task_guard.disconnect();
        }
    }

    /// Instructs the server to finish handling all connections and return when they have finished
    /// draining logs.
    pub async fn wait_for_servers_to_complete(&self) {
        match self.drain_listeners_task.lock().await.take() {
            Some(task) => task.await,
            None => unreachable!("The accessor server task is only awaited for once"),
        }
    }

    /// Handle requests to `fuchsia.logger.Log`. All request types read the
    /// whole backlog from memory, `DumpLogs(Safe)` stops listening after that.
    async fn handle_requests(
        logs_repo: Arc<LogsRepository>,
        mut stream: flogger::LogRequestStream,
        sender: Arc<SyncMutex<mpsc::UnboundedSender<fasync::Task<()>>>>,
    ) -> Result<(), LogsError> {
        let connection_id = logs_repo.new_interest_connection();
        while let Some(request) = stream.next().await {
            let request = request.map_err(|source| LogsError::HandlingRequests {
                protocol: flogger::LogMarker::PROTOCOL_NAME,
                source,
            })?;

            let (listener, options, dump_logs, selectors) = match request {
                flogger::LogRequest::ListenSafe { log_listener, options, .. } => {
                    (log_listener, options, false, None)
                }
                flogger::LogRequest::DumpLogsSafe { log_listener, options, .. } => {
                    (log_listener, options, true, None)
                }
                flogger::LogRequest::ListenSafeWithSelectors {
                    log_listener,
                    options,
                    selectors,
                    ..
                } => (log_listener, options, false, Some(selectors)),
            };

            let listener = Listener::new(listener, options)?;
            let mode =
                if dump_logs { StreamMode::Snapshot } else { StreamMode::SnapshotThenSubscribe };
            // NOTE: The LogListener code path isn't instrumented for tracing at the moment.
            let logs = logs_repo.logs_cursor(mode, None, ftrace::Id::random()).await;
            if let Some(s) = selectors {
                logs_repo.update_logs_interest(connection_id, s).await;
            }

            let Ok(sender_guard) = sender.lock() else { continue };
            sender_guard.unbounded_send(listener.spawn(logs, dump_logs)).ok();
        }
        logs_repo.finish_interest_connection(connection_id).await;
        Ok(())
    }
}
