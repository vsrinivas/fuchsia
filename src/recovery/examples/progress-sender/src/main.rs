// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{self, Error};
use fidl_fuchsia_recovery_ui::{ProgressRendererMarker, ProgressRendererSynchronousProxy, Status};
use fuchsia_component::client::connect_channel_to_protocol;
use fuchsia_syslog::{fx_log_debug, fx_log_err, fx_log_info};
use fuchsia_zircon as zx;
use rand::prelude::*;

#[fuchsia::main(logging = true)]
async fn main() -> Result<(), Error> {
    // Need inner_main to catch top level errors for now
    match inner_main().await {
        Ok(_) => Ok(()),
        Err(e) => {
            fx_log_err!("Error occurred running progress sender: {:?}", e);
            Err(e)
        }
    }
}

// TODO(fxbug.dev/89425): Ideally we wouldn't need to have separate inner_main() and main()
// functions in order to catch and log top-level errors.  Instead, the #[fuchsia::main] macro
// could catch and log the error.
async fn inner_main() -> Result<(), Error> {
    fx_log_info!("Progress sender running");
    let (server_end, client_end) = zx::Channel::create()?;
    connect_channel_to_protocol::<ProgressRendererMarker>(server_end)?;
    let renderer = ProgressRendererSynchronousProxy::new(client_end);

    fx_log_info!("Sleeping for 3 seconds");
    std::thread::sleep(std::time::Duration::from_secs(3));
    fx_log_info!("Sleep complete, sending progress updates");

    let mut rng = rand::thread_rng();
    let num_updates = 200;
    for i in 0..num_updates {
        // Increment progress
        let progress = (1.0f32 / num_updates as f32) * i as f32;
        fx_log_debug!("Sending: {}", progress);
        if i % 17 == 0 {
            let sleep_time: f64 = rng.gen();
            std::thread::sleep(std::time::Duration::from_millis((sleep_time * 800f64) as u64));
        }
        renderer
            .render(Status::Active, progress, zx::Time::INFINITE)
            .expect("Died while sending progress update");
        if i > 100 && i < 150 {
            // Fast update section for fun and for profit
            std::thread::sleep(std::time::Duration::from_millis(10));
        } else {
            std::thread::sleep(std::time::Duration::from_millis(50));
        }
    }

    // Send complete message
    renderer
        .render(Status::Complete, 100.0f32, zx::Time::INFINITE)
        .expect("Should have sent complete message");
    fx_log_info!("Sent progress complete");
    Ok(())
}
