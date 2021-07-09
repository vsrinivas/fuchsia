// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use sampler_config::SamplerConfig;

/// Parses every config file in the production config directory
/// to make sure there are no malformed configurations being submitted.
#[fuchsia::test]
async fn validate_sampler_configs() {
    let config_directory = "/pkg/config/metrics";
    match SamplerConfig::from_directory(60, &config_directory) {
        Ok(config) => {
            assert!(!config.project_configs.is_empty());
        }
        Err(e) => {
            panic!("{:?}", e);
        }
    }
}
