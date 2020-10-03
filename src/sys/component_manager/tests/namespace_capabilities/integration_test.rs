// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fuchsia_async as fasync, test_utils_lib::opaque_test::OpaqueTestBuilder};

#[fasync::run_singlethreaded(test)]
async fn component_manager_namespace() {
    const COMPONENT_MANAGER_URL: &str = "fuchsia-pkg://fuchsia.com/namespace-capabilities-integration-test#meta/component-manager.cmx";
    const ROOT_URL: &str = "fuchsia-pkg://fuchsia.com/namespace-capabilities-integration-test#meta/integration-test-root.cm";
    let mut test = OpaqueTestBuilder::new(ROOT_URL)
        .component_manager_url(COMPONENT_MANAGER_URL)
        .build()
        .await
        .unwrap();
    let exit_status = test.component_manager_app.wait().await.unwrap();
    assert!(exit_status.success(), "component_manager failed to exit: {:?}", exit_status.reason());
}
