// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "register", description = "")]
pub struct RegisterCommand {
    /// repositories will be named `NAME`. Defaults to `devhost`.
    #[argh(positional, default = "\"devhost\".to_string()")]
    pub name: String,

    /// set up a rewrite rule mapping each `alias` host to
    /// to the repository identified by `name`.
    #[argh(option)]
    pub alias: Vec<String>,
}
