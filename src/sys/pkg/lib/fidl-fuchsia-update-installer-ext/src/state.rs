// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Wrapper types for the State union.

use {
    event_queue::Event,
    fidl_fuchsia_update_installer as fidl, fuchsia_inspect as inspect,
    proptest::prelude::*,
    proptest_derive::Arbitrary,
    serde::{Deserialize, Serialize},
    std::convert::{TryFrom, TryInto},
    thiserror::Error,
};

/// The state of an update installation attempt.
#[derive(Arbitrary, Clone, Debug, Serialize, Deserialize, PartialEq)]
#[serde(tag = "id", rename_all = "snake_case")]
#[allow(missing_docs)]
pub enum State {
    Prepare,
    Fetch(UpdateInfoAndProgress),
    Stage(UpdateInfoAndProgress),
    WaitToReboot(UpdateInfoAndProgress),
    Reboot(UpdateInfoAndProgress),
    DeferReboot(UpdateInfoAndProgress),
    Complete(UpdateInfoAndProgress),
    FailPrepare,
    FailFetch(UpdateInfoAndProgress),
    FailStage(UpdateInfoAndProgress),
}

/// The variant names for each state, with data stripped.
#[allow(missing_docs)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum StateId {
    Prepare,
    Fetch,
    Stage,
    WaitToReboot,
    Reboot,
    DeferReboot,
    Complete,
    FailPrepare,
    FailFetch,
    FailStage,
}

/// Immutable metadata for an update attempt.
#[derive(Arbitrary, Clone, Copy, Debug, Serialize, Deserialize, PartialEq, PartialOrd)]
pub struct UpdateInfo {
    download_size: u64,
}

/// Builder of UpdateInfo
#[derive(Clone, Debug)]
pub struct UpdateInfoBuilder;

/// Builder of UpdateInfo, with a known download_size field.
#[derive(Clone, Debug)]
pub struct UpdateInfoBuilderWithDownloadSize {
    download_size: u64,
}

/// Mutable progress information for an update attempt.
#[derive(Arbitrary, Clone, Debug, Serialize, PartialEq, PartialOrd)]
pub struct Progress {
    /// Within the range of [0.0, 1.0]
    #[proptest(strategy = "0.0f32 ..= 1.0")]
    fraction_completed: f32,

    bytes_downloaded: u64,
}

/// Builder of Progress.
#[derive(Clone, Debug)]
pub struct ProgressBuilder;

/// Builder of Progress, with a known fraction_completed field.
#[derive(Clone, Debug)]
pub struct ProgressBuilderWithFraction {
    fraction_completed: f32,
}

/// Builder of Progress, with a known fraction_completed and bytes_downloaded field.
#[derive(Clone, Debug)]
pub struct ProgressBuilderWithFractionAndBytes {
    fraction_completed: f32,
    bytes_downloaded: u64,
}

/// An UpdateInfo and Progress that are guaranteed to be consistent with each other.
///
/// Specifically, `progress.bytes_downloaded <= info.download_size`.
#[derive(Clone, Debug, Serialize, PartialEq, PartialOrd)]
pub struct UpdateInfoAndProgress {
    info: UpdateInfo,
    progress: Progress,
}

/// Builder of UpdateInfoAndProgress.
#[derive(Clone, Debug)]
pub struct UpdateInfoAndProgressBuilder;

/// Builder of UpdateInfoAndProgress, with a known UpdateInfo field.
#[derive(Clone, Debug)]
pub struct UpdateInfoAndProgressBuilderWithInfo {
    info: UpdateInfo,
}

/// Builder of UpdateInfoAndProgress, with a known UpdateInfo and Progress field.
#[derive(Clone, Debug)]
pub struct UpdateInfoAndProgressBuilderWithInfoAndProgress {
    info: UpdateInfo,
    progress: Progress,
}

impl State {
    /// Obtain the variant name (strip out the data).
    pub fn id(&self) -> StateId {
        match self {
            State::Prepare => StateId::Prepare,
            State::Fetch(_) => StateId::Fetch,
            State::Stage(_) => StateId::Stage,
            State::WaitToReboot(_) => StateId::WaitToReboot,
            State::Reboot(_) => StateId::Reboot,
            State::DeferReboot(_) => StateId::DeferReboot,
            State::Complete(_) => StateId::Complete,
            State::FailPrepare => StateId::FailPrepare,
            State::FailFetch(_) => StateId::FailFetch,
            State::FailStage(_) => StateId::FailStage,
        }
    }

