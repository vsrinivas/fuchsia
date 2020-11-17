// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        descriptor::EventDescriptor,
        events::{Event, EventStream, ExitStatus},
    },
    anyhow::Error,
    fidl_fuchsia_sys2 as fsys,
    regex::RegexSet,
    std::{convert::TryFrom, fmt},
    thiserror::Error,
};
#[derive(Debug, Error, PartialEq, Eq)]
pub enum FieldMatcherError {
    #[error("Missing field: `{field_name}`")]
    MissingField { field_name: &'static str },
    #[error("Field `{field_name}` mismatch. Expected: `{expected}`, Actual: `{actual}`")]
    FieldMismatch { field_name: &'static str, expected: String, actual: String },
}

#[derive(Debug)]
pub struct FieldMatcherErrors {
    field_matcher_errors: Vec<FieldMatcherError>,
}

impl fmt::Display for FieldMatcherErrors {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        for err in &self.field_matcher_errors {
            writeln!(f, "{}", err)?;
        }
        Ok(())
    }
}

#[derive(Debug, Error)]
pub enum EventMatcherError {
    #[error("{errors}")]
    FieldMatcherErrors { errors: FieldMatcherErrors },
}

// A matcher that implements this trait is able to match against values of type `T`.
// A matcher corresponds to a field named `NAME`.
trait RawFieldMatcher<T>: Clone + std::fmt::Debug + ToString {
    const NAME: &'static str;
    fn matches(&self, other: &T) -> bool;
}

#[derive(Clone, Debug)]
pub struct EventTypeMatcher {
    event_type: fsys::EventType,
}

impl EventTypeMatcher {
    fn new(event_type: fsys::EventType) -> Self {
        Self { event_type }
    }

    pub fn value(&self) -> &fsys::EventType {
        &self.event_type
    }
}

impl fmt::Display for EventTypeMatcher {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}", self.event_type)
    }
}

impl RawFieldMatcher<fsys::EventType> for EventTypeMatcher {
    const NAME: &'static str = "event_type";

    fn matches(&self, other: &fsys::EventType) -> bool {
        self.event_type == *other
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct CapabilityNameMatcher {
    capability_name: String,
}

impl CapabilityNameMatcher {
    fn new(capability_name: impl Into<String>) -> Self {
        Self { capability_name: capability_name.into() }
    }
}

impl fmt::Display for CapabilityNameMatcher {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.capability_name)
    }
}

impl RawFieldMatcher<String> for CapabilityNameMatcher {
    const NAME: &'static str = "capability_name";

    fn matches(&self, other: &String) -> bool {
        self.capability_name == *other
    }
}

#[derive(Clone, Debug)]
pub struct MonikerMatcher {
    monikers: RegexSet,
}

impl MonikerMatcher {
    fn new<I, S>(monikers: I) -> Self
    where
        S: AsRef<str>,
        I: IntoIterator<Item = S>,
    {
        Self { monikers: RegexSet::new(monikers).unwrap() }
    }
}

impl fmt::Display for MonikerMatcher {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}", self.monikers)
    }
}

impl RawFieldMatcher<String> for MonikerMatcher {
    const NAME: &'static str = "target_monikers";

    fn matches(&self, other: &String) -> bool {
        self.monikers.is_match(other)
    }
}

#[derive(Debug, PartialEq, Eq, Clone, Ord, PartialOrd)]
/// Used for matching against events. If the matcher doesn't crash the exit code
/// then `AnyCrash` can be used to match against any Stopped event caused by a crash.
/// that indicate failure are crushed into `Crash`.
pub enum ExitStatusMatcher {
    Clean,
    AnyCrash,
    Crash(i32),
}

impl fmt::Display for ExitStatusMatcher {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}", self)
    }
}

impl RawFieldMatcher<ExitStatus> for ExitStatusMatcher {
    const NAME: &'static str = "exit_status";

    fn matches(&self, other: &ExitStatus) -> bool {
        match (self, other) {
            (ExitStatusMatcher::Clean, ExitStatus::Clean) => true,
            (ExitStatusMatcher::AnyCrash, ExitStatus::Crash(_)) => true,
            (ExitStatusMatcher::Crash(exit_code), ExitStatus::Crash(other_exit_code)) => {
                exit_code == other_exit_code
            }
            _ => false,
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct EventIsOkMatcher {
    event_is_ok: bool,
}

impl EventIsOkMatcher {
    fn new(event_is_ok: bool) -> Self {
        Self { event_is_ok }
    }
}

impl fmt::Display for EventIsOkMatcher {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.event_is_ok)
    }
}

impl RawFieldMatcher<bool> for EventIsOkMatcher {
    const NAME: &'static str = "event_is_ok";

