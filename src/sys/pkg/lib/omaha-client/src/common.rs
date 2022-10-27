// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The omaha_client::common module contains those types that are common to many parts of the
//! library.  Many of these don't belong to a specific sub-module.

use crate::{
    protocol::{self, request::InstallSource, Cohort},
    storage::Storage,
    time::PartialComplexTime,
};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fmt;
use std::time::Duration;
use tracing::error;
use typed_builder::TypedBuilder;
use version::Version;

/// Omaha has historically supported multiple methods of counting devices.  Currently, the
/// only recommended method is the Client Regulated - Date method.
///
/// See https://github.com/google/omaha/blob/HEAD/doc/ServerProtocolV3.md#client-regulated-counting-date-based
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub enum UserCounting {
    ClientRegulatedByDate(
        /// Date (sent by the server) of the last contact with Omaha.
        Option<u32>,
    ),
}

/// Helper implementation to bridge from the protocol to the internal representation for tracking
/// the data for client-regulated user counting.
impl From<Option<protocol::response::DayStart>> for UserCounting {
    fn from(opt_day_start: Option<protocol::response::DayStart>) -> Self {
        match opt_day_start {
            Some(day_start) => UserCounting::ClientRegulatedByDate(day_start.elapsed_days),
            None => UserCounting::ClientRegulatedByDate(None),
        }
    }
}

/// The App struct holds information about an application to perform an update check for.
#[derive(Clone, Debug, Eq, PartialEq, TypedBuilder)]
pub struct App {
    /// This is the app_id that Omaha uses to identify a given application.
    #[builder(setter(into))]
    pub id: String,

    /// This is the current version of the application.
    #[builder(setter(into))]
    pub version: Version,

    /// This is the fingerprint for the application package.
    ///
    /// See https://github.com/google/omaha/blob/HEAD/doc/ServerProtocolV3.md#packages--fingerprints
    #[builder(default)]
    #[builder(setter(into, strip_option))]
    pub fingerprint: Option<String>,

    /// The app's current cohort information (cohort id, hint, etc).  This is both provided to Omaha
    /// as well as returned by Omaha.
    #[builder(default)]
    pub cohort: Cohort,

    /// The app's current user-counting information.  This is both provided to Omaha as well as
    /// returned by Omaha.
    #[builder(default=UserCounting::ClientRegulatedByDate(None))]
    pub user_counting: UserCounting,

    /// Extra fields to include in requests to Omaha.  The client library does not inspect or
    /// operate on these, it just sends them to the service as part of the "app" objects in each
    /// request.
    #[builder(default)]
    #[builder(setter(into))]
    pub extra_fields: HashMap<String, String>,
}

/// Structure used to serialize per app data to be persisted.
/// Be careful when making changes to this struct to keep backward compatibility.
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct PersistedApp {
    pub cohort: Cohort,
    pub user_counting: UserCounting,
}

impl From<&App> for PersistedApp {
    fn from(app: &App) -> Self {
        PersistedApp { cohort: app.cohort.clone(), user_counting: app.user_counting.clone() }
    }
}

impl App {
    /// Load data from |storage|, only overwrite existing fields if data exists.
    pub async fn load<'a>(&'a mut self, storage: &'a impl Storage) {
        if let Some(app_json) = storage.get_string(&self.id).await {
            match serde_json::from_str::<PersistedApp>(&app_json) {
                Ok(persisted_app) => {
                    // Do not overwrite existing fields in app.
                    if self.cohort.id.is_none() {
                        self.cohort.id = persisted_app.cohort.id;
                    }
                    if self.cohort.hint.is_none() {
                        self.cohort.hint = persisted_app.cohort.hint;
                    }
                    if self.cohort.name.is_none() {
                        self.cohort.name = persisted_app.cohort.name;
                    }
                    if self.user_counting == UserCounting::ClientRegulatedByDate(None) {
                        self.user_counting = persisted_app.user_counting;
                    }
                }
                Err(e) => {
                    error!("Unable to deserialize PersistedApp from json {}: {}", app_json, e);
                }
            }
        }
    }

    /// Persist cohort and user counting to |storage|, will try to set all of them to storage even
    /// if previous set fails.
    /// It will NOT call commit() on |storage|, caller is responsible to call commit().
    pub async fn persist<'a>(&'a self, storage: &'a mut impl Storage) {
        let persisted_app = PersistedApp::from(self);
        match serde_json::to_string(&persisted_app) {
            Ok(json) => {
                if let Err(e) = storage.set_string(&self.id, &json).await {
                    error!("Unable to persist cohort id: {}", e);
                }
            }
            Err(e) => {
                error!("Unable to serialize PersistedApp {:?}: {}", persisted_app, e);
            }
        }
    }

    /// Get the current channel name from cohort name, returns empty string if no cohort name set
    /// for the app.
    pub fn get_current_channel(&self) -> &str {
        self.cohort.name.as_deref().unwrap_or("")
    }

    /// Get the target channel name from cohort hint, fallback to current channel if no hint.
    pub fn get_target_channel(&self) -> &str {
        self.cohort.hint.as_deref().unwrap_or_else(|| self.get_current_channel())
    }

    /// Set the cohort hint to |channel|.
    pub fn set_target_channel(&mut self, channel: Option<String>, id: Option<String>) {
        self.cohort.hint = channel;
        if let Some(id) = id {
            self.id = id;
        }
    }

    pub fn valid(&self) -> bool {
        !self.id.is_empty() && self.version != Version::from([0])
    }
}

