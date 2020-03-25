use {
    anyhow::{format_err, Error},
    fuchsia_inspect::reader::NodeHierarchy,
};

use crate::harness::{
    control::ControlHarness,
    inspect::{expect_hierarchies, InspectHarness},
};

// bt-gap hierarchy, emulator bt-host hierarchy
// NOTE: device that tests run on may have real adapters that result in additional bt-host
// hierarchies
const MIN_NUM_HIERARCHIES: usize = 2;

fn hierarchy_has_child(hierarchy: &NodeHierarchy, name: &str) -> bool {
    hierarchy.children.iter().find(|c| c.name == name).is_some()
}

async fn test_hierarchies_published(
    (harness, _btgap): (InspectHarness, ControlHarness),
) -> Result<(), Error> {
    let mut state = expect_hierarchies(&harness, MIN_NUM_HIERARCHIES).await?;

    let mut gap_hierarchies_count: usize = 0;
    let mut host_hierarchies_count: usize = 0;

    loop {
        let hierarchy = state.hierarchies.pop();
        match hierarchy {
            Some(hierarchy) => {
                if hierarchy_has_child(&hierarchy, "adapter") {
                    host_hierarchies_count += 1;
                } else if hierarchy_has_child(&hierarchy, "system") {
                    gap_hierarchies_count += 1;
                }
            }
            None => break,
        }
    }

    expect_true!(gap_hierarchies_count == 1)?;

    // emulator + possible real adapters
    expect_true!(host_hierarchies_count >= 1)?;

    Ok(())
}

/// Run all test cases.
pub fn run_all() -> Result<(), Error> {
    run_suite!("bt-gap inspect", [test_hierarchies_published])
}
