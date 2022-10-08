// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod tests;

use crate::{
    common::{App, UserCounting},
    configuration::Config,
    cup_ecdsa::{CupDecorationError, CupRequest, Cupv2RequestHandler, RequestMetadata},
    protocol::{
        request::{
            Event, InstallSource, Ping, Request, RequestWrapper, UpdateCheck, GUID, HEADER_APP_ID,
            HEADER_INTERACTIVITY, HEADER_UPDATER_NAME,
        },
        PROTOCOL_V3,
    },
};
use http;
use std::fmt::Display;
use std::result;
use thiserror::Error;
use tracing::*;

type ProtocolApp = crate::protocol::request::App;

/// Building a request can fail for multiple reasons, this enum consolidates them into a single
/// type that can be used to express those reasons.
#[derive(Debug, Error)]
pub enum Error {
    #[error("Unexpected JSON error constructing update check")]
    Json(#[from] serde_json::Error),

    #[error("Http error performing update check")]
    Http(#[from] http::Error),

    #[error("Error decorating outgoing request with CUPv2 parameters")]
    Cup(#[from] CupDecorationError),
}

/// The builder's own Result type.
pub type Result<T> = result::Result<T, Error>;

/// These are the parameters that describe how the request should be performed.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct RequestParams {
    /// The install source for a request changes a number of properties of the request, including
    /// the HTTP request headers, and influences how Omaha services the request (e.g. throttling)
    pub source: InstallSource,

    /// If true, the request should use any configured proxies.  This allows the bypassing of
    /// proxies if there are difficulties in communicating with the Omaha service.
    pub use_configured_proxies: bool,

    /// If true, the request should set the "updatedisabled" property for all apps in the update
    /// check request.
    pub disable_updates: bool,

    /// If true, the request should set the "sameversionupdate" property for all apps in the update
    /// check request.
    pub offer_update_if_same_version: bool,
}

/// The AppEntry holds the data for the app whose request is currently being constructed.  An app
/// can only have a single cohort, update check, or ping, but may have multiple events.  Note that
/// while this object allows for no update check, no ping, and no events, that doesn't make sense
/// via the protocol.
///
/// This struct has ownership over it's members, so that they may later be moved out when the
/// request itself is built.
#[derive(Clone)]
struct AppEntry {
    /// The identifying data for the application.
    app: App,

    /// The updatecheck object if an update check should be performed, if None, the request will not
    /// include an updatecheck.
    update_check: Option<UpdateCheck>,

    /// Set to true if a ping should be send.
    ping: bool,

    /// Any events that need to be sent to the Omaha service.
    events: Vec<Event>,
}

impl AppEntry {
    /// Basic constructor for the AppEntry.  All AppEntries MUST have an App and a Cohort,
    /// everything else can be omitted.
    fn new(app: &App) -> AppEntry {
        AppEntry { app: app.clone(), update_check: None, ping: false, events: Vec::new() }
    }
}

/// Conversion method to construct a ProtocolApp from an AppEntry.  This consumes the entry, moving
/// it's members into the generated ProtocolApp.
impl From<AppEntry> for ProtocolApp {
    fn from(entry: AppEntry) -> ProtocolApp {
        if entry.update_check.is_none() && entry.events.is_empty() && !entry.ping {
            warn!(
                "Generated protocol::request for {} has no update check, ping, or events",
                entry.app.id
            );
        }
        let ping = if entry.ping {
            let UserCounting::ClientRegulatedByDate(days) = entry.app.user_counting;
            Some(Ping { date_last_active: days, date_last_roll_call: days })
        } else {
            None
        };
        ProtocolApp {
            id: entry.app.id,
            version: entry.app.version.to_string(),
            fingerprint: entry.app.fingerprint,
            cohort: Some(entry.app.cohort),
            update_check: entry.update_check,
            events: entry.events,
            ping,
            extra_fields: entry.app.extra_fields,
        }
    }
}

/// The RequestBuilder is used to create the protocol requests.  Each request is represented by an
/// instance of protocol::request::Request.
pub struct RequestBuilder<'a> {
    // The static data identifying the updater binary.
    config: &'a Config,

    // The parameters that control how this request is to be made.
    params: RequestParams,

    // The applications to include in this request, with their associated update checks, pings, and
    // events to report.
    app_entries: Vec<AppEntry>,

    request_id: Option<GUID>,
    session_id: Option<GUID>,
}

/// The RequestBuilder is a stateful builder for protocol::request::Request objects.  After being
/// instantiated with the base parameters for the current request, it has functions for accumulating
/// an update check, a ping, and multiple events for individual App objects.
///
/// The 'add_*()' functions are all insensitive to order for a given App and it's Cohort.  However,
/// if multiple different App entries are used, then order matters.  The order in the request is
/// the order that the Apps are added to the RequestBuilder.
///
/// Further, the cohort is only captured on the _first_ time a given App is added to the request.
/// If, for some reason, the same App is added twice, but with a different cohort, the latter cohort
/// is ignored.
///
/// The operation being added (update check, ping, or event) is added to the existing App.  The app
/// maintains its existing place in the list of Apps to be added to the request.
impl<'a> RequestBuilder<'a> {
    /// Constructor for creating a new RequestBuilder based on the Updater configuration and the
    /// parameters for the current request.
    pub fn new(config: &'a Config, params: &RequestParams) -> Self {
        RequestBuilder {
            config,
            params: params.clone(),
            app_entries: Vec::new(),
            request_id: None,
            session_id: None,
        }
    }

