// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    fidl_fuchsia_test_manager::{HarnessMarker, LaunchOptions},
};

#[fuchsia_async::run_singlethreaded(test)]
async fn can_launch_and_connect_to_test_service() {
    let launcher = fuchsia_component::client::launcher().expect("Cannot create launcher");
    let app = fuchsia_component::client::launch(
        &launcher,
        "fuchsia-pkg://fuchsia.com/component_manager_for_test_integration_test#meta\
         /component_manager_for_test.cmx"
            .to_string(),
        None,
    )
    .expect("cannot launch component_manager_for_test");

    let harness = app.connect_to_service::<HarnessMarker>().expect("Cannot connect to Harness");

    let (suite_proxy, suite_server_end) = fidl::endpoints::create_proxy().unwrap();
    let (_controller_proxy, controller_server_end) = fidl::endpoints::create_proxy().unwrap();
    harness
        .launch_suite(
            "fuchsia-pkg://fuchsia.com/component_manager_for_test_integration_test\
                   #meta/passing-test-example.cm",
            LaunchOptions {},
            suite_server_end,
            controller_server_end,
        )
        .await
        .expect("launch_test call failed")
        .map_err(|e| format_err!("error launching test: {:?}", e))
        .unwrap();

    let (case_iterator, server_end) =
        fidl::endpoints::create_proxy().expect("Cannot receive test cases");
    suite_proxy.get_tests(server_end).ok();
    let mut tests = vec![];
    loop {
        let chunk = case_iterator.get_next().await.expect("Cannot get next test cases");
        if chunk.is_empty() {
            break;
        }
        tests.extend(chunk);
    }

    // make sure component_manager_for_test was able to launch test and expose the service.
    assert_eq!(tests.len(), 3);
}
