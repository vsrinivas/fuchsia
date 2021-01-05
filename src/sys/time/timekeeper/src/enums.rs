// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A collection of enumerations that are used widely across Timekeeper, usually in both
//! operational and in diagnostics code.

use {
    crate::rtc::RtcCreationError,
    time_metrics_registry::{
        RealTimeClockEventsMetricDimensionEventType as CobaltRtcEvent, TimeMetricDimensionRole,
        TimeMetricDimensionTrack,
        TimekeeperTimeSourceEventsMetricDimensionEventType as CobaltTimeSourceEvent,
        TimekeeperTrackEventsMetricDimensionEventType as CobaltTrackEvent,
    },
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
    ReadNotAttempted,
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

impl Into<CobaltRtcEvent> for InitializeRtcOutcome {
    fn into(self) -> CobaltRtcEvent {
        match self {
            Self::NoDevices => CobaltRtcEvent::NoDevices,
            Self::MultipleDevices => CobaltRtcEvent::MultipleDevices,
            Self::ConnectionFailed => CobaltRtcEvent::ConnectionFailed,
            Self::ReadFailed => CobaltRtcEvent::ReadFailed,
            // TODO(jsankey): Define a better Cobalt enum for this case
            Self::ReadNotAttempted => CobaltRtcEvent::ReadSucceeded,
            Self::InvalidBeforeBackstop => CobaltRtcEvent::ReadInvalidBeforeBackstop,
            Self::Succeeded => CobaltRtcEvent::ReadSucceeded,
        }
    }
}

/// The possible outcomes of an attempt to write to the real time clock.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum WriteRtcOutcome {
    Failed,
    Succeeded,
}

impl Into<CobaltRtcEvent> for WriteRtcOutcome {
    fn into(self) -> CobaltRtcEvent {
        match self {
            Self::Failed => CobaltRtcEvent::WriteFailed,
            Self::Succeeded => CobaltRtcEvent::WriteSucceeded,
        }
    }
}

/// The role a time source is playing within time synchronization.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Role {
    Primary,
    Monitor,
    // TODO(jsankey): Add Gating and Fallback when some product requires them.
}

impl Into<TimeMetricDimensionRole> for Role {
    fn into(self) -> TimeMetricDimensionRole {
        match self {
            Self::Primary => TimeMetricDimensionRole::Primary,
            Self::Monitor => TimeMetricDimensionRole::Monitor,
        }
    }
}

/// Which of the independent estimates of time is applicable. Timekeeper maintains a Primary track
/// that is externally visible and optionally a internal Monitor track that is used to validate
/// proposed changes to the time synchronization source or algorithms.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Track {
    Primary,
    Monitor,
}

impl From<Role> for Track {
    fn from(role: Role) -> Track {
        match role {
            Role::Primary => Track::Primary,
            Role::Monitor => Track::Monitor,
        }
    }
}

impl Into<TimeMetricDimensionTrack> for Track {
    fn into(self) -> TimeMetricDimensionTrack {
        match self {
            Self::Primary => TimeMetricDimensionTrack::Primary,
            Self::Monitor => TimeMetricDimensionTrack::Monitor,
        }
    }
}

/// The time sources from which the userspace clock might be started.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum StartClockSource {
    Rtc,
    External(Role),
}

/// The strategies that can be used to align a clock with the estimated UTC.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ClockCorrectionStrategy {
    /// The clock will be immediately stepped to the new value.
    Step,
    /// The clock will be gradually slewed to the new value by applying a small rate change for
    /// as long as is necessary.
    NominalRateSlew,
    /// The clock will be slewed to the new value by applying whatever rate change is necessary to
    /// complete the correction within the maximum allowed duration.
    MaxDurationSlew,
}

// Required to instantiate a circular buffer of clock corrections in inspect.
impl Default for ClockCorrectionStrategy {
    fn default() -> Self {
        ClockCorrectionStrategy::Step
    }
}

impl Into<CobaltTrackEvent> for ClockCorrectionStrategy {
    fn into(self) -> CobaltTrackEvent {
        match self {
            Self::Step => CobaltTrackEvent::CorrectionByStep,
            Self::NominalRateSlew => CobaltTrackEvent::CorrectionByNominalRateSlew,
            Self::MaxDurationSlew => CobaltTrackEvent::CorrectionByMaxDurationSlew,
        }
    }
}

/// The reasons a clock may have been updated.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ClockUpdateReason {
    TimeStep,
    BeginSlew,
    EndSlew,
    ReduceError,
    IncreaseError,
    ChangeFrequency,
}

impl Into<CobaltTrackEvent> for ClockUpdateReason {
    fn into(self) -> CobaltTrackEvent {
        match self {
            Self::TimeStep => CobaltTrackEvent::ClockUpdateTimeStep,
            Self::BeginSlew => CobaltTrackEvent::ClockUpdateBeginSlew,
            Self::EndSlew => CobaltTrackEvent::ClockUpdateEndSlew,
            Self::ReduceError => CobaltTrackEvent::ClockUpdateReduceError,
            Self::IncreaseError => CobaltTrackEvent::ClockUpdateIncreaseError,
            Self::ChangeFrequency => CobaltTrackEvent::ClockUpdateChangeFrequency,
        }
    }
}

/// The reasons a received time sample may not be valid.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum SampleValidationError {
    MonotonicInFuture,
    MonotonicTooOld,
    BeforeBackstop,
    TooCloseToPrevious,
}

impl Into<CobaltTimeSourceEvent> for SampleValidationError {
    fn into(self) -> CobaltTimeSourceEvent {
        match self {
            Self::MonotonicInFuture => CobaltTimeSourceEvent::SampleRejectedMonotonicInFuture,
            Self::MonotonicTooOld => CobaltTimeSourceEvent::SampleRejectedMonotonicTooOld,
            Self::BeforeBackstop => CobaltTimeSourceEvent::SampleRejectedBeforeBackstop,
            Self::TooCloseToPrevious => CobaltTimeSourceEvent::SampleRejectedTooCloseToPrevious,
        }
    }
}

/// The reasons a time source may have failed.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum TimeSourceError {
    LaunchFailed,
    StreamFailed,
    CallFailed,
    SampleTimeOut,
}

impl Into<CobaltTimeSourceEvent> for TimeSourceError {
    fn into(self) -> CobaltTimeSourceEvent {
        match self {
            Self::LaunchFailed => CobaltTimeSourceEvent::LaunchFailed,
            Self::StreamFailed => CobaltTimeSourceEvent::RestartedStreamFailed,
            Self::CallFailed => CobaltTimeSourceEvent::RestartedCallFailed,
            Self::SampleTimeOut => CobaltTimeSourceEvent::RestartedSampleTimeOut,
        }
    }
}
