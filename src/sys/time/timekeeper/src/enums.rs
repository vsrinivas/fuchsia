// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A collection of enumerations that are used widely across Timekeeper, usually in both
//! operational and in diagnostics code.

use {
    crate::rtc::RtcCreationError,
    time_metrics_registry::RealTimeClockEventsMetricDimensionEventType as CobaltRtcEventType,
};

/// The state of the userspace UTC clock when Timekeeper was initialized.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum InitialClockState {
    NotSet,
    PreviouslySet,
}

/// The possible outcomes of an attempt to initialize and read the real time clock.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum InitializeRtcOutcome {
    NoDevices,
    MultipleDevices,
    ConnectionFailed,
    ReadFailed,
    InvalidBeforeBackstop,
    Succeeded,
}

impl From<RtcCreationError> for InitializeRtcOutcome {
    fn from(other: RtcCreationError) -> InitializeRtcOutcome {
        match other {
            RtcCreationError::NoDevices => Self::NoDevices,
            RtcCreationError::MultipleDevices(_) => Self::MultipleDevices,
            RtcCreationError::ConnectionFailed(_) => Self::ConnectionFailed,
        }
    }
}

impl Into<CobaltRtcEventType> for InitializeRtcOutcome {
    fn into(self) -> CobaltRtcEventType {
        match self {
            Self::NoDevices => CobaltRtcEventType::NoDevices,
            Self::MultipleDevices => CobaltRtcEventType::MultipleDevices,
            Self::ConnectionFailed => CobaltRtcEventType::ConnectionFailed,
            Self::ReadFailed => CobaltRtcEventType::ReadFailed,
            Self::InvalidBeforeBackstop => CobaltRtcEventType::ReadInvalidBeforeBackstop,
            Self::Succeeded => CobaltRtcEventType::ReadSucceeded,
        }
    }
}

/// The possible outcomes of an attempt to write to the real time clock.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum WriteRtcOutcome {
    Failed,
    Succeeded,
}

impl Into<CobaltRtcEventType> for WriteRtcOutcome {
    fn into(self) -> CobaltRtcEventType {
        match self {
            Self::Failed => CobaltRtcEventType::WriteFailed,
            Self::Succeeded => CobaltRtcEventType::WriteSucceeded,
        }
    }
}

/// The sources from which the userspace clock might be started.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum StartClockSource {
    Rtc,
    Primary,
}
