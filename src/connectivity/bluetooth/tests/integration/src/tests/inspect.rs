// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    bt_test_harness::{access::AccessHarness, inspect::InspectHarness},
    fuchsia_bluetooth::{expect_eq, expectation::asynchronous::ExpectableExt},
    fuchsia_inspect_contrib::reader::NodeHierarchy,
    test_harness::run_suite,
};

const GAP_CHILD_NODE: &str = "system";

fn hierarchy_has_child(hierarchy: &NodeHierarchy, name: &str) -> bool {
    hierarchy.children.iter().find(|c| c.name == name).is_some()
}

async fn test_gap_hierarchy_published(
    (harness, _btgap): (InspectHarness, AccessHarness),
) -> Result<(), Error> {
    harness.write_state().moniker = vec!["bt-gap.cmx".to_string()];
    let min_num_hierarchies: usize = 1;
    let mut state = harness.expect_n_hierarchies(min_num_hierarchies).await?;

    let mut gap_hierarchies_count: usize = 0;

    loop {
        let hierarchy = state.hierarchies.pop();
        match hierarchy {
            Some(hierarchy) => {
                if hierarchy_has_child(&hierarchy, GAP_CHILD_NODE) {
                    gap_hierarchies_count += 1;
                }
            }
            None => break,
        }
    }

    expect_eq!(1, gap_hierarchies_count)?;

    Ok(())
}

/// Run all test cases.
pub fn run_all() -> Result<(), Error> {
    run_suite!("inspect", [test_gap_hierarchy_published])
}
