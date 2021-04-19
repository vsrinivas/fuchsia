// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_repository_args::{RepositoryCommand, SubCommand},
};

mod serve;

#[ffx_plugin("repository")]
pub async fn cmd_repository(cmd: RepositoryCommand) -> Result<()> {
    match cmd.sub {
        SubCommand::Serve(subcmd) => serve::serve(subcmd).await,
    }
}
