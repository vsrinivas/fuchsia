// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use thermal_config::ThermalConfig;

/// This is a simple unit test that is used to test the `thermal_config` GN template. The test works
/// by building a `thermal_config` target for the unit test package. Then this unit test function
/// verifies the file is present and valid.
#[test]
fn test_parse_from_filesytem() -> Result<(), Error> {
    let test_config_path = "/pkg/config/power_manager/test_config.json";
    ThermalConfig::read(&std::path::Path::new(test_config_path)).map(|_| ())
}
