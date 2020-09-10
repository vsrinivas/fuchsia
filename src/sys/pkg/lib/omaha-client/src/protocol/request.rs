// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::protocol::Cohort;
use serde::{Serialize, Serializer};
use serde_repr::Serialize_repr;
use std::collections::HashMap;

#[cfg(test)]
mod tests;

/// This is the key for the http request header that identifies the 'updater' that is sending a
/// request.
pub const HEADER_UPDATER_NAME: &str = "X-Goog-Update-Updater";

/// This is the key for the http request header that identifies whether this is an interactive
/// or a background update (see InstallSource).
pub const HEADER_INTERACTIVITY: &str = "X-Goog-Update-Interactivity";

/// This is the key for the http request header that identifies the app id(s) that are included in
/// this request.
pub const HEADER_APP_ID: &str = "X-Goog-Update-AppId";

/// An Omaha protocol request.
///
/// This holds the data for constructing a request to the Omaha service.
///
/// See https://github.com/google/omaha/blob/master/doc/ServerProtocolV3.md#request
#[derive(Debug, Default, Serialize)]
pub struct Request {
    /// The current Omaha protocol version (which this is meant to be used with, is 3.0.  This
    /// should always be set to "3.0".
    ///
    /// This is the 'protocol' attribute of the request object.
    #[serde(rename = "protocol")]
    pub protocol_version: String,

    /// This is the string identifying the updater software itself (this client). e.g. "fuchsia"
    pub updater: String,

    /// The version of the updater itself (e.g. "Fuchsia/Rust-0.0.0.1").  This is the version of the
    /// updater implemented using this Crate.
    ///
    /// This is the 'updaterversion' attribute of the request object.
    #[serde(rename = "updaterversion")]
    pub updater_version: String,

    /// The install source trigger for this request.
    #[serde(rename = "installsource")]
    pub install_source: InstallSource,

    /// The system update is always done by "the machine" aka system-level or administrator
    /// privileges.
    ///
    /// This is the 'ismachine' attribute of the request object.
    #[serde(rename = "ismachine")]
    pub is_machine: bool,

    /// The randomly generated GUID for a single Omaha request.
    ///
    /// This is the 'requestid' attribute of the request object.
    #[serde(rename = "requestid")]
    #[serde(skip_serializing_if = "Option::is_none")]
    pub request_id: Option<GUID>,

    /// The randomly generated GUID for all Omaha requests in an update session.
    ///
    /// This is the 'sessionid' attribute of the request object.
    #[serde(rename = "sessionid")]
    #[serde(skip_serializing_if = "Option::is_none")]
    pub session_id: Option<GUID>,

    /// Information about the device operating system.
    ///
    /// This is the 'os' child object of the request object.
    pub os: OS,

    /// The applications to update.
    ///
    /// These are the 'app' children objects of the request object
    #[serde(rename = "app")]
    pub apps: Vec<App>,
}

/// This is a serialization wrapper for a Request, as a Request object serializes into a value,
/// for an object, not an object that is '{"request": {....} }'.  This wrapper provides the request
/// wrapping that Omaha expects to see.
#[derive(Debug, Default, Serialize)]
pub struct RequestWrapper {
    pub request: Request,
}

/// Enum of the possible reasons that this update request was initiated.
#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum InstallSource {
    /// This update check was triggered "on demand", by a user.
    OnDemand,

    /// This update check was triggered as part of a background task, unattended by a user.
    ScheduledTask,
}

impl Default for InstallSource {
    fn default() -> Self {
        InstallSource::ScheduledTask
    }
}

/// Information about the platform / operating system.
///
/// See https://github.com/google/omaha/blob/master/doc/ServerProtocolV3.md#os
#[derive(Clone, Debug, Default, Eq, PartialEq, Serialize)]
pub struct OS {
    /// The device platform (e.g. 'Fuchsia')
    pub platform: String,

    /// The version of the platform
    pub version: String,

    /// The patch level of the platform (e.g. "12345_arm64")
    #[serde(rename = "sp")]
    pub service_pack: String,

    /// The platform architecture (e.g. "x86-64")
    pub arch: String,
}

/// Information about an individual app that an update check is being performed for.
///
/// While unlikely, it's possible for a single request to have an update check, a ping, and for it
/// to be reporting an event.
///
/// See https://github.com/google/omaha/blob/master/doc/ServerProtocolV3.md#app-request
#[derive(Debug, Default, Clone, Serialize)]
pub struct App {
    /// This is the GUID or product ID that uniquely identifies the product to Omaha.
    ///
    /// This is the 'appid' attribute of the app object.
    #[serde(rename = "appid")]
    pub id: String,

