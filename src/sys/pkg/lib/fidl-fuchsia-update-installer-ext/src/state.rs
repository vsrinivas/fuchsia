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
    typed_builder::TypedBuilder,
};

/// The state of an update installation attempt.
#[derive(Arbitrary, Clone, Debug, Serialize, Deserialize, PartialEq)]
#[serde(tag = "id", rename_all = "snake_case")]
#[allow(missing_docs)]
pub enum State {
    Prepare,
    Stage(UpdateInfoAndProgress),
    Fetch(UpdateInfoAndProgress),
    Commit(UpdateInfoAndProgress),
    WaitToReboot(UpdateInfoAndProgress),
    Reboot(UpdateInfoAndProgress),
    DeferReboot(UpdateInfoAndProgress),
    Complete(UpdateInfoAndProgress),
    FailPrepare(PrepareFailureReason),
    FailStage(FailStageData),
    FailFetch(FailFetchData),
    FailCommit(UpdateInfoAndProgress),
}

/// The variant names for each state, with data stripped.
#[allow(missing_docs)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum StateId {
    Prepare,
    Stage,
    Fetch,
    Commit,
    WaitToReboot,
    Reboot,
    DeferReboot,
    Complete,
    FailPrepare,
    FailStage,
    FailFetch,
    FailCommit,
}

/// Immutable metadata for an update attempt.
#[derive(
    Arbitrary, Clone, Copy, Debug, Serialize, Deserialize, PartialEq, PartialOrd, TypedBuilder,
)]
pub struct UpdateInfo {
    download_size: u64,
}

/// Mutable progress information for an update attempt.
#[derive(Arbitrary, Clone, Copy, Debug, Serialize, PartialEq, PartialOrd, TypedBuilder)]
pub struct Progress {
    /// Within the range of [0.0, 1.0]
    #[proptest(strategy = "0.0f32 ..= 1.0")]
    #[builder(setter(transform = |x: f32| x.clamp(0.0, 1.0)))]
    fraction_completed: f32,

    bytes_downloaded: u64,
}

/// An UpdateInfo and Progress that are guaranteed to be consistent with each other.
///
/// Specifically, `progress.bytes_downloaded <= info.download_size`.
#[derive(Clone, Copy, Debug, Serialize, PartialEq, PartialOrd)]
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

#[derive(Arbitrary, Clone, Copy, Debug, PartialEq, Deserialize, Serialize)]
#[serde(tag = "reason", rename_all = "snake_case")]
#[allow(missing_docs)]
pub enum PrepareFailureReason {
    Internal,
    OutOfSpace,
    UnsupportedDowngrade,
}

#[derive(Arbitrary, Copy, Clone, Debug, PartialEq, Deserialize, Serialize)]
#[serde(rename_all = "snake_case")]
#[allow(missing_docs)]
pub enum StageFailureReason {
    Internal,
    OutOfSpace,
}

#[derive(Clone, Copy, Debug, PartialEq)]
#[allow(missing_docs)]
pub struct FailStageData {
    info_and_progress: UpdateInfoAndProgress,
    reason: StageFailureReason,
}

#[derive(Arbitrary, Copy, Clone, Debug, PartialEq, Deserialize, Serialize)]
#[serde(rename_all = "snake_case")]
#[allow(missing_docs)]
pub enum FetchFailureReason {
    Internal,
    OutOfSpace,
}

#[derive(Clone, Copy, Debug, PartialEq)]
#[allow(missing_docs)]
pub struct FailFetchData {
    info_and_progress: UpdateInfoAndProgress,
    reason: FetchFailureReason,
}

impl State {
    /// Obtain the variant name (strip out the data).
    pub fn id(&self) -> StateId {
        match self {
            State::Prepare => StateId::Prepare,
            State::Stage(_) => StateId::Stage,
            State::Fetch(_) => StateId::Fetch,
            State::Commit(_) => StateId::Commit,
            State::WaitToReboot(_) => StateId::WaitToReboot,
            State::Reboot(_) => StateId::Reboot,
            State::DeferReboot(_) => StateId::DeferReboot,
            State::Complete(_) => StateId::Complete,
            State::FailPrepare(_) => StateId::FailPrepare,
            State::FailStage(_) => StateId::FailStage,
            State::FailFetch(_) => StateId::FailFetch,
            State::FailCommit(_) => StateId::FailCommit,
        }
    }

    /// Determines if this state is terminal and represents a successful attempt.
    pub fn is_success(&self) -> bool {
        matches!(self.id(), StateId::Reboot | StateId::DeferReboot | StateId::Complete)
    }

