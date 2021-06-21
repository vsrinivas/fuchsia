// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    event_queue::Event, fidl_fuchsia_update as fidl, proptest::prelude::*,
    proptest_derive::Arbitrary, std::convert::TryFrom, thiserror::Error,
};

/// Wrapper type for [`fidl_fuchsia_update::State`] which works with
/// [`event_queue`] and [`proptest`].
///
/// Use [`From`] (and [`Into`]) to convert between the fidl type and this one.
///
/// See [`fidl_fuchsia_update::State`] for docs on what each state means.
#[allow(missing_docs)] // states are documented in fidl.
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
        // Merge states that have the same update info but different installation
        // progress
        if let State::InstallingUpdate(InstallingData { update: update0, .. }) = self {
            if let State::InstallingUpdate(InstallingData { update: update1, .. }) = other {
                return update0 == update1;
            }
        }
        false
    }
}

impl From<State> for fidl::State {
    fn from(other: State) -> Self {
        match other {
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

/// Wrapper type for [`fidl_fuchsia_update::InstallationErrorData`] which works
/// with [`proptest`].
///
/// Use [`From`] (and [`Into`]) to convert between the fidl type and this one.
#[derive(Clone, Debug, PartialEq, Arbitrary)]
pub struct InstallationErrorData {
    pub update: Option<UpdateInfo>,
    pub installation_progress: Option<InstallationProgress>,
}
impl From<InstallationErrorData> for fidl::InstallationErrorData {
    fn from(other: InstallationErrorData) -> Self {
        fidl::InstallationErrorData {
            update: other.update.map(|ext| ext.into()),
            installation_progress: other.installation_progress.map(|ext| ext.into()),
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

/// Wrapper type for [`fidl_fuchsia_update::InstallationProgress`] which works
/// with [`proptest`].
///
/// Use [`From`] (and [`Into`]) to convert between the fidl type and this one.
#[derive(Clone, Debug, PartialEq, Arbitrary)]
pub struct InstallationProgress {
    #[proptest(strategy = "prop::option::of(prop::num::f32::NORMAL)")]
    pub fraction_completed: Option<f32>,
}
impl From<InstallationProgress> for fidl::InstallationProgress {
    fn from(other: InstallationProgress) -> Self {
        fidl::InstallationProgress {
            fraction_completed: other.fraction_completed,
            ..fidl::InstallationProgress::EMPTY
        }
    }
}
impl From<fidl::InstallationProgress> for InstallationProgress {
    fn from(progress: fidl::InstallationProgress) -> Self {
        Self { fraction_completed: progress.fraction_completed }
    }
}

/// Wrapper type for [`fidl_fuchsia_update::InstallingData`] which works with
/// [`proptest`].
///
/// Use [`From`] (and [`Into`]) to convert between the fidl type and this one.
#[derive(Clone, Debug, PartialEq, Arbitrary)]
pub struct InstallingData {
    pub update: Option<UpdateInfo>,
    pub installation_progress: Option<InstallationProgress>,
}
impl From<InstallingData> for fidl::InstallingData {
    fn from(other: InstallingData) -> Self {
        fidl::InstallingData {
            update: other.update.map(|ext| ext.into()),
            installation_progress: other.installation_progress.map(|ext| ext.into()),
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

/// Wrapper type for [`fidl_fuchsia_update::InstallationDeferredData`] which
/// works with [`proptest`].
///
/// Use [`From`] (and [`Into`]) to convert between the fidl type and this one.
#[derive(Clone, Debug, PartialEq)]
pub struct InstallationDeferredData {
    pub update: Option<UpdateInfo>,
    pub deferral_reason: Option<fidl::InstallationDeferralReason>,
}

impl From<InstallationDeferredData> for fidl::InstallationDeferredData {
    fn from(other: InstallationDeferredData) -> Self {
        fidl::InstallationDeferredData {
            update: other.update.map(|ext| ext.into()),
            deferral_reason: other.deferral_reason,
            ..fidl::InstallationDeferredData::EMPTY
        }
    }
}
impl From<fidl::InstallationDeferredData> for InstallationDeferredData {
    fn from(data: fidl::InstallationDeferredData) -> Self {
        Self { update: data.update.map(|o| o.into()), deferral_reason: data.deferral_reason }
    }
}

// Manually impl Arbitrary because fidl::InstallationDeferralReason does not
// impl Arbitrary. We could have created another wrapper, but we opted not to in
// order to guarantee the ext crate stays in sync with the FIDL for
// InstallationDeferralReason.
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

/// Wrapper type for [`fidl_fuchsia_update::UpdateInfo`] which works with
/// [`proptest`].
///
/// Use [`From`] (and [`Into`]) to convert between the fidl type and this one.
#[derive(Clone, Debug, PartialEq, Arbitrary)]
pub struct UpdateInfo {
    #[proptest(strategy = "proptest_util::random_version_available()")]
    pub version_available: Option<String>,
    pub download_size: Option<u64>,
}
impl From<UpdateInfo> for fidl::UpdateInfo {
    fn from(other: UpdateInfo) -> Self {
        fidl::UpdateInfo {
            version_available: other.version_available,
            download_size: other.download_size,
            ..fidl::UpdateInfo::EMPTY
        }
    }
}
impl From<fidl::UpdateInfo> for UpdateInfo {
    fn from(info: fidl::UpdateInfo) -> Self {
        Self { version_available: info.version_available, download_size: info.download_size }
    }
}

/// Wrapper type for [`fidl_fuchsia_update::Initiator`] which works with
/// [`proptest`].
///
/// Use [`From`] (and [`Into`]) to convert between the fidl type and this one.
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

/// An error which can be returned when validating [`CheckOptions`].
#[derive(Debug, Error, PartialEq, Eq)]
pub enum CheckOptionsDecodeError {
    /// The initiator field was not set.
    #[error("missing field 'initiator'")]
    MissingInitiator,
}

/// Wrapper type for [`fidl_fuchsia_update::CheckOptions`] which validates the
/// options on construction and works with [`proptest`].
///
/// Use [`TryFrom`] (or [`TryInto`]) to convert the fidl type and this one, and
/// [`From`] (or [`Into`]) to convert back to the fidl type.
#[derive(Clone, Debug, PartialEq, Arbitrary)]
pub struct CheckOptions {
    pub initiator: Initiator,
    pub allow_attaching_to_existing_update_check: bool,
}

impl CheckOptions {
    /// Create a `[CheckOptionsBuilder]` for constructing a new
    /// `[CheckOptions]`.
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

/// A builder for constructing a new [`CheckOptions`]
#[derive(Clone, Debug, PartialEq, Default)]
pub struct CheckOptionsBuilder;

/// A builder for constructing a new [`CheckOptions`] with initiator already
/// set.
#[derive(Clone, Debug, PartialEq)]
pub struct CheckOptionsBuilderWithInitiator {
    initiator: Initiator,
    allow_attaching_to_existing_update_check: bool,
}

impl CheckOptionsBuilder {
    /// Create a `[CheckOptionsBuilder]` for constructing a new
    /// `[CheckOptions]`.
    pub fn new() -> Self {
        Self
    }

    /// Set the initiator of the update.
    pub fn initiator(self, initiator: Initiator) -> CheckOptionsBuilderWithInitiator {
        CheckOptionsBuilderWithInitiator {
            initiator,
            allow_attaching_to_existing_update_check: false,
        }
    }
}

impl CheckOptionsBuilderWithInitiator {
    /// Set the allow_attaching_to_existing_update_check flag.
    pub fn allow_attaching_to_existing_update_check(mut self, allow: bool) -> Self {
        self.allow_attaching_to_existing_update_check = allow;
        self
    }

    /// Build the [`CheckOptions`].
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
        /// pick an arbitrary version_available value for `UpdateInfo`
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