    /// Determines if this state is terminal and represents a successful attempt.
    pub fn is_success(&self) -> bool {
        match self.id() {
            StateId::Reboot | StateId::DeferReboot | StateId::Complete => true,
            _ => false,
        }
    }

    /// Determines if this state is terminal and represents a failure.
    pub fn is_failure(&self) -> bool {
        match self.id() {
            StateId::FailPrepare | StateId::FailFetch | StateId::FailStage => true,
            _ => false,
        }
    }

    /// Determines if this state is terminal (terminal states are final, no futher state
    /// transitions should occur).
    pub fn is_terminal(&self) -> bool {
        self.is_success() || self.is_failure()
    }

    /// Returns the name of the state, intended for use in log/diagnostics output.
    pub fn name(&self) -> &'static str {
        match self {
            State::Prepare => "prepare",
            State::Fetch(_) => "fetch",
            State::Stage(_) => "stage",
            State::WaitToReboot(_) => "wait_to_reboot",
            State::Reboot(_) => "reboot",
            State::DeferReboot(_) => "defer_reboot",
            State::Complete(_) => "complete",
            State::FailPrepare => "fail_prepare",
            State::FailFetch(_) => "fail_fetch",
            State::FailStage(_) => "fail_stage",
        }
    }

    /// Serializes this state to a Fuchsia Inspect node.
    pub fn write_to_inspect(&self, node: &inspect::Node) {
        node.record_string("state", self.name());
        use State::*;

        match self {
            Prepare | FailPrepare => {}
            Fetch(info_progress)
            | Stage(info_progress)
            | WaitToReboot(info_progress)
            | Reboot(info_progress)
            | DeferReboot(info_progress)
            | Complete(info_progress)
            | FailFetch(info_progress)
            | FailStage(info_progress) => {
                info_progress.write_to_inspect(node);
            }
        }
    }

    /// Extracts progress, if the state supports it.
    pub fn progress(&self) -> Option<&Progress> {
        let info_and_progress = match self {
            State::Prepare | State::FailPrepare => return None,
            State::Fetch(data)
            | State::Stage(data)
            | State::WaitToReboot(data)
            | State::Reboot(data)
            | State::DeferReboot(data)
            | State::Complete(data)
            | State::FailFetch(data)
            | State::FailStage(data) => data,
        };
        let UpdateInfoAndProgress { info: _, progress } = info_and_progress;
        Some(progress)
    }
}

impl Event for State {
    fn can_merge(&self, other: &Self) -> bool {
        self.id() == other.id()
    }
}

impl UpdateInfo {
    /// Starts building an instance of UpdateInfo.
    pub fn builder() -> UpdateInfoBuilder {
        UpdateInfoBuilder
    }

    /// Gets the download_size field.
    pub fn download_size(&self) -> u64 {
        self.download_size
    }

    fn write_to_inspect(&self, node: &inspect::Node) {
        let UpdateInfo { download_size } = self;
        node.record_uint("download_size", *download_size)
    }
}

impl UpdateInfoBuilder {
    /// Sets the download_size field.
    pub fn download_size(self, download_size: u64) -> UpdateInfoBuilderWithDownloadSize {
        UpdateInfoBuilderWithDownloadSize { download_size }
    }
}

impl UpdateInfoBuilderWithDownloadSize {
    /// Builds the UpdateInfo instance.
    pub fn build(self) -> UpdateInfo {
        let Self { download_size } = self;
        UpdateInfo { download_size }
    }
}

impl Progress {
    /// Starts building an instance of Progress.
    pub fn builder() -> ProgressBuilder {
        ProgressBuilder
    }

    /// Produces a Progress at 0% complete and 0 bytes downloaded.
    pub fn none() -> Self {
        Self { fraction_completed: 0.0, bytes_downloaded: 0 }
    }

    /// Produces a Progress at 100% complete and all bytes downloaded, based on the download_size
    /// in `info`.
    pub fn done(info: &UpdateInfo) -> Self {
        Self { fraction_completed: 1.0, bytes_downloaded: info.download_size }
    }

