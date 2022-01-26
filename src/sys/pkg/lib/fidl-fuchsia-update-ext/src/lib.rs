// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! `fidl_fuchsia_update_ext` contains wrapper types around the auto-generated
//! `fidl_fuchsia_update` bindings.

#[cfg(target_os = "fuchsia")]
mod commit;
mod types;

#[cfg(target_os = "fuchsia")]
pub use crate::commit::{query_commit_status, CommitStatus};

pub use crate::types::{
    proptest_util::random_version_available, AttemptOptions, CheckOptions, CheckOptionsBuilder,
    CheckOptionsDecodeError, Initiator, InstallationDeferredData, InstallationErrorData,
    InstallationProgress, InstallingData, State, UpdateInfo,
};
