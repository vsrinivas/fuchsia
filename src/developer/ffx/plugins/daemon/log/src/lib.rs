// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::ffx_bail,
    ffx_config::get,
    ffx_core::ffx_plugin,
    ffx_daemon_log_args::LogCommand,
    std::fs::File,
    std::io::{BufRead, BufReader},
    std::path::PathBuf,
};

#[ffx_plugin()]
pub async fn daemon(_cmd: LogCommand) -> Result<()> {
    if !get("log.enabled").await? {
        ffx_bail!("Logging is not enabled.");
    }
    let mut log_path: PathBuf = get("log.dir").await?;
    log_path.push("ffx.daemon.log");
    let file = File::open(log_path).or_else(|_| ffx_bail!("Daemon log not found."))?;
    let reader = BufReader::new(file);
    for line in reader.lines() {
        println!("{}", line?);
    }
    Ok(())
}