    /// Gets the fraction_completed field.
    pub fn fraction_completed(&self) -> f32 {
        self.fraction_completed
    }

    /// Gets the bytes_downloaded field.
    pub fn bytes_downloaded(&self) -> u64 {
        self.bytes_downloaded
    }

    fn write_to_inspect(&self, node: &inspect::Node) {
        let Progress { fraction_completed, bytes_downloaded } = self;
        node.record_double("fraction_completed", *fraction_completed as f64);
        node.record_uint("bytes_downloaded", *bytes_downloaded);
    }
}

impl ProgressBuilder {
    /// Sets the fraction_completed field, claming the provided float to the range [0.0, 1.0] and
    /// converting NaN to 0.0.
    pub fn fraction_completed(self, fraction_completed: f32) -> ProgressBuilderWithFraction {
        ProgressBuilderWithFraction { fraction_completed: fraction_completed.max(0.0).min(1.0) }
    }
}

impl ProgressBuilderWithFraction {
    /// Sets the bytes_downloaded field.
    pub fn bytes_downloaded(self, bytes_downloaded: u64) -> ProgressBuilderWithFractionAndBytes {
        ProgressBuilderWithFractionAndBytes {
            fraction_completed: self.fraction_completed,
            bytes_downloaded,
        }
    }
}

impl ProgressBuilderWithFractionAndBytes {
    /// Builds the Progress instance.
    pub fn build(self) -> Progress {
        let Self { fraction_completed, bytes_downloaded } = self;
        Progress { fraction_completed, bytes_downloaded }
    }
}

impl UpdateInfoAndProgress {
    /// Starts building an instance of UpdateInfoAndProgress.
    pub fn builder() -> UpdateInfoAndProgressBuilder {
        UpdateInfoAndProgressBuilder
    }

    /// Constructs an UpdateInfoAndProgress from the 2 fields, ensuring that the 2 structs are
    /// consistent with each other, returning an error if they are not.
    pub fn new(
        info: UpdateInfo,
        progress: Progress,
    ) -> Result<Self, BytesFetchedExceedsDownloadSize> {
        if progress.bytes_downloaded > info.download_size {
            return Err(BytesFetchedExceedsDownloadSize);
        }

        Ok(Self { info, progress })
    }

    /// Constructs an UpdateInfoAndProgress from an UpdateInfo, setting the progress fields to be
    /// 100% done with all bytes downloaded.
    pub fn done(info: UpdateInfo) -> Self {
        Self { progress: Progress::done(&info), info }
    }

    /// Returns the info field.
    pub fn info(&self) -> UpdateInfo {
        self.info
    }

    /// Returns the progress field.
    pub fn progress(&self) -> &Progress {
        &self.progress
    }

    fn write_to_inspect(&self, node: &inspect::Node) {
        node.record_child("info", |n| {
            self.info.write_to_inspect(n);
        });
        node.record_child("progress", |n| {
            self.progress.write_to_inspect(n);
        });
    }
}

impl UpdateInfoAndProgressBuilder {
    /// Sets the UpdateInfo field.
    pub fn info(self, info: UpdateInfo) -> UpdateInfoAndProgressBuilderWithInfo {
        UpdateInfoAndProgressBuilderWithInfo { info }
    }
}

impl UpdateInfoAndProgressBuilderWithInfo {
    /// Sets the Progress field, clamping `progress.bytes_downloaded` to be `<=
    /// info.download_size`. Users of this API should independently ensure that this invariant is
    /// not violated.
    pub fn progress(
        self,
        mut progress: Progress,
    ) -> UpdateInfoAndProgressBuilderWithInfoAndProgress {
        if progress.bytes_downloaded > self.info.download_size {
            progress.bytes_downloaded = self.info.download_size;
        }

        UpdateInfoAndProgressBuilderWithInfoAndProgress { info: self.info, progress }
    }
}

impl UpdateInfoAndProgressBuilderWithInfoAndProgress {
    /// Builds the UpdateInfoAndProgress instance.
    pub fn build(self) -> UpdateInfoAndProgress {
        let Self { info, progress } = self;
        UpdateInfoAndProgress { info, progress }
    }
}

