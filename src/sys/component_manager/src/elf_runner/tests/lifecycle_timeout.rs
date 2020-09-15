// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    elf_test_helper::{self as test_helper, ExitStatus},
    fuchsia_async as fasync,
    fuchsia_component::client::ScopedInstance,
    fuchsia_syslog::{self as fxlog},
    regex::Regex,
    test_utils_lib::events::{Destroyed, Event, EventMatcher, EventSource, Stopped},
};

/// Test that a component tree which contains a root component with no program
/// and two children is stopped properly. One of the children inherits whatever
/// stop timeout might exist and the other child gets an environment with a
/// timeout explicitly set.
#[fasync::run_singlethreaded(test)]
async fn test_stop_timeouts() {
    fxlog::init().unwrap();

    let event_source = EventSource::new_sync().unwrap();
    event_source.start_component_tree().await;
    let mut event_stream =
        event_source.subscribe(vec![Stopped::NAME, Destroyed::NAME]).await.unwrap();
    let collection_name = String::from("test-collection");
    // What is going on here? A scoped dynamic instance is created and then
    // dropped. When a the instance is dropped it stops the instance.
    let child_name = {
        ScopedInstance::new(
            collection_name.clone(),
            String::from(
                "fuchsia-pkg://fuchsia.com/elf_runner_lifecycle_test#meta/lifecycle_timeout_root.cm",
            ),
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
    let target_moniker = format!("{}*", moniker_stem);
    let custom_child_regex =
        Regex::new(&format!("{}\\d+/custom-timeout-child:\\d+$", moniker_stem)).unwrap();
    let inherited_child_regex =
        Regex::new(&format!("{}\\d+/inherited-timeout-child:\\d+$", moniker_stem)).unwrap();
    let mut expected_events = vec![
        EventMatcher::new().expect_type::<Stopped>().expect_moniker(&target_moniker),
        EventMatcher::new().expect_type::<Stopped>().expect_moniker(&target_moniker),
        EventMatcher::new().expect_type::<Stopped>().expect_moniker(&target_moniker),
        EventMatcher::new().expect_type::<Destroyed>().expect_moniker(&target_moniker),
        EventMatcher::new().expect_type::<Destroyed>().expect_moniker(&target_moniker),
        EventMatcher::new().expect_type::<Destroyed>().expect_moniker(&target_moniker),
    ];

    let rxs = vec![inherited_child_regex, custom_child_regex];
    // Each child we expect to hit its stop timeout and be killed, which should
    // be reported as a non-zero status code.I
    test_helper::validate_exit(&mut event_stream, &mut expected_events, rxs, ExitStatus::Clean)
        .await;

    assert!(
        expected_events.is_empty(),
        format!("Some expected events were not received: {:?}", expected_events)
    );
}
