// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl::endpoints::{create_proxy, DiscoverableService},
    fidl_fuchsia_component as fcomp,
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_sys2 as fsys, fidl_test_policy as ftest, fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_zircon::{self as zx, AsHandleRef},
    matches::assert_matches,
    test_utils_lib::{events::*, test_utils::*},
};

const CM_URL: &str =
    "fuchsia-pkg://fuchsia.com/security-policy-integration-test#meta/cm_for_test.cmx";
const ROOT_URL: &str =
    "fuchsia-pkg://fuchsia.com/security-policy-integration-test#meta/test_root.cm";
const TEST_CONFIG_PATH: &str = "/pkg/data/cm_config.json";

pub fn connect_to_root_service<S: DiscoverableService>(
    test: &BlackBoxTest,
) -> Result<S::Proxy, Error> {
    let mut service_path = test.get_hub_v2_path();
    service_path.extend(&["exec", "expose", "svc", S::SERVICE_NAME]);
    fuchsia_component::client::connect_to_service_at_path::<S>(service_path.to_str().unwrap())
}

async fn start_policy_test() -> Result<(BlackBoxTest, fsys::RealmProxy), Error> {
    let test = BlackBoxTest::custom(CM_URL, ROOT_URL, vec![], Some(TEST_CONFIG_PATH)).await?;
    let event_source = test.connect_to_event_source().await?;
    let mut event_stream = event_source.subscribe(vec![Started::NAME]).await?;
    event_source.start_component_tree().await?;

    // Wait for the root component to be started so we can connect to its Realm service through the
    // hub.
    let event =
        event_stream.expect_exact::<Started>(EventMatcher::new().expect_moniker(".")).await?;
    event.resume().await?;

    let realm = connect_to_root_service::<fsys::RealmMarker>(&test)
        .context("failed to connect to root sys2.Realm")?;
    Ok((test, realm))
}

async fn bind_child(realm: &fsys::RealmProxy, name: &str) -> Result<DirectoryProxy, fcomp::Error> {
    let mut child_ref = fsys::ChildRef { name: name.to_string(), collection: None };
    let (exposed_dir, server_end) = create_proxy().unwrap();
    realm
        .bind_child(&mut child_ref, server_end)
        .await
        .expect("binding child failed")
        .map(|_| exposed_dir)
}

#[fasync::run_singlethreaded(test)]
async fn verify_ambient_vmex_default_denied() -> Result<(), Error> {
    let (_test, realm) = start_policy_test().await?;

    let child_name = "policy_not_requested";
    let exposed_dir = bind_child(&realm, child_name).await.expect("bind should succeed");
    let ops = client::connect_to_protocol_at_dir::<ftest::ProtectedOperationsMarker>(&exposed_dir)
        .context("failed to connect to test service after bind")?;

    let vmo = zx::Vmo::create(1).unwrap();
    let result = ops.ambient_replace_as_executable(vmo).await.context("fidl call failed")?;
    assert_matches!(result.map_err(zx::Status::from_raw), Err(zx::Status::ACCESS_DENIED));

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn verify_ambient_vmex_allowed() -> Result<(), Error> {
    let (_test, realm) = start_policy_test().await?;

    let child_name = "policy_allowed";
    let exposed_dir = bind_child(&realm, child_name).await.expect("bind should succeed");
    let ops = client::connect_to_protocol_at_dir::<ftest::ProtectedOperationsMarker>(&exposed_dir)
        .context("failed to connect to test service after bind")?;

    let vmo = zx::Vmo::create(1).unwrap();
    let result = ops.ambient_replace_as_executable(vmo).await.context("fidl call failed")?;
    match result.map_err(zx::Status::from_raw) {
        Ok(exec_vmo) => {
            assert!(exec_vmo.basic_info().unwrap().rights.contains(zx::Rights::EXECUTE));
        }
        Err(zx::Status::ACCESS_DENIED) => {
            panic!("Unexpected ACCESS_DENIED when policy should be allowed")
        }
        Err(err) => panic!("Unexpected error {}", err),
    }

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn verify_ambient_vmex_denied() -> Result<(), Error> {
    let (_test, realm) = start_policy_test().await?;

    // This security policy is enforced inside the ELF runner. The component will fail to launch
    // because of the denial, but BindChild will return success because the runtime successfully
    // asks the runner to start the component. We watch for the exposed_dir to get dropped to detect
    // the launch failure.
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
