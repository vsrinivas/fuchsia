// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The update_check module contains the structures and functions for performing a single update
/// check with Omaha.
use crate::{
    common::{ProtocolState, UpdateCheckSchedule, UserCounting},
    protocol::Cohort,
    storage::{Storage, StorageExt},
    time::PartialComplexTime,
};
use log::error;
use std::convert::TryFrom;
use std::time::Duration;

// These are the keys used to persist data to storage.
pub const LAST_UPDATE_TIME: &str = "last_update_time";
pub const SERVER_DICTATED_POLL_INTERVAL: &str = "server_dictated_poll_interval";

/// The Context provides the protocol context for a given update check operation.  This is
/// information that's passed to the Policy to allow it to properly reason about what can and cannot
/// be done at this time.
#[derive(Clone, Debug)]
pub struct Context {
    /// The last-computed time to next check for an update.
    pub schedule: UpdateCheckSchedule,

    /// The state of the protocol (retries, errors, etc.) as of the last update check that was
    /// attempted.
    pub state: ProtocolState,
}

impl Context {
    /// Load and initialize update check context from persistent storage.
    pub async fn load(storage: &impl Storage) -> Self {
        let last_update_time =
            storage.get_time(LAST_UPDATE_TIME).await.map(PartialComplexTime::Wall);
        let server_dictated_poll_interval = storage
            .get_int(SERVER_DICTATED_POLL_INTERVAL)
            .await
            .and_then(|t| u64::try_from(t).ok())
            .map(Duration::from_micros);
        Context {
            schedule: UpdateCheckSchedule::builder().last_time(last_update_time).build(),
            state: ProtocolState { server_dictated_poll_interval, ..Default::default() },
        }
    }

    /// Persist data in Context to |storage|, will try to set all of them to storage even if
    /// previous set fails.
    /// It will NOT call commit() on |storage|, caller is responsible to call commit().
    pub async fn persist<'a>(&'a self, storage: &'a mut impl Storage) {
        if let Err(e) = storage
            .set_option_int(
                LAST_UPDATE_TIME,
                self.schedule
                    .last_update_time
                    .and_then(PartialComplexTime::checked_to_micros_since_epoch),
            )
            .await
        {
            error!("Unable to persist {}: {}", LAST_UPDATE_TIME, e);
        }

        if let Err(e) = storage
            .set_option_int(
                SERVER_DICTATED_POLL_INTERVAL,
                self.state
                    .server_dictated_poll_interval
                    .map(|t| t.as_micros())
                    .and_then(|t| i64::try_from(t).ok()),
            )
            .await
        {
            error!("Unable to persist {}: {}", SERVER_DICTATED_POLL_INTERVAL, e);
        }
    }
}

/// The response context from the update check contains any extra information that Omaha returns to
/// the client, separate from the data about a particular app itself.
#[derive(Debug)]
pub struct Response {
    /// The set of responses for all the apps in the request.
    pub app_responses: Vec<AppResponse>,
}

/// For each application that had an update check performed, a new App (potentially with new Cohort
/// and UserCounting data) and a corresponding response Action are returned from the update check.
#[derive(Debug)]
pub struct AppResponse {
    /// The returned information about an application.
    pub app_id: String,

    /// Cohort data returned from Omaha
    pub cohort: Cohort,

    pub user_counting: UserCounting,

    /// The resultant action of its update check.
    pub result: Action,
}

/// The Action is the result of an update check for a single App.  This is just informational, for
/// the purposes of updating the protocol state.  Any update action should already have been taken
/// by the Installer.
#[derive(Debug, Clone, PartialEq)]
pub enum Action {
    /// Omaha's response was "no update"
    NoUpdate,

    /// Policy deferred the update.  The update check was successful, and Omaha returned that an
    /// update is available, but it is not able to be acted on at this time.
    DeferredByPolicy,

    /// Policy Denied the update.  The update check was successful, and Omaha returned that an
    /// update is available, but it is not allowed to be installed per Policy.
    DeniedByPolicy,

    /// The install process encountered an error.
    /// TODO: Attach an error to this
    InstallPlanExecutionError,

    /// An update was performed.
    Updated,
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::storage::MemStorage;
    use futures::executor::block_on;

    #[test]
    fn test_load_context() {
        block_on(async {
            let mut storage = MemStorage::new();
            let last_update_time = 123456789;
            let poll_interval = Duration::from_micros(56789u64);
            storage.set_int(LAST_UPDATE_TIME, last_update_time).await.unwrap();
            storage
                .set_int(SERVER_DICTATED_POLL_INTERVAL, poll_interval.as_micros() as i64)
                .await
                .unwrap();

            let context = Context::load(&storage).await;

            let last_update_time = PartialComplexTime::from_micros_since_epoch(last_update_time);
            assert_eq!(context.schedule.last_update_time, Some(last_update_time));
            assert_eq!(context.state.server_dictated_poll_interval, Some(poll_interval));
        });
    }

    #[test]
    fn test_load_context_empty_storage() {
        block_on(async {
            let storage = MemStorage::new();
            let context = Context::load(&storage).await;
            assert_eq!(None, context.schedule.last_update_time);
            assert_eq!(None, context.state.server_dictated_poll_interval);
        });
    }

    #[test]
    fn test_persist_context() {
        block_on(async {
            let mut storage = MemStorage::new();
            let last_update_time = PartialComplexTime::from_micros_since_epoch(123456789);
            let server_dictated_poll_interval = Some(Duration::from_micros(56789));
            let context = Context {
                schedule: UpdateCheckSchedule::builder().last_time(last_update_time).build(),
                state: ProtocolState { server_dictated_poll_interval, ..ProtocolState::default() },
            };
            context.persist(&mut storage).await;
            assert_eq!(Some(123456789), storage.get_int(LAST_UPDATE_TIME).await);
            assert_eq!(Some(56789), storage.get_int(SERVER_DICTATED_POLL_INTERVAL).await);
            assert_eq!(false, storage.committed());
        });
    }

    #[test]
    fn test_persist_context_remove_poll_interval() {
        block_on(async {
            let mut storage = MemStorage::new();
            let last_update_time = PartialComplexTime::from_micros_since_epoch(123456789);
            storage.set_int(SERVER_DICTATED_POLL_INTERVAL, 987654).await.unwrap();

            let context = Context {
                schedule: UpdateCheckSchedule::builder().last_time(last_update_time).build(),
                state: ProtocolState {
                    server_dictated_poll_interval: None,
                    ..ProtocolState::default()
                },
            };
            context.persist(&mut storage).await;
            assert_eq!(Some(123456789), storage.get_int(LAST_UPDATE_TIME).await);
            assert_eq!(None, storage.get_int(SERVER_DICTATED_POLL_INTERVAL).await);
            assert_eq!(false, storage.committed());
        });
    }
}
