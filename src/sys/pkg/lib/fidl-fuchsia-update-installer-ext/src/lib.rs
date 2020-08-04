// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! `fidl_fuchsia_update_installer_ext` contains wrapper types around the auto-generated
//! `fidl_fuchsia_update_installer` bindings.

pub mod state;
pub use state::{Progress, State, UpdateInfo};
