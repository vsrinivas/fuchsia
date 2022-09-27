// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_core::ffx_plugin;
use ffx_target_default_args::{SubCommand, TargetDefaultCommand, TargetDefaultGetCommand};

pub(crate) const TARGET_DEFAULT_KEY: &str = "target.default";

#[ffx_plugin()]
pub async fn exec_target_default(cmd: TargetDefaultCommand) -> Result<()> {
    exec_target_default_impl(cmd, &mut std::io::stdout()).await
}

pub async fn exec_target_default_impl<W: std::io::Write>(
    cmd: TargetDefaultCommand,
    writer: &mut W,
) -> Result<()> {
    match &cmd.subcommand {
        SubCommand::Get(TargetDefaultGetCommand { level: Some(level), build_dir }) => {
            let res: String = ffx_config::query(TARGET_DEFAULT_KEY)
                .level(Some(*level))
                .build(build_dir.as_deref().map(|dir| dir.into()))
                .get()
                .await
                .unwrap_or("".to_owned());
            writeln!(writer, "{}", res)?;
        }
        SubCommand::Get(_) => {
            let res: String = ffx_config::get(TARGET_DEFAULT_KEY).await.unwrap_or("".to_owned());
            writeln!(writer, "{}", res)?;
        }
        SubCommand::Set(set) => {
            ffx_config::query(TARGET_DEFAULT_KEY)
                .level(Some(set.level))
                .build(set.build_dir.as_deref().map(|dir| dir.into()))
                .set(serde_json::Value::String(set.nodename.clone()))
                .await?
        }
        SubCommand::Unset(unset) => {
            let _ = ffx_config::query(TARGET_DEFAULT_KEY)
                .level(Some(unset.level))
                .build(unset.build_dir.as_deref().map(|dir| dir.into()))
                .remove()
                .await
                .map_err(|e| eprintln!("warning: {}", e));
        }
    };
    Ok(())
}
