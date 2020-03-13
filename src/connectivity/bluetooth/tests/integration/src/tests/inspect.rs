use {anyhow::Error, fuchsia_inspect::assert_inspect_tree};

use crate::harness::{
    control::ControlHarness,
    inspect::{expect_hierarchies, InspectHarness},
};

async fn test_hierarchy_published(
    (harness, _btgap): (InspectHarness, ControlHarness),
) -> Result<(), Error> {
    let state = expect_hierarchies(&harness).await?;

    assert_eq!(1, state.hierarchies.len());

    // Don't assert the entire tree to avoid test fragility.
    assert_inspect_tree!(&state.hierarchies[0], root: contains {
        system: contains {
            device_class: "default",
        },
    });

    Ok(())
}

/// Run all test cases.
pub fn run_all() -> Result<(), Error> {
    run_suite!("bt-gap inspect", [test_hierarchy_published])
}
