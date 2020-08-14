// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_test_policy as ftest, fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_zircon as zx,
    futures::future::{select, Either},
    security_policy_test_util::{bind_child, start_policy_test},
};

const COMPONENT_MANAGER_URL: &str = "fuchsia-pkg://fuchsia.com/security-policy-critical-integration-test#meta/component_manager.cmx";
const ROOT_URL: &str =
    "fuchsia-pkg://fuchsia.com/security-policy-critical-integration-test#meta/test_root.cm";
const TEST_CONFIG_PATH: &str = "/pkg/data/cm_config";

const COMPONENT_MANAGER_DEATH_TIMEOUT: i64 = 5;

#[fasync::run_singlethreaded(test)]
async fn verify_main_process_critical_default_denied() -> Result<(), Error> {
    let (mut test, realm) =
        start_policy_test(COMPONENT_MANAGER_URL, ROOT_URL, TEST_CONFIG_PATH).await?;

    let child_name = "policy_not_requested";
    let exposed_dir = bind_child(&realm, child_name).await.expect("bind should succeed");
    let exit_controller =
        client::connect_to_protocol_at_dir::<ftest::ExitControllerMarker>(&exposed_dir)
            .context("failed to connect to test service after bind")?;

    exit_controller.exit(1)?;

    // The child will now exit. Observe this by seeing the exit_controller handle be closed.
    let exit_controller_channel =
        exit_controller.into_channel().expect("failed to turn exit_controller into channel");
    let on_signal_fut =
        fasync::OnSignals::new(&exit_controller_channel, zx::Signals::CHANNEL_PEER_CLOSED);
    on_signal_fut.await.context("failed to wait for exposed dir handle to become readable")?;

    // component_manager should still be running. Observe this by not seeing component_manager exit
    // within COMPONENT_MANAGER_DEATH_TIMEOUT seconds.
    let timer = fasync::Timer::new(fasync::Time::after(zx::Duration::from_seconds(
        COMPONENT_MANAGER_DEATH_TIMEOUT,
    )));
    match select(timer, test.component_manager_app.wait()).await {
        Either::Left(((), _)) => return Ok(()),
        Either::Right((Ok(exit_status), _)) => {
            if exit_status.exited() {
                panic!("unexpected termination of component_manager");
            } else {
                panic!("unexpected message on realm channel");
            }
        }
        Either::Right((Err(e), _)) => return Err(e.into()),
    }
}

#[fasync::run_singlethreaded(test)]
async fn verify_main_process_critical_nonzero_flag_used() -> Result<(), Error> {
    let (mut test, realm) =
        start_policy_test(COMPONENT_MANAGER_URL, ROOT_URL, TEST_CONFIG_PATH).await?;

    let child_name = "policy_allowed";
    let exposed_dir = bind_child(&realm, child_name).await.expect("bind should succeed");
    let exit_controller =
        client::connect_to_protocol_at_dir::<ftest::ExitControllerMarker>(&exposed_dir)
            .context("failed to connect to test service after bind")?;

    exit_controller.exit(0)?;

    // The child will now exit. Observe this by seeing the exit_controller handle be closed.
    let exit_controller_channel =
        exit_controller.into_channel().expect("failed to turn exit_controller into channel");
    let on_signal_fut =
        fasync::OnSignals::new(&exit_controller_channel, zx::Signals::CHANNEL_PEER_CLOSED);
    on_signal_fut.await.context("failed to wait for exposed dir handle to become readable")?;

    // component_manager should still be running. The critical marking will not kill
    // component_manager's job in this case because the critical component exited with a 0 return
    // code. Observe this by not seeing component_manager exit within
    // COMPONENT_MANAGER_DEATH_TIMEOUT seconds.
    let timer = fasync::Timer::new(fasync::Time::after(zx::Duration::from_seconds(
        COMPONENT_MANAGER_DEATH_TIMEOUT,
    )));
    match select(timer, test.component_manager_app.wait()).await {
        Either::Left(((), _)) => return Ok(()),
        Either::Right((Ok(exit_status), _)) => {
            if exit_status.exited() {
                panic!("unexpected termination of component_manager");
            } else {
                panic!("unexpected message on realm channel");
            }
        }
        Either::Right((Err(e), _)) => return Err(e.into()),
    }
}

#[fasync::run_singlethreaded(test)]
async fn verify_main_process_critical_allowed() -> Result<(), Error> {
    let (mut test, realm) =
        start_policy_test(COMPONENT_MANAGER_URL, ROOT_URL, TEST_CONFIG_PATH).await?;

    let child_name = "policy_allowed";
    let exposed_dir = bind_child(&realm, child_name).await.expect("bind should succeed");
    let exit_controller =
        client::connect_to_protocol_at_dir::<ftest::ExitControllerMarker>(&exposed_dir)
            .context("failed to connect to test service after bind")?;

    exit_controller.exit(1)?;

    // The child will now exit. Observe this by seeing the exit_controller handle be closed.
    let exit_controller_channel =
        exit_controller.into_channel().expect("failed to turn exit_controller into channel");
    let on_signal_fut =
        fasync::OnSignals::new(&exit_controller_channel, zx::Signals::CHANNEL_PEER_CLOSED);
    on_signal_fut.await.context("failed to wait for exposed dir handle to become readable")?;

    // component_manager should be killed too as a result of the critical marking.
    let exit_status = test.component_manager_app.wait().await?;
    assert!(exit_status.exited(), "component_manager failed to exit: {:?}", exit_status.reason());

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn verify_main_process_critical_denied() -> Result<(), Error> {
    let (_test, realm) =
        start_policy_test(COMPONENT_MANAGER_URL, ROOT_URL, TEST_CONFIG_PATH).await?;

    // This security policy is enforced inside the ELF runner. The component will fail to launch
    // because of the denial, but BindChild will return success because the runtime successfully
    // asks the runner to start the component. We watch for the exposed_dir to get dropped to
    // detect the launch failure.
    // N.B. We could alternatively look for a Started and then a Stopped event to verify that the
    // component failed to launch, but fxb/53414 prevented that at the time this was written.
    let child_name = "policy_denied";
    let exposed_dir = bind_child(&realm, child_name).await.expect("bind should succeed");

    let chan = exposed_dir.into_channel().unwrap();
    fasync::OnSignals::new(&chan, zx::Signals::CHANNEL_PEER_CLOSED)
        .await
        .expect("failed to wait for exposed_dir PEER_CLOSED");

    Ok(())
}
