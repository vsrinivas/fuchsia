// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, ffx_core::ffx_plugin, ffx_preflight_args::PreflightCommand};

#[ffx_plugin()]
pub async fn preflight_cmd(_cmd: PreflightCommand) -> Result<()> {
    println!("Hello, Fuchsia!");
    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_preflight_cmd() -> Result<()> {
        let response = preflight_cmd(PreflightCommand {}).await;
        assert!(response.unwrap() == ());
        Ok(())
    }
}
