// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, pbms::AuthFlowChoice, std::path::PathBuf};

/// Retrieve image data.
#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "get")]
pub struct GetCommand {
    /// get the data again, even if it's already present locally.
    #[argh(switch)]
    pub force: bool,

    /// use specific auth flow for oauth2.
    #[argh(option, default = "AuthFlowChoice::Default")]
    pub auth: AuthFlowChoice,

    /// use an insecure oauth2 token flow (deprecated).
    #[argh(switch)]
    pub oob_auth: bool,

    /// repositories will be named `NAME`. Defaults to the product bundle name.
    #[argh(option)]
    pub repository: Option<String>,

    /// url to the product bundle to download.
    #[argh(positional)]
    pub product_bundle_url: String,

    /// local directory to download the product bundle into.
    #[argh(positional, default = "PathBuf::from(\"local_pb\")")]
    pub out_dir: PathBuf,
}
