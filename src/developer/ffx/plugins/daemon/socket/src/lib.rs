// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Result, Context};
use ffx_core::ffx_plugin;
use ffx_daemon::SocketDetails;
use ffx_daemon_socket_args::SocketCommand;
use ffx_writer::Writer;

#[ffx_plugin()]
pub async fn daemon_socket(
    _cmd: SocketCommand,
    #[ffx(machine = SocketDetails)] writer: Writer,
) -> Result<()> {
    let context = ffx_config::global_env_context().context("Loading global environment context")?;
    let env = context.load().await?;

    let socket_path = env.get_ascendd_path()?;

    let details = SocketDetails::new(socket_path);

    if writer.is_machine() {
        writer.machine(&details).context("writing machine representation of socket details")
    } else {
        Ok(println!("{details}"))
    }
}