    /// The version of the product that's currently installed.  This is in 'A.B.C.D' format.
    ///
    /// This is the version attribute of the app object.
    pub version: String,

    /// The fingerprint for the application.
    ///
    /// This is the fp attribute of the app object.
    #[serde(rename = "fp")]
    #[serde(skip_serializing_if = "Option::is_none")]
    pub fingerprint: Option<String>,

    /// This is the cohort id, as previously assigned by the Omaha service.  This is a machine-
    /// readable string, not meant for user display.
    ///
    /// This holds the following fields of the app object:
    ///   cohort
    ///   cohorthint
    ///   cohortname
    #[serde(flatten)]
    pub cohort: Option<Cohort>,

    /// If present, this request is an update check.
    #[serde(rename = "updatecheck")]
    #[serde(skip_serializing_if = "Option::is_none")]
    pub update_check: Option<UpdateCheck>,

    /// These are events to report to Omaha.
    #[serde(rename = "event")]
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub events: Vec<Event>,

    /// An optional status ping.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ping: Option<Ping>,

    /// Extra fields to include (App-specific fields used to extend the protocol).
    ///
    /// # NOTE:  Can break the omaha protocol if improperly used.
    ///
    /// This is listed last in the struct, and should remain so, due to how Serde behaves when
    /// flattening fields into the parent.  If this map contains a field whose name matches that of
    /// another field in the struct (such as `id`), it will overwrite that field.  If that field is
    /// optionally serialized (such as `update_check`), it will still overwrite that field
    /// (regardless of the presence or not of the field it's overwriting).
    #[serde(flatten)]
    pub extra_fields: HashMap<String, String>,
}

/// This is an update check for the parent App object.
///
/// See https://github.com/google/omaha/blob/master/doc/ServerProtocolV3.md#updatecheck-request
#[derive(Clone, Debug, Default, Eq, PartialEq, Serialize)]
pub struct UpdateCheck {
    /// If the update is disabled, the client will not honor an 'update' response.  The default
    /// value of false indicates that the client will attempt an update if instructed that one is
    /// available.
    #[serde(skip_serializing_if = "std::ops::Not::not")]
    #[serde(rename = "updatedisabled")]
    pub disabled: bool,
}

impl UpdateCheck {
    /// Public constructor for an update check request on an app that will not honor an 'update'
    /// response and will not perform an update if one is available.
    pub fn disabled() -> Self {
        UpdateCheck { disabled: true }
    }
}

/// This is a status ping to the Omaha service.
///
/// See https://github.com/google/omaha/blob/master/doc/ServerProtocolV3.md#ping-request
///
/// These pings only support the Client-Regulated Counting method (Date-based).  For more info, see
/// https://github.com/google/omaha/blob/master/doc/ServerProtocolV3.md#client-regulated-Counting-days-based
#[derive(Debug, Default, Clone, Eq, PartialEq, Serialize)]
pub struct Ping {
    /// This is the January 1, 2007 epoch-based value for the date that was previously sent to the
    /// client by the service, as the elapsed_days value of the daystart object, if the application
    /// is active.
    ///
    /// This is the 'ad' attribute of the ping object.
    #[serde(rename = "ad")]
    #[serde(skip_serializing_if = "Option::is_none")]
    pub date_last_active: Option<i32>,

    /// This is the January 1, 2007 epoch-based value for the date that was previously sent to the
    /// client by the service, as the elapsed_days value of the daystart object, if the application
    /// is active or not.
    ///
    /// This is the 'rd' attribute of the ping object.
    #[serde(rename = "rd")]
    #[serde(skip_serializing_if = "Option::is_none")]
    pub date_last_roll_call: Option<i32>,
}

/// An event that is being reported to the Omaha service.
///
/// See https://github.com/google/omaha/blob/master/doc/ServerProtocolV3.md#event-request
#[derive(Debug, Default, Clone, Eq, PartialEq, Serialize)]
pub struct Event {
    /// This is the event type for the event (see the enum for more information).
    ///
    /// This is the eventtype attribute of the event object.
    #[serde(rename = "eventtype")]
    pub event_type: EventType,

    /// This is the result code for the event.  All event types share a namespace for result codes.
    ///
    /// This is the eventresult attribute of the event object.
    #[serde(rename = "eventresult")]
    pub event_result: EventResult,

    /// This is an opaque error value that may be provided.  It's meaning is application specific.
    ///
    /// This is the errorcode attribute of the event object.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub errorcode: Option<EventErrorCode>,

