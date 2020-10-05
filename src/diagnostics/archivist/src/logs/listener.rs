// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use super::{buffer::Accounted, message::Message};
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_logger::{
    LogFilterOptions, LogListenerSafeMarker, LogListenerSafeProxy, LogMessage,
};
use fuchsia_async::Task;
use futures::prelude::*;
use log::{debug, error, trace};
use std::{sync::Arc, task::Poll};
use thiserror::Error;

mod asbestos;
mod filter;

pub use asbestos::pretend_scary_listener_is_safe;
use filter::MessageFilter;

/// An individual log listener. Wraps the FIDL type `LogListenerProxy` in filtering options provided
/// when connecting.
pub struct Listener {
    listener: LogListenerSafeProxy,
    filter: MessageFilter,
    status: Status,
}

#[derive(PartialEq)]
enum Status {
    Fine,
    Stale,
}

impl Listener {
    /// Create a new `Listener`. Fails if `client` can't be converted into a `LogListenerProxy` or
    /// if `LogFilterOptions` are invalid.
    pub fn new(
        log_listener: ClientEnd<LogListenerSafeMarker>,
        options: Option<Box<LogFilterOptions>>,
    ) -> Result<Self, ListenerError> {
        debug!("New listener with options {:?}", &options);
        Ok(Self {
            status: Status::Fine,
            listener: log_listener
                .into_proxy()
                .map_err(|source| ListenerError::CreatingListenerProxy { source })?,
            filter: MessageFilter::new(options)?,
        })
    }

    pub fn spawn(
        self,
        logs: impl Stream<Item = Arc<Message>> + Send + Unpin + 'static,
        call_done: bool,
    ) -> Task<()> {
        Task::spawn(async move { self.run(logs, call_done).await })
    }

    /// Send messages to the listener. First eagerly collects any backlog and sends it out in
    /// batches before waiting for wakeups.
    async fn run(mut self, mut logs: impl Stream<Item = Arc<Message>> + Unpin, call_done: bool) {
        debug!("Backfilling from cursor until pending.");
        let mut backlog = vec![];
        futures::future::poll_fn(|cx| {
            loop {
                match logs.poll_next_unpin(cx) {
                    Poll::Ready(Some(next)) => backlog.push(next),
                    _ => break,
                }
            }

            Poll::Ready(())
        })
        .await;

        self.backfill(backlog).await;
        debug!("Done backfilling.");

        pin_utils::pin_mut!(logs);
        while let Some(message) = logs.next().await {
            self.send_log(&message).await;
        }

        if call_done {
            self.listener.done().ok();
        }
        debug!("Listener exiting.");
    }

    /// Returns whether this listener should continue receiving messages.
    fn is_healthy(&self) -> bool {
        self.status == Status::Fine
    }

    /// Send all messages currently in the provided buffer to this listener. Attempts to batch up
    /// to the message size limit. Returns early if the listener appears to be unhealthy.
    async fn backfill<'a>(&mut self, mut messages: Vec<Arc<Message>>) {
        messages.sort_by_key(|m| m.metadata.timestamp);
        let mut batch_size = 0;
        let mut filtered_batch = vec![];
        for msg in messages {
            let size = msg.bytes_used();
            if self.filter.should_send(&msg) {
                if batch_size + size > fidl_fuchsia_logger::MAX_LOG_MANY_SIZE_BYTES as usize {
                    self.send_filtered_logs(&mut filtered_batch).await;
                    if !self.is_healthy() {
                        return;
                    }
                    filtered_batch.clear();
                    batch_size = 0;
                }
                batch_size += size;
                trace!("Batching {:?}.", msg.id);
                filtered_batch.push(msg.for_listener());
            }
        }

        if !filtered_batch.is_empty() {
            self.send_filtered_logs(&mut filtered_batch).await;
        }
    }

    /// Send a batch of pre-filtered log messages to this listener.
    async fn send_filtered_logs(&mut self, log_messages: &mut Vec<LogMessage>) {
        trace!("Flushing batch.");
        self.check_result({
            let mut log_messages = log_messages.iter_mut();
            let fut = self.listener.log_many(&mut log_messages);
            fut.await
        });
    }

    /// Send a single log message if it should be sent according to this listener's filter settings.
    async fn send_log(&mut self, log_message: &Message) {
        if self.filter.should_send(log_message) {
            trace!("Sending {:?}.", log_message.id);
            let mut to_send = log_message.for_listener();
            self.check_result(self.listener.log(&mut to_send).await);
        }
    }

    /// Consume the result of sending logs to this listener, potentially marking it stale.
    fn check_result(&mut self, result: Result<(), fidl::Error>) {
        if let Err(e) = result {
            if e.is_closed() {
                self.status = Status::Stale;
            } else {
                error!("Error calling listener: {:?}", e);
            }
        }
    }
}

#[derive(Debug, Error)]
pub enum ListenerError {
    #[error("{count} tags provided, max {}", fidl_fuchsia_logger::MAX_TAGS)]
    TooManyTags { count: usize },

    #[error("tag at index {index} is too long, max {}", fidl_fuchsia_logger::MAX_TAG_LEN_BYTES)]
    TagTooLong { index: usize },

    #[error("couldn't create LogListenerProxy")]
    CreatingListenerProxy { source: fidl::Error },

    #[error("couldn't decode value: {source}")]
    Decode {
        #[from]
        source: super::error::StreamError,
    },
}