    /// Insert the given app (with its cohort), and run the associated closure on it.  If the app
    /// already exists in the request (by app id), just run the closure on the AppEntry.
    fn insert_and_modify_entry<F>(&mut self, app: &App, modify: F)
    where
        F: FnOnce(&mut AppEntry),
    {
        if let Some(app_entry) = self.app_entries.iter_mut().find(|e| e.app.id == app.id) {
            // found an existing App in the Vec, so just run the closure on this AppEntry.
            modify(app_entry);
        } else {
            // The App wasn't found, so add it to the list after running the closure on a newly
            // generated AppEntry for this App.
            let mut app_entry = AppEntry::new(app);
            modify(&mut app_entry);
            self.app_entries.push(app_entry);
        }
    }

    /// This function adds an update check for the given App, in the given Cohort.  This function is
    /// an idempotent accumulator, in that it only once adds the App with it's associated Cohort to
    /// the request.  Afterward, it just adds the update check to the App.
    pub fn add_update_check(mut self, app: &App) -> Self {
        let update_check = UpdateCheck {
            disabled: self.params.disable_updates,
            offer_update_if_same_version: self.params.offer_update_if_same_version,
        };

        self.insert_and_modify_entry(app, |entry| {
            entry.update_check = Some(update_check);
        });
        self
    }

    /// This function adds a Ping for the given App, in the given Cohort.  This function is an
    /// idempotent accumulator, in that it only once adds the App with it's associated Cohort to the
    /// request.  Afterward, it just marks the App as needing a Ping.
    pub fn add_ping(mut self, app: &App) -> Self {
        self.insert_and_modify_entry(app, |entry| {
            entry.ping = true;
        });
        self
    }

    /// This function adds an Event for the given App, in the given Cohort.  This function is an
    /// idempotent accumulator, in that it only once adds the App with it's associated Cohort to the
    /// request.  Afterward, it just adds the Event to the App.
    pub fn add_event(mut self, app: &App, event: Event) -> Self {
        self.insert_and_modify_entry(app, |entry| {
            entry.events.push(event);
        });
        self
    }

    /// Set the request id of the request.
    pub fn request_id(self, request_id: GUID) -> Self {
        Self { request_id: Some(request_id), ..self }
    }

    /// Set the session id of the request.
    pub fn session_id(self, session_id: GUID) -> Self {
        Self { session_id: Some(session_id), ..self }
    }