    /// The version of the app that was present on the machine at the time of the update-check of
    /// this update flow, regardless of the success or failure of the update operation.
    #[serde(skip_serializing_if = "Option::is_none")]
    #[serde(rename = "previousversion")]
    pub previous_version: Option<String>,
}

impl Event {
    /// Creates a new successful event for the given event type.
    pub fn success(event_type: EventType) -> Self {
        Self { event_type, event_result: EventResult::Success, ..Self::default() }
    }

    /// Creates a new error event for the given event error code.
    pub fn error(errorcode: EventErrorCode) -> Self {
        Self {
            event_type: EventType::UpdateComplete,
            event_result: EventResult::Error,
            errorcode: Some(errorcode),
            ..Self::default()
        }
    }
}

/// The type of event that is being reported.  These are specified by the Omaha protocol.
///
/// See https://github.com/google/omaha/blob/master/doc/ServerProtocolV3.md#event-request
#[derive(Debug, Clone, Eq, PartialEq, Serialize_repr)]
#[repr(u8)]
pub enum EventType {
    Unknown = 0,

    /// The initial download of the application is complete.
    DownloadComplete = 1,

    /// The initial installation of the application is complete.
    InstallComplete = 2,

    /// The application update is complete.
    UpdateComplete = 3,

    /// The download of the update for the application has started.
    UpdateDownloadStarted = 13,

    /// The download of the update for the application is complete.
    UpdateDownloadFinished = 14,

    /// The application is now using the updated software.  This is sent after a successful boot
    /// into the update software.
    RebootedAfterUpdate = 54,
}

impl Default for EventType {
    fn default() -> Self {
        EventType::Unknown
    }
}

/// The result of event that is being reported.  These are specified by the Omaha protocol.
///
/// See https://github.com/google/omaha/blob/master/doc/ServerProtocolV3.md#event-request
#[derive(Debug, Clone, Eq, PartialEq, Serialize_repr)]
#[repr(u8)]
pub enum EventResult {
    Error = 0,
    Success = 1,
    SuccessAndRestartRequired = 2,
    SuccessAndAppRestartRequired = 3,
    Cancelled = 4,
    ErrorInSystemInstaller = 8,

    /// The client acknowledges that it received the 'update' response, but it will not be acting
    /// on the update at this time (deferred by Policy).
    UpdateDeferred = 9,
}

impl Default for EventResult {
    fn default() -> Self {
        EventResult::Error
    }
}

/// The error code of the event.  These are application specific.
#[derive(Debug, Clone, Eq, PartialEq, Serialize_repr)]
#[repr(i32)]
pub enum EventErrorCode {
    /// Error when parsing Omaha response.
    ParseResponse = 0,
    /// Error when constructing install plan.
    ConstructInstallPlan = 1,
    /// Error when installing the update.
    Installation = 2,
    /// The update is denied by policy.
    DeniedByPolicy = 3,
}

impl Default for EventErrorCode {
    fn default() -> Self {
        EventErrorCode::ParseResponse
    }
}

/// The GUID used in Omaha protocol for sessionid and requestid.
///
/// See https://github.com/google/omaha/blob/master/doc/ServerProtocolV3.md#guids
#[derive(Debug, Default, Clone, Eq, PartialEq)]
pub struct GUID {
    uuid: uuid::Uuid,
}

impl GUID {
    /// Creates a new random GUID.
    #[cfg(not(test))]
    pub fn new() -> Self {
        Self { uuid: uuid::Uuid::new_v4() }
    }

    // For unit tests, creates GUID using a thread local counter, so that for every test case,
    // the first GUID will be {00000000-0000-0000-0000-000000000000},
    // and the second will be {00000000-0000-0000-0000-000000000001}, and so on.
    #[cfg(test)]
    pub fn new() -> Self {
        thread_local! {
            static COUNTER: std::cell::RefCell<u128> =
            std::cell::RefCell::new(0);
        }
        COUNTER.with(|counter| {
            let mut counter = counter.borrow_mut();
            let guid = Self::from_u128(*counter);
            *counter += 1;
            guid
        })
    }

    #[cfg(test)]
    pub fn from_u128(n: u128) -> Self {
        // TODO: use uuid::Uuid::from_u128() when it's available.
        Self { uuid: uuid::Uuid::from_slice(&n.to_be_bytes()).unwrap() }
    }
}

// Wrap the uuid in {}.
impl Serialize for GUID {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        serializer.serialize_str(&format!("{{{}}}", self.uuid))
    }
}
