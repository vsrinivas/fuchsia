// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{
        events::{Event, EventMode, EventSource, EventSubscription, Purged, Started, Stopped},
        matcher::{EventMatcher, ExitStatusMatcher},
        sequence::{EventSequence, Ordering},
    },
    fuchsia_component_test::ScopedInstance,
};

/// Test that a component tree which contains a root component with no program
/// and two children is stopped properly. One of the children inherits whatever
/// stop timeout might exist and the other child gets an environment with a
/// timeout explicitly set.
#[fuchsia::test]
async fn test_stop_timeouts() {
    let event_source = EventSource::new().unwrap();
    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(
            vec![Started::NAME, Stopped::NAME, Purged::NAME],
            EventMode::Async,
        )])
        .await
        .unwrap();
    event_source.start_component_tree().await;
    let collection_name = String::from("test-collection");
    // What is going on here? A scoped dynamic instance is created and then
    // dropped. When a the instance is dropped it stops the instance.
    let target_monikers = {
        let instance = ScopedInstance::new(
            collection_name.clone(),
            String::from(
                "fuchsia-pkg://fuchsia.com/elf_runner_lifecycle_test#meta/lifecycle_timeout_root.cm",
            ),
        )
        .await
        .unwrap();

        let _ = instance.connect_to_binder().unwrap();

        // Why do we have three duplicate events sets here? We expect three things
        // to stop, the root component and its two children. The problem is that
        // there isn't a great way to express the path of the children because
        // it looks something like "./{collection}:{root-name}:{X}/{child-name}".
        // We don't know what "X" is for sure, it will tend to be "1", but there
        // is no contract around this and the validation logic does not accept
        // generic regexes.
        let child_name = instance.child_name().to_string();
        let moniker_stem = format!("./{}:{}:", collection_name, child_name);
        let custom_timeout_child = format!("{}\\d+/custom-timeout-child:\\d+$", moniker_stem);
        let inherited_timeout_child = format!("{}\\d+/inherited-timeout-child:\\d+$", moniker_stem);
        let target_monikers = [moniker_stem, custom_timeout_child, inherited_timeout_child];

        for _ in 0..target_monikers.len() {
            let _ = EventMatcher::ok()
                .monikers(&target_monikers)
                .wait::<Started>(&mut event_stream)
                .await
                .expect("failed to observe events");
        }

        target_monikers
    };

    EventSequence::new()
        .all_of(
            vec![
                EventMatcher::ok().monikers(&target_monikers).stop(Some(ExitStatusMatcher::Clean)),
                EventMatcher::ok().monikers(&target_monikers).stop(Some(ExitStatusMatcher::Clean)),
                EventMatcher::ok()
                    .monikers(&target_monikers)
                    .stop(Some(ExitStatusMatcher::AnyCrash)),
                EventMatcher::ok().r#type(Purged::TYPE).monikers(&target_monikers),
                EventMatcher::ok().r#type(Purged::TYPE).monikers(&target_monikers),
                EventMatcher::ok().r#type(Purged::TYPE).monikers(&target_monikers),
            ],
            Ordering::Ordered,
        )
        .expect(event_stream)
        .await
        .unwrap();
}
