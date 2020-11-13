// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {anyhow::Error, argh::FromArgs, log::warn};

mod config;
mod executor;

/// args used to configure lapis.
#[derive(Debug, Default, FromArgs, PartialEq)]
#[argh(subcommand, name = "lapis")]
pub struct Args {
    /// required minimal sample rate.
    #[argh(option)]
    minimum_sample_rate_sec: i64,
}

pub async fn main(opt: Args) -> Result<(), Error> {
    let sampler_executor = executor::SamplerExecutor::new(config::SamplerConfig::from_directory(
        opt.minimum_sample_rate_sec,
        "/config/data/metrics",
    ))
    .await?;

    sampler_executor.execute().await;
    warn!("Diagnostics sampler is unexpectedly exiting.");
    Ok(())
}
