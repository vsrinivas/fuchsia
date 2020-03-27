// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use fidl::endpoints::ClientEnd;
use fidl_fuchsia_logger::{
    LogFilterOptions, LogLevelFilter, LogListenerMarker, LogListenerProxy, LogMessage,
};
use std::collections::HashSet;
use thiserror::Error;

/// An individual log listener. Wraps the FIDL type `LogListenerProxy` in filtering options provided
/// when connecting.
pub(super) struct Listener {
    listener: LogListenerProxy,
    filter: MessageFilter,
    status: Status,
}

#[derive(PartialEq)]
enum Status {
    Fine,
    Stale,
}

impl Listener {
    pub fn new(
        log_listener: ClientEnd<LogListenerMarker>,
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
    pub fn backfill<'a>(&mut self, messages: impl Iterator<Item = &'a (LogMessage, usize)>) {
        let mut batch_size = 0;
        let mut filtered_batch = vec![];
        for (msg, size) in messages {
            if self.filter.should_send(msg) {
                if batch_size + size > fidl_fuchsia_logger::MAX_LOG_MANY_SIZE_BYTES as usize {
                    self.send_filtered_logs(&mut filtered_batch);
                    if !self.is_healthy() {
                        return;
                    }
                    filtered_batch.clear();
                    batch_size = 0;
                }
                batch_size += size;
                filtered_batch.push(msg.clone());
            }
        }

        if !filtered_batch.is_empty() {
            self.send_filtered_logs(&mut filtered_batch);
        }
    }

    /// Send a batch of pre-filtered log messages to this listener.
    pub fn send_filtered_logs(&mut self, log_messages: &mut Vec<LogMessage>) {
        self.check_result(self.listener.log_many(&mut log_messages.iter_mut()));
    }

    /// Send a single log message if it should be sent according to this listener's filter settings.
    pub fn send_log(&mut self, log_message: &mut LogMessage) {
        if self.filter.should_send(log_message) {
            self.check_result(self.listener.log(log_message));
        }
    }

    /// Consume the result of sending logs to this listener, potentially marking it stale.
    fn check_result(&mut self, result: Result<(), fidl::Error>) {
        if let Err(e) = result {
            if e.is_closed() {
                self.status = Status::Stale;
            } else {
                eprintln!("Error calling listener: {:?}", e);
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
}

pub(super) struct MessageFilter {
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
    /// Constructs a new `MessageFilter` from the filter options provided to the methods
    /// `fuchsia.logger.Log.{Listen,DumpLogs}`.
    pub fn new(options: Option<Box<LogFilterOptions>>) -> Result<Self, ListenerError> {
        let mut this = Self::default();

        if let Some(mut options) = options {
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
