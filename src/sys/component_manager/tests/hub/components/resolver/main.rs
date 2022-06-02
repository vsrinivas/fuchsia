// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use hub_report::*;

#[fuchsia::main(logging_tags = [ "resolver" ])]
async fn main() {
    expect_dir_listing(
        "/hub/children/child_a",
        vec!["children", "component_type", "moniker", "id", "url"],
    )
    .await;
    expect_dir_listing(
        "/hub/children/child_b",
        vec!["children", "component_type", "moniker", "id", "url"],
    )
    .await;
    resolve_component("./child_a/does_not_exist", false).await;
    resolve_component("./child_a", true).await;
    expect_dir_listing(
        "/hub/children/child_a",
        vec!["children", "component_type", "id", "moniker", "resolved", "url"],
    )
    .await;
    resolve_component("./child_b", true).await;
    expect_dir_listing(
        "/hub/children/child_b",
        vec!["children", "component_type", "id", "moniker", "resolved", "url"],
    )
    .await;
    start_component("./child_b", true).await;
    expect_dir_listing(
        "/hub/children/child_b",
        vec!["children", "component_type", "exec", "id", "moniker", "resolved", "url"],
    )
    .await;
    start_component("./child_a", true).await;
    expect_dir_listing(
        "/hub/children/child_a",
        vec!["children", "component_type", "exec", "id", "moniker", "resolved", "url"],
    )
    .await;
    stop_component("./child_b", true).await;
    expect_dir_listing(
        "/hub/children/child_b",
        vec!["children", "component_type", "id", "moniker", "resolved", "url"],
    )
    .await;
    stop_component("./child_a", true).await;
    expect_dir_listing(
        "/hub/children/child_a",
        vec!["children", "component_type", "id", "moniker", "resolved", "url"],
    )
    .await;
}