    /// Determines if this state is terminal and represents a failure.
    pub fn is_failure(&self) -> bool {
        matches!(self.id(), StateId::FailPrepare | StateId::FailFetch | StateId::FailStage)
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
            State::Stage(_) => "stage",
            State::Fetch(_) => "fetch",
            State::Commit(_) => "commit",
            State::WaitToReboot(_) => "wait_to_reboot",
            State::Reboot(_) => "reboot",
            State::DeferReboot(_) => "defer_reboot",
            State::Complete(_) => "complete",
            State::FailPrepare(_) => "fail_prepare",
            State::FailStage(_) => "fail_stage",
            State::FailFetch(_) => "fail_fetch",
            State::FailCommit(_) => "fail_commit",
        }
    }

    /// Serializes this state to a Fuchsia Inspect node.
    pub fn write_to_inspect(&self, node: &inspect::Node) {
        node.record_string("state", self.name());
        use State::*;

        match self {
            Prepare => {}
            FailStage(data) => data.write_to_inspect(node),
            FailFetch(data) => data.write_to_inspect(node),
            FailPrepare(reason) => reason.write_to_inspect(node),
            Stage(info_progress)
            | Fetch(info_progress)
            | Commit(info_progress)
            | WaitToReboot(info_progress)
            | Reboot(info_progress)
            | DeferReboot(info_progress)
            | Complete(info_progress)
            | FailCommit(info_progress) => {
                info_progress.write_to_inspect(node);
            }
        }
    }

    /// Extracts info_and_progress, if the state supports it.
    fn info_and_progress(&self) -> Option<&UpdateInfoAndProgress> {
        match self {
            State::Prepare | State::FailPrepare(_) => None,
            State::FailStage(data) => Some(&data.info_and_progress),
            State::FailFetch(data) => Some(&data.info_and_progress),
            State::Stage(data)
            | State::Fetch(data)
            | State::Commit(data)
            | State::WaitToReboot(data)
            | State::Reboot(data)
            | State::DeferReboot(data)
            | State::Complete(data)
            | State::FailCommit(data) => Some(data),
        }
    }

    /// Extracts progress, if the state supports it.
    pub fn progress(&self) -> Option<&Progress> {
        match self.info_and_progress() {
            Some(UpdateInfoAndProgress { info: _, progress }) => Some(progress),
            _ => None,
        }
    }

    /// Extracts the download_size field in UpdateInfo, if the state supports it.
    pub fn download_size(&self) -> Option<u64> {
        match self.info_and_progress() {
            Some(UpdateInfoAndProgress { info, progress: _ }) => Some(info.download_size()),
            _ => None,
        }
    }
}

impl Event for State {
    fn can_merge(&self, other: &Self) -> bool {
        self.id() == other.id()
    }
}

impl UpdateInfo {
    /// Gets the download_size field.
    pub fn download_size(&self) -> u64 {
        self.download_size
    }

    fn write_to_inspect(&self, node: &inspect::Node) {
        let UpdateInfo { download_size } = self;
        node.record_uint("download_size", *download_size)
    }
}

impl Progress {
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

    /// Constructs a FailStageData with the given reason.
    pub fn with_stage_reason(self, reason: StageFailureReason) -> FailStageData {
        FailStageData { info_and_progress: self, reason }
    }

