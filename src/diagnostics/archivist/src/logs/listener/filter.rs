// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use super::ListenerError;
use fidl_fuchsia_logger::{LogFilterOptions, LogLevelFilter, LogMessage};
use std::collections::HashSet;

/// Controls whether messages are seen by a given `Listener`. Created from
/// `fidl_fuchsia_logger::LogFilterOptions`.
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
    pub fn should_send(&self, log_message: &LogMessage) -> bool {
        let reject_pid = self.pid.map(|p| p != log_message.pid).unwrap_or(false);
        let reject_tid = self.tid.map(|t| t != log_message.tid).unwrap_or(false);
        let reject_severity = self.min_severity.map(|m| m > log_message.severity).unwrap_or(false);
        let reject_tags =
            !self.tags.is_empty() && !log_message.tags.iter().any(|t| self.tags.contains(t));

        !(reject_pid || reject_tid || reject_severity || reject_tags)
    }
}
