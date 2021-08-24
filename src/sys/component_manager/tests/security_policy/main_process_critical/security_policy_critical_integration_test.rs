// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    component_events::{events::*, matcher::*},
    fidl::endpoints::Proxy,
    fidl_fuchsia_component as fcomponent, fidl_test_policy as ftest, fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_zircon as zx,
    futures::future::{select, Either},
    security_policy_test_util::{open_exposed_dir, start_policy_test},
};

const COMPONENT_MANAGER_URL: &str = "fuchsia-pkg://fuchsia.com/security-policy-critical-integration-test#meta/component_manager.cmx";
const ROOT_URL: &str =
    "fuchsia-pkg://fuchsia.com/security-policy-critical-integration-test#meta/test_root.cm";
const TEST_CONFIG_PATH: &str = "/pkg/data/cm_config";

const COMPONENT_MANAGER_DEATH_TIMEOUT: i64 = 5;

#[fasync::run_singlethreaded(test)]
async fn verify_main_process_critical_default_denied() -> Result<(), Error> {
    let (mut test, realm, _event) =
        start_policy_test(COMPONENT_MANAGER_URL, ROOT_URL, TEST_CONFIG_PATH).await?;

    let child_name = "policy_not_requested";
    let exposed_dir = open_exposed_dir(&realm, child_name).await.expect("bind should succeed");
    let exit_controller =
        client::connect_to_protocol_at_dir_root::<ftest::ExitControllerMarker>(&exposed_dir)
            .context("failed to connect to test service after bind")?;

    exit_controller.exit(1)?;

    // The child will now exit. Observe this by seeing the exit_controller handle be closed.
    exit_controller
        .on_closed()
        .await
        .context("failed to wait for exposed dir handle to become readable")?;

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
    let (mut test, realm, _event_stream) =
        start_policy_test(COMPONENT_MANAGER_URL, ROOT_URL, TEST_CONFIG_PATH).await?;

    let child_name = "policy_allowed";
    let exposed_dir = open_exposed_dir(&realm, child_name).await.expect("bind should succeed");
    let exit_controller =
        client::connect_to_protocol_at_dir_root::<ftest::ExitControllerMarker>(&exposed_dir)
            .context("failed to connect to test service after bind")?;

    exit_controller.exit(0)?;

    // The child will now exit. Observe this by seeing the exit_controller handle be closed.
    exit_controller
        .on_closed()
        .await
        .context("failed to wait for exposed dir handle to become readable")?;

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
    let (mut test, realm, _event_stream) =
        start_policy_test(COMPONENT_MANAGER_URL, ROOT_URL, TEST_CONFIG_PATH).await?;

    let child_name = "policy_allowed";
    let exposed_dir = open_exposed_dir(&realm, child_name).await.expect("bind should succeed");
    let exit_controller =
        client::connect_to_protocol_at_dir_root::<ftest::ExitControllerMarker>(&exposed_dir)
            .context("failed to connect to test service after bind")?;

    exit_controller.exit(1)?;

    // The child will now exit. Observe this by seeing the exit_controller handle be closed.
    exit_controller
        .on_closed()
        .await
        .context("failed to wait for exposed dir handle to become readable")?;

    // component_manager should be killed too as a result of the critical marking.
    let exit_status = test.component_manager_app.wait().await?;
    assert!(exit_status.exited(), "component_manager failed to exit: {:?}", exit_status.reason());

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn verify_main_process_critical_denied() -> Result<(), Error> {
    let (_test, realm, mut event_stream) =
        start_policy_test(COMPONENT_MANAGER_URL, ROOT_URL, TEST_CONFIG_PATH).await?;

    let child_name = "policy_denied";
    let exposed_dir =
        open_exposed_dir(&realm, child_name).await.expect("open exposed dir should succeed");
    client::connect_to_protocol_at_dir_root::<fcomponent::BinderMarker>(&exposed_dir)
        .context("failed to connect to fuchsia.component.Binder of child")?;

    let mut matcher = EventMatcher::ok().moniker(format!("./{}:0", child_name));
    matcher.expect_match::<Started>(&mut event_stream).await;
    matcher.expect_match::<Stopped>(&mut event_stream).await;

    Ok(())
}
