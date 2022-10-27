// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]
#![allow(clippy::let_unit_value)]

use {anyhow::Error, fuchsia_async as fasync};

mod args;
mod channel;
mod check;
mod commit;
mod install;
mod monitor_state;
mod monitor_updates;
mod revert;

async fn handle_cmd(cmd: args::Command) -> Result<(), Error> {
    match cmd {
        args::Command::Channel(args::Channel { cmd }) => {
            crate::channel::handle_channel_control_cmd(cmd).await?;
        }
        args::Command::CheckNow(check_now) => {
            crate::check::handle_check_now_cmd(check_now).await?;
        }
        args::Command::MonitorUpdates(_) => {
            crate::monitor_updates::handle_monitor_updates_cmd().await?;
        }
        args::Command::ForceInstall(args) => {
            crate::install::handle_force_install(
                args.update_pkg_url,
                args.reboot,
                args.service_initiated,
            )
            .await?;
        }
        args::Command::WaitForCommit(_) => {
            crate::commit::handle_wait_for_commit().await?;
        }
        args::Command::Revert(_) => {
            crate::revert::handle_revert().await?;
        }
    }
    Ok(())
}

pub fn main() -> Result<(), Error> {
    let mut executor = fasync::LocalExecutor::new()?;
    let args::Update { cmd } = argh::from_env();
    executor.run_singlethreaded(handle_cmd(cmd))
}
