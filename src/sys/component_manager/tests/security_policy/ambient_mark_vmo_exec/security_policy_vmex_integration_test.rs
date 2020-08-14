// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_test_policy as ftest, fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_zircon::{self as zx, AsHandleRef},
    matches::assert_matches,
    security_policy_test_util::{bind_child, start_policy_test},
};

const CM_URL: &str =
    "fuchsia-pkg://fuchsia.com/security-policy-vmex-integration-test#meta/cm_for_test.cmx";
const ROOT_URL: &str =
    "fuchsia-pkg://fuchsia.com/security-policy-vmex-integration-test#meta/test_root.cm";
const TEST_CONFIG_PATH: &str = "/pkg/data/cm_config";

#[fasync::run_singlethreaded(test)]
async fn verify_ambient_vmex_default_denied() -> Result<(), Error> {
    let (_test, realm) = start_policy_test(CM_URL, ROOT_URL, TEST_CONFIG_PATH).await?;

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
    let (_test, realm) = start_policy_test(CM_URL, ROOT_URL, TEST_CONFIG_PATH).await?;

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
    let (_test, realm) = start_policy_test(CM_URL, ROOT_URL, TEST_CONFIG_PATH).await?;

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
