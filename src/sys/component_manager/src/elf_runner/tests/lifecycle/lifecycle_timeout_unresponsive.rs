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
/// This test invokes components which don't stop when they're told to. We
/// still expect them to be stopped when the system kills them.
#[fuchsia::test]
async fn test_stop_timeouts() {
    let event_source = EventSource::new().unwrap();

    let started_event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Started::NAME], EventMode::Async)])
        .await
        .unwrap();

    let event_stream_root = event_source
        .subscribe(vec![EventSubscription::new(
            vec![Stopped::NAME, Purged::NAME],
            EventMode::Async,
        )])
        .await
        .unwrap();
    let event_stream_custom = event_source
        .subscribe(vec![EventSubscription::new(
            vec![Stopped::NAME, Purged::NAME],
            EventMode::Async,
        )])
        .await
        .unwrap();
    let event_stream_inherited = event_source
        .subscribe(vec![EventSubscription::new(
            vec![Stopped::NAME, Purged::NAME],
            EventMode::Async,
        )])
        .await
        .unwrap();

    let collection_name = String::from("test-collection");

    let (root_moniker, custom_moniker, inherited_moniker) = {
        // What is going on here? A scoped dynamic instance is created and then
        // dropped. When a the instance is dropped it stops the instance.
        let instance = ScopedInstance::new(
            collection_name.clone(),
            String::from(concat!(
                "fuchsia-pkg://fuchsia.com/elf_runner_lifecycle_test",
                "#meta/lifecycle_timeout_unresponsive_root.cm"
            )),
        )
        .await
        .unwrap();

        // Make sure we start the root component, since it has no runtime, this
        // is sufficient.
        instance.connect_to_binder().unwrap();

        let moniker_stem = format!("./{}:{}", collection_name, instance.child_name());
        let root_moniker = format!("{}$", moniker_stem);
        let custom_timeout_child = format!("{}/custom-timeout-child$", moniker_stem);
        let inherited_timeout_child = format!("{}/inherited-timeout-child$", moniker_stem);

        EventSequence::new()
            .all_of(
                vec![
                    EventMatcher::ok().r#type(Started::TYPE).moniker_regex(&root_moniker),
                    EventMatcher::ok().r#type(Started::TYPE).moniker_regex(&custom_timeout_child),
                    EventMatcher::ok()
                        .r#type(Started::TYPE)
                        .moniker_regex(&inherited_timeout_child),
                ],
                Ordering::Unordered,
            )
            .expect(started_event_stream)
            .await
            .unwrap();

        (root_moniker, custom_timeout_child, inherited_timeout_child)
    };

    EventSequence::new()
        .has_subset(
            vec![
                EventMatcher::ok()
                    .monikers_regex(vec![root_moniker.clone()])
                    .stop(Some(ExitStatusMatcher::AnyCrash)),
                EventMatcher::ok().r#type(Purged::TYPE).monikers_regex(vec![root_moniker.clone()]),
            ],
            Ordering::Ordered,
        )
        .expect(event_stream_root)
        .await
        .unwrap();

    EventSequence::new()
        .has_subset(
            vec![
                EventMatcher::ok()
                    .monikers_regex(vec![custom_moniker.clone()])
                    .stop(Some(ExitStatusMatcher::AnyCrash)),
                EventMatcher::ok()
                    .r#type(Purged::TYPE)
                    .monikers_regex(vec![custom_moniker.clone()]),
            ],
            Ordering::Ordered,
        )
        .expect(event_stream_custom)
        .await
        .unwrap();

    EventSequence::new()
        .has_subset(
            vec![
                EventMatcher::ok()
                    .monikers_regex(vec![inherited_moniker.clone()])
                    .stop(Some(ExitStatusMatcher::AnyCrash)),
                EventMatcher::ok()
                    .r#type(Purged::TYPE)
                    .monikers_regex(vec![inherited_moniker.clone()]),
            ],
            Ordering::Ordered,
        )
        .expect(event_stream_inherited)
        .await
        .unwrap();
}
