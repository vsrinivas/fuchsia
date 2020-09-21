// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async as fasync,
    fuchsia_component::client::ScopedInstance,
    fuchsia_syslog::{self as fxlog},
    test_utils_lib::events::{
        Destroyed, Event, EventMatcher, EventSource, ExitStatusMatcher, Ordering, Stopped,
    },
};

/// This test invokes components which don't stop when they're told to. We
/// still expect them to be stopped when the system kills them.
#[fasync::run_singlethreaded(test)]
async fn test_stop_timeouts() {
    fxlog::init().unwrap();

    let event_source = EventSource::new_sync().unwrap();
    let mut event_stream =
        event_source.subscribe(vec![Stopped::NAME, Destroyed::NAME]).await.unwrap();
    event_source.start_component_tree().await;
    let collection_name = String::from("test-collection");
    // What is going on here? A scoped dynamic instance is created and then
    // dropped. When a the instance is dropped it stops the instance.
    let child_name = {
        ScopedInstance::new(
            collection_name.clone(),
            String::from(concat!(
                "fuchsia-pkg://fuchsia.com/elf_runner_lifecycle_test",
                "#meta/lifecycle_timeout_unresponsive_root.cm"
            )),
        )
        .await
        .unwrap()
        .child_name()
    };

    // Why do we have three duplicate events sets here? We expect three things
    // to stop, the root component and its two children. The problem is that
    // there isn't a great way to express the path of the children because
    // it looks something like "./{collection}:{root-name}:{X}/{child-name}".
    // We don't know what "X" is for sure, it will tend to be "1", but there
    // is no contract around this and the validation logic does not accept
    // generic regexes.
    let moniker_stem = format!("./{}:{}:", collection_name, child_name);
    let custom_timeout_child = format!("{}\\d+/custom-timeout-child:\\d+$", moniker_stem);
    let inherited_timeout_child = format!("{}\\d+/inherited-timeout-child:\\d+$", moniker_stem);
    let target_monikers = [moniker_stem, custom_timeout_child, inherited_timeout_child];
    let expected_events = vec![
        EventMatcher::new()
            .expect_type(Stopped::TYPE)
            .expect_monikers(&target_monikers)
            .expect_stop(Some(ExitStatusMatcher::AnyCrash)),
        EventMatcher::new()
            .expect_monikers(&target_monikers)
            .expect_stop(Some(ExitStatusMatcher::AnyCrash)),
        EventMatcher::new()
            .expect_monikers(&target_monikers)
            .expect_stop(Some(ExitStatusMatcher::AnyCrash)),
        EventMatcher::new().expect_type(Destroyed::TYPE).expect_monikers(&target_monikers),
        EventMatcher::new().expect_type(Destroyed::TYPE).expect_monikers(&target_monikers),
        EventMatcher::new().expect_type(Destroyed::TYPE).expect_monikers(&target_monikers),
    ];

    event_stream.validate(Ordering::Ordered, expected_events).await.unwrap();
}
