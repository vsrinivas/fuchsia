// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use tracing::debug;

use crate::config::HandsFreeFeatureSupport;
use crate::hfp::Hfp;
use crate::profile::register_hands_free;

mod config;
mod features;
mod hfp;
mod peer;
mod profile;
mod service_definition;

#[fuchsia::main(logging_tags = ["hfp-hf"])]
async fn main() -> Result<(), Error> {
    let feature_support = HandsFreeFeatureSupport::load()?;
    debug!("Starting HFP Hands Free");
    let (profile_client, profile_svc) = register_hands_free(feature_support)?;
    let hfp = Hfp::new(profile_client, profile_svc, feature_support);
    hfp.run().await?;
    Ok(())
}
