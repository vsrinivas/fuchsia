// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Command line argument parsing for fgsutil.

use argh::FromArgs;

#[derive(Debug, Default, FromArgs, PartialEq)]
#[argh(
    name = "fgsutil",
    description = "GCS download utility for Fuchsia",
    note = "GCS authentication credentials are stored in ~/.fuchsia/fgsutil/config",
    error_code(1, "Invalid credentials.")
)]
pub struct Args {
    /// display version
    #[argh(switch)]
    pub version: bool,
}
