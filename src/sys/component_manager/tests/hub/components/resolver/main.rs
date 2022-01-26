// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use hub_report::*;

#[fuchsia::component(logging_tags = [ "resolver" ])]
async fn main() {
    expect_dir_listing(
        "/hub/children/child_a",
        vec!["children", "component_type", "debug", "id", "url"],
    )
    .await;
    expect_dir_listing(
        "/hub/children/child_b",
        vec!["children", "component_type", "debug", "id", "url"],
    )
    .await;
    expect_dir_listing("/hub/debug", vec!["fuchsia.sys2.LifecycleController"]).await;
    expect_dir_listing("/hub/children/child_a/debug/", vec!["fuchsia.sys2.LifecycleController"])
        .await;
    expect_dir_listing("/hub/children/child_b/debug/", vec!["fuchsia.sys2.LifecycleController"])
        .await;
    resolve_component(
        "/hub/children/child_a/debug/fuchsia.sys2.LifecycleController",
        "./does_not_exist",
        false,
    )
    .await;
    resolve_component("/hub/children/child_a/debug/fuchsia.sys2.LifecycleController", ".", true)
        .await;
    expect_dir_listing(
        "/hub/children/child_a",
        vec!["children", "component_type", "debug", "id", "resolved", "url"],
    )
    .await;
    resolve_component("/hub/debug/fuchsia.sys2.LifecycleController", "./child_b", true).await;
    expect_dir_listing(
        "/hub/children/child_b",
        vec!["children", "component_type", "debug", "id", "resolved", "url"],
    )
    .await;
    start_component("/hub/debug/fuchsia.sys2.LifecycleController", "./child_b", true).await;
    expect_dir_listing(
        "/hub/children/child_b",
        vec!["children", "component_type", "debug", "exec", "id", "resolved", "url"],
    )
    .await;
    start_component("/hub/children/child_a/debug/fuchsia.sys2.LifecycleController", ".", true)
        .await;
    expect_dir_listing(
        "/hub/children/child_a",
        vec!["children", "component_type", "debug", "exec", "id", "resolved", "url"],
    )
    .await;
    stop_component("/hub/debug/fuchsia.sys2.LifecycleController", "./child_b", true).await;
    expect_dir_listing(
        "/hub/children/child_b",
        vec!["children", "component_type", "debug", "id", "resolved", "url"],
    )
    .await;
    stop_component("/hub/children/child_a/debug/fuchsia.sys2.LifecycleController", ".", true).await;
    expect_dir_listing(
        "/hub/children/child_a",
        vec!["children", "component_type", "debug", "id", "resolved", "url"],
    )
    .await;
}
