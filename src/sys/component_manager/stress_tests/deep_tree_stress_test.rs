// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// These tests will create a deep realm tree and stress test component manager by creating many
/// components at each level.
use {
    cm_stress_tests_lib::{create_child, stop_child},
    fuchsia_async as fasync,
};

async fn create_deep_tree_stress_test(num_children: u16, height: u16, stop_children: bool) {
    let child = create_child(
        "children",
        "fuchsia-pkg://fuchsia.com/component-manager-stress-tests#meta/child-for-stress-test.cm",
    )
    .await
    .unwrap();

    child.realm.create_children(num_children, height).await.unwrap();
    if stop_children {
        child.realm.stop_children().await.unwrap();
    }
    stop_child(child).await.unwrap();
}

#[fasync::run_singlethreaded(test)]
#[ignore]
// TODO(58378): Re-enable this test.
async fn tree_with_height_100_with_1_children_each() {
    create_deep_tree_stress_test(1, 100, false).await;
    create_deep_tree_stress_test(1, 100, true).await;
}

#[fasync::run_singlethreaded(test)]
#[ignore]
// TODO(60417): Enable this when issue is fixed.
async fn tree_with_height_6_with_3_children_each() {
    create_deep_tree_stress_test(3, 6, false).await;
    create_deep_tree_stress_test(3, 6, true).await;
}

#[fasync::run_singlethreaded(test)]
#[ignore]
// TODO(58378): Re-enable this test.
async fn tree_with_height_2_with_10_children_each() {
    create_deep_tree_stress_test(10, 2, false).await;
    create_deep_tree_stress_test(10, 2, true).await;
}