impl<'de> Deserialize<'de> for UpdateInfoAndProgress {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        use serde::de::Error;

        #[derive(Debug, Deserialize)]
        pub struct DeUpdateInfoAndProgress {
            info: UpdateInfo,
            progress: Progress,
        }

        let info_progress = DeUpdateInfoAndProgress::deserialize(deserializer)?;

        UpdateInfoAndProgress::new(info_progress.info, info_progress.progress)
            .map_err(|e| D::Error::custom(e.to_string()))
    }
}

impl<'de> Deserialize<'de> for Progress {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        #[derive(Debug, Deserialize)]
        pub struct DeProgress {
            fraction_completed: f32,
            bytes_downloaded: u64,
        }

        let progress = DeProgress::deserialize(deserializer)?;

        Ok(Progress::builder()
            .fraction_completed(progress.fraction_completed)
            .bytes_downloaded(progress.bytes_downloaded)
            .build())
    }
}

/// An error encountered while pairing an [`UpdateInfo`] and [`Progress`].
#[derive(Debug, Error, PartialEq, Eq)]
#[error("more bytes were fetched than should have been fetched")]
pub struct BytesFetchedExceedsDownloadSize;

/// An error encountered while decoding a [fidl_fuchsia_update_installer::State]
/// into a [State].
#[derive(Debug, Error, PartialEq, Eq)]
#[allow(missing_docs)]
pub enum DecodeStateError {
    #[error("missing field {0:?}")]
    MissingField(RequiredStateField),

