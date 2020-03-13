// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {event_queue::Event, fidl_fuchsia_update as fidl};

#[derive(Clone, Debug, PartialEq)]
pub enum State {
    CheckingForUpdates,
    ErrorCheckingForUpdate,
    NoUpdateAvailable,
    InstallationDeferredByPolicy(InstallationDeferredData),
    InstallingUpdate(InstallingData),
    WaitingForReboot(InstallingData),
    InstallationError(InstallationErrorData),
}

impl State {
    /// Returns true if this state is an error state.
    pub fn is_error(&self) -> bool {
        match self {
            State::ErrorCheckingForUpdate | State::InstallationError(_) => true,
            State::CheckingForUpdates
            | State::NoUpdateAvailable
            | State::InstallationDeferredByPolicy(_)
            | State::InstallingUpdate(_)
            | State::WaitingForReboot(_) => false,
        }
    }
}

impl Event for State {
    fn can_merge(&self, other: &State) -> bool {
        if self == other {
            return true;
        }
        // Merge states that have the same update info but different installation progress
        if let State::InstallingUpdate(InstallingData { update: update0, .. }) = self {
            if let State::InstallingUpdate(InstallingData { update: update1, .. }) = other {
                return update0 == update1;
            }
        }
        false
    }
}

impl Into<fidl::State> for State {
    fn into(self) -> fidl::State {
        match self {
            State::CheckingForUpdates => {
                fidl::State::CheckingForUpdates(fidl::CheckingForUpdatesData {})
            }
            State::ErrorCheckingForUpdate => {
                fidl::State::ErrorCheckingForUpdate(fidl::ErrorCheckingForUpdateData {})
            }
            State::NoUpdateAvailable => {
                fidl::State::NoUpdateAvailable(fidl::NoUpdateAvailableData {})
            }
            State::InstallationDeferredByPolicy(data) => {
                fidl::State::InstallationDeferredByPolicy(data.into())
            }
            State::InstallingUpdate(data) => fidl::State::InstallingUpdate(data.into()),
            State::WaitingForReboot(data) => fidl::State::WaitingForReboot(data.into()),
            State::InstallationError(data) => fidl::State::InstallationError(data.into()),
        }
    }
}

