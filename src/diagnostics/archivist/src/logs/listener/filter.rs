// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use super::ListenerError;
use diagnostics_data::{LegacySeverity, LogsData};
use diagnostics_message::fx_log_severity_t;
use fidl_fuchsia_logger::{LogFilterOptions, LogLevelFilter};
use std::{collections::HashSet, convert::TryFrom};

/// Controls whether messages are seen by a given `Listener`. Created from
/// `fidl_fuchsia_logger::LogFilterOptions`.
#[derive(Default)]
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
                this.min_severity = Some(LegacySeverity::try_from(raw_level)?);
            } else if options.min_severity != LogLevelFilter::None {
                this.min_severity = Some(LegacySeverity::try_from(options.min_severity as i32)?);
            }
        }

        Ok(this)
    }

    /// This filter defaults to open, allowing messages through. If multiple portions of the filter
    /// are specified, they are additive, only allowing messages through that pass all criteria.
    pub fn should_send(&self, log_message: &LogsData) -> bool {
        let reject_pid = self.pid.map(|p| log_message.pid() != Some(p)).unwrap_or(false);
        let reject_tid = self.tid.map(|t| log_message.tid() != Some(t)).unwrap_or(false);
        let reject_severity = self
            .min_severity
            .map(|m| {
                fx_log_severity_t::from(m) > fx_log_severity_t::from(log_message.legacy_severity())
            })
            .unwrap_or(false);
        let reject_tags = if self.tags.is_empty() {
            false
        } else if log_message.tags().map(|t| t.is_empty()).unwrap_or(true) {
            !self.tags.contains(log_message.component_name())
        } else {
            !log_message
                .tags()
                .map(|tags| {
                    tags.iter().any(|tag| self.tags.contains(tag) || self.include_tag_prefix(tag))
                })
                .unwrap_or(false)
        };

        !(reject_pid || reject_tid || reject_severity || reject_tags)
    }

    // Rust uses tags of the form "<foo>::<bar>" so if we have a filter for "<foo>" we should
    // include messages that have "<foo>" as a prefix.
    fn include_tag_prefix(&self, tag: &str) -> bool {
        if tag.contains("::") {
            self.tags.iter().any(|t| {
                tag.len() > t.len() + 2 && &tag[t.len()..t.len() + 2] == "::" && tag.starts_with(t)
            })
        } else {
            false
        }
    }
}

#[cfg(test)]
mod tests {
    use diagnostics_data::Severity;

    use super::*;
    use crate::{events::types::ComponentIdentifier, identity::ComponentIdentity};

