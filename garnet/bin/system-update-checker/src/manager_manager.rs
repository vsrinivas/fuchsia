// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::apply::{apply_system_update, Initiator};
use crate::check::{check_for_system_update, SystemUpdateStatus};
use failure::{Error, ResultExt};
use fidl_fuchsia_update::{CheckStartedResult, ManagerState};
use fuchsia_async as fasync;
use fuchsia_merkle::Hash;
use fuchsia_syslog::{fx_log_err, fx_log_info};
use futures::future::BoxFuture;
use futures::prelude::*;
use parking_lot::Mutex;
use std::sync::Arc;

pub trait StateChangeCallback: Clone + Send + Sync + 'static {
    fn on_state_change(&self, new_state: ManagerState) -> Result<(), Error>;
}

#[derive(Clone)]
pub struct ManagerManager<C, A, S>
where
    C: UpdateChecker,
    A: UpdateApplier,
    S: StateChangeCallback,
{
    state: Arc<Mutex<ManagerManagerState<S>>>,
    update_checker: C,
    update_applier: A,
}

impl<C, A, S> ManagerManager<C, A, S>
where
    C: UpdateChecker,
    A: UpdateApplier,
    S: StateChangeCallback,
    Self: Clone,
{
    pub fn from_checker_and_applier(update_checker: C, update_applier: A) -> Self {
        Self {
            state: Arc::new(Mutex::new(ManagerManagerState::new())),
            update_checker,
            update_applier,
        }
    }

    pub fn try_start_update(
        &self,
        initiator: Initiator,
        callback: Option<S>,
    ) -> CheckStartedResult {
        let mut state = self.state.lock();
        match state.manager_state {
            ManagerState::Idle => {
                callback.map(|cb| state.temporary_callbacks.push(cb));
                state.advance_manager_state(ManagerState::CheckingForUpdates);
                let manager_manager = (*self).clone();
                // Spawn so that callers of this method are not blocked
                fasync::spawn(async move {
                    await!(manager_manager.do_system_update_check_and_return_to_idle(initiator))
                });
                CheckStartedResult::Started
            }
            _ => {
                callback.map(|cb| state.temporary_callbacks.push(cb));
                CheckStartedResult::InProgress
            }
        }
    }

    pub fn get_state(&self) -> ManagerState {
        self.state.lock().manager_state
    }

    pub fn add_permanent_callback(&self, callback: S) {
        self.state.lock().permanent_callbacks.push(callback);
    }

    async fn do_system_update_check_and_return_to_idle(&self, initiator: Initiator) {
        if let Err(e) = await!(self.do_system_update_check(initiator)) {
            fx_log_err!("update attempt failed: {}", e);
            self.state.lock().advance_manager_state(ManagerState::EncounteredError);
        }
        let mut state = self.state.lock();
        match state.manager_state {
            ManagerState::WaitingForReboot => fx_log_err!(
                "system-update-checker is in the WaitingForReboot state. \
                 This should not have happened, because the sytem-updater should \
                 have rebooted the device before it returned."
            ),
            _ => {
                state.advance_manager_state(ManagerState::Idle);
            }
        }
    }

    async fn do_system_update_check(&self, initiator: Initiator) -> Result<(), Error> {
        match await!(self.update_checker.check()).context("check_for_system_update failed")? {
            SystemUpdateStatus::UpToDate { system_image } => {
                fx_log_info!("current system_image merkle: {}", system_image);
                fx_log_info!("system_image is already up-to-date");
                return Ok(());
            }
            SystemUpdateStatus::UpdateAvailable { current_system_image, latest_system_image } => {
                fx_log_info!("current system_image merkle: {}", current_system_image);
                fx_log_info!("new system_image available: {}", latest_system_image);
                {
                    self.state.lock().advance_manager_state(ManagerState::PerformingUpdate);
                }
                await!(self.update_applier.apply(
                    current_system_image,
                    latest_system_image,
                    initiator
                ))
                .context("apply_system_update failed")?;
                // On success, system-updater reboots the system before returning, so this code
                // should never run. The only way to leave WaitingForReboot state is to restart
                // the component
                self.state.lock().advance_manager_state(ManagerState::WaitingForReboot);
            }
        }
        Ok(())
    }
}

struct ManagerManagerState<S>
where
    S: StateChangeCallback,
{
    permanent_callbacks: Vec<S>,
    temporary_callbacks: Vec<S>,
    manager_state: ManagerState,
}

impl<S> ManagerManagerState<S>
where
    S: StateChangeCallback,
{
    fn new() -> Self {
        ManagerManagerState {
            permanent_callbacks: vec![],
            temporary_callbacks: vec![],
            manager_state: ManagerState::Idle,
        }
    }

    fn advance_manager_state(&mut self, next_manager_state: ManagerState) {
        self.manager_state = next_manager_state;
        self.send_on_state();
        if self.manager_state == ManagerState::Idle {
            self.temporary_callbacks.clear();
        }
    }

    fn send_on_state(&mut self) {
        let manager_state = self.manager_state;
        self.permanent_callbacks.retain(|cb| cb.on_state_change(manager_state).is_ok());
        self.temporary_callbacks.retain(|cb| cb.on_state_change(manager_state).is_ok());
    }
}

// For mocking
pub trait UpdateChecker: Clone + Send + Sync + 'static {
    fn check(&self) -> BoxFuture<Result<SystemUpdateStatus, crate::errors::Error>>;
}

#[derive(Clone, Copy)]
pub struct RealUpdateChecker;

impl UpdateChecker for RealUpdateChecker {
    fn check(&self) -> BoxFuture<Result<SystemUpdateStatus, crate::errors::Error>> {
        check_for_system_update().boxed()
    }
}

// For mocking
pub trait UpdateApplier: Clone + Send + Sync + 'static {
    fn apply(
        &self,
        current_system_image: Hash,
        latest_system_image: Hash,
        initiator: Initiator,
    ) -> BoxFuture<Result<(), crate::errors::Error>>;
}

#[derive(Clone, Copy)]
pub struct RealUpdateApplier;

impl UpdateApplier for RealUpdateApplier {
    fn apply(
        &self,
        current_system_image: Hash,
        latest_system_image: Hash,
        initiator: Initiator,
    ) -> BoxFuture<Result<(), crate::errors::Error>> {
        apply_system_update(current_system_image, latest_system_image, initiator).boxed()
    }
}