impl From<fidl::State> for State {
    fn from(fidl_state: fidl::State) -> Self {
        match fidl_state {
            fidl::State::CheckingForUpdates(_) => State::CheckingForUpdates,
            fidl::State::ErrorCheckingForUpdate(_) => State::ErrorCheckingForUpdate,
            fidl::State::NoUpdateAvailable(_) => State::NoUpdateAvailable,
            fidl::State::InstallationDeferredByPolicy(data) => {
                State::InstallationDeferredByPolicy(data.into())
            }
            fidl::State::InstallingUpdate(data) => State::InstallingUpdate(data.into()),
            fidl::State::WaitingForReboot(data) => State::WaitingForReboot(data.into()),
            fidl::State::InstallationError(data) => State::InstallationError(data.into()),
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct InstallationErrorData {
    pub update: Option<UpdateInfo>,
    pub installation_progress: Option<InstallationProgress>,
}
impl Into<fidl::InstallationErrorData> for InstallationErrorData {
    fn into(self) -> fidl::InstallationErrorData {
        fidl::InstallationErrorData {
            update: self.update.map(|ext| ext.into()),
            installation_progress: self.installation_progress.map(|ext| ext.into()),
        }
    }
}
impl From<fidl::InstallationErrorData> for InstallationErrorData {
    fn from(data: fidl::InstallationErrorData) -> Self {
        Self {
            update: data.update.map(|o| o.into()),
            installation_progress: data.installation_progress.map(|o| o.into()),
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct InstallationProgress {
    pub fraction_completed: Option<f32>,
}
impl Into<fidl::InstallationProgress> for InstallationProgress {
    fn into(self) -> fidl::InstallationProgress {
        fidl::InstallationProgress { fraction_completed: self.fraction_completed }
    }
}
impl From<fidl::InstallationProgress> for InstallationProgress {
    fn from(progress: fidl::InstallationProgress) -> Self {
        Self { fraction_completed: progress.fraction_completed }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct InstallingData {
    pub update: Option<UpdateInfo>,
    pub installation_progress: Option<InstallationProgress>,
}
impl Into<fidl::InstallingData> for InstallingData {
    fn into(self) -> fidl::InstallingData {
        fidl::InstallingData {
            update: self.update.map(|ext| ext.into()),
            installation_progress: self.installation_progress.map(|ext| ext.into()),
        }
    }
}
impl From<fidl::InstallingData> for InstallingData {
    fn from(data: fidl::InstallingData) -> Self {
        Self {
            update: data.update.map(|o| o.into()),
            installation_progress: data.installation_progress.map(|o| o.into()),
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct InstallationDeferredData {
    pub update: Option<UpdateInfo>,
}
impl Into<fidl::InstallationDeferredData> for InstallationDeferredData {
    fn into(self) -> fidl::InstallationDeferredData {
        fidl::InstallationDeferredData { update: self.update.map(|ext| ext.into()) }
    }
}
impl From<fidl::InstallationDeferredData> for InstallationDeferredData {
    fn from(data: fidl::InstallationDeferredData) -> Self {
        Self { update: data.update.map(|o| o.into()) }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct UpdateInfo {
    pub version_available: Option<String>,
    pub download_size: Option<u64>,
}
impl Into<fidl::UpdateInfo> for UpdateInfo {
    fn into(self) -> fidl::UpdateInfo {
        fidl::UpdateInfo {
            version_available: self.version_available,
            download_size: self.download_size,
        }
    }
}
impl From<fidl::UpdateInfo> for UpdateInfo {
    fn from(info: fidl::UpdateInfo) -> Self {
        Self { version_available: info.version_available, download_size: info.download_size }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct CheckOptionsBuilder {
    initiator: Option<fidl::Initiator>,
    allow_attaching_to_existing_update_check: Option<bool>,
}
impl CheckOptionsBuilder {
    pub fn new() -> Self {
        CheckOptionsBuilder { initiator: None, allow_attaching_to_existing_update_check: None }
    }
    pub fn initiator(mut self, initiator: fidl::Initiator) -> Self {
        self.initiator = Some(initiator);
        self
    }
    pub fn allow_attaching_to_existing_update_check(mut self, allow: bool) -> Self {
        self.allow_attaching_to_existing_update_check = Some(allow);
        self
    }
    pub fn build(self) -> fidl::CheckOptions {
        fidl::CheckOptions {
            initiator: self.initiator,
            allow_attaching_to_existing_update_check: self.allow_attaching_to_existing_update_check,
        }
    }
}

pub mod proptest_util {
    use super::*;
    use proptest::prelude::*;

    prop_compose! {
        pub fn random_installation_error_data()(
            update in random_update_info(),
            installation_progress in random_installation_progress()
        ) -> InstallationErrorData {
                InstallationErrorData {
                    update,
                    installation_progress
                }
        }
    }

    prop_compose! {
        pub fn random_installing_data() (
            update in random_update_info(),
            installation_progress in random_installation_progress()
        )-> InstallingData {
            InstallingData { update, installation_progress  }
        }
    }

    prop_compose! {
        pub fn random_installation_deferred_data() (
            update in random_update_info()
        )-> InstallationDeferredData {
            InstallationDeferredData { update }
        }
    }

    prop_compose! {
        pub fn random_installation_progress() (
            fraction_completed in proptest::option::of(prop::num::f32::NORMAL),
        )(
            progress in proptest::option::of(Just(InstallationProgress { fraction_completed })),
        ) -> Option<InstallationProgress> {
            progress
        }
    }

    prop_compose! {
        pub fn random_update_info() (
            version_available in random_version_available(),
            download_size in proptest::option::of(prop::num::u64::ANY)
        )(
            update_info in proptest::option::of(Just(UpdateInfo { version_available, download_size }))
        ) -> Option<UpdateInfo> {
            update_info
        }
    }

    prop_compose! {
        pub fn random_version_available()(
            version_available in proptest::option::of(Just("[0-9A-Z]{10,20}".to_string()))
        ) -> Option<String> {
            version_available
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use proptest::prelude::*;
    use proptest_util::*;

    prop_compose! {
        fn random_update_state()(
            installation_deferred_data in proptest_util::random_installation_deferred_data(),
            installing_data in proptest_util::random_installing_data(),
            installation_error_data in proptest_util::random_installation_error_data()
        )
        (
            state in prop_oneof![
                Just(State::CheckingForUpdates),
                Just(State::ErrorCheckingForUpdate),
                Just(State::NoUpdateAvailable),
                Just(State::InstallationDeferredByPolicy(installation_deferred_data)),
                Just(State::InstallingUpdate(installing_data.clone())),
                Just(State::WaitingForReboot(installing_data)),
                Just(State::InstallationError(installation_error_data)),
            ]) -> State
        {
            state
        }
    }

    proptest! {
        // states with the same update info but different progress should merge
        #[test]
        fn test_can_merge(
            update_info in random_update_info(),
            progress0 in random_installation_progress(),
            progress1 in random_installation_progress(),
        ) {
            let event0 = State::InstallingUpdate(
                InstallingData {
                    update: update_info.clone(),
                    installation_progress: progress0,
                }
            );
            let event1 = State::InstallingUpdate(
                InstallingData {
                    update: update_info,
                    installation_progress: progress1,
                }
            );
            prop_assert!(event0.can_merge(&event1));
        }

        #[test]
        fn test_state_roundtrips(state in random_update_state()) {
            let state0: State = state.clone();
            let fidl_intermediate: fidl::State = state.into();
            let state1: State = fidl_intermediate.into();
            prop_assert_eq!(state0, state1);
        }
    }
}
