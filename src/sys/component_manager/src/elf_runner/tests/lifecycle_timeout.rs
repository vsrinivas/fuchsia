// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_test_events::EventType,
    fuchsia_async as fasync,
    fuchsia_component::client::create_scoped_dynamic_instance,
    fuchsia_syslog::{self as fxlog},
    test_utils_lib::events::{EventSource, Ordering, RecordedEvent},
};

/// Test that a component tree which contains a root component with no program
/// and two children is stopped properly. One of the children inherits whatever
/// stop timeout might exist and the other child gets an environment with a
/// timeout explicitly set.
#[fasync::run_singlethreaded(test)]
async fn test_stop_timeouts() {
    fxlog::init().unwrap();

    let event_source = EventSource::new_sync().unwrap();
    event_source.start_component_tree().await.unwrap();
    let mut event_stream =
        event_source.subscribe(vec![EventType::Stopped, EventType::Destroyed]).await.unwrap();
    let collection_name = String::from("test-collection");
    // What is going on here? A scoped dynamic instance is created and then
    // dropped. When a the instance is dropped it stops the instance.
    let child_name = {
        create_scoped_dynamic_instance(
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
    let expected_events = vec![
        RecordedEvent {
            event_type: EventType::Stopped,
            target_moniker: format!("./{}:{}:*", collection_name, child_name).to_string(),
            capability_id: None,
        },
        RecordedEvent {
            event_type: EventType::Stopped,
            target_moniker: format!("./{}:{}:*", collection_name, child_name).to_string(),
            capability_id: None,
        },
        RecordedEvent {
            event_type: EventType::Stopped,
            target_moniker: format!("./{}:{}:*", collection_name, child_name).to_string(),
            capability_id: None,
        },
        RecordedEvent {
            event_type: EventType::Destroyed,
            target_moniker: format!("./{}:{}:*", collection_name, child_name).to_string(),
            capability_id: None,
        },
        RecordedEvent {
            event_type: EventType::Destroyed,
            target_moniker: format!("./{}:{}:*", collection_name, child_name).to_string(),
            capability_id: None,
        },
        RecordedEvent {
            event_type: EventType::Destroyed,
            target_moniker: format!("./{}:{}:*", collection_name, child_name).to_string(),
            capability_id: None,
        },
    ];
    event_stream.validate(Ordering::Ordered, expected_events).await.unwrap();
}
