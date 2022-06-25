// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bt_test_harness::inspect_v2::InspectHarness, diagnostics_reader::DiagnosticsHierarchy,
    fuchsia_bluetooth::expectation::asynchronous::ExpectableExt,
};

const GAP_CHILD_NODE: &str = "system";

fn hierarchy_has_child(hierarchy: &DiagnosticsHierarchy, name: &str) -> bool {
    hierarchy.children.iter().find(|c| c.name == name).is_some()
}

#[test_harness::run_singlethreaded_test]
async fn test_gap_hierarchy_published(harness: InspectHarness) {
    harness.write_state().moniker_to_track = vec!["bt-init".to_string(), "bt-gap".to_string()];
    let min_num_hierarchies: usize = 1;
    let state = harness.expect_n_hierarchies(min_num_hierarchies).await.unwrap();

    let gap_child_node_hierarchies_count =
        state.hierarchies.iter().filter(|h| hierarchy_has_child(&h, GAP_CHILD_NODE)).count();
    assert_eq!(1, gap_child_node_hierarchies_count);
}