    fn matches(&self, other: &bool) -> bool {
        self.event_is_ok == *other
    }
}

/// A field matcher is an optional matcher that compares against an optional field.
/// If there is a mismatch, an error string is generated. If there is no matcher specified,
/// then there is no error. If there is matcher without a corresponding field then that's a missing
/// field error. Otherwise, the FieldMatcher delegates to the RawFieldMatcher to determine if the
/// matcher matches against the raw field.
trait FieldMatcher<T> {
    fn matches(&self, other: &Option<T>) -> Result<(), FieldMatcherError>;
}

impl<LeftHandSide, RightHandSide> FieldMatcher<RightHandSide> for Option<LeftHandSide>
where
    LeftHandSide: RawFieldMatcher<RightHandSide>,
    RightHandSide: std::fmt::Debug,
{
    fn matches(&self, other: &Option<RightHandSide>) -> Result<(), FieldMatcherError> {
        match (self, other) {
            (Some(value), Some(other_value)) => match value.matches(other_value) {
                true => Ok(()),
                false => Err(FieldMatcherError::FieldMismatch {
                    field_name: LeftHandSide::NAME,
                    expected: value.to_string(),
                    actual: format!("{:?}", other_value),
                }),
            },
            (Some(_), None) => {
                Err(FieldMatcherError::MissingField { field_name: LeftHandSide::NAME })
            }
            (None, _) => Ok(()),
        }
    }
}

#[derive(Clone, Debug, Default)]
pub struct EventMatcher {
    pub event_type: Option<EventTypeMatcher>,
    pub target_monikers: Option<MonikerMatcher>,
    pub capability_name: Option<CapabilityNameMatcher>,
    pub exit_status: Option<ExitStatusMatcher>,
    pub event_is_ok: Option<EventIsOkMatcher>,
}

impl EventMatcher {
    pub fn ok() -> Self {
        let mut matcher = EventMatcher::default();
        matcher.event_is_ok = Some(EventIsOkMatcher::new(true));
        matcher
    }

    pub fn err() -> Self {
        let mut matcher = EventMatcher::default();
        matcher.event_is_ok = Some(EventIsOkMatcher::new(false));
        matcher
    }

    pub fn r#type(mut self, event_type: fsys::EventType) -> Self {
        self.event_type = Some(EventTypeMatcher::new(event_type));
        self
    }

    /// The expected target moniker as a regular expression.
    pub fn moniker(self, moniker: impl Into<String>) -> Self {
        self.monikers(&[moniker.into()])
    }

    /// The expected target monikers as regular expressions. This will match against
    /// regular expression in the iterator provided.
    pub fn monikers<I, S>(mut self, monikers: I) -> Self
    where
        S: AsRef<str>,
        I: IntoIterator<Item = S>,
    {
        self.target_monikers = Some(MonikerMatcher::new(monikers));
        self
    }

    /// The expected capability name.
    pub fn capability_name(mut self, capability_name: impl Into<String>) -> Self {
        self.capability_name = Some(CapabilityNameMatcher::new(capability_name));
        self
    }

    /// The expected exit status. Only applies to the Stop event.
    pub fn stop(mut self, exit_status: Option<ExitStatusMatcher>) -> Self {
        self.event_type = Some(EventTypeMatcher::new(fsys::EventType::Stopped));
        self.exit_status = exit_status;
        self
    }

    /// Expects the next event to match the provided EventMatcher.
    /// Returns the casted type if successful and an error otherwise.
    pub async fn expect_match<T: Event>(&mut self, event_stream: &mut EventStream) -> T {
        let event = event_stream.next().await.unwrap();
        let descriptor = EventDescriptor::try_from(&event).unwrap();
        let event = T::try_from(event).unwrap();
        self.matches(&descriptor).unwrap();
        event
    }

    /// Waits for an event matching the matcher.
    /// Implicitly resumes all other events.
    /// Returns the casted type if successful and an error otherwise.
    pub async fn wait<T: Event>(self, event_stream: &mut EventStream) -> Result<T, Error> {
        let expected_event_matcher = self.r#type(T::TYPE);
        loop {
            let event = event_stream.next().await?;
            let descriptor = EventDescriptor::try_from(&event)?;
            if expected_event_matcher.matches(&descriptor).is_ok() {
                return T::try_from(event);
            }
        }
    }

    pub fn matches(&self, other: &EventDescriptor) -> Result<(), EventMatcherError> {
        let mut field_matcher_errors = vec![];

        if let Err(e) = self.event_type.matches(&other.event_type) {
            field_matcher_errors.push(e);
        }
        if let Err(e) = self.target_monikers.matches(&other.target_moniker) {
            field_matcher_errors.push(e);
        }
        if let Err(e) = self.capability_name.matches(&other.capability_name) {
            field_matcher_errors.push(e);
        }
        if let Err(e) = self.exit_status.matches(&other.exit_status) {
            field_matcher_errors.push(e);
        }
        if let Err(e) = self.event_is_ok.matches(&other.event_is_ok) {
            field_matcher_errors.push(e);
        }
        if !field_matcher_errors.is_empty() {
            return Err(EventMatcherError::FieldMatcherErrors {
                errors: FieldMatcherErrors { field_matcher_errors },
            });
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn event_matcher_errors() {
        let matcher =
            EventMatcher::ok().capability_name("foobar").stop(Some(ExitStatusMatcher::AnyCrash));
        let descriptor = EventDescriptor {
            event_type: None,
            capability_name: None,
            target_moniker: None,
            exit_status: Some(ExitStatus::Clean),
            event_is_ok: Some(false),
        };
        let EventMatcherError::FieldMatcherErrors { errors } =
            matcher.matches(&descriptor).unwrap_err();
        assert!(errors
            .field_matcher_errors
            .contains(&FieldMatcherError::MissingField { field_name: "event_type" }));
        assert!(errors
            .field_matcher_errors
            .contains(&FieldMatcherError::MissingField { field_name: "capability_name" }));
        assert!(errors.field_matcher_errors.contains(&FieldMatcherError::FieldMismatch {
            field_name: "event_is_ok",
            expected: "true".to_string(),
            actual: "false".to_string()
        }));
        assert!(errors.field_matcher_errors.contains(&FieldMatcherError::FieldMismatch {
            field_name: "exit_status",
            expected: "AnyCrash".to_string(),
            actual: "Clean".to_string()
        }));
    }
}
