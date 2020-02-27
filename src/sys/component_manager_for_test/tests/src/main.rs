// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_test::SuiteMarker;

#[fuchsia_async::run_singlethreaded(test)]
async fn can_launch_and_connect_to_test_service() {
    let launcher = fuchsia_component::client::launcher().expect("Cannot create launcher");
    let app = fuchsia_component::client::launch(
        &launcher,
        "fuchsia-pkg://fuchsia.com/component_manager_for_test_integration_test#meta\
         /component_manager_for_test.cmx"
            .to_string(),
        Some(vec!["fuchsia-pkg://fuchsia.com/component_manager_for_test_integration_test\
                   #meta/passing-test-example_v2.cm"
            .to_string()]),
    )
    .expect("cannot launch component_manager_for_test");

    let suite = app.connect_to_service::<SuiteMarker>().expect("Cannot connect to Suite");

    let (case_iterator, server_end) =
        fidl::endpoints::create_proxy().expect("Cannot receive test cases");
    suite.get_tests(server_end).ok();
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
