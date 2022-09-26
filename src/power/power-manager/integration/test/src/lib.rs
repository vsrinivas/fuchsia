// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use power_manager_integration_test_lib::{TestEnvBuilder, ThermalClient};

/// Integration test for Power Manager to verify correct behavior of the thermal client service.
#[fuchsia::test]
async fn thermal_client_service_test() {
    let mut env = TestEnvBuilder::new()
        .power_manager_node_config_path(&"/pkg/thermal_client_service_test/node_config.json5")
        .temperature_driver_paths(vec!["/dev/class/thermal/000"])
        .build()
        .await;

    // The client name here ('client0') must match the name of the client from the thermal
    // configuration file (../config_files/thermal_config.json5)
    let mut client0 = ThermalClient::new(&env, "client0");

    // Verify initial thermal state is 0
    assert_eq!(client0.get_thermal_state().await.unwrap(), 0);

    // Set temperature to 80 which is above the configured "onset" temperature of 50 (see the
    // `temperature_input_configs` section in ../config_files/node_config.json5), causing thermal
    // load to be nonzero
    env.set_temperature("/dev/class/thermal/000", 80.0);

    // Verify thermal state for client0 is now 1
    assert_eq!(client0.get_thermal_state().await.unwrap(), 1);

    // Set temperature back below the onset threshold
    env.set_temperature("/dev/class/thermal/000", 40.0);

    // Verify client0 thermal state goes back to 0
    assert_eq!(client0.get_thermal_state().await.unwrap(), 0);

    env.destroy().await;
}
