// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fuchsia_syslog::{fx_log_err, fx_log_info},
    std::fs,
};

/// Persists the current channel after a successful update.
pub async fn update_current_channel() {
    fx_log_info!("updating current channel");
    const TARGET_PATH: &str = "/misc/ota/target_channel.json";
    const CURRENT_TEMP_PATH: &str = "/misc/ota/current_channel.json.part";
    const CURRENT_PATH: &str = "/misc/ota/current_channel.json";

    // FIXME synchronous IO in an async context.

    let res: Result<(), Error> = async {
        let channel = fs::read(TARGET_PATH).context("no target channel recorded")?;
        fs::write(CURRENT_TEMP_PATH, channel).context("write current channel config")?;
        fs::rename(CURRENT_TEMP_PATH, CURRENT_PATH).context("commit current channel config")?;
        Ok(())
    }
    .await;

    if let Err(e) = res {
        fx_log_err!("Could not persist current channel: {:#}", e);
    }
}