    fn test_message_with_tag(tag: Option<&str>) -> LogsData {
        let identity = ComponentIdentity::from_identifier_and_url(
            ComponentIdentifier::Legacy {
                moniker: vec!["bogus", "specious-at-best.cmx"].into(),
                instance_id: "0".into(),
            },
            "fuchsia-pkg://not-a-package",
        );
        let mut builder = diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
            timestamp_nanos: fuchsia_zircon::Time::from_nanos(1).into(),
            component_url: Some(identity.url.clone()),
            moniker: identity.to_string(),
            severity: Severity::Info,
        });
        if let Some(tag) = tag {
            builder = builder.add_tag(tag);
        }
        builder.build()
    }

    fn test_message() -> LogsData {
        test_message_with_tag(None)
    }

    #[fuchsia::test]
    fn should_send_verbose() {
        let mut message = test_message();
        let mut filter = MessageFilter {
            min_severity: Some(LegacySeverity::Verbose(15)),
            ..MessageFilter::default()
        };
        for verbosity in 1..15 {
            message.set_legacy_verbosity(verbosity);
            assert!(filter.should_send(&message));
        }

        filter.min_severity = Some(LegacySeverity::Debug);
        message.set_legacy_verbosity(1);
        assert!(filter.should_send(&message));
    }

    #[fuchsia::test]
    fn should_reject_verbose() {
        let mut message = test_message();
        let mut filter = MessageFilter {
            min_severity: Some(LegacySeverity::Verbose(1)),
            ..MessageFilter::default()
        };

        for verbosity in 2..15 {
            message.set_legacy_verbosity(verbosity);
            assert!(!filter.should_send(&message));
        }

        filter.min_severity = Some(LegacySeverity::Info);
        message.set_legacy_verbosity(1);
        assert!(!filter.should_send(&message));
    }

    #[fuchsia::test]
    fn should_send_info() {
        let mut message = test_message();
        let mut filter =
            MessageFilter { min_severity: Some(LegacySeverity::Info), ..MessageFilter::default() };

        message.metadata.severity = Severity::Info;
        assert!(filter.should_send(&message));

        filter.min_severity = Some(LegacySeverity::Debug);
        message.metadata.severity = Severity::Info;
        assert!(filter.should_send(&message));
    }

    #[fuchsia::test]
    fn should_reject_info() {
        let mut message = test_message();
        let filter =
            MessageFilter { min_severity: Some(LegacySeverity::Warn), ..MessageFilter::default() };

        message.metadata.severity = Severity::Info;
        assert!(!filter.should_send(&message));
    }

    #[fuchsia::test]
    fn should_send_warn() {
        let mut message = test_message();
        let mut filter =
            MessageFilter { min_severity: Some(LegacySeverity::Warn), ..MessageFilter::default() };

        message.metadata.severity = Severity::Warn;
        assert!(filter.should_send(&message));

        filter.min_severity = Some(LegacySeverity::Info);
        message.metadata.severity = Severity::Warn;
        assert!(filter.should_send(&message));
    }

    #[fuchsia::test]
    fn should_reject_warn() {
        let mut message = test_message();
        let filter =
            MessageFilter { min_severity: Some(LegacySeverity::Error), ..MessageFilter::default() };

        message.metadata.severity = Severity::Warn;
        assert!(!filter.should_send(&message));
    }

    #[fuchsia::test]
    fn should_send_error() {
        let mut message = test_message();
        let mut filter =
            MessageFilter { min_severity: Some(LegacySeverity::Error), ..MessageFilter::default() };

        message.metadata.severity = Severity::Error;
        assert!(filter.should_send(&message));

        filter.min_severity = Some(LegacySeverity::Warn);
        message.metadata.severity = Severity::Error;
        assert!(filter.should_send(&message));
    }

    #[fuchsia::test]
    fn should_reject_error() {
        let mut message = test_message();
        let filter =
            MessageFilter { min_severity: Some(LegacySeverity::Fatal), ..MessageFilter::default() };

        message.metadata.severity = Severity::Error;
        assert!(!filter.should_send(&message));
    }

    #[fuchsia::test]
    fn should_send_debug() {
        let mut message = test_message();
        let mut filter =
            MessageFilter { min_severity: Some(LegacySeverity::Debug), ..MessageFilter::default() };

        message.metadata.severity = Severity::Debug;
        assert!(filter.should_send(&message));

        filter.min_severity = Some(LegacySeverity::Trace);
        message.metadata.severity = Severity::Debug;
        assert!(filter.should_send(&message));
    }

    #[fuchsia::test]
    fn should_reject_debug() {
        let mut message = test_message();
        let filter =
            MessageFilter { min_severity: Some(LegacySeverity::Info), ..MessageFilter::default() };

        message.metadata.severity = Severity::Debug;
        assert!(!filter.should_send(&message));
    }

    #[fuchsia::test]
    fn should_send_trace() {
        let mut message = test_message();
        let filter =
            MessageFilter { min_severity: Some(LegacySeverity::Trace), ..MessageFilter::default() };

        message.metadata.severity = Severity::Trace;
        assert!(filter.should_send(&message));
    }

    #[fuchsia::test]
    fn should_reject_trace() {
        let mut message = test_message();
        let filter =
            MessageFilter { min_severity: Some(LegacySeverity::Debug), ..MessageFilter::default() };

        message.metadata.severity = Severity::Trace;
        assert!(!filter.should_send(&message));
    }

    #[fuchsia::test]
    fn should_send_attributed_tag() {
        let message = test_message();
        let filter = MessageFilter {
            tags: vec!["specious-at-best.cmx".to_string()].into_iter().collect(),
            ..MessageFilter::default()
        };

        assert!(filter.should_send(&message), "the filter should have sent {:#?}", message);
    }

    #[fuchsia::test]
    fn should_send_prefix_tag() {
        let message = test_message_with_tag(Some("foo::bar::baz"));

        let filter = MessageFilter {
            tags: vec!["foo".to_string()].into_iter().collect(),
            ..MessageFilter::default()
        };

        assert!(filter.should_send(&message), "the filter should have sent {:#?}", message);

        let message2 = test_message_with_tag(Some("foobar"));
        assert!(!filter.should_send(&message2), "the filter should not have sent {:#?}", message2);

        let filter = MessageFilter {
            tags: vec!["foo::bar".to_string()].into_iter().collect(),
            ..MessageFilter::default()
        };

        assert!(filter.should_send(&message), "the filter should have sent {:#?}", message);

        let filter = MessageFilter {
            tags: vec!["foo:ba".to_string()].into_iter().collect(),
            ..MessageFilter::default()
        };

        assert!(!filter.should_send(&message), "the filter should not have sent {:#?}", message);
    }
}
