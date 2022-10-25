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

const COMPONENT_MANAGER_URL: &str = "#meta/cm_for_test.cm";
const ROOT_URL: &str = "#meta/test_root.cm";

const COMPONENT_MANAGER_DEATH_TIMEOUT: i64 = 5;

#[fasync::run_singlethreaded(test)]
async fn verify_main_process_critical_default_denied() -> Result<(), Error> {
    let (test, realm, _event) = start_policy_test(COMPONENT_MANAGER_URL, ROOT_URL).await?;

    let event_source = EventSource::new().unwrap();
    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Stopped::NAME])])
        .await
        .context("failed to subscribe to EventSource")?;

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

    let moniker = format!("./realm_builder:{}/component_manager", test.root.child_name());

    let wait_for_cm_exit = Box::pin(async move {
        EventMatcher::ok().moniker(&moniker).wait::<Stopped>(&mut event_stream).await.unwrap();
    });

    match select(timer, wait_for_cm_exit).await {
        Either::Left(_) => return Ok(()),
        Either::Right(_) => {
            panic!("unexpected exit of component manager")
        }
    }
}

#[fasync::run_singlethreaded(test)]
async fn verify_main_process_critical_nonzero_flag_used() -> Result<(), Error> {
    let (test, realm, _event) = start_policy_test(COMPONENT_MANAGER_URL, ROOT_URL).await?;

    let event_source = EventSource::new().unwrap();
    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Stopped::NAME])])
        .await
        .context("failed to subscribe to EventSource")?;

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

    let moniker = format!("./realm_builder:{}/component_manager", test.root.child_name());

    let wait_for_cm_exit = Box::pin(async move {
        EventMatcher::ok().moniker(&moniker).wait::<Stopped>(&mut event_stream).await.unwrap();
    });

    match select(timer, wait_for_cm_exit).await {
        Either::Left(_) => return Ok(()),
        Either::Right(_) => {
            panic!("unexpected exit of component manager")
        }
    }
}

#[fasync::run_singlethreaded(test)]
async fn verify_main_process_critical_allowed() -> Result<(), Error> {
    let (test, realm, _event) = start_policy_test(COMPONENT_MANAGER_URL, ROOT_URL).await?;

    let event_source = EventSource::new().unwrap();
    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Stopped::NAME])])
        .await
        .context("failed to subscribe to EventSource")?;

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
    let moniker = format!("./realm_builder:{}/component_manager", test.root.child_name());

    EventMatcher::ok().moniker(&moniker).wait::<Stopped>(&mut event_stream).await.unwrap();

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn verify_main_process_critical_denied() -> Result<(), Error> {
    let (_test, realm, mut event_stream) =
        start_policy_test(COMPONENT_MANAGER_URL, ROOT_URL).await?;

    let child_name = "policy_denied";
    let exposed_dir =
        open_exposed_dir(&realm, child_name).await.expect("open exposed dir should succeed");
    client::connect_to_protocol_at_dir_root::<fcomponent::BinderMarker>(&exposed_dir)
        .context("failed to connect to fuchsia.component.Binder of child")?;

    let mut matcher = EventMatcher::ok().moniker(format!("./root/{}", child_name));
    matcher.expect_match::<Started>(&mut event_stream).await;
    matcher.expect_match::<Stopped>(&mut event_stream).await;

    Ok(())
}
