// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Forward log messages from the system log to individual listeners.

use fidl::endpoints::ClientEnd;
use futures::future::join_all;
use std::collections::HashSet;
use thiserror::Error;

use fidl_fuchsia_logger::{
    LogFilterOptions, LogLevelFilter, LogListenerMarker, LogListenerProxy, LogMessage,
};

/// A pool of log listeners, each of which recieves a stream of log messages from the diagnostics
/// service. Listners are dropped from the pool when they are no longer connected.
pub(super) struct Pool {
    listeners: Vec<Listener>,
}

impl Pool {
    /// Creates a new empty `Pool`.
    pub fn new() -> Self {
        Self { listeners: Vec::new() }
    }

    /// Sends the provided log message to all listeners in the pool. The returned future completes
    /// when all listeners have acknowledged receipt of the message.
    pub async fn run(&mut self, log_message: &LogMessage) {
        let mut pending = Vec::new();

        for listener in &mut self.listeners {
            let mut log_message = log_message.clone();
            pending.push(async move {
                let fut = listener.send_log(&mut log_message);
                fut.await
            });
        }

        join_all(pending).await;
        self.listeners.retain(|l| l.status == Status::Fine);
    }

    /// Add a new listener to the pool. Listeners in the pool will recieve `LogMessage`s until they
    /// close their channel.
    pub fn add(&mut self, listener: Listener) {
        self.listeners.push(listener);
    }
}

/// An individual log listener. Wraps the FIDL type `LogListenerProxy` in filtering options provided
/// when connecting.
pub(super) struct Listener {
    status: Status,
    proxy: LogListenerProxy,
    filter: MessageFilter,
}

#[derive(PartialEq)]
enum Status {
    Fine,
    Stale,
}

impl Listener {
    pub fn new(
        client: ClientEnd<LogListenerMarker>,
        options: Option<Box<LogFilterOptions>>,
    ) -> Result<Self, ListenerError> {
        let filter =
            if let Some(o) = options { MessageFilter::new(o)? } else { Default::default() };
        let proxy = client
            .into_proxy()
            .map_err(|source| ListenerError::CreatingListenerProxy { source })?;

        Ok(Self { filter, proxy, status: Status::Fine })
    }

    /// Send all messages currently in the provided buffer to this listener. Attempts to batch up
    /// to the message size limit. Returns early if the listener appears to be unhealthy.
    ///
    /// The `status` field of the listener may change in this method, a `Pool` is expected to drop
    /// stale listeners.
    pub async fn backfill(&mut self, existing: Vec<(LogMessage, usize)>) {
        let mut batch_len = 0;
        let mut batch = vec![];

        for (msg, msg_len) in existing {
            if self.filter.should_send(&msg) {
                if batch_len + msg_len > fidl_fuchsia_logger::MAX_LOG_MANY_SIZE_BYTES as usize {
                    let to_send = std::mem::replace(&mut batch, Vec::new());
                    self.send_filtered_logs(to_send).await;
                    if self.status != Status::Fine {
                        return;
                    }

                    batch.clear();
                    batch_len = 0;
                }

                batch.push(msg.clone());
                batch_len += msg_len;
            }
        }

        if batch.len() > 0 {
            self.send_filtered_logs(batch).await;
            if self.status != Status::Fine {
                return;
            }
        }
    }

    /// Send a batch of pre-filtered log messages to this listener.
    async fn send_filtered_logs(&mut self, mut messages: Vec<LogMessage>) {
        self.check_status({
            let mut msgs = messages.iter_mut();
            self.proxy.log_many(&mut msgs)
        });
    }

    /// Send a single log message if it should be sent according to this listener's filter settings.
    pub async fn send_log(&mut self, log_message: &mut LogMessage) {
        if self.filter.should_send(log_message) {
            let res = self.proxy.log(log_message);
            self.check_status(res);
        }
    }

    /// Consume the result of sending logs to this listener, potentially marking it stale.
    fn check_status(&mut self, result: Result<(), fidl::Error>) {
        if let Err(e) = result {
            if e.is_closed() {
                self.status = Status::Stale;
            } else {
                eprintln!("Error calling listener: {:?}", e);
            }
        }
    }

    /// When we're done with a listener, we inform it that no more messages are coming.
    ///
    /// Should only be observed by listeners who request `DumpLogs`.
    pub fn done(self) {
        self.proxy.done().ok();
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
}

struct MessageFilter {
    /// Only send messages of greater or equal severity to this value.
    min_severity: Option<i32>,

    /// Only send messages that purport to come from this PID.
    pid: Option<u64>,

    /// Only send messages that purport to come from this TID.
    tid: Option<u64>,

    /// Only send messages whose tags match one or more of those provided.
    tags: HashSet<String>,
}

impl Default for MessageFilter {
    fn default() -> Self {
        Self { min_severity: None, pid: None, tid: None, tags: HashSet::new() }
    }
}

impl MessageFilter {
    fn new(mut options: Box<LogFilterOptions>) -> Result<Self, ListenerError> {
        let mut this = Self::default();

        this.tags = options.tags.drain(..).collect();

        let count = this.tags.len();
        if count > fidl_fuchsia_logger::MAX_TAGS as usize {
            return Err(ListenerError::TooManyTags { count });
        }

        for (index, tag) in this.tags.iter().enumerate() {
            if tag.len() > fidl_fuchsia_logger::MAX_TAG_LEN_BYTES as usize {
                return Err(ListenerError::TagTooLong { index });
            }
        }

        if options.filter_by_pid {
            this.pid = Some(options.pid)
        }
        if options.filter_by_tid {
            this.tid = Some(options.tid)
        }

        if options.verbosity > 0 {
            this.min_severity = Some(-(options.verbosity as i32))
        } else if options.min_severity != LogLevelFilter::None {
            this.min_severity = Some(options.min_severity as i32)
        }

        Ok(this)
    }

    /// This filter defaults to open, allowing messages through. If multiple portions of the filter
    /// are specified, they are additive, only allowing messages through that pass all criteria.
    fn should_send(&self, log_message: &LogMessage) -> bool {
        let reject_pid = self.pid.map(|p| p != log_message.pid).unwrap_or(false);
        let reject_tid = self.tid.map(|t| t != log_message.tid).unwrap_or(false);
        let reject_severity = self.min_severity.map(|m| m > log_message.severity).unwrap_or(false);
        let reject_tags =
            !self.tags.is_empty() && !log_message.tags.iter().any(|t| self.tags.contains(t));

        !(reject_pid || reject_tid || reject_severity || reject_tags)
    }
}
