// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    event_queue::Event, fidl_fuchsia_update as fidl, proptest::prelude::*,
    proptest_derive::Arbitrary, std::convert::TryFrom, thiserror::Error,
};

#[derive(Clone, Debug, PartialEq, Arbitrary)]
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
                fidl::State::CheckingForUpdates(fidl::CheckingForUpdatesData::EMPTY)
            }
            State::ErrorCheckingForUpdate => {
                fidl::State::ErrorCheckingForUpdate(fidl::ErrorCheckingForUpdateData::EMPTY)
            }
            State::NoUpdateAvailable => {
                fidl::State::NoUpdateAvailable(fidl::NoUpdateAvailableData::EMPTY)
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

#[derive(Clone, Debug, PartialEq, Arbitrary)]
pub struct InstallationErrorData {
    pub update: Option<UpdateInfo>,
    pub installation_progress: Option<InstallationProgress>,
}
impl Into<fidl::InstallationErrorData> for InstallationErrorData {
    fn into(self) -> fidl::InstallationErrorData {
        fidl::InstallationErrorData {
            update: self.update.map(|ext| ext.into()),
            installation_progress: self.installation_progress.map(|ext| ext.into()),
            ..fidl::InstallationErrorData::EMPTY
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

#[derive(Clone, Debug, PartialEq, Arbitrary)]
pub struct InstallationProgress {
    #[proptest(strategy = "prop::option::of(prop::num::f32::NORMAL)")]
    pub fraction_completed: Option<f32>,
}
impl Into<fidl::InstallationProgress> for InstallationProgress {
    fn into(self) -> fidl::InstallationProgress {
        fidl::InstallationProgress {
            fraction_completed: self.fraction_completed,
            ..fidl::InstallationProgress::EMPTY
        }
    }
}
impl From<fidl::InstallationProgress> for InstallationProgress {
    fn from(progress: fidl::InstallationProgress) -> Self {
        Self { fraction_completed: progress.fraction_completed }
    }
}

#[derive(Clone, Debug, PartialEq, Arbitrary)]
pub struct InstallingData {
    pub update: Option<UpdateInfo>,
    pub installation_progress: Option<InstallationProgress>,
}
impl Into<fidl::InstallingData> for InstallingData {
    fn into(self) -> fidl::InstallingData {
        fidl::InstallingData {
            update: self.update.map(|ext| ext.into()),
            installation_progress: self.installation_progress.map(|ext| ext.into()),
            ..fidl::InstallingData::EMPTY
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
    pub deferral_reason: Option<fidl::InstallationDeferralReason>,
}

impl Into<fidl::InstallationDeferredData> for InstallationDeferredData {
    fn into(self) -> fidl::InstallationDeferredData {
        fidl::InstallationDeferredData {
            update: self.update.map(|ext| ext.into()),
            deferral_reason: self.deferral_reason,
            ..fidl::InstallationDeferredData::EMPTY
        }
    }
}
impl From<fidl::InstallationDeferredData> for InstallationDeferredData {
    fn from(data: fidl::InstallationDeferredData) -> Self {
        Self { update: data.update.map(|o| o.into()), deferral_reason: data.deferral_reason }
    }
}

// Manually impl Arbitrary because fidl::InstallationDeferralReason does not impl Arbitrary.
// We could have created another wrapper, but we opted not to in order to guarantee the ext
// crate stays in sync with the FIDL for InstallationDeferralReason.
impl Arbitrary for InstallationDeferredData {
    type Parameters = ();
    type Strategy = BoxedStrategy<Self>;

    fn arbitrary_with((): Self::Parameters) -> Self::Strategy {
        any::<UpdateInfo>()
            .prop_flat_map(|info| {
                (
                    proptest::option::of(Just(info)),
                    proptest::option::of(Just(
                        fidl::InstallationDeferralReason::CurrentSystemNotCommitted,
                    )),
                )
            })
            .prop_map(|(update, deferral_reason)| Self { update, deferral_reason })
            .boxed()
    }
}

#[derive(Clone, Debug, PartialEq, Arbitrary)]
pub struct UpdateInfo {
    #[proptest(strategy = "proptest_util::random_version_available()")]
    pub version_available: Option<String>,
    pub download_size: Option<u64>,
}
impl Into<fidl::UpdateInfo> for UpdateInfo {
    fn into(self) -> fidl::UpdateInfo {
        fidl::UpdateInfo {
            version_available: self.version_available,
            download_size: self.download_size,
            ..fidl::UpdateInfo::EMPTY
        }
    }
}
impl From<fidl::UpdateInfo> for UpdateInfo {
    fn from(info: fidl::UpdateInfo) -> Self {
        Self { version_available: info.version_available, download_size: info.download_size }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Arbitrary)]
pub enum Initiator {
    User,
    Service,
}

impl From<fidl::Initiator> for Initiator {
    fn from(initiator: fidl::Initiator) -> Self {
        match initiator {
            fidl::Initiator::User => Initiator::User,
            fidl::Initiator::Service => Initiator::Service,
        }
    }
}

impl From<Initiator> for fidl::Initiator {
    fn from(initiator: Initiator) -> Self {
        match initiator {
            Initiator::User => fidl::Initiator::User,
            Initiator::Service => fidl::Initiator::Service,
        }
    }
}

#[derive(Debug, Error, PartialEq, Eq)]
pub enum CheckOptionsDecodeError {
    #[error("missing field 'initiator'")]
    MissingInitiator,
}

#[derive(Clone, Debug, PartialEq, Arbitrary)]
pub struct CheckOptions {
    pub initiator: Initiator,
    pub allow_attaching_to_existing_update_check: bool,
}

impl CheckOptions {
    pub fn builder() -> CheckOptionsBuilder {
        CheckOptionsBuilder
    }
}

impl TryFrom<fidl::CheckOptions> for CheckOptions {
    type Error = CheckOptionsDecodeError;

    fn try_from(o: fidl::CheckOptions) -> Result<Self, Self::Error> {
        Ok(Self {
            initiator: o.initiator.ok_or(CheckOptionsDecodeError::MissingInitiator)?.into(),
            allow_attaching_to_existing_update_check: o
                .allow_attaching_to_existing_update_check
                .unwrap_or(false),
        })
    }
}

impl From<CheckOptions> for fidl::CheckOptions {
    fn from(o: CheckOptions) -> Self {
        Self {
            initiator: Some(o.initiator.into()),
            allow_attaching_to_existing_update_check: Some(
                o.allow_attaching_to_existing_update_check,
            ),
            ..Self::EMPTY
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct CheckOptionsBuilder;

#[derive(Clone, Debug, PartialEq)]
pub struct CheckOptionsBuilderWithInitiator {
    initiator: Initiator,
    allow_attaching_to_existing_update_check: bool,
}

impl CheckOptionsBuilder {
    pub fn new() -> Self {
        Self
    }

    pub fn initiator(self, initiator: Initiator) -> CheckOptionsBuilderWithInitiator {
        CheckOptionsBuilderWithInitiator {
            initiator,
            allow_attaching_to_existing_update_check: false,
        }
    }
}

impl CheckOptionsBuilderWithInitiator {
    pub fn allow_attaching_to_existing_update_check(mut self, allow: bool) -> Self {
        self.allow_attaching_to_existing_update_check = allow;
        self
    }

    pub fn build(self) -> CheckOptions {
        CheckOptions {
            initiator: self.initiator,
            allow_attaching_to_existing_update_check: self.allow_attaching_to_existing_update_check,
        }
    }
}

pub mod proptest_util {
    use proptest::prelude::*;

    prop_compose! {
        pub fn random_version_available()(
            version_available in proptest::option::of("[0-9A-Z]{10,20}")
        ) -> Option<String> {
            version_available
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    proptest! {
        // states with the same update info but different progress should merge
        #[test]
        fn test_can_merge(
            update_info: Option<UpdateInfo>,
            progress0: Option<InstallationProgress>,
            progress1: Option<InstallationProgress>,
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
        fn test_state_roundtrips(state: State) {
            let state0: State = state.clone();
            let fidl_intermediate: fidl::State = state.into();
            let state1: State = fidl_intermediate.into();
            prop_assert_eq!(state0, state1);
        }

        #[test]
        fn test_initiator_roundtrips(initiator: Initiator) {
            prop_assert_eq!(
                Initiator::from(fidl::Initiator::from(initiator.clone())),
                initiator
            );
        }

        #[test]
        fn test_check_options_roundtrips(check_options: CheckOptions) {
            prop_assert_eq!(
                CheckOptions::try_from(fidl::CheckOptions::from(check_options.clone())),
                Ok(check_options)
            );
        }

        #[test]
        fn test_check_options_initiator_required(allow_attaching_to_existing_update_check: bool) {
            prop_assert_eq!(
                CheckOptions::try_from(fidl::CheckOptions {
                    initiator: None,
                    allow_attaching_to_existing_update_check: Some(allow_attaching_to_existing_update_check),
                    ..fidl::CheckOptions::EMPTY
                }),
                Err(CheckOptionsDecodeError::MissingInitiator)
            );
        }
    }

    #[test]
    fn check_options_builder() {
        assert_eq!(
            CheckOptions::builder()
                .initiator(Initiator::User)
                .allow_attaching_to_existing_update_check(true)
                .build(),
            CheckOptions {
                initiator: Initiator::User,
                allow_attaching_to_existing_update_check: true,
            }
        );
    }
}
