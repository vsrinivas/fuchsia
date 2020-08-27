// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use super::{buffer::Accounted, message::Message};
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_logger::{
    LogFilterOptions, LogListenerSafeMarker, LogListenerSafeProxy, LogMessage,
};
use log::error;
use thiserror::Error;

mod asbestos;
mod filter;
pub mod pool;

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
        Ok(Self {
            status: Status::Fine,
            listener: log_listener
                .into_proxy()
                .map_err(|source| ListenerError::CreatingListenerProxy { source })?,
            filter: MessageFilter::new(options)?,
        })
    }

    /// Returns whether this listener should continue receiving messages.
    pub fn is_healthy(&self) -> bool {
        self.status == Status::Fine
    }

    /// Send all messages currently in the provided buffer to this listener. Attempts to batch up
    /// to the message size limit. Returns early if the listener appears to be unhealthy.
    pub async fn backfill<'a>(&mut self, messages: impl Iterator<Item = &'a Message>) {
        let mut messages: Vec<_> = messages.collect();
        messages.sort_by_key(|m| m.0.metadata.timestamp);
        let mut batch_size = 0;
        let mut filtered_batch = vec![];
        for msg in messages {
            let size = msg.bytes_used();
            if self.filter.should_send(msg) {
                if batch_size + size > fidl_fuchsia_logger::MAX_LOG_MANY_SIZE_BYTES as usize {
                    self.send_filtered_logs(&mut filtered_batch).await;
                    if !self.is_healthy() {
                        return;
                    }
                    filtered_batch.clear();
                    batch_size = 0;
                }
                batch_size += size;
                filtered_batch.push(msg.for_listener());
            }
        }

        if !filtered_batch.is_empty() {
            self.send_filtered_logs(&mut filtered_batch).await;
        }
    }

    /// Send a batch of pre-filtered log messages to this listener.
    pub async fn send_filtered_logs(&mut self, log_messages: &mut Vec<LogMessage>) {
        self.check_result({
            let mut log_messages = log_messages.iter_mut();
            let fut = self.listener.log_many(&mut log_messages);
            fut.await
        });
    }

    /// Send a single log message if it should be sent according to this listener's filter settings.
    pub async fn send_log(&mut self, log_message: Message) {
        if self.filter.should_send(&log_message) {
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

    /// Notify the listener that `DumpLogs` has completed.
    pub fn done(self) {
        self.listener.done().ok();
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
