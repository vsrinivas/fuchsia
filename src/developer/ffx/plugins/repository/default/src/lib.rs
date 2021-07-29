// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_repository_default_args::{RepositoryDefaultCommand, SubCommand},
};

pub(crate) const CONFIG_KEY_DEFAULT: &str = "repository.default";

#[ffx_plugin("ffx_repository")]
pub async fn exec_repository_default(cmd: RepositoryDefaultCommand) -> Result<()> {
    exec_repository_default_impl(cmd, &mut std::io::stdout()).await
}

pub async fn exec_repository_default_impl<W: std::io::Write>(
    cmd: RepositoryDefaultCommand,
    writer: &mut W,
) -> Result<()> {
    match &cmd.subcommand {
        SubCommand::Get(_) => {
            let res: String = ffx_config::get(CONFIG_KEY_DEFAULT).await.unwrap_or("".to_owned());
            writeln!(writer, "{}", res)?;
        }
        SubCommand::Set(set) => {
            ffx_config::set(
                (CONFIG_KEY_DEFAULT, &set.level, &set.build_dir),
                serde_json::Value::String(set.name.clone()),
            )
            .await?
        }
        SubCommand::Unset(unset) => {
            let _ = ffx_config::remove((CONFIG_KEY_DEFAULT, &unset.level, &unset.build_dir))
                .await
                .map_err(|e| eprintln!("warning: {}", e));
        }
    };
    Ok(())
}
