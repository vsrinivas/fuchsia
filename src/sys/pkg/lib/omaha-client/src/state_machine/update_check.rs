// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The update_check module contains the structures and functions for performing a single update
/// check with Omaha.
use crate::{
    clock,
    common::{ProtocolState, UpdateCheckSchedule, UserCounting},
    protocol::Cohort,
    state_machine::time::{i64_to_time, time_to_i64},
    storage::Storage,
};
use log::error;
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
        let micros = storage.get_int(LAST_UPDATE_TIME).await.unwrap_or(0);
        let last_update_time = i64_to_time(micros);
        let server_dictated_poll_interval = storage
            .get_int(SERVER_DICTATED_POLL_INTERVAL)
            .await
            .map(|micros| Duration::from_micros(micros as u64));
        Context {
            schedule: UpdateCheckSchedule {
                last_update_time,
                next_update_time: clock::now(),
                next_update_window_start: clock::now(),
            },
            state: ProtocolState { server_dictated_poll_interval, ..ProtocolState::default() },
        }
    }

    /// Persist data in Context to |storage|, will try to set all of them to storage even if
    /// previous set fails.
    /// It will NOT call commit() on |storage|, caller is responsible to call commit().
    pub async fn persist<'a>(&'a self, storage: &'a mut impl Storage) {
        let micros = time_to_i64(self.schedule.last_update_time);
        if let Err(e) = storage.set_int(LAST_UPDATE_TIME, micros).await {
            error!("Unable to persist {}: {}", LAST_UPDATE_TIME, e);
        }

        if let Some(interval) = &self.state.server_dictated_poll_interval {
            let interval = interval.as_micros() as i64;
            if let Err(e) = storage.set_int(SERVER_DICTATED_POLL_INTERVAL, interval).await {
                error!("Unable to persist {}: {}", SERVER_DICTATED_POLL_INTERVAL, e);
            }
        } else {
            if let Err(e) = storage.remove(SERVER_DICTATED_POLL_INTERVAL).await {
                error!("Unable to remove {}: {}", SERVER_DICTATED_POLL_INTERVAL, e);
            }
        }
    }
}

/// The response context from the update check contains any extra information that Omaha returns to
/// the client, separate from the data about a particular app itself.
#[derive(Debug)]
pub struct Response {
    /// The set of responses for all the apps in the request.
    pub app_responses: Vec<AppResponse>,

    /// If Omaha dictated that a longer poll interval be used, it will be reported here.
    pub server_dictated_poll_interval: Option<Duration>,
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
    use std::time::SystemTime;

    #[test]
    fn test_load_context() {
        block_on(async {
            let mut storage = MemStorage::new();
            let last_update_time = i64_to_time(123456789);
            storage.set_int(LAST_UPDATE_TIME, time_to_i64(last_update_time)).await.unwrap();
            let poll_interval = Duration::from_micros(56789);
            storage
                .set_int(SERVER_DICTATED_POLL_INTERVAL, poll_interval.as_micros() as i64)
                .await
                .unwrap();

            let context = Context::load(&storage).await;
            assert_eq!(last_update_time, context.schedule.last_update_time);
            assert_eq!(Some(poll_interval), context.state.server_dictated_poll_interval);
        });
    }

    #[test]
    fn test_load_context_empty_storage() {
        block_on(async {
            let storage = MemStorage::new();
            let context = Context::load(&storage).await;
            assert_eq!(SystemTime::UNIX_EPOCH, context.schedule.last_update_time);
            assert_eq!(None, context.state.server_dictated_poll_interval);
        });
    }

    #[test]
    fn test_persist_context() {
        block_on(async {
            let mut storage = MemStorage::new();
            let last_update_time = i64_to_time(123456789);
            let server_dictated_poll_interval = Some(Duration::from_micros(56789));
            let context = Context {
                schedule: UpdateCheckSchedule {
                    last_update_time,
                    next_update_time: clock::now(),
                    next_update_window_start: clock::now(),
                },
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
            let last_update_time = i64_to_time(123456789);
            storage.set_int(SERVER_DICTATED_POLL_INTERVAL, 987654).await.unwrap();

            let context = Context {
                schedule: UpdateCheckSchedule {
                    last_update_time,
                    next_update_time: clock::now(),
                    next_update_window_start: clock::now(),
                },
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