    /// Constructs a FailFetchData with the given reason.
    pub fn with_fetch_reason(self, reason: FetchFailureReason) -> FailFetchData {
        FailFetchData { info_and_progress: self, reason }
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

impl FailStageData {
    fn write_to_inspect(&self, node: &inspect::Node) {
        self.info_and_progress.write_to_inspect(node);
        self.reason.write_to_inspect(node);
    }

    /// Get the reason associated with this FailStageData
    pub fn reason(&self) -> StageFailureReason {
        self.reason
    }
}

impl FailFetchData {
    fn write_to_inspect(&self, node: &inspect::Node) {
        self.info_and_progress.write_to_inspect(node);
        self.reason.write_to_inspect(node);
    }

    /// Get the reason associated with this FetchFailData
    pub fn reason(&self) -> FetchFailureReason {
        self.reason
    }
}

impl PrepareFailureReason {
    fn write_to_inspect(&self, node: &inspect::Node) {
        node.record_string("reason", format!("{:?}", self))
    }
}

impl From<fidl::PrepareFailureReason> for PrepareFailureReason {
    fn from(reason: fidl::PrepareFailureReason) -> Self {
        match reason {
            fidl::PrepareFailureReason::Internal => PrepareFailureReason::Internal,
            fidl::PrepareFailureReason::OutOfSpace => PrepareFailureReason::OutOfSpace,
            fidl::PrepareFailureReason::UnsupportedDowngrade => {
                PrepareFailureReason::UnsupportedDowngrade
            }
        }
    }
}

impl From<PrepareFailureReason> for fidl::PrepareFailureReason {
    fn from(reason: PrepareFailureReason) -> Self {
        match reason {
            PrepareFailureReason::Internal => fidl::PrepareFailureReason::Internal,
            PrepareFailureReason::OutOfSpace => fidl::PrepareFailureReason::OutOfSpace,
            PrepareFailureReason::UnsupportedDowngrade => {
                fidl::PrepareFailureReason::UnsupportedDowngrade
            }
        }
    }
}

impl StageFailureReason {
    fn write_to_inspect(&self, node: &inspect::Node) {
        node.record_string("reason", format!("{:?}", self))
    }
}

impl From<fidl::StageFailureReason> for StageFailureReason {
    fn from(reason: fidl::StageFailureReason) -> Self {
        match reason {
            fidl::StageFailureReason::Internal => StageFailureReason::Internal,
            fidl::StageFailureReason::OutOfSpace => StageFailureReason::OutOfSpace,
        }
    }
}

impl From<StageFailureReason> for fidl::StageFailureReason {
    fn from(reason: StageFailureReason) -> Self {
        match reason {
            StageFailureReason::Internal => fidl::StageFailureReason::Internal,
            StageFailureReason::OutOfSpace => fidl::StageFailureReason::OutOfSpace,
        }
    }
}

impl FetchFailureReason {
    fn write_to_inspect(&self, node: &inspect::Node) {
        node.record_string("reason", format!("{:?}", self))
    }
}

impl From<fidl::FetchFailureReason> for FetchFailureReason {
    fn from(reason: fidl::FetchFailureReason) -> Self {
        match reason {
            fidl::FetchFailureReason::Internal => FetchFailureReason::Internal,
            fidl::FetchFailureReason::OutOfSpace => FetchFailureReason::OutOfSpace,
        }
    }
}

impl From<FetchFailureReason> for fidl::FetchFailureReason {
    fn from(reason: FetchFailureReason) -> Self {
        match reason {
            FetchFailureReason::Internal => fidl::FetchFailureReason::Internal,
            FetchFailureReason::OutOfSpace => fidl::FetchFailureReason::OutOfSpace,
        }
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

impl Serialize for FailStageData {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        use serde::ser::SerializeStruct;

        let mut state = serializer.serialize_struct("FailStageData", 3)?;
        state.serialize_field("info", &self.info_and_progress.info)?;
        state.serialize_field("progress", &self.info_and_progress.progress)?;
        state.serialize_field("reason", &self.reason)?;
        state.end()
    }
}

impl<'de> Deserialize<'de> for FailStageData {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        use serde::de::Error;

        #[derive(Debug, Deserialize)]
        pub struct DeFailStageData {
            info: UpdateInfo,
            progress: Progress,
            reason: StageFailureReason,
        }

        let DeFailStageData { info, progress, reason } =
            DeFailStageData::deserialize(deserializer)?;

        UpdateInfoAndProgress::new(info, progress)
            .map_err(|e| D::Error::custom(e.to_string()))
            .map(|info_and_progress| info_and_progress.with_stage_reason(reason))
    }
}

impl Serialize for FailFetchData {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        use serde::ser::SerializeStruct;

        let mut state = serializer.serialize_struct("FailFetchData", 3)?;
        state.serialize_field("info", &self.info_and_progress.info)?;
        state.serialize_field("progress", &self.info_and_progress.progress)?;
        state.serialize_field("reason", &self.reason)?;
        state.end()
    }
}

impl<'de> Deserialize<'de> for FailFetchData {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        use serde::de::Error;

        #[derive(Debug, Deserialize)]
        pub struct DeFailFetchData {
            info: UpdateInfo,
            progress: Progress,
            reason: FetchFailureReason,
        }

        let DeFailFetchData { info, progress, reason } =
            DeFailFetchData::deserialize(deserializer)?;

