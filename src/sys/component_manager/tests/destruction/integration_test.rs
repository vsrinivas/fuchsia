// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    breakpoint_system_client::*,
    failure::Error,
    fuchsia_async as fasync,
    io_util::{open_directory_in_namespace, OPEN_RIGHT_READABLE},
    test_utils::*,
};

/// Drains the required number of events, sorts them and compares them
/// to the expected events
fn expect_next(events: &mut Vec<DrainedEvent>, expected: Vec<DrainedEvent>) {
    let num_events: usize = expected.len();
    let mut next: Vec<DrainedEvent> = events.drain(0..num_events).collect();
    next.sort_unstable();
    assert_eq!(next, expected);
}

#[fasync::run_singlethreaded(test)]
async fn destruction() -> Result<(), Error> {
    let test = BlackBoxTest::default(
        "fuchsia-pkg://fuchsia.com/destruction_integration_test#meta/collection_realm.cm",
    )
    .await?;

    let breakpoint_system = test
        .connect_to_breakpoint_system()
        .await
        .expect("Breakpoint system should be available, but is not");

    let sink =
        breakpoint_system.soak_events(vec![StopInstance::TYPE, PostDestroyInstance::TYPE]).await?;
    let receiver = breakpoint_system.set_breakpoints(vec![PostDestroyInstance::TYPE]).await?;
    breakpoint_system.start_component_manager().await?;

    // Wait for `coll:root` to be destroyed.
    let invocation = receiver.wait_until_exact::<PostDestroyInstance>("/coll:root:1").await?;

    // Assert that root component has no children.
    let child_dir_path = test.get_hub_v2_path().join("children");
    let child_dir_path = child_dir_path.to_str().expect("invalid chars");
    let child_dir = open_directory_in_namespace(child_dir_path, OPEN_RIGHT_READABLE)?;
    let child_dir_contents = list_directory(&child_dir).await?;
    assert!(child_dir_contents.is_empty());

    // Assert the expected lifecycle events. The leaves can be stopped/destroyed in either order.
    let mut events = sink.drain().await;

    expect_next(
        &mut events,
        vec![
            DrainedEvent {
                event_type: StopInstance::TYPE,
                target_moniker: "/coll:root:1/trigger_a:0".to_string(),
            },
            DrainedEvent {
                event_type: StopInstance::TYPE,
                target_moniker: "/coll:root:1/trigger_b:0".to_string(),
            },
        ],
    );

    expect_next(
        &mut events,
        vec![DrainedEvent {
            event_type: StopInstance::TYPE,
            target_moniker: "/coll:root:1".to_string(),
        }],
    );

    expect_next(
        &mut events,
        vec![
            DrainedEvent {
                event_type: PostDestroyInstance::TYPE,
                target_moniker: "/coll:root:1/trigger_a:0".to_string(),
            },
            DrainedEvent {
                event_type: PostDestroyInstance::TYPE,
                target_moniker: "/coll:root:1/trigger_b:0".to_string(),
            },
        ],
    );

    expect_next(
        &mut events,
        vec![DrainedEvent {
            event_type: PostDestroyInstance::TYPE,
            target_moniker: "/coll:root:1".to_string(),
        }],
    );

    assert!(events.is_empty());
    invocation.resume().await?;

    Ok(())
}
