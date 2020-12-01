// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {anyhow::Error, argh::FromArgs, log::warn};

pub mod config;
mod executor;

/// The name of the subcommand and the logs-tag.
pub const PROGRAM_NAME: &str = "lapis";

/// args used to configure lapis.
#[derive(Debug, Default, FromArgs, PartialEq)]
#[argh(subcommand, name = "lapis")]
pub struct Args {
    /// required minimal sample rate.
    #[argh(option)]
    minimum_sample_rate_sec: i64,
}

pub async fn main(opt: Args) -> Result<(), Error> {
    match config::SamplerConfig::from_directory(opt.minimum_sample_rate_sec, "/config/data/metrics")
    {
        Ok(sampler_config) => {
            let sampler_executor = executor::SamplerExecutor::new(sampler_config).await?;

            sampler_executor.execute().await;
            warn!("Diagnostics sampler is unexpectedly exiting.");
            Ok(())
        }
        Err(e) => {
            warn!("Failed to parse lapis configurations from /config/data/metric: {:?}", e);
            Ok(())
        }
    }
}
