// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_test_policy as ftest, fuchsia_async as fasync,
    fuchsia_component::client,
    security_policy_test_util::{bind_child, start_policy_test},
};

const COMPONENT_MANAGER_URL: &str = "fuchsia-pkg://fuchsia.com/security-policy-capability-allowlist-integration-test#meta/component_manager.cmx";
const ROOT_URL: &str = "fuchsia-pkg://fuchsia.com/security-policy-capability-allowlist-integration-test#meta/test_root.cm";
const TEST_CONFIG_PATH: &str = "/pkg/data/cm_config";

#[fasync::run_singlethreaded(test)]
async fn verify_restricted_capability_allowed() -> Result<(), Error> {
    let (_test, realm) =
        start_policy_test(COMPONENT_MANAGER_URL, ROOT_URL, TEST_CONFIG_PATH).await?;
    let child_name = "policy_allowed";
    let exposed_dir = bind_child(&realm, child_name).await.expect("bind should succeed");
    let access_controller =
        client::connect_to_protocol_at_dir_root::<ftest::AccessMarker>(&exposed_dir)?;
    assert!(access_controller.access_restricted_protocol().await?);
    assert!(access_controller.access_restricted_directory().await?);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn verify_restrited_capability_disallowed() -> Result<(), Error> {
    let (_test, realm) =
        start_policy_test(COMPONENT_MANAGER_URL, ROOT_URL, TEST_CONFIG_PATH).await?;
    let child_name = "policy_denied";
    let exposed_dir = bind_child(&realm, child_name).await.expect("bind should succeed");
    let access_controller =
        client::connect_to_protocol_at_dir_root::<ftest::AccessMarker>(&exposed_dir)?;
    assert_eq!(access_controller.access_restricted_protocol().await?, false);
    assert_eq!(access_controller.access_restricted_directory().await?, false);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn verify_unrestricted_capability_allowed() -> Result<(), Error> {
    let (_test, realm) =
        start_policy_test(COMPONENT_MANAGER_URL, ROOT_URL, TEST_CONFIG_PATH).await?;
    let child_name = "policy_not_violated";
    let exposed_dir = bind_child(&realm, child_name).await.expect("bind should succeed");
    let access_controller =
        client::connect_to_protocol_at_dir_root::<ftest::AccessMarker>(&exposed_dir)?;
    assert!(access_controller.access_unrestricted_protocol().await?);
    assert!(access_controller.access_unrestricted_directory().await?);
    Ok(())
}
