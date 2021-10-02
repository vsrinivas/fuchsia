// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(target_os = "fuchsia")]

use crate::{Data, Logs, Severity};
use fidl_fuchsia_diagnostics::Severity as StreamSeverity;
use fidl_fuchsia_logger::{LogLevelFilter, LogMessage};
use fuchsia_zircon as zx;
use std::{convert::TryFrom, fmt::Write, os::raw::c_int};
use thiserror::Error;

#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
#[repr(i8)]
pub enum LegacySeverity {
    Trace,
    Debug,
    Verbose(i8),
    Info,
    Warn,
    Error,
    Fatal,
}

impl LegacySeverity {
    /// Splits this legacy value into a severity and an optional verbosity.
    pub fn for_structured(self) -> (Severity, Option<i8>) {
        match self {
            LegacySeverity::Trace => (Severity::Trace, None),
            LegacySeverity::Debug => (Severity::Debug, None),
            LegacySeverity::Info => (Severity::Info, None),
            LegacySeverity::Warn => (Severity::Warn, None),
            LegacySeverity::Error => (Severity::Error, None),
            LegacySeverity::Fatal => (Severity::Fatal, None),
            LegacySeverity::Verbose(v) => (Severity::Debug, Some(v)),
        }
    }
}

impl From<Severity> for LegacySeverity {
    fn from(severity: Severity) -> Self {
        match severity {
            Severity::Trace => Self::Trace,
            Severity::Debug => Self::Debug,
            Severity::Info => Self::Info,
            Severity::Warn => Self::Warn,
            Severity::Error => Self::Error,
            Severity::Fatal => Self::Fatal,
        }
    }
}

impl From<StreamSeverity> for LegacySeverity {
    fn from(fidl_severity: StreamSeverity) -> Self {
        match fidl_severity {
            StreamSeverity::Trace => Self::Trace,
            StreamSeverity::Debug => Self::Debug,
            StreamSeverity::Info => Self::Info,
            StreamSeverity::Warn => Self::Warn,
            StreamSeverity::Error => Self::Error,
            StreamSeverity::Fatal => Self::Fatal,
        }
    }
}

impl From<LegacySeverity> for c_int {
    fn from(severity: LegacySeverity) -> c_int {
        match severity {
            LegacySeverity::Trace => LogLevelFilter::Trace as _,
            LegacySeverity::Debug => LogLevelFilter::Debug as _,
            LegacySeverity::Info => LogLevelFilter::Info as _,
            LegacySeverity::Warn => LogLevelFilter::Warn as _,
            LegacySeverity::Error => LogLevelFilter::Error as _,
            LegacySeverity::Fatal => LogLevelFilter::Fatal as _,
            LegacySeverity::Verbose(v) => (LogLevelFilter::Info as i8 - v) as _,
        }
    }
}

#[derive(Debug, Eq, Error, PartialEq)]
pub enum SeverityError {
    #[error("invalid or corrupt severity received: {provided}")]
    Invalid { provided: c_int },
}

impl TryFrom<c_int> for LegacySeverity {
    type Error = SeverityError;

    fn try_from(raw: c_int) -> Result<LegacySeverity, SeverityError> {
        // Handle legacy/deprecated level filter values.
        if -10 <= raw && raw <= -3 {
            Ok(LegacySeverity::Verbose(-raw as i8))
        } else if raw == -2 {
            // legacy values from trace verbosity
            Ok(LegacySeverity::Trace)
        } else if raw == -1 {
            // legacy value from debug verbosity
            Ok(LegacySeverity::Debug)
        } else if raw == 0 {
            // legacy value for INFO
            Ok(LegacySeverity::Info)
        } else if raw == 1 {
            // legacy value for WARNING
            Ok(LegacySeverity::Warn)
        } else if raw == 2 {
            // legacy value for ERROR
            Ok(LegacySeverity::Error)
        } else if raw == 3 {
            // legacy value for FATAL
            Ok(LegacySeverity::Fatal)
        } else if raw < LogLevelFilter::Info as i32 && raw > LogLevelFilter::Debug as i32 {
            // Verbosity scale exists as incremental steps between INFO & DEBUG
            Ok(LegacySeverity::Verbose(LogLevelFilter::Info as i8 - raw as i8))
        } else if let Some(level) = LogLevelFilter::from_primitive(raw as i8) {
            // Handle current level filter values.
            match level {
                // Match defined severities at their given filter level.
                LogLevelFilter::Trace => Ok(LegacySeverity::Trace),
                LogLevelFilter::Debug => Ok(LegacySeverity::Debug),
                LogLevelFilter::Info => Ok(LegacySeverity::Info),
                LogLevelFilter::Warn => Ok(LegacySeverity::Warn),
                LogLevelFilter::Error => Ok(LegacySeverity::Error),
                LogLevelFilter::Fatal => Ok(LegacySeverity::Fatal),
                _ => Err(SeverityError::Invalid { provided: raw }),
            }
        } else {
            Err(SeverityError::Invalid { provided: raw })
        }
    }
}

