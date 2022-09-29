// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_hardware_power_statecontrol as fpower, fuchsia_async as fasync,
    power_manager_integration_test_lib::{
        client_connectors::{RebootWatcherClient, ThermalClient},
        TestEnvBuilder,
    },
};

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

/// Verifies that shutdown/reboot requests sent to the Power Manager are handled as expected:
///     1) forward the reboot reason to connected RebootWatcher clients
///     2) update Driver Manager with the appropriate termination SystemPowerState
///     3) send a shutdown request to the system controller
#[fuchsia::test]
async fn shutdown_test() {
    let mut env = TestEnvBuilder::new()
        .power_manager_node_config_path(&"/pkg/shutdown_test/node_config.json5")
        .build()
        .await;

    let mut reboot_watcher = RebootWatcherClient::new(&env).await;

    // Send a reboot request to the Power Manager (in a separate Task because it never returns)
    let shutdown_client = env.connect_to_protocol::<fpower::AdminMarker>();
    let _shutdown_task =
        fasync::Task::local(shutdown_client.reboot(fpower::RebootReason::SystemUpdate));

    // Verify Power Manager forwards the reboot reason to the watcher client
    assert_eq!(reboot_watcher.get_reboot_reason().await, fpower::RebootReason::SystemUpdate);

    // Verify the system controller service gets the shutdown request from Power Manager
    env.mocks.system_controller_service.wait_for_shutdown_request().await;

    // Verify the Driver Manager received the correct termination state request from Power Manager.
    // The termination state should only be checked after we know the Power Manager has sent the
    // request to Driver Manager. In this case, we know that to be true once the previous
    // `wait_for_shutdown_request` completes.
    assert_eq!(
        env.mocks.driver_manager.get_current_termination_state(),
        fpower::SystemPowerState::Reboot
    );

    env.destroy().await;
}
