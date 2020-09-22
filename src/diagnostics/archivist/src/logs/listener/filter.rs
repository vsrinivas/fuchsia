// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use super::{
    super::message::{LegacySeverity, Message},
    ListenerError,
};
use fidl_fuchsia_logger::{LogFilterOptions, LogLevelFilter};
use std::{collections::HashSet, convert::TryFrom};

/// Controls whether messages are seen by a given `Listener`. Created from
/// `fidl_fuchsia_logger::LogFilterOptions`.
pub(super) struct MessageFilter {
    /// Only send messages of greater or equal severity to this value.
    min_severity: Option<LegacySeverity>,

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
                // verbosity scale sits in the interstitial space between
                // INFO and DEBUG
                let raw_level: i32 = std::cmp::max(
                    fidl_fuchsia_logger::LogLevelFilter::Debug as i32 + 1,
                    fidl_fuchsia_logger::LogLevelFilter::Info as i32
                        - (options.verbosity as i32
                            * fidl_fuchsia_logger::LOG_VERBOSITY_STEP_SIZE as i32),
                );
                this.min_severity = Some(LegacySeverity::try_from(raw_level as i32)?);
            } else if options.min_severity != LogLevelFilter::None {
                this.min_severity = Some(LegacySeverity::try_from(options.min_severity as i32)?);
            }
        }

        Ok(this)
    }

    /// This filter defaults to open, allowing messages through. If multiple portions of the filter
    /// are specified, they are additive, only allowing messages through that pass all criteria.
    pub fn should_send(&self, log_message: &Message) -> bool {
        let reject_pid = self.pid.map(|p| log_message.pid() != Some(p)).unwrap_or(false);
        let reject_tid = self.tid.map(|t| log_message.tid() != Some(t)).unwrap_or(false);
        let reject_severity = self
            .min_severity
            .map(|m| m.for_listener() > log_message.legacy_severity().for_listener())
            .unwrap_or(false);
        let reject_tags = if self.tags.is_empty() {
            false
        } else if log_message.tags().count() == 0 {
            !self.tags.contains(log_message.component_name())
        } else {
            !log_message.tags().any(|tag| self.tags.contains(tag))
        };

        !(reject_pid || reject_tid || reject_severity || reject_tags)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::logs::message::{LegacySeverity, LogsHierarchy, Severity};
    use fidl_fuchsia_sys_internal::SourceIdentity;

    fn test_message() -> Message {
        let identity = SourceIdentity {
            instance_id: None,
            realm_path: Some(vec!["bogus".to_string()]),
            component_name: Some("specious-at-best.cmx".to_string()),
            component_url: Some("fuchsia-pkg://not-a-package".to_string()),
        };
        Message::new(
            fuchsia_zircon::Time::from_nanos(1),
            Severity::Info,
            1, // size
            0, // dropped logs
            &identity,
            LogsHierarchy::new("root", vec![], vec![]),
        )
    }

    #[test]
    fn should_send_verbose() {
        let mut message = test_message();
        let mut filter = MessageFilter::default();

        filter.min_severity = Some(LegacySeverity::Verbose(15));
        for verbosity in 1..15 {
            message.set_legacy_verbosity(verbosity);
            assert_eq!(filter.should_send(&message), true);
        }

        filter.min_severity = Some(LegacySeverity::Debug);
        message.set_legacy_verbosity(1);
        assert_eq!(filter.should_send(&message), true);
    }

    #[test]
    fn should_reject_verbose() {
        let mut message = test_message();
        let mut filter = MessageFilter::default();

        filter.min_severity = Some(LegacySeverity::Verbose(1));
        for verbosity in 2..15 {
            message.set_legacy_verbosity(verbosity);
            assert_eq!(filter.should_send(&message), false);
        }

        filter.min_severity = Some(LegacySeverity::Info);
        message.set_legacy_verbosity(1);
        assert_eq!(filter.should_send(&message), false);
    }

    #[test]
    fn should_send_info() {
        let mut message = test_message();
        let mut filter = MessageFilter::default();

        filter.min_severity = Some(LegacySeverity::Info);
        message.metadata.severity = Severity::Info;
        assert_eq!(filter.should_send(&message), true);

        filter.min_severity = Some(LegacySeverity::Debug);
        message.metadata.severity = Severity::Info;
        assert_eq!(filter.should_send(&message), true);
    }

    #[test]
    fn should_reject_info() {
        let mut message = test_message();
        let mut filter = MessageFilter::default();

        filter.min_severity = Some(LegacySeverity::Warn);
        message.metadata.severity = Severity::Info;
        assert_eq!(filter.should_send(&message), false);
    }

    #[test]
    fn should_send_warn() {
        let mut message = test_message();
        let mut filter = MessageFilter::default();

        filter.min_severity = Some(LegacySeverity::Warn);
        message.metadata.severity = Severity::Warn;
        assert_eq!(filter.should_send(&message), true);

        filter.min_severity = Some(LegacySeverity::Info);
        message.metadata.severity = Severity::Warn;
        assert_eq!(filter.should_send(&message), true);
    }

    #[test]
    fn should_reject_warn() {
        let mut message = test_message();
        let mut filter = MessageFilter::default();

        filter.min_severity = Some(LegacySeverity::Error);
        message.metadata.severity = Severity::Warn;
        assert_eq!(filter.should_send(&message), false);
    }

    #[test]
    fn should_send_error() {
        let mut message = test_message();
        let mut filter = MessageFilter::default();

        filter.min_severity = Some(LegacySeverity::Error);
        message.metadata.severity = Severity::Error;
        assert_eq!(filter.should_send(&message), true);

        filter.min_severity = Some(LegacySeverity::Warn);
        message.metadata.severity = Severity::Error;
        assert_eq!(filter.should_send(&message), true);
    }

    #[test]
    fn should_reject_error() {
        let mut message = test_message();
        let mut filter = MessageFilter::default();

        filter.min_severity = Some(LegacySeverity::Fatal);
        message.metadata.severity = Severity::Error;
        assert_eq!(filter.should_send(&message), false);
    }

    #[test]
    fn should_send_debug() {
        let mut message = test_message();
        let mut filter = MessageFilter::default();

        filter.min_severity = Some(LegacySeverity::Debug);
        message.metadata.severity = Severity::Debug;
        assert_eq!(filter.should_send(&message), true);

        filter.min_severity = Some(LegacySeverity::Trace);
        message.metadata.severity = Severity::Debug;
        assert_eq!(filter.should_send(&message), true);
    }

    #[test]
    fn should_reject_debug() {
        let mut message = test_message();
        let mut filter = MessageFilter::default();

        filter.min_severity = Some(LegacySeverity::Info);
        message.metadata.severity = Severity::Debug;
        assert_eq!(filter.should_send(&message), false);
    }

    #[test]
    fn should_send_trace() {
        let mut message = test_message();
        let mut filter = MessageFilter::default();

        filter.min_severity = Some(LegacySeverity::Trace);
        message.metadata.severity = Severity::Trace;
        assert_eq!(filter.should_send(&message), true);
    }

    #[test]
    fn should_reject_trace() {
        let mut message = test_message();
        let mut filter = MessageFilter::default();

        filter.min_severity = Some(LegacySeverity::Debug);
        message.metadata.severity = Severity::Trace;
        assert_eq!(filter.should_send(&message), false);
    }

    #[test]
    fn should_send_attributed_tag() {
        let message = test_message();
        let mut filter = MessageFilter::default();

        filter.tags = vec!["specious-at-best.cmx".to_string()].into_iter().collect();
        assert_eq!(filter.should_send(&message), true);
    }
}