/// Options controlling a single update check
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct CheckOptions {
    /// Was this check initiated by a person that's waiting for an answer?
    ///  This is used to ignore the background poll rate, and to be aggressive about
    ///  failing fast, so as not to hang on not receiving a response.
    pub source: InstallSource,
}

/// This describes the data around the scheduling of update checks
#[derive(Clone, Copy, Default, PartialEq, Eq, TypedBuilder)]
pub struct UpdateCheckSchedule {
    // TODO(fxb/64804): Theoretically last_update_time and last_update_check_time
    // do not need to coexist and we can do all the reporting we want via
    // last_update_time. However, the last update check metric doesn't (as currently
    // worded) match up with what last_update_time actually records.
    /// When the last update check was attempted (start time of the check process).
    #[builder(default, setter(into))]
    pub last_update_time: Option<PartialComplexTime>,

    /// When the last update check was attempted.
    #[builder(default, setter(into))]
    pub last_update_check_time: Option<PartialComplexTime>,

    /// When the next update should happen.
    #[builder(default, setter(into))]
    pub next_update_time: Option<CheckTiming>,
}

/// The fields used to describe the timing of the next update check.
///
/// This exists as a separate type mostly so that it can be moved around atomically, in a little bit
/// neater fashion than it could be if it was a tuple of `(PartialComplexTime, Option<Duration>)`.
#[derive(Clone, Copy, Debug, PartialEq, Eq, TypedBuilder)]
pub struct CheckTiming {
    /// The upper time bounds on when it should be performed (expressed as along those timelines
    /// that are valid based on currently known time quality).
    #[builder(setter(into))]
    pub time: PartialComplexTime,

    /// The minimum wait until the next check, regardless of the wall or monotonic time it should be
    /// performed at.  This is handled separately as it creates a lower bound vs. the upper bound(s)
    /// that the `time` field provides.
    #[builder(default, setter(strip_option))]
    pub minimum_wait: Option<Duration>,
}

/// Helper struct that provides a nicer format for Debug printing `Option` by dropping the
/// `Some(...)` that wraps its value, and instead uses the Display trait implementation of the
/// value.
///
/// Examples:
/// `"MyStruct { option_string_field: None }"`
/// `"MyStruct { option_string_field: "string field value" }"`
///
pub struct PrettyOptionDisplay<T>(pub Option<T>)
where
    T: fmt::Display;
impl<T> fmt::Display for PrettyOptionDisplay<T>
where
    T: fmt::Display,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match &self.0 {
            None => write!(f, "None"),
            Some(value) => fmt::Display::fmt(value, f),
        }
    }
}
impl<T> fmt::Debug for PrettyOptionDisplay<T>
where
    T: fmt::Display,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Display::fmt(self, f)
    }
}

/// The default Debug implementation for SystemTime will only print seconds since unix epoch, which
/// is not terribly useful in logs, so this prints a more human-relatable format.
///
/// e.g.
/// `UpdateCheckSchedule { last_update_time: None, next_uptime_time: None }`
/// `UpdateCheckSchedule { last_update_time: "2001-07-08 16:34:56.026 UTC (994518299.026420000)", next_uptime_time: None }`
impl fmt::Debug for UpdateCheckSchedule {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("UpdateCheckSchedule")
            .field("last_update_time", &PrettyOptionDisplay(self.last_update_time))
            .field("next_update_time", &PrettyOptionDisplay(self.next_update_time))
            .finish()
    }
}

