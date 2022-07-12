// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{Context, Error},
    fidl::endpoints::Proxy,
    fuchsia_async as fasync, tracing,
};

const MAX_RETRY_COUNT: u32 = 10;
const RETRY_COUNTER_RESET_PERIOD_MIN: i64 = 5;
const RETRY_COUNTER_PERIOD_MAX_SEC: i64 = 60;

#[fuchsia::main(logging_tags = ["lowpan", "monitor"])]
async fn main() -> Result<(), Error> {
    tracing::info!("lowpan-monitor started");
    let mut retry_counter: u32 = 0;

    // Attempt to launch lowpan-ot-driver
    loop {
        let last_launch_attempt_timestamp = fasync::Time::now();

        let binder_proxy = fuchsia_component::client::connect_to_protocol::<
            fidl_fuchsia_component::BinderMarker,
        >()
        .context("failed to connect to fuchsia.component.Binder in lowpan-ot-driver")?;

        tracing::info!("lowpan-monitor connected");

        binder_proxy.on_closed().await?;

        if (fasync::Time::now() - last_launch_attempt_timestamp).into_minutes()
            >= RETRY_COUNTER_RESET_PERIOD_MIN
        {
            retry_counter = 0;
        }

        retry_counter += 1;

        if retry_counter >= MAX_RETRY_COUNT {
            break;
        }

        // Exponential backoff between relaunch retries
        let retry_delay_backoff_sec =
            if retry_counter < 6 { 1 << retry_counter } else { RETRY_COUNTER_PERIOD_MAX_SEC };

        tracing::info!("lowpan-monitor detects the termination of lowpan-ot-driver (failed {} times), restarting in {} sec", retry_counter, retry_delay_backoff_sec);

        fasync::Timer::new(fasync::Time::after(fuchsia_zircon::Duration::from_seconds(
            retry_delay_backoff_sec,
        )))
        .await;
    }
    tracing::error!(
        "too many failures when attempt to launch lowpan-ot-driver, lowpan-monitor will stop"
    );
    Ok(())
}
