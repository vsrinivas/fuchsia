// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use sampler_config::SamplerConfig;

/// Parses every config file in the production config directory
/// to make sure there are no malformed configurations being submitted.
#[fuchsia::test]
async fn validate_sampler_configs() {
    let config_directory = "/pkg/config/metrics";
    let fire_directory = "/pkg/config/fire";
    // Since this program validates multiple config directories individually, failing on Err() will
    // validate whatever config files are present without requiring projects to be generated.
    SamplerConfig::from_directories(60, &config_directory, &fire_directory).unwrap();
}