impl fmt::Display for CheckTiming {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self.minimum_wait {
            None => fmt::Display::fmt(&self.time, f),
            Some(wait) => write!(f, "{} wait: {:?}", &self.time, &wait),
        }
    }
}

/// These hold the data maintained request-to-request so that the requirements for
/// backoffs, throttling, proxy use, etc. can all be properly maintained.  This is
/// NOT the state machine's internal state.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct ProtocolState {
    /// If the server has dictated the next poll interval, this holds what that
    /// interval is.
    pub server_dictated_poll_interval: Option<std::time::Duration>,

    /// The number of consecutive failed update checks.  Used to perform backoffs.
    pub consecutive_failed_update_checks: u32,

    /// The number of consecutive proxied requests.  Used to periodically not use
    /// proxies, in the case of an invalid proxy configuration.
    pub consecutive_proxied_requests: u32,
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        storage::MemStorage,
        time::{MockTimeSource, TimeSource},
    };
    use futures::executor::block_on;
    use pretty_assertions::assert_eq;
    use std::str::FromStr;
    use std::time::SystemTime;

    #[test]
    fn test_app_new_version() {
        let app = App::builder()
            .id("some_id")
            .version([1, 2])
            .cohort(Cohort::from_hint("some-channel"))
            .build();
        assert_eq!(app.id, "some_id");
        assert_eq!(app.version, [1, 2].into());
        assert_eq!(app.fingerprint, None);
        assert_eq!(app.cohort.hint, Some("some-channel".to_string()));
        assert_eq!(app.cohort.name, None);
        assert_eq!(app.cohort.id, None);
        assert_eq!(app.user_counting, UserCounting::ClientRegulatedByDate(None));
        assert!(app.extra_fields.is_empty(), "Extra fields are not empty");
    }

    #[test]
    fn test_app_with_fingerprint() {
        let app = App::builder()
            .id("some_id_2")
            .version([4, 6])
            .cohort(Cohort::from_hint("test-channel"))
            .fingerprint("some_fp")
            .build();
        assert_eq!(app.id, "some_id_2");
        assert_eq!(app.version, [4, 6].into());
        assert_eq!(app.fingerprint, Some("some_fp".to_string()));
        assert_eq!(app.cohort.hint, Some("test-channel".to_string()));
        assert_eq!(app.cohort.name, None);
        assert_eq!(app.cohort.id, None);
        assert_eq!(app.user_counting, UserCounting::ClientRegulatedByDate(None));
        assert!(app.extra_fields.is_empty(), "Extra fields are not empty");
    }

    #[test]
    fn test_app_with_user_counting() {
        let app = App::builder()
            .id("some_id_2")
            .version([4, 6])
            .cohort(Cohort::from_hint("test-channel"))
            .user_counting(UserCounting::ClientRegulatedByDate(Some(42)))
            .build();
        assert_eq!(app.id, "some_id_2");
        assert_eq!(app.version, [4, 6].into());
        assert_eq!(app.cohort.hint, Some("test-channel".to_string()));
        assert_eq!(app.cohort.name, None);
        assert_eq!(app.cohort.id, None);
        assert_eq!(app.user_counting, UserCounting::ClientRegulatedByDate(Some(42)));
        assert!(app.extra_fields.is_empty(), "Extra fields are not empty");
    }

    #[test]
    fn test_app_with_extras() {
        let app = App::builder()
            .id("some_id_2")
            .version([4, 6])
            .cohort(Cohort::from_hint("test-channel"))
            .extra_fields([
                ("key1".to_string(), "value1".to_string()),
                ("key2".to_string(), "value2".to_string()),
            ])
            .build();
        assert_eq!(app.id, "some_id_2");
        assert_eq!(app.version, [4, 6].into());
        assert_eq!(app.cohort.hint, Some("test-channel".to_string()));
        assert_eq!(app.cohort.name, None);
        assert_eq!(app.cohort.id, None);
        assert_eq!(app.user_counting, UserCounting::ClientRegulatedByDate(None));
        assert_eq!(app.extra_fields.len(), 2);
        assert_eq!(app.extra_fields["key1"], "value1");
        assert_eq!(app.extra_fields["key2"], "value2");
    }

    #[test]
    fn test_app_load() {
        block_on(async {
            let mut storage = MemStorage::new();
            let json = serde_json::json!({
            "cohort": {
                "cohort": "some_id",
                "cohorthint":"some_hint",
                "cohortname": "some_name"
            },
            "user_counting": {
                "ClientRegulatedByDate":123
            }});
            let json = serde_json::to_string(&json).unwrap();
            let mut app = App::builder().id("some_id").version([1, 2]).build();
            storage.set_string(&app.id, &json).await.unwrap();
            app.load(&storage).await;

            let cohort = Cohort {
                id: Some("some_id".to_string()),
                hint: Some("some_hint".to_string()),
                name: Some("some_name".to_string()),
            };
            assert_eq!(cohort, app.cohort);
            assert_eq!(UserCounting::ClientRegulatedByDate(Some(123)), app.user_counting);
        });
    }

    #[test]
    fn test_app_load_empty_storage() {
        block_on(async {
            let storage = MemStorage::new();
            let cohort = Cohort {
                id: Some("some_id".to_string()),
                hint: Some("some_hint".to_string()),
                name: Some("some_name".to_string()),
            };
            let mut app = App::builder()
                .id("some_id")
                .version([1, 2])
                .cohort(cohort)
                .user_counting(UserCounting::ClientRegulatedByDate(Some(123)))
                .build();
            app.load(&storage).await;

            // existing data not overwritten
            let cohort = Cohort {
                id: Some("some_id".to_string()),
                hint: Some("some_hint".to_string()),
                name: Some("some_name".to_string()),
            };
            assert_eq!(cohort, app.cohort);
            assert_eq!(UserCounting::ClientRegulatedByDate(Some(123)), app.user_counting);
        });
    }

    #[test]
    fn test_app_load_malformed() {
        block_on(async {
            let mut storage = MemStorage::new();
            let cohort = Cohort {
                id: Some("some_id".to_string()),
                hint: Some("some_hint".to_string()),
                name: Some("some_name".to_string()),
            };
            let mut app = App::builder()
                .id("some_id")
                .version([1, 2])
                .cohort(cohort)
                .user_counting(UserCounting::ClientRegulatedByDate(Some(123)))
                .build();
            storage.set_string(&app.id, "not a json").await.unwrap();
            app.load(&storage).await;

            // existing data not overwritten
            let cohort = Cohort {
                id: Some("some_id".to_string()),
                hint: Some("some_hint".to_string()),
                name: Some("some_name".to_string()),
            };
            assert_eq!(cohort, app.cohort);
            assert_eq!(UserCounting::ClientRegulatedByDate(Some(123)), app.user_counting);
        });
    }

    #[test]
    fn test_app_load_partial() {
        block_on(async {
            let mut storage = MemStorage::new();
            let json = serde_json::json!({
            "cohort": {
                "cohorthint":"some_hint_2",
                "cohortname": "some_name_2"
            },
            "user_counting": {
                "ClientRegulatedByDate":null
            }});
            let json = serde_json::to_string(&json).unwrap();
            let cohort = Cohort {
                id: Some("some_id".to_string()),
                hint: Some("some_hint".to_string()),
                name: Some("some_name".to_string()),
            };
            let mut app = App::builder()
                .id("some_id")
                .version([1, 2])
                .cohort(cohort)
                .user_counting(UserCounting::ClientRegulatedByDate(Some(123)))
                .build();
            storage.set_string(&app.id, &json).await.unwrap();
            app.load(&storage).await;

            // existing data not overwritten
            let cohort = Cohort {
                id: Some("some_id".to_string()),
                hint: Some("some_hint".to_string()),
                name: Some("some_name".to_string()),
            };
            assert_eq!(cohort, app.cohort);
            assert_eq!(UserCounting::ClientRegulatedByDate(Some(123)), app.user_counting);
        });
    }

    #[test]
    fn test_app_load_override() {
        block_on(async {
            let mut storage = MemStorage::new();
            let json = serde_json::json!({
            "cohort": {
                "cohort": "some_id_2",
                "cohorthint":"some_hint_2",
                "cohortname": "some_name_2"
            },
            "user_counting": {
                "ClientRegulatedByDate":123
            }});
            let json = serde_json::to_string(&json).unwrap();
            let cohort = Cohort {
                id: Some("some_id".to_string()),
                hint: Some("some_hint".to_string()),
                name: None,
            };
            let mut app = App::builder()
                .id("some_id")
                .version([1, 2])
                .cohort(cohort)
                .user_counting(UserCounting::ClientRegulatedByDate(Some(123)))
                .build();
            storage.set_string(&app.id, &json).await.unwrap();
            app.load(&storage).await;

            // existing data not overwritten
            let cohort = Cohort {
                id: Some("some_id".to_string()),
                hint: Some("some_hint".to_string()),
                name: Some("some_name_2".to_string()),
            };
            assert_eq!(cohort, app.cohort);
            assert_eq!(UserCounting::ClientRegulatedByDate(Some(123)), app.user_counting);
        });
    }

    #[test]
    fn test_app_persist() {
        block_on(async {
            let mut storage = MemStorage::new();
            let cohort = Cohort {
                id: Some("some_id".to_string()),
                hint: Some("some_hint".to_string()),
                name: Some("some_name".to_string()),
            };
            let app = App::builder()
                .id("some_id")
                .version([1, 2])
                .cohort(cohort)
                .user_counting(UserCounting::ClientRegulatedByDate(Some(123)))
                .build();
            app.persist(&mut storage).await;

            let expected = serde_json::json!({
            "cohort": {
                "cohort": "some_id",
                "cohorthint":"some_hint",
                "cohortname": "some_name"
            },
            "user_counting": {
                "ClientRegulatedByDate":123
            }});
            let json = storage.get_string(&app.id).await.unwrap();
            assert_eq!(expected, serde_json::Value::from_str(&json).unwrap());
            assert!(!storage.committed());
        });
    }

    #[test]
    fn test_app_persist_empty() {
        block_on(async {
            let mut storage = MemStorage::new();
            let cohort = Cohort { id: None, hint: None, name: None };
            let app = App::builder().id("some_id").version([1, 2]).cohort(cohort).build();
            app.persist(&mut storage).await;

            let expected = serde_json::json!({
            "cohort": {},
            "user_counting": {
                "ClientRegulatedByDate":null
            }});
            let json = storage.get_string(&app.id).await.unwrap();
            assert_eq!(expected, serde_json::Value::from_str(&json).unwrap());
            assert!(!storage.committed());
        });
    }

    #[test]
    fn test_app_get_current_channel() {
        let cohort = Cohort { name: Some("current-channel-123".to_string()), ..Cohort::default() };
        let app = App::builder().id("some_id").version([0, 1]).cohort(cohort).build();
        assert_eq!("current-channel-123", app.get_current_channel());
    }

    #[test]
    fn test_app_get_current_channel_default() {
        let app = App::builder().id("some_id").version([0, 1]).build();
        assert_eq!("", app.get_current_channel());
    }

    #[test]
    fn test_app_get_target_channel() {
        let cohort = Cohort::from_hint("target-channel-456");
        let app = App::builder().id("some_id").version([0, 1]).cohort(cohort).build();
        assert_eq!("target-channel-456", app.get_target_channel());
    }

    #[test]
    fn test_app_get_target_channel_fallback() {
        let cohort = Cohort { name: Some("current-channel-123".to_string()), ..Cohort::default() };
        let app = App::builder().id("some_id").version([0, 1]).cohort(cohort).build();
        assert_eq!("current-channel-123", app.get_target_channel());
    }

    #[test]
    fn test_app_get_target_channel_default() {
        let app = App::builder().id("some_id").version([0, 1]).build();
        assert_eq!("", app.get_target_channel());
    }

    #[test]
    fn test_app_set_target_channel() {
        let mut app = App::builder().id("some_id").version([0, 1]).build();
        assert_eq!("", app.get_target_channel());
        app.set_target_channel(Some("new-target-channel".to_string()), None);
        assert_eq!("new-target-channel", app.get_target_channel());
        app.set_target_channel(None, None);
        assert_eq!("", app.get_target_channel());
    }

    #[test]
    fn test_app_set_target_channel_and_id() {
        let mut app = App::builder().id("some_id").version([0, 1]).build();
        assert_eq!("", app.get_target_channel());
        app.set_target_channel(Some("new-target-channel".to_string()), Some("new-id".to_string()));
        assert_eq!("new-target-channel", app.get_target_channel());
        assert_eq!("new-id", app.id);
        app.set_target_channel(None, None);
        assert_eq!("", app.get_target_channel());
        assert_eq!("new-id", app.id);
    }

    #[test]
    fn test_app_valid() {
        let app = App::builder().id("some_id").version([0, 1]).build();
        assert!(app.valid());
    }

    #[test]
    fn test_app_not_valid() {
        let app = App::builder().id("").version([0, 1]).build();
        assert!(!app.valid());
        let app = App::builder().id("some_id").version([0]).build();
        assert!(!app.valid());
    }

    #[test]
    fn test_pretty_option_display_with_none() {
        assert_eq!("None", format!("{:?}", PrettyOptionDisplay(Option::<String>::None)));
    }

    #[test]
    fn test_pretty_option_display_with_some() {
        assert_eq!("this is a test", format!("{:?}", PrettyOptionDisplay(Some("this is a test"))));
    }

    #[test]
    fn test_update_check_schedule_debug_with_defaults() {
        assert_eq!(
            "UpdateCheckSchedule { \
                last_update_time: None, \
                next_update_time: None \
            }",
            format!("{:?}", UpdateCheckSchedule::default())
        );
    }

    #[test]
    fn test_update_check_schedule_debug_with_values() {
        let mock_time = MockTimeSource::new_from_now();
        let last = mock_time.now();
        let next = last + Duration::from_secs(1000);
        assert_eq!(
            format!(
                "UpdateCheckSchedule {{ last_update_time: {}, next_update_time: {} }}",
                PartialComplexTime::from(last),
                next
            ),
            format!(
                "{:?}",
                UpdateCheckSchedule::builder()
                    .last_update_time(last)
                    .next_update_time(CheckTiming::builder().time(next).build())
                    .build()
            )
        );
    }

    #[test]
    fn test_update_check_schedule_builder_all_fields() {
        let mock_time = MockTimeSource::new_from_now();
        let now = PartialComplexTime::from(mock_time.now());
        assert_eq!(
            UpdateCheckSchedule::builder()
                .last_update_time(PartialComplexTime::from(
                    SystemTime::UNIX_EPOCH + Duration::from_secs(100000)
                ))
                .next_update_time(
                    CheckTiming::builder().time(now).minimum_wait(Duration::from_secs(100)).build()
                )
                .build(),
            UpdateCheckSchedule {
                last_update_time: Some(PartialComplexTime::from(
                    SystemTime::UNIX_EPOCH + Duration::from_secs(100000)
                )),
                next_update_time: Some(CheckTiming {
                    time: now,
                    minimum_wait: Some(Duration::from_secs(100))
                }),
                ..Default::default()
            }
        );
    }

    #[test]
    fn test_update_check_schedule_builder_all_fields_from_options() {
        let next_time = PartialComplexTime::from(MockTimeSource::new_from_now().now());
        assert_eq!(
            UpdateCheckSchedule::builder()
                .last_update_time(Some(PartialComplexTime::from(
                    SystemTime::UNIX_EPOCH + Duration::from_secs(100000)
                )))
                .next_update_time(Some(
                    CheckTiming::builder()
                        .time(next_time)
                        .minimum_wait(Duration::from_secs(100))
                        .build()
                ))
                .build(),
            UpdateCheckSchedule {
                last_update_time: Some(PartialComplexTime::from(
                    SystemTime::UNIX_EPOCH + Duration::from_secs(100000)
                )),
                next_update_time: Some(CheckTiming {
                    time: next_time,
                    minimum_wait: Some(Duration::from_secs(100))
                }),
                ..Default::default()
            }
        );
    }

    #[test]
    fn test_update_check_schedule_builder_subset_fields() {
        assert_eq!(
            UpdateCheckSchedule::builder()
                .last_update_time(PartialComplexTime::from(
                    SystemTime::UNIX_EPOCH + Duration::from_secs(100000)
                ))
                .build(),
            UpdateCheckSchedule {
                last_update_time: Some(PartialComplexTime::from(
                    SystemTime::UNIX_EPOCH + Duration::from_secs(100000)
                )),
                ..Default::default()
            }
        );

        let next_time = PartialComplexTime::from(MockTimeSource::new_from_now().now());
        assert_eq!(
            UpdateCheckSchedule::builder()
                .next_update_time(
                    CheckTiming::builder()
                        .time(next_time)
                        .minimum_wait(Duration::from_secs(5))
                        .build()
                )
                .build(),
            UpdateCheckSchedule {
                next_update_time: Some(CheckTiming {
                    time: next_time,
                    minimum_wait: Some(Duration::from_secs(5))
                }),
                ..Default::default()
            }
        );
    }

    #[test]
    fn test_update_check_schedule_builder_defaults_are_same_as_default_impl() {
        assert_eq!(
            UpdateCheckSchedule::builder().build(),
            UpdateCheckSchedule { ..Default::default() }
        );
    }
}
