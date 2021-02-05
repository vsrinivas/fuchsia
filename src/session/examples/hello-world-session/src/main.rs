// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_info,
};

/// Creates a simple session that just prints "Hello World" to the syslog.
#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["hello_world_session"])
        .context("Failed to initialize logger.")?;

    fx_log_info!("Hello World!");

    Ok(())
}

#[cfg(test)]
mod tests {
    #[fuchsia::test]
    async fn smoke_test() {
        assert!(true);
    }
}
