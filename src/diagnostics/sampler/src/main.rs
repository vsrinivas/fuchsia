// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{Context, Error},
    argh::FromArgs,
    fuchsia_async as fasync,
    log::warn,
};

mod config;
mod executor;

/// args used to configure lapis.
#[derive(Debug, Default, FromArgs)]
pub struct Args {
    /// required minimal sample rate.
    #[argh(option)]
    minimum_sample_rate_sec: i64,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let opt: Args = argh::from_env();

    fuchsia_syslog::init_with_tags(&["sampler"])
        .context("Initializing diagnostics sampler logging")
        .unwrap();

    let sampler_executor = executor::SamplerExecutor::new(config::SamplerConfig::from_directory(
        opt.minimum_sample_rate_sec,
        "/config/data/metrics",
    ))
    .await?;

    sampler_executor.execute().await;
    warn!("Diagnostics sampler is unexpectedly exiting.");
    Ok(())
}