    #[error("state contained invalid 'info' field")]
    DecodeUpdateInfo(#[source] DecodeUpdateInfoError),

    #[error("state contained invalid 'progress' field")]
    DecodeProgress(#[source] DecodeProgressError),

    #[error("the provided update info and progress are inconsistent with each other")]
    InconsistentUpdateInfoAndProgress(#[source] BytesFetchedExceedsDownloadSize),
}

/// Required fields in a [fidl_fuchsia_update_installer::State].
#[derive(Debug, PartialEq, Eq)]
#[allow(missing_docs)]
pub enum RequiredStateField {
    Info,
    Progress,
}

impl From<State> for fidl::State {
    fn from(state: State) -> Self {
        match state {
            State::Prepare => fidl::State::Prepare(fidl::PrepareData {}),
            State::Fetch(UpdateInfoAndProgress { info, progress }) => {
                fidl::State::Fetch(fidl::FetchData {
                    info: Some(info.into()),
                    progress: Some(progress.into()),
                })
            }
            State::Stage(UpdateInfoAndProgress { info, progress }) => {
                fidl::State::Stage(fidl::StageData {
                    info: Some(info.into()),
                    progress: Some(progress.into()),
                })
            }
            State::WaitToReboot(UpdateInfoAndProgress { info, progress }) => {
                fidl::State::WaitToReboot(fidl::WaitToRebootData {
                    info: Some(info.into()),
                    progress: Some(progress.into()),
                })
            }
            State::Reboot(UpdateInfoAndProgress { info, progress }) => {
                fidl::State::Reboot(fidl::RebootData {
                    info: Some(info.into()),
                    progress: Some(progress.into()),
                })
            }
            State::DeferReboot(UpdateInfoAndProgress { info, progress }) => {
                fidl::State::DeferReboot(fidl::DeferRebootData {
                    info: Some(info.into()),
                    progress: Some(progress.into()),
                })
            }
            State::Complete(UpdateInfoAndProgress { info, progress }) => {
                fidl::State::Complete(fidl::CompleteData {
                    info: Some(info.into()),
                    progress: Some(progress.into()),
                })
            }
            State::FailPrepare => fidl::State::FailPrepare(fidl::FailPrepareData {}),
            State::FailFetch(UpdateInfoAndProgress { info, progress }) => {
                fidl::State::FailFetch(fidl::FailFetchData {
                    info: Some(info.into()),
                    progress: Some(progress.into()),
                })
            }
            State::FailStage(UpdateInfoAndProgress { info, progress }) => {
                fidl::State::FailStage(fidl::FailStageData {
                    info: Some(info.into()),
                    progress: Some(progress.into()),
                })
            }
        }
    }
}

impl TryFrom<fidl::State> for State {
    type Error = DecodeStateError;

    fn try_from(state: fidl::State) -> Result<Self, Self::Error> {
        fn decode_info_progress(
            info: Option<fidl::UpdateInfo>,
            progress: Option<fidl::InstallationProgress>,
        ) -> Result<UpdateInfoAndProgress, DecodeStateError> {
            let info: UpdateInfo = info
                .ok_or(DecodeStateError::MissingField(RequiredStateField::Info))?
                .try_into()
                .map_err(DecodeStateError::DecodeUpdateInfo)?;
            let progress: Progress = progress
                .ok_or(DecodeStateError::MissingField(RequiredStateField::Progress))?
                .try_into()
                .map_err(DecodeStateError::DecodeProgress)?;

            UpdateInfoAndProgress::new(info, progress)
                .map_err(DecodeStateError::InconsistentUpdateInfoAndProgress)
        }

        Ok(match state {
            fidl::State::Prepare(fidl::PrepareData {}) => State::Prepare,
            fidl::State::Fetch(fidl::FetchData { info, progress }) => {
                State::Fetch(decode_info_progress(info, progress)?)
            }
            fidl::State::Stage(fidl::StageData { info, progress }) => {
                State::Stage(decode_info_progress(info, progress)?)
            }
            fidl::State::WaitToReboot(fidl::WaitToRebootData { info, progress }) => {
                State::WaitToReboot(decode_info_progress(info, progress)?)
            }
            fidl::State::Reboot(fidl::RebootData { info, progress }) => {
                State::Reboot(decode_info_progress(info, progress)?)
            }
            fidl::State::DeferReboot(fidl::DeferRebootData { info, progress }) => {
                State::DeferReboot(decode_info_progress(info, progress)?)
            }
            fidl::State::Complete(fidl::CompleteData { info, progress }) => {
                State::Complete(decode_info_progress(info, progress)?)
            }
            fidl::State::FailPrepare(fidl::FailPrepareData {}) => State::FailPrepare,
            fidl::State::FailFetch(fidl::FailFetchData { info, progress }) => {
                State::FailFetch(decode_info_progress(info, progress)?)
            }
            fidl::State::FailStage(fidl::FailStageData { info, progress }) => {
                State::FailStage(decode_info_progress(info, progress)?)
            }
        })
    }
}

// TODO remove ambiguous mapping of 0 to/from None when the system-updater actually computes a
// download size and emits bytes_downloaded information.
fn none_or_some_nonzero(n: u64) -> Option<u64> {
    if n == 0 {
        None
    } else {
        Some(n)
    }
}

/// An error encountered while decoding a [fidl_fuchsia_update_installer::UpdateInfo] into a
/// [UpdateInfo].
#[derive(Debug, Error, PartialEq, Eq)]
#[allow(missing_docs)]
pub enum DecodeUpdateInfoError {}

impl From<UpdateInfo> for fidl::UpdateInfo {
    fn from(info: UpdateInfo) -> Self {
        fidl::UpdateInfo { download_size: none_or_some_nonzero(info.download_size) }
    }
}

impl TryFrom<fidl::UpdateInfo> for UpdateInfo {
    type Error = DecodeUpdateInfoError;

    fn try_from(info: fidl::UpdateInfo) -> Result<Self, Self::Error> {
        Ok(UpdateInfo { download_size: info.download_size.unwrap_or(0) })
    }
}

/// An error encountered while decoding a [fidl_fuchsia_update_installer::InstallationProgress]
/// into a [Progress].
#[derive(Debug, Error, PartialEq, Eq)]
#[allow(missing_docs)]
pub enum DecodeProgressError {
    #[error("missing field {0:?}")]
    MissingField(RequiredProgressField),

    #[error("fraction completed not in range [0.0, 1.0]")]
    FractionCompletedOutOfRange,
}

/// Required fields in a [fidl_fuchsia_update_installer::InstallationProgress].
#[derive(Debug, PartialEq, Eq)]
#[allow(missing_docs)]
pub enum RequiredProgressField {
    FractionCompleted,
}

impl From<Progress> for fidl::InstallationProgress {
    fn from(progress: Progress) -> Self {
        fidl::InstallationProgress {
            fraction_completed: Some(progress.fraction_completed),
            bytes_downloaded: none_or_some_nonzero(progress.bytes_downloaded),
        }
    }
}

impl TryFrom<fidl::InstallationProgress> for Progress {
    type Error = DecodeProgressError;

    fn try_from(progress: fidl::InstallationProgress) -> Result<Self, Self::Error> {
        Ok(Progress {
            fraction_completed: {
                let n = progress.fraction_completed.ok_or(DecodeProgressError::MissingField(
                    RequiredProgressField::FractionCompleted,
                ))?;
                if n < 0.0 || n > 1.0 {
                    return Err(DecodeProgressError::FractionCompletedOutOfRange);
                }
                n
            },
            bytes_downloaded: progress.bytes_downloaded.unwrap_or(0),
        })
    }
}

impl Arbitrary for UpdateInfoAndProgress {
    type Parameters = ();
    type Strategy = BoxedStrategy<Self>;

    fn arbitrary_with((): Self::Parameters) -> Self::Strategy {
        arb_info_and_progress().prop_map(|(info, progress)| Self { info, progress }).boxed()
    }
}

/// Returns a strategy generating and UpdateInfo and Progress such that the Progress does not
/// exceed the bounds of the UpdateInfo.
fn arb_info_and_progress() -> impl Strategy<Value = (UpdateInfo, Progress)> {
    prop_compose! {
        fn arb_progress_for_info(
            info: UpdateInfo
        )(
            fraction_completed: f32,
            bytes_downloaded in 0..=info.download_size
        ) -> Progress {
            Progress::builder()
                .fraction_completed(fraction_completed)
                .bytes_downloaded(bytes_downloaded)
                .build()
        }
    }

    any::<UpdateInfo>().prop_flat_map(|info| (Just(info), arb_progress_for_info(info)))
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_inspect::{assert_inspect_tree, Inspector},
        matches::assert_matches,
        serde_json::json,
    };

    prop_compose! {
        fn arb_progress()(fraction_completed: f32, bytes_downloaded: u64) -> Progress {
            Progress::builder()
                .fraction_completed(fraction_completed)
                .bytes_downloaded(bytes_downloaded)
                .build()
        }
    }

    /// Returns a strategy generating (a, b) such that a < b.
    fn a_lt_b() -> impl Strategy<Value = (u64, u64)> {
        (0..u64::MAX).prop_flat_map(|a| (Just(a), a + 1..))
    }

    proptest! {
        #[test]
        fn progress_builder_clamps_fraction_completed(progress in arb_progress()) {
            prop_assert!(progress.fraction_completed() >= 0.0);
            prop_assert!(progress.fraction_completed() <= 1.0);
        }

        #[test]
        fn progress_builder_roundtrips(progress: Progress) {
            prop_assert_eq!(
                Progress::builder()
                    .fraction_completed(progress.fraction_completed())
                    .bytes_downloaded(progress.bytes_downloaded())
                    .build(),
                progress
            );
        }

        #[test]
        fn update_info_builder_roundtrips(info: UpdateInfo) {
            prop_assert_eq!(
                UpdateInfo::builder()
                    .download_size(info.download_size())
                    .build(),
                info
            );
        }

        #[test]
        fn update_info_and_progress_builder_roundtrips(info_progress: UpdateInfoAndProgress) {
            prop_assert_eq!(
                UpdateInfoAndProgress::builder()
                    .info(info_progress.info)
                    .progress(info_progress.progress.clone())
                    .build(),
                info_progress
            );
        }

        #[test]
        fn update_info_roundtrips_through_fidl(info: UpdateInfo) {
            let as_fidl: fidl::UpdateInfo = info.clone().into();
            prop_assert_eq!(as_fidl.try_into(), Ok(info));
        }

        #[test]
        fn progress_roundtrips_through_fidl(progress: Progress) {
            let as_fidl: fidl::InstallationProgress = progress.clone().into();
            prop_assert_eq!(as_fidl.try_into(), Ok(progress));
        }

        #[test]
        fn update_info_and_progress_builder_produces_valid_instances(
            info: UpdateInfo,
            progress: Progress
        ) {
            let info_progress = UpdateInfoAndProgress::builder()
                .info(info)
                .progress(progress)
                .build();

            prop_assert_eq!(
                UpdateInfoAndProgress::new(info_progress.info.clone(), info_progress.progress.clone()),
                Ok(info_progress)
            );
        }

        #[test]
        fn update_info_and_progress_new_rejects_too_many_bytes(
            (a, b) in a_lt_b(),
            mut info: UpdateInfo,
            mut progress: Progress
        ) {
            info.download_size = a;
            progress.bytes_downloaded = b;

            prop_assert_eq!(
                UpdateInfoAndProgress::new(info, progress),
                Err(BytesFetchedExceedsDownloadSize)
            );
        }

        #[test]
        fn state_roundtrips_through_fidl(state: State) {
            let as_fidl: fidl::State = state.clone().into();
            prop_assert_eq!(as_fidl.try_into(), Ok(state));
        }

        #[test]
        fn state_roundtrips_through_json(state: State) {
            let as_json = serde_json::to_value(&state).unwrap();
            let state2 = serde_json::from_value(as_json).unwrap();
            prop_assert_eq!(state, state2);
        }


        // Test that:
        // * write_to_inspect doesn't panic on arbitrary inputs
        // * we create a string property called 'state' in all cases
        #[test]
        fn state_populates_inspect_with_id(state: State) {
            let inspector = Inspector::new();
            state.write_to_inspect(inspector.root());

            assert_inspect_tree! {
                inspector,
                root: contains {
                    "state": state.name(),
                }
            };
        }

        #[test]
        fn progress_rejects_invalid_fraction_completed(progress: Progress, fraction_completed: f32) {
            let fraction_valid = fraction_completed >= 0.0 && fraction_completed <= 1.0;
            prop_assume!(!fraction_valid);
            // Note, the above doesn't look simplified, but not all the usual math rules apply to
            // types that are PartialOrd and not Ord:
            //use std::f32::NAN;
            //assert!(!(NAN >= 0.0 && NAN <= 1.0)); // This assertion passes.
            //assert!(NAN < 0.0 || NAN > 1.0); // This assertion fails.

            let mut as_fidl: fidl::InstallationProgress = progress.into();
            as_fidl.fraction_completed = Some(fraction_completed);
            prop_assert_eq!(Progress::try_from(as_fidl), Err(DecodeProgressError::FractionCompletedOutOfRange));
        }

        #[test]
        fn state_rejects_too_many_bytes_fetched(state: State, (a, b) in a_lt_b()) {
            let mut as_fidl: fidl::State = state.into();

            let break_info_progress = |info: &mut Option<fidl::UpdateInfo>, progress: &mut Option<fidl::InstallationProgress>| {
                info.as_mut().unwrap().download_size = Some(a);
                progress.as_mut().unwrap().bytes_downloaded = Some(b);
            };

            match &mut as_fidl {
                fidl::State::Prepare(fidl::PrepareData {}) => prop_assume!(false),
                fidl::State::Fetch(fidl::FetchData { info, progress }) => break_info_progress(info, progress),
                fidl::State::Stage(fidl::StageData { info, progress }) => break_info_progress(info, progress),
                fidl::State::WaitToReboot(fidl::WaitToRebootData { info, progress }) => break_info_progress(info, progress),
                fidl::State::Reboot(fidl::RebootData { info, progress }) => break_info_progress(info, progress),
                fidl::State::DeferReboot(fidl::DeferRebootData { info, progress }) => break_info_progress(info, progress),
                fidl::State::Complete(fidl::CompleteData { info, progress }) => break_info_progress(info, progress),
                fidl::State::FailPrepare(fidl::FailPrepareData {}) => prop_assume!(false),
                fidl::State::FailFetch(fidl::FailFetchData { info, progress }) => break_info_progress(info, progress),
                fidl::State::FailStage(fidl::FailStageData { info, progress }) => break_info_progress(info, progress),
            }
            prop_assert_eq!(
                State::try_from(as_fidl),
                Err(DecodeStateError::InconsistentUpdateInfoAndProgress(BytesFetchedExceedsDownloadSize))
            );
        }

        // States can merge with identical states.
        #[test]
        fn state_can_merge_reflexive(state: State) {
            prop_assert!(state.can_merge(&state));
        }

        // States with the same ids can merge, even if the data is different.
        #[test]
        fn states_with_same_ids_can_merge(state: State, different_data: UpdateInfoAndProgress) {
            let state_with_different_data = match state.clone() {
                 State::Prepare => State::Prepare,
                    State::Fetch(_) => State::Fetch(different_data),
                    State::Stage(_) => State::Stage(different_data),
                    State::WaitToReboot(_) => State::WaitToReboot(different_data),
                    State::Reboot(_) => State::Reboot(different_data),
                    State::DeferReboot(_) => State::DeferReboot(different_data),
                    State::Complete(_) => State::Complete(different_data),
                    State::FailPrepare => State::FailPrepare,
                    State::FailFetch(_) => State::FailFetch(different_data),
                    State::FailStage(_) => State::FailStage(different_data),
            };
            prop_assert!(state.can_merge(&state_with_different_data));
        }

        #[test]
        fn states_with_different_ids_cannot_merge(state0: State, state1: State) {
            prop_assume!(state0.id() != state1.id());
            prop_assert!(!state0.can_merge(&state1));
        }

    }

    #[test]
    fn state_populates_inspect() {
        let state = State::Reboot(UpdateInfoAndProgress {
            info: UpdateInfo { download_size: 4096 },
            progress: Progress { bytes_downloaded: 2048, fraction_completed: 0.5 },
        });
        let inspector = Inspector::new();
        state.write_to_inspect(&inspector.root());
        assert_inspect_tree! {
            inspector,
            root: {
                "state": "reboot",
                "info": {
                    "download_size": 4096u64,
                },
                "progress": {
                    "bytes_downloaded": 2048u64,
                    "fraction_completed": 0.5f64,
                }
            }
        }
    }

    #[test]
    fn progress_fraction_completed_required() {
        assert_eq!(
            Progress::try_from(fidl::InstallationProgress::empty()),
            Err(DecodeProgressError::MissingField(RequiredProgressField::FractionCompleted)),
        );
    }

    #[test]
    fn json_deserializes_state() {
        assert_eq!(
            serde_json::from_value::<State>(json!({
                "id": "reboot",
                "info": {
                    "download_size": 100,
                },
                "progress": {
                    "bytes_downloaded": 100,
                    "fraction_completed": 1.0,
                },
            }))
            .unwrap(),
            State::Reboot(UpdateInfoAndProgress {
                info: UpdateInfo { download_size: 100 },
                progress: Progress { bytes_downloaded: 100, fraction_completed: 1.0 },
            })
        );
    }

    #[test]
    fn json_deserialize_detects_inconsistent_info_and_progress() {
        let too_much_download = json!({
            "id": "reboot",
            "info": {
                "download_size": 100,
            },
            "progress": {
                "bytes_downloaded": 101,
                "fraction_completed": 1.0,
            },
        });

        assert_matches!(serde_json::from_value::<State>(too_much_download), Err(_));
    }

    #[test]
    fn json_deserialize_clamps_invalid_fraction_completed() {
        let too_much_progress = json!({
            "bytes_downloaded": 0,
            "fraction_completed": 1.1,
        });
        assert_eq!(
            serde_json::from_value::<Progress>(too_much_progress).unwrap(),
            Progress { bytes_downloaded: 0, fraction_completed: 1.0 }
        );

        let negative_progress = json!({
            "bytes_downloaded": 0,
            "fraction_completed": -0.5,
        });
        assert_eq!(
            serde_json::from_value::<Progress>(negative_progress).unwrap(),
            Progress { bytes_downloaded: 0, fraction_completed: 0.0 }
        );
    }

    #[test]
    fn update_info_and_progress_builder_clamps_bytes_downloaded_to_download_size() {
        assert_eq!(
            UpdateInfoAndProgress::builder()
                .info(UpdateInfo { download_size: 100 })
                .progress(Progress { bytes_downloaded: 200, fraction_completed: 1.0 })
                .build(),
            UpdateInfoAndProgress {
                info: UpdateInfo { download_size: 100 },
                progress: Progress { bytes_downloaded: 100, fraction_completed: 1.0 },
            }
        );
    }
}