        UpdateInfoAndProgress::new(info, progress)
            .map_err(|e| D::Error::custom(e.to_string()))
            .map(|info_and_progress| info_and_progress.with_fetch_reason(reason))
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
    Reason,
}

impl From<State> for fidl::State {
    fn from(state: State) -> Self {
        match state {
            State::Prepare => fidl::State::Prepare(fidl::PrepareData::EMPTY),
            State::Stage(UpdateInfoAndProgress { info, progress }) => {
                fidl::State::Stage(fidl::StageData {
                    info: Some(info.into()),
                    progress: Some(progress.into()),
                    ..fidl::StageData::EMPTY
                })
            }
            State::Fetch(UpdateInfoAndProgress { info, progress }) => {
                fidl::State::Fetch(fidl::FetchData {
                    info: Some(info.into()),
                    progress: Some(progress.into()),
                    ..fidl::FetchData::EMPTY
                })
            }
            State::Commit(UpdateInfoAndProgress { info, progress }) => {
                fidl::State::Commit(fidl::CommitData {
                    info: Some(info.into()),
                    progress: Some(progress.into()),
                    ..fidl::CommitData::EMPTY
                })
            }
            State::WaitToReboot(UpdateInfoAndProgress { info, progress }) => {
                fidl::State::WaitToReboot(fidl::WaitToRebootData {
                    info: Some(info.into()),
                    progress: Some(progress.into()),
                    ..fidl::WaitToRebootData::EMPTY
                })
            }
            State::Reboot(UpdateInfoAndProgress { info, progress }) => {
                fidl::State::Reboot(fidl::RebootData {
                    info: Some(info.into()),
                    progress: Some(progress.into()),
                    ..fidl::RebootData::EMPTY
                })
            }
            State::DeferReboot(UpdateInfoAndProgress { info, progress }) => {
                fidl::State::DeferReboot(fidl::DeferRebootData {
                    info: Some(info.into()),
                    progress: Some(progress.into()),
                    ..fidl::DeferRebootData::EMPTY
                })
            }
            State::Complete(UpdateInfoAndProgress { info, progress }) => {
                fidl::State::Complete(fidl::CompleteData {
                    info: Some(info.into()),
                    progress: Some(progress.into()),
                    ..fidl::CompleteData::EMPTY
                })
            }
            State::FailPrepare(reason) => fidl::State::FailPrepare(fidl::FailPrepareData {
                reason: Some(reason.into()),
                ..fidl::FailPrepareData::EMPTY
            }),
            State::FailStage(FailStageData { info_and_progress, reason }) => {
                fidl::State::FailStage(fidl::FailStageData {
                    info: Some(info_and_progress.info.into()),
                    progress: Some(info_and_progress.progress.into()),
                    reason: Some(reason.into()),
                    ..fidl::FailStageData::EMPTY
                })
            }
            State::FailFetch(FailFetchData { info_and_progress, reason }) => {
                fidl::State::FailFetch(fidl::FailFetchData {
                    info: Some(info_and_progress.info.into()),
                    progress: Some(info_and_progress.progress.into()),
                    reason: Some(reason.into()),
                    ..fidl::FailFetchData::EMPTY
                })
            }
            State::FailCommit(UpdateInfoAndProgress { info, progress }) => {
                fidl::State::FailCommit(fidl::FailCommitData {
                    info: Some(info.into()),
                    progress: Some(progress.into()),
                    ..fidl::FailCommitData::EMPTY
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
            fidl::State::Prepare(fidl::PrepareData { .. }) => State::Prepare,
            fidl::State::Stage(fidl::StageData { info, progress, .. }) => {
                State::Stage(decode_info_progress(info, progress)?)
            }
            fidl::State::Fetch(fidl::FetchData { info, progress, .. }) => {
                State::Fetch(decode_info_progress(info, progress)?)
            }
            fidl::State::Commit(fidl::CommitData { info, progress, .. }) => {
                State::Commit(decode_info_progress(info, progress)?)
            }
            fidl::State::WaitToReboot(fidl::WaitToRebootData { info, progress, .. }) => {
                State::WaitToReboot(decode_info_progress(info, progress)?)
            }
            fidl::State::Reboot(fidl::RebootData { info, progress, .. }) => {
                State::Reboot(decode_info_progress(info, progress)?)
            }
            fidl::State::DeferReboot(fidl::DeferRebootData { info, progress, .. }) => {
                State::DeferReboot(decode_info_progress(info, progress)?)
            }
            fidl::State::Complete(fidl::CompleteData { info, progress, .. }) => {
                State::Complete(decode_info_progress(info, progress)?)
            }
            fidl::State::FailPrepare(fidl::FailPrepareData { reason, .. }) => State::FailPrepare(
                reason.ok_or(DecodeStateError::MissingField(RequiredStateField::Reason))?.into(),
            ),
            fidl::State::FailStage(fidl::FailStageData { info, progress, reason, .. }) => {
                State::FailStage(
                    decode_info_progress(info, progress)?.with_stage_reason(
                        reason
                            .ok_or(DecodeStateError::MissingField(RequiredStateField::Reason))?
                            .into(),
                    ),
                )
            }
            fidl::State::FailFetch(fidl::FailFetchData { info, progress, reason, .. }) => {
                State::FailFetch(
                    decode_info_progress(info, progress)?.with_fetch_reason(
                        reason
                            .ok_or(DecodeStateError::MissingField(RequiredStateField::Reason))?
                            .into(),
                    ),
                )
            }
            fidl::State::FailCommit(fidl::FailCommitData { info, progress, .. }) => {
                State::FailCommit(decode_info_progress(info, progress)?)
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
        fidl::UpdateInfo {
            download_size: none_or_some_nonzero(info.download_size),
            ..fidl::UpdateInfo::EMPTY
        }
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
            ..fidl::InstallationProgress::EMPTY
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
                if !(0.0..=1.0).contains(&n) {
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

impl Arbitrary for FailStageData {
    type Parameters = ();
    type Strategy = BoxedStrategy<Self>;

    fn arbitrary_with((): Self::Parameters) -> Self::Strategy {
        arb_info_and_progress()
            .prop_flat_map(|(info, progress)| {
                any::<StageFailureReason>().prop_map(move |reason| {
                    UpdateInfoAndProgress { info, progress }.with_stage_reason(reason)
                })
            })
            .boxed()
    }
}

impl Arbitrary for FailFetchData {
    type Parameters = ();
    type Strategy = BoxedStrategy<Self>;

    fn arbitrary_with((): Self::Parameters) -> Self::Strategy {
        arb_info_and_progress()
            .prop_flat_map(|(info, progress)| {
                any::<FetchFailureReason>().prop_map(move |reason| {
                    UpdateInfoAndProgress { info, progress }.with_fetch_reason(reason)
                })
            })
            .boxed()
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
        assert_matches::assert_matches,
        fuchsia_inspect::{assert_data_tree, Inspector},
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

            assert_data_tree! {
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
                fidl::State::Prepare(fidl::PrepareData { .. }) => prop_assume!(false),
                fidl::State::Stage(fidl::StageData { info, progress, .. }) => break_info_progress(info, progress),
                fidl::State::Fetch(fidl::FetchData { info, progress, .. }) => break_info_progress(info, progress),
                fidl::State::Commit(fidl::CommitData { info, progress, .. }) => break_info_progress(info, progress),
                fidl::State::WaitToReboot(fidl::WaitToRebootData { info, progress, .. }) => break_info_progress(info, progress),
                fidl::State::Reboot(fidl::RebootData { info, progress, .. }) => break_info_progress(info, progress),
                fidl::State::DeferReboot(fidl::DeferRebootData { info, progress, .. }) => break_info_progress(info, progress),
                fidl::State::Complete(fidl::CompleteData { info, progress, .. }) => break_info_progress(info, progress),
                fidl::State::FailPrepare(fidl::FailPrepareData { .. }) => prop_assume!(false),
                fidl::State::FailStage(fidl::FailStageData { info, progress, .. }) => break_info_progress(info, progress),
                fidl::State::FailFetch(fidl::FailFetchData { info, progress, .. }) => break_info_progress(info, progress),
                fidl::State::FailCommit(fidl::FailCommitData { info, progress, .. }) => break_info_progress(info, progress),
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
        fn states_with_same_ids_can_merge(
            state: State,
            different_data: UpdateInfoAndProgress,
            different_prepare_reason: PrepareFailureReason,
            different_fetch_reason: FetchFailureReason,
            different_stage_reason: StageFailureReason,
        ) {
            let state_with_different_data = match state.clone() {
                 State::Prepare => State::Prepare,
                 State::Stage(_) => State::Stage(different_data),
                    State::Fetch(_) => State::Fetch(different_data),
                    State::Commit(_) => State::Commit(different_data),
                    State::WaitToReboot(_) => State::WaitToReboot(different_data),
                    State::Reboot(_) => State::Reboot(different_data),
                    State::DeferReboot(_) => State::DeferReboot(different_data),
                    State::Complete(_) => State::Complete(different_data),
                    // We currently allow merging states with different failure reasons, though
                    // we don't expect that to ever happen in practice.
                    State::FailPrepare(_) => State::FailPrepare(different_prepare_reason),
                    State::FailStage(_) => State::FailStage(different_data.with_stage_reason(different_stage_reason)),
                    State::FailFetch(_) => State::FailFetch(different_data.with_fetch_reason(different_fetch_reason)),
                    State::FailCommit(_) => State::FailCommit(different_data),
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
    fn populates_inspect_fail_stage() {
        let state = State::FailStage(
            UpdateInfoAndProgress {
                info: UpdateInfo { download_size: 4096 },
                progress: Progress { bytes_downloaded: 2048, fraction_completed: 0.5 },
            }
            .with_stage_reason(StageFailureReason::Internal),
        );
        let inspector = Inspector::new();
        state.write_to_inspect(&inspector.root());
        assert_data_tree! {
            inspector,
            root: {
                "state": "fail_stage",
                "info": {
                    "download_size": 4096u64,
                },
                "progress": {
                    "bytes_downloaded": 2048u64,
                    "fraction_completed": 0.5f64,
                },
                "reason": "Internal",
            }
        }
    }

    #[test]
    fn populates_inspect_fail_fetch() {
        let state = State::FailFetch(
            UpdateInfoAndProgress {
                info: UpdateInfo { download_size: 4096 },
                progress: Progress { bytes_downloaded: 2048, fraction_completed: 0.5 },
            }
            .with_fetch_reason(FetchFailureReason::Internal),
        );
        let inspector = Inspector::new();
        state.write_to_inspect(&inspector.root());
        assert_data_tree! {
            inspector,
            root: {
                "state": "fail_fetch",
                "info": {
                    "download_size": 4096u64,
                },
                "progress": {
                    "bytes_downloaded": 2048u64,
                    "fraction_completed": 0.5f64,
                },
                "reason": "Internal",
            }
        }
    }

    #[test]
    fn populates_inspect_fail_prepare() {
        let state = State::FailPrepare(PrepareFailureReason::OutOfSpace);
        let inspector = Inspector::new();
        state.write_to_inspect(&inspector.root());
        assert_data_tree! {
            inspector,
            root: {
                "state": "fail_prepare",
                "reason": "OutOfSpace",
            }
        }
    }

    #[test]
    fn populates_inspect_reboot() {
        let state = State::Reboot(UpdateInfoAndProgress {
            info: UpdateInfo { download_size: 4096 },
            progress: Progress { bytes_downloaded: 2048, fraction_completed: 0.5 },
        });
        let inspector = Inspector::new();
        state.write_to_inspect(&inspector.root());
        assert_data_tree! {
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
            Progress::try_from(fidl::InstallationProgress::EMPTY),
            Err(DecodeProgressError::MissingField(RequiredProgressField::FractionCompleted)),
        );
    }

    #[test]
    fn json_deserializes_state_reboot() {
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
    fn json_deserializes_state_fail_prepare() {
        assert_eq!(
            serde_json::from_value::<State>(json!({
                "id": "fail_prepare",
                "reason": "internal",
            }))
            .unwrap(),
            State::FailPrepare(PrepareFailureReason::Internal)
        );
    }

    #[test]
    fn json_deserializes_state_fail_stage() {
        assert_eq!(
            serde_json::from_value::<State>(json!({
                "id": "fail_stage",
                "info": {
                    "download_size": 100,
                },
                "progress": {
                    "bytes_downloaded": 100,
                    "fraction_completed": 1.0,
                },
                "reason": "out_of_space",
            }))
            .unwrap(),
            State::FailStage(
                UpdateInfoAndProgress {
                    info: UpdateInfo { download_size: 100 },
                    progress: Progress { bytes_downloaded: 100, fraction_completed: 1.0 },
                }
                .with_stage_reason(StageFailureReason::OutOfSpace)
            )
        );
    }

    #[test]
    fn json_deserializes_state_fail_fetch() {
        assert_eq!(
            serde_json::from_value::<State>(json!({
                "id": "fail_fetch",
                "info": {
                    "download_size": 100,
                },
                "progress": {
                    "bytes_downloaded": 100,
                    "fraction_completed": 1.0,
                },
                "reason": "out_of_space",
            }))
            .unwrap(),
            State::FailFetch(
                UpdateInfoAndProgress {
                    info: UpdateInfo { download_size: 100 },
                    progress: Progress { bytes_downloaded: 100, fraction_completed: 1.0 },
                }
                .with_fetch_reason(FetchFailureReason::OutOfSpace)
            )
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