    /// This function constructs the protocol::request::Request object from this Builder.
    ///
    /// Note that the builder is not consumed in the process, and can be used afterward.
    pub fn build(
        &self,
        cup_handler: Option<&impl Cupv2RequestHandler>,
    ) -> Result<(http::Request<hyper::Body>, Option<RequestMetadata>)> {
        let (intermediate, request_metadata) = self.build_intermediate(cup_handler)?;
        if self.app_entries.iter().any(|app| app.update_check.is_some()) {
            info!("Building Request: {}", intermediate);
        }
        Ok((Into::<Result<http::Request<hyper::Body>>>::into(intermediate)?, request_metadata))
    }

    /// Helper function that constructs the request body from the builder.
    fn build_intermediate(
        &self,
        cup_handler: Option<&impl Cupv2RequestHandler>,
    ) -> Result<(Intermediate, Option<RequestMetadata>)> {
        let mut headers = vec![
            // Set the content-type to be JSON.
            (http::header::CONTENT_TYPE.as_str(), "application/json".to_string()),
            // The updater name header is always set directly from the name in the configuration
            (HEADER_UPDATER_NAME, self.config.updater.name.clone()),
            // The interactivity header is set based on the source of the request that's set in
            // the request params
            (
                HEADER_INTERACTIVITY,
                match self.params.source {
                    InstallSource::OnDemand => "fg".to_string(),
                    InstallSource::ScheduledTask => "bg".to_string(),
                },
            ),
        ];
        // And the app id header is based on the first app id in the request.
        // TODO: Send all app ids, or only send the first based on configuration.
        if let Some(main_app) = self.app_entries.first() {
            headers.push((HEADER_APP_ID, main_app.app.id.clone()));
        }

        let apps = self.app_entries.iter().cloned().map(ProtocolApp::from).collect();

        let mut intermediate = Intermediate {
            uri: self.config.service_url.clone(),
            headers,
            body: RequestWrapper {
                request: Request {
                    protocol_version: PROTOCOL_V3.to_string(),
                    updater: self.config.updater.name.clone(),
                    updater_version: self.config.updater.version.to_string(),
                    install_source: self.params.source,
                    is_machine: true,
                    request_id: self.request_id.clone(),
                    session_id: self.session_id.clone(),
                    os: self.config.os.clone(),
                    apps,
                },
            },
        };

        let request_metadata = match cup_handler.as_ref() {
            Some(handler) => Some(handler.decorate_request(&mut intermediate)?),
            _ => None,
        };

        Ok((intermediate, request_metadata))
    }
}

/// As the name implies, this is an intermediate that can be used to construct an http::Request from
/// the data that's in the Builder.  It allows for type-aware inspection of the constructed protocol
/// request, as well as the full construction of the http request (uri, headers, body).
///
/// This struct owns all of it's data, so that they can be moved directly into the constructed http
/// request.
#[derive(Debug)]
pub struct Intermediate {
    /// The URI for the http request.
    pub uri: String,

    /// The http request headers, in key:&str=value:String pairs
    pub headers: Vec<(&'static str, String)>,

    /// The request body, still in object form as a RequestWrapper
    pub body: RequestWrapper,
}

impl Intermediate {
    pub fn serialize_body(&self) -> serde_json::Result<Vec<u8>> {
        serde_json::to_vec(&self.body)
    }
}

impl Display for Intermediate {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        writeln!(f, "uri: {} ", self.uri)?;
        for (name, value) in &self.headers {
            writeln!(f, "header: {}={}", name, value)?;
        }
        match serde_json::to_value(&self.body) {
            Ok(value) => writeln!(f, "body: {:#}", value),
            Err(e) => writeln!(f, "err: {}", e),
        }
    }
}

impl From<Intermediate> for Result<http::Request<hyper::Body>> {
    fn from(intermediate: Intermediate) -> Self {
        let mut builder = hyper::Request::post(&intermediate.uri);
        for (key, value) in &intermediate.headers {
            builder = builder.header(*key, value);
        }

        let request = builder.body(intermediate.serialize_body()?.into())?;
        Ok(request)
    }
}

impl CupRequest for Intermediate {
    fn get_uri(&self) -> &str {
        &self.uri
    }
    fn set_uri(&mut self, uri: String) {
        self.uri = uri;
    }
    fn get_serialized_body(&self) -> serde_json::Result<Vec<u8>> {
        self.serialize_body()
    }
}
