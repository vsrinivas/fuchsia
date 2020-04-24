// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async as fasync,
    fuchsia_component::client::ScopedInstance,
    fuchsia_inspect::testing::{assert_inspect_tree, AnyProperty, InspectDataFetcher},
};

const TEST_COMPONENT: &str =
    "fuchsia-pkg://fuchsia.com/archivist-integration-tests-v2#meta/stub_inspect_component.cm";

#[fasync::run_singlethreaded(test)]
async fn read_v2_components_inspect() {
    let _test_app = ScopedInstance::new("coll".to_string(), TEST_COMPONENT.to_string())
        .await
        .expect("Failed to create dynamic component");
    let data = InspectDataFetcher::new()
        .add_selector("driver/coll\\:auto-*:root")
        .get()
        .await
        .expect("got inspect data");

    assert_inspect_tree!(data[0], root: {
        "fuchsia.inspect.Health": {
            status: "OK",
            start_timestamp_nanos: AnyProperty,
        }
    });
}
