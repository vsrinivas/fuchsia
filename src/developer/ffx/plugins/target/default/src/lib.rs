// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_target_default_args::{SubCommand, TargetDefaultCommand},
};

pub(crate) const TARGET_DEFAULT_KEY: &str = "target.default";

#[ffx_plugin()]
pub async fn exec_target_default(cmd: TargetDefaultCommand) -> Result<()> {
    match &cmd.subcommand {
        SubCommand::Get(_) => {
            let res: String = ffx_config::get(TARGET_DEFAULT_KEY).await.unwrap_or("".to_owned());
            println!("{}", res);
        }
        SubCommand::Set(set) => {
            ffx_config::set(
                (TARGET_DEFAULT_KEY, &set.level, &set.build_dir),
                serde_json::Value::String(set.nodename.clone()),
            )
            .await?
        }
    };
    Ok(())
}
