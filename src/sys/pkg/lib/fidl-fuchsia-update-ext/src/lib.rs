// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! `fidl_fuchsia_update_ext` contains wrapper types around the auto-generated `fidl_fuchsia_update`
//! bindings.

mod types;
pub use crate::types::{
    proptest_util::random_version_available, CheckOptions, CheckOptionsBuilder,
    CheckOptionsDecodeError, Initiator, InstallationDeferredData, InstallationErrorData,
    InstallationProgress, InstallingData, State, UpdateInfo,
};
