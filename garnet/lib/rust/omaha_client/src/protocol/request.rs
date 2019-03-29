// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::Cohort;

/// An Omaha protocol request.
///
/// This holds the data for constructing a request to the Omaha service.
///
/// See https://github.com/google/omaha/blob/master/doc/ServerProtocolV3.md#request
#[derive(Debug)]
#[allow(dead_code)] // to be removed shortly.
pub struct Request {
    /// The current Omaha protocol version (which this is meant to be used with, is 3.0.  This
    /// should always be set to "3.0".
    ///
    /// This is the 'protocol' attribute of the request object.
    pub protocol_version: String,

    /// The version of the updater itself (e.g. "Fuchsia/Rust-0.0.0.1").  This is the version of the
    /// updater implemented using this Crate.
    ///
    /// This is the 'updaterversion' attribute of the request object.
    pub updater_version: String,

    /// The install source trigger for this request.
    pub install_source: InstallSource,

    /// The system update is always done by "the machine" aka system-level or administrator
    /// privileges.
    ///
    /// This is the 'ismachine' attribute of the request object.
    pub is_machine: bool,

    /// Information about the device operating system.
    ///
    /// This is the 'os' child object of the request object.
    pub os: OS,

    /// The applications to update.
    ///
    /// These are the 'app' children objects of the request object
    pub apps: Vec<App>,
}

/// Enum of the possible reasons that this update request was initiated.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum InstallSource {
    /// This update check was triggered "on demand", by a user.
    OnDemand,

    /// This update check was triggered as part of a background task, unattended by a user.
    ScheduledTask,
}

/// Information about the platform / operating system.
///
/// See https://github.com/google/omaha/blob/master/doc/ServerProtocolV3.md#os
#[derive(Debug, Clone)]
#[allow(dead_code)] // to be removed shortly.
pub struct OS {
    /// The device platform (e.g. 'Fuchsia')
    pub platform: String,

    /// The version of the platform
    pub version: String,

    /// The patch level of the platform (e.g. "12345_arm64")
    pub service_pack: String,

    /// The platform architecture (e.g. "x86-64")
    pub arch: String,
}

/// Information about an individual app that an update check is being performed for.
///
/// Each app in a request may have at most one 'ping' children, and either a single 'updatecheck'
/// child, or one or more 'event' children.  This is enforced in the App struct by the use of the
/// CheckOrEvents enum.
///
/// See https://github.com/google/omaha/blob/master/doc/ServerProtocolV3.md#app-request
#[derive(Debug, Clone)]
#[allow(dead_code)] // to be removed shortly.
pub struct App {
    /// This is the GUID or product ID that uniquely identifies the product to Omaha.
    ///
    /// This is the 'appid' attribute of the app object.
    pub id: String,

    /// The version of the product that's currently installed.  This is in 'A.B.C.D' format.
    ///
    /// This is the version attribute of the app object.
    pub version: String,

    /// The fingerprint for the application.
    ///
    /// This is the fp attribute of the app object.
    pub fingerprint: Option<String>,

    /// This is the cohort id, as previously assigned by the Omaha service.  This is a machine-
    /// readable string, not meant for user display.
    ///
    /// This holds the following fields of the app object:
    ///   cohort
    ///   cohorthint
    ///   cohortname
    pub cohort: Option<Cohort>,

    /// Either a single update check, multiple events, or nothing.
    ///
    /// These are children objects of the app object.
    pub check_or_events: Option<CheckOrEvents>,

    /// An optional status ping.
    pub ping: Option<Ping>,
}

/// This enum enforces the requirement that either a single UpdateCheck, or multiple Events are
/// present in the request for a single application.
#[derive(Debug, Clone)]
#[allow(dead_code)]
pub enum CheckOrEvents {
    /// This is an update check request.
    Check(UpdateCheck),

    /// This is a reporting of one or more update-related events.
    Events(Vec<Event>),
}

/// This is an update check for the parent App object.
///
/// See https://github.com/google/omaha/blob/master/doc/ServerProtocolV3.md#updatecheck-request
#[derive(Debug, Default, Clone)]
#[allow(dead_code)]
pub struct UpdateCheck {
    /// If the update is disabled, the client will not honor an 'update' response.  The default
    /// value of false indicates that the client will attempt an update if instructed that one is
    /// available.
    pub disabled: bool,
}

/// This is a status ping to the Omaha service.
///
/// See https://github.com/google/omaha/blob/master/doc/ServerProtocolV3.md#ping-request
///
/// These pings only support the Client-Regulated Counting method (Date-based).  For more info, see
/// https://github.com/google/omaha/blob/master/doc/ServerProtocolV3.md#client-regulated-Counting-days-based
#[derive(Debug, Clone)]
#[allow(dead_code)]
pub struct Ping {
    /// This is the January 1, 2007 epoch-based value for the date that was previously sent to the
    /// client by the service, as the elapsed_days value of the daystart object, if the application
    /// is active.
    ///
    /// This is the 'ad' attribute of the ping object.
    pub date_last_active: Option<i32>,

    /// This is the January 1, 2007 epoch-based value for the date that was previously sent to the
    /// client by the service, as the elapsed_days value of the daystart object, if the application
    /// is active or not.
    ///
    /// This is the 'rd' attribute of the ping object.
    pub date_last_roll_call: Option<i32>,
}

/// An event that is being reported to the Omaha service.
///
/// See https://github.com/google/omaha/blob/master/doc/ServerProtocolV3.md#event-request
#[derive(Debug, Clone)]
#[allow(dead_code)]
pub struct Event {
    /// This is the event type for the event (see the enum for more information).
    ///
    /// This is the eventtype attribute of the event object.
    pub event_type: EventType,

    /// This is the result code for the event.  All event types share a namespace for result codes.
    ///
    /// This is the eventresult attribute of the event object.
    pub event_result: EventResult,

    /// This is an opaque error value that may be provided.  It's meaning is application specific.
    ///
    /// This is the errorcode attribute of the event object.
    pub errorcode: Option<i32>,
}

/// The type of event that is being reported.  These are specified by the Omaha protocol.
///
/// See https://github.com/google/omaha/blob/master/doc/ServerProtocolV3.md#event-request
#[derive(Debug, Clone)]
#[allow(dead_code)]
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

/// The result of event that is being reported.  These are specified by the Omaha protocol.
///
/// See https://github.com/google/omaha/blob/master/doc/ServerProtocolV3.md#event-request
#[derive(Debug, Clone)]
#[allow(dead_code)]
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
