// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::logs::{error::LogsError, repository::LogsRepository};
use fidl::endpoints::DiscoverableProtocolMarker;
use fidl_fuchsia_diagnostics as fdiagnostics;
use fuchsia_async as fasync;
use futures::{channel::mpsc, StreamExt};
use std::sync::Arc;
use tracing::warn;

pub struct LogSettingsServer {
    /// The repository holding the logs.
    logs_repo: Arc<LogsRepository>,

    /// Sender which is used to close the stream of Log connections after ingestion of logsink
    /// completes.
    task_sender: mpsc::UnboundedSender<fasync::Task<()>>,

    /// Task draining the receiver for the `task_sender`s.
    _drain_listeners_task: fasync::Task<()>,
}

impl LogSettingsServer {
    pub fn new(logs_repo: Arc<LogsRepository>) -> Self {
        let (task_sender, rcv) = mpsc::unbounded();
        Self {
            logs_repo,
            task_sender,
            _drain_listeners_task: fasync::Task::spawn(async move {
                rcv.for_each_concurrent(None, |rx| async move { rx.await }).await;
            }),
        }
    }

    /// Spawn a task to handle requests from components reading the shared log.
    pub fn spawn(&self, stream: fdiagnostics::LogSettingsRequestStream) {
        let logs_repo = self.logs_repo.clone();
        if let Err(e) = self.task_sender.unbounded_send(fasync::Task::spawn(async move {
            if let Err(e) = Self::handle_requests(logs_repo, stream).await {
                warn!("error handling Log requests: {}", e);
            }
        })) {
            warn!("Couldn't queue listener task: {:?}", e);
        }
    }

    pub async fn handle_requests(
        logs_repo: Arc<LogsRepository>,
        mut stream: fdiagnostics::LogSettingsRequestStream,
    ) -> Result<(), LogsError> {
        let connection_id = logs_repo.new_interest_connection();
        while let Some(request) = stream.next().await {
            let request = request.map_err(|source| LogsError::HandlingRequests {
                protocol: fdiagnostics::LogSettingsMarker::PROTOCOL_NAME,
                source,
            })?;
            match request {
                fdiagnostics::LogSettingsRequest::RegisterInterest { selectors, .. } => {
                    logs_repo.update_logs_interest(connection_id, selectors).await;
                }
            }
        }
        logs_repo.finish_interest_connection(connection_id).await;

        Ok(())
    }
}
