// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{
        events::{Destroyed, Event, EventStream, Started},
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
    let event_stream_start = EventStream::open_at_path("/events/started").await.unwrap();
    let event_stream_1 = EventStream::open_at_path("/events/stopped_destroyed").await.unwrap();
    let event_stream_2 = EventStream::open_at_path("/events/stopped_destroyed").await.unwrap();
    let event_stream_3 = EventStream::open_at_path("/events/stopped_destroyed").await.unwrap();
    let collection_name = String::from("test-collection");
    // What is going on here? A scoped dynamic instance is created and then
    // dropped. When a the instance is dropped it stops the instance.

    let (parent, custom_timeout_child, inherited_timeout_child) = {
        let instance = ScopedInstance::new(
            collection_name.clone(),
            String::from(
                "fuchsia-pkg://fuchsia.com/elf_runner_lifecycle_test#meta/lifecycle_timeout_root.cm",
            ),
        )
        .await
        .unwrap();

        instance.connect_to_binder().unwrap();

        let child_name = instance.child_name().to_string();
        let root_moniker = format!("./{}:{}", collection_name, child_name);
        let custom_timeout_child = format!("{}/custom-timeout-child", root_moniker);
        let inherited_timeout_child = format!("{}/inherited-timeout-child", root_moniker);

        EventSequence::new()
            .all_of(
                vec![
                    EventMatcher::ok().r#type(Started::TYPE).moniker(root_moniker.clone()),
                    EventMatcher::ok().r#type(Started::TYPE).moniker(custom_timeout_child.clone()),
                    EventMatcher::ok()
                        .r#type(Started::TYPE)
                        .moniker(inherited_timeout_child.clone()),
                ],
                Ordering::Unordered,
            )
            .expect(event_stream_start)
            .await
            .unwrap();

        (root_moniker, custom_timeout_child, inherited_timeout_child)
    };

    // We expect three components to stop: the root component and its two children.
    EventSequence::new()
        .has_subset(
            vec![
                EventMatcher::ok()
                    .moniker(custom_timeout_child.clone())
                    .stop(Some(ExitStatusMatcher::Clean)),
                EventMatcher::ok().r#type(Destroyed::TYPE).moniker(custom_timeout_child.clone()),
            ],
            Ordering::Ordered,
        )
        .expect(event_stream_1)
        .await
        .unwrap();

    EventSequence::new()
        .has_subset(
            vec![
                EventMatcher::ok()
                    .moniker(inherited_timeout_child.clone())
                    .stop(Some(ExitStatusMatcher::Clean)),
                EventMatcher::ok().r#type(Destroyed::TYPE).moniker(inherited_timeout_child.clone()),
            ],
            Ordering::Ordered,
        )
        .expect(event_stream_2)
        .await
        .unwrap();

    EventSequence::new()
        .has_subset(
            vec![
                EventMatcher::ok().moniker(parent.clone()).stop(Some(ExitStatusMatcher::AnyCrash)),
                EventMatcher::ok().r#type(Destroyed::TYPE).moniker(parent.clone()),
            ],
            Ordering::Ordered,
        )
        .expect(event_stream_3)
        .await
        .unwrap();
}