/// Convert this `Message` to a FIDL representation suitable for sending to `LogListenerSafe`.
impl Into<LogMessage> for &Data<Logs> {
    fn into(self) -> LogMessage {
        let mut msg = self.msg().unwrap_or("").to_string();

        for property in self.non_legacy_contents() {
            match property {
                other => {
                    write!(&mut msg, " {}", other)
                        .expect("allocations have to fail for this to fail");
                }
            }
        }
        let file = self.metadata.file.as_ref();
        let line = self.metadata.line.as_ref();
        if let (Some(file), Some(line)) = (file, line) {
            msg = format!("[{}({})] {}", file, line, msg);
        }

        let tags = match &self.metadata.tags {
            None => vec![self.component_name().to_string()],
            Some(tags) if tags.is_empty() => vec![self.component_name().to_string()],
            Some(tags) => tags.clone(),
        };

        LogMessage {
            pid: self.pid().unwrap_or(zx::sys::ZX_KOID_INVALID),
            tid: self.tid().unwrap_or(zx::sys::ZX_KOID_INVALID),
            time: self.metadata.timestamp.into(),
            severity: self.legacy_severity().into(),
            dropped_logs: self.dropped_logs().unwrap_or(0) as _,
            tags,
            msg,
        }
    }
}

/// Convert this `Message` to a FIDL representation suitable for sending to `LogListenerSafe`.
impl Into<LogMessage> for Data<Logs> {
    fn into(self) -> LogMessage {
        let mut msg = self.msg().unwrap_or("").to_string();

        for property in self.non_legacy_contents() {
            match property {
                other => {
                    write!(&mut msg, " {}", other)
                        .expect("allocations have to fail for this to fail");
                }
            }
        }
        let file = self.metadata.file.as_ref();
        let line = self.metadata.line.as_ref();
        if let (Some(file), Some(line)) = (file, line) {
            msg = format!("[{}({})] {}", file, line, msg);
        }

        LogMessage {
            pid: self.pid().unwrap_or(zx::sys::ZX_KOID_INVALID),
            tid: self.tid().unwrap_or(zx::sys::ZX_KOID_INVALID),
            time: self.metadata.timestamp.into(),
            severity: self.legacy_severity().into(),
            dropped_logs: self.dropped_logs().unwrap_or(0) as _,
            tags: match self.metadata.tags {
                Some(tags) => tags,
                None => vec![self.component_name().to_string()],
            },
            msg,
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::{BuilderArgs, LogsDataBuilder};
    use fuchsia_syslog::levels::{DEBUG, ERROR, INFO, TRACE, WARN};
    use std::convert::TryFrom;

    const TEST_URL: &'static str = "fuchsia-pkg://test";
    const TEST_MONIKER: &'static str = "fake-test/moniker";

    macro_rules! severity_roundtrip_test {
        ($raw:expr, $expected:expr) => {
            let legacy = LegacySeverity::try_from($raw).unwrap();
            let (severity, verbosity) = legacy.for_structured();
            let mut msg = LogsDataBuilder::new(BuilderArgs {
                timestamp_nanos: 0i64.into(),
                component_url: Some(TEST_URL.to_string()),
                moniker: TEST_MONIKER.to_string(),
                size_bytes: 1,
                severity,
            })
            .build();
            if let Some(v) = verbosity {
                msg.set_legacy_verbosity(v);
            }

            let legacy_msg: LogMessage = (&msg).into();
            assert_eq!(
                legacy_msg.severity, $expected,
                "failed to round trip severity for {:?} (raw {}), intermediates: {:#?}\n{:#?}",
                legacy, $raw, msg, legacy_msg
            );
        };
    }

    #[test]
    fn verbosity_roundtrip_legacy_v10() {
        severity_roundtrip_test!(-10, INFO - 10);
    }

    #[test]
    fn verbosity_roundtrip_legacy_v5() {
        severity_roundtrip_test!(-5, INFO - 5);
    }

    #[test]
    fn verbosity_roundtrip_legacy_v4() {
        severity_roundtrip_test!(-4, INFO - 4);
    }

    #[test]
    fn verbosity_roundtrip_legacy_v3() {
        severity_roundtrip_test!(-3, INFO - 3);
    }

    #[test]
    fn verbosity_roundtrip_legacy_v2() {
        severity_roundtrip_test!(-2, TRACE);
    }

    #[test]
    fn severity_roundtrip_legacy_v1() {
        severity_roundtrip_test!(-1, DEBUG);
    }

    #[test]
    fn verbosity_roundtrip_legacy_v0() {
        severity_roundtrip_test!(0, INFO);
    }

    #[test]
    fn severity_roundtrip_legacy_warn() {
        severity_roundtrip_test!(1, WARN);
    }

    #[test]
    fn verbosity_roundtrip_legacy_error() {
        severity_roundtrip_test!(2, ERROR);
    }

    #[test]
    fn severity_roundtrip_trace() {
        severity_roundtrip_test!(TRACE, TRACE);
    }

    #[test]
    fn severity_roundtrip_debug() {
        severity_roundtrip_test!(DEBUG, DEBUG);
    }

    #[test]
    fn severity_roundtrip_info() {
        severity_roundtrip_test!(INFO, INFO);
    }

    #[test]
    fn severity_roundtrip_warn() {
        severity_roundtrip_test!(WARN, WARN);
    }

    #[test]
    fn severity_roundtrip_error() {
        severity_roundtrip_test!(ERROR, ERROR);
    }
}
