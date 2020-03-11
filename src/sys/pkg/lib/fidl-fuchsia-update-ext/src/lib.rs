// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! `fidl_fuchsia_update_ext` contains wrapper types around the auto-generated `fidl_fuchsia_update`
//! bindings.

mod types;
pub use crate::types::{
    proptest_util::{
        random_installation_deferred_data, random_installation_error_data,
        random_installation_progress, random_installing_data, random_update_info,
        random_version_available,
    },
    CheckOptions, CheckOptionsBuilder, InstallationDeferredData, InstallationErrorData,
    InstallationProgress, InstallingData, State, UpdateInfo,
};
