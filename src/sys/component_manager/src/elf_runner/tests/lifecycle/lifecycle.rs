// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{
        events::{EventStream, Started},
        matcher::{EventMatcher, ExitStatusMatcher},
        sequence::EventSequence,
    },
    fuchsia_component_test::ScopedInstance,
};

#[fuchsia::test]
async fn test_normal_behavior() {
    let mut event_stream = EventStream::open().await.unwrap();
    let collection_name = String::from("test-collection");
    // What is going on here? A scoped dynamic instance is created and then
    // dropped. When a the instance is dropped it stops the instance.
    let (moniker, destroy_waiter) = {
        let mut instance = ScopedInstance::new(
            collection_name.clone(),
            String::from("fuchsia-pkg://fuchsia.com/elf_runner_lifecycle_test#meta/lifecycle.cm"),
        )
        .await
        .unwrap();

        instance.connect_to_binder().unwrap();

        let moniker = format!("./{}:{}", collection_name, instance.child_name());

        EventMatcher::ok()
            .moniker(moniker.clone())
            .wait::<Started>(&mut event_stream)
            .await
            .expect("failed to observe events");

        (moniker, instance.take_destroy_waiter())
    };
    let () = destroy_waiter.await.expect("failed to destroy child");

    EventSequence::new()
        .then(EventMatcher::ok().moniker(&moniker).stop(Some(ExitStatusMatcher::Clean)))
        .expect(event_stream)
        .await
        .unwrap();
}
