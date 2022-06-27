// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_core::ffx_plugin;
use ffx_repository_default_args::{RepositoryDefaultCommand, SubCommand};

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
            ffx_config::query(CONFIG_KEY_DEFAULT)
                .level(Some(set.level))
                .build(set.build_dir.as_deref())
                .set(serde_json::Value::String(set.name.clone()))
                .await?
        }
        SubCommand::Unset(unset) => {
            let _ = ffx_config::query(CONFIG_KEY_DEFAULT)
                .level(Some(unset.level))
                .build(unset.build_dir.as_deref())
                .remove()
                .await
                .map_err(|e| eprintln!("warning: {}", e));
        }
    };
    Ok(())
}
