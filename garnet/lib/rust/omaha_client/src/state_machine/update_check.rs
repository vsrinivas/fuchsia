// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The update_check module contains the structures and functions for performing a single update
/// check with Omaha.
use crate::common::{App, ProtocolState, UpdateCheckSchedule};

/// The Context provides the protocol context for a given update check operation.  This is
/// information that's passed to the Policy to allow it to properly reason about what can and cannot
/// be done at this time.
#[derive(Debug)]
pub struct Context {
    /// The last-computed time to next check for an update.
    pub schedule: UpdateCheckSchedule,

    /// The state of the protocol (retries, errors, etc.) as of the last update check that was
    /// attempted.
    pub state: ProtocolState,
}

/// The response context from the update check contains any extra information that Omaha returns to
/// the client, separate from the data about a particular app itself.
#[derive(Debug)]
pub struct Response {
    /// The set of responses for all the apps in the request.
    pub app_responses: Vec<AppResponse>,

    /// If Omaha dictated that a longer poll interval be used, it will be reported here.
    #[allow(dead_code)]
    pub server_dictated_poll_interval: Option<u64>,
}

/// For each application that had an update check performed, a new App (potentially with new Cohort
/// and UserCounting data) and a corresponding response Action are returned from the update check.
#[allow(dead_code)]
#[derive(Debug)]
pub struct AppResponse {
    /// The returned information about an application.
    pub app: App,

    /// The resultant action of its update check.
    pub result: Action,
}

/// The Action is the result of an update check for a single App.  This is just informational, for
/// the purposes of updating the protocol state.  Any update action should already have been taken
/// by the Installer.
#[allow(dead_code)]
#[derive(Debug)]
pub enum Action {
    /// Omaha's response was "no update"
    NoUpdate,

    /// The update check and/or install process encountered an error.  See the specific error to
    /// see what happened.
    Error(failure::Error),

    /// An update was found.  See the value for the version that's available.
    Update(String),
}
