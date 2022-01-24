// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{
        events::*,
        matcher::EventMatcher,
        sequence::{EventSequence, Ordering},
    },
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component_test::new::{Capability, ChildOptions, RealmBuilder, Ref, Route},
};

#[fasync::run_singlethreaded(test)]
async fn destroy() {
    let builder = RealmBuilder::new().await.unwrap();
    let collection_realm = builder
        .add_child("collection_realm", "#meta/collection_realm.cm", ChildOptions::new().eager())
        .await
        .unwrap();
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&collection_realm),
        )
        .await
        .unwrap();
    let instance =
        builder.build_in_nested_component_manager("#meta/component_manager.cm").await.unwrap();
    let proxy =
        instance.root.connect_to_protocol_at_exposed_dir::<fsys::EventSourceMarker>().unwrap();

    let mut event_source = EventSource::from_proxy(proxy);

    let expectation = EventSequence::new()
        .all_of(
            vec![
                EventMatcher::ok()
                    .r#type(Stopped::TYPE)
                    .moniker_regex("./collection_realm/coll:parent/trigger_a"),
                EventMatcher::ok()
                    .r#type(Stopped::TYPE)
                    .moniker_regex("./collection_realm/coll:parent/trigger_b"),
            ],
            Ordering::Unordered,
        )
        .then(
            EventMatcher::ok()
                .r#type(Stopped::TYPE)
                .moniker_regex("./collection_realm/coll:parent"),
        )
        .all_of(
            vec![
                EventMatcher::ok()
                    .r#type(Purged::TYPE)
                    .moniker_regex("./collection_realm/coll:parent/trigger_a"),
                EventMatcher::ok()
                    .r#type(Purged::TYPE)
                    .moniker_regex("./collection_realm/coll:parent/trigger_b"),
            ],
            Ordering::Unordered,
        )
        .then(
            EventMatcher::ok().r#type(Purged::TYPE).moniker_regex("./collection_realm/coll:parent"),
        )
        .subscribe_and_expect(&mut event_source)
        .await
        .unwrap();
    event_source.start_component_tree().await;

    // Assert the expected lifecycle events. The leaves can be stopped/purged in either order.
    expectation.await.unwrap();
}

#[fasync::run_singlethreaded(test)]
async fn destroy_and_recreate() {
    let builder = RealmBuilder::new().await.unwrap();
    let destroy_and_recreate = builder
        .add_child(
            "destroy_and_recreate",
            "#meta/destroy_and_recreate.cm",
            ChildOptions::new().eager(),
        )
        .await
        .unwrap();
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&destroy_and_recreate),
        )
        .await
        .unwrap();
    let instance =
        builder.build_in_nested_component_manager("#meta/component_manager.cm").await.unwrap();
    let proxy =
        instance.root.connect_to_protocol_at_exposed_dir::<fsys::EventSourceMarker>().unwrap();

    let event_source = EventSource::from_proxy(proxy);

    let event_stream = event_source
        .subscribe(vec![EventSubscription::new(
            vec![Started::NAME, Destroyed::NAME, Purged::NAME],
            EventMode::Async,
        )])
        .await
        .unwrap();

    event_source.start_component_tree().await;

    EventSequence::new()
        .has_subset(
            vec![
                EventMatcher::ok()
                    .r#type(Started::TYPE)
                    .moniker_regex("./destroy_and_recreate/coll:trigger"),
                EventMatcher::ok()
                    .r#type(Destroyed::TYPE)
                    .moniker_regex("./destroy_and_recreate/coll:trigger"),
            ],
            Ordering::Ordered,
        )
        .has_subset(
            vec![
                EventMatcher::ok()
                    .r#type(Started::TYPE)
                    .moniker_regex("./destroy_and_recreate/coll:trigger"),
                EventMatcher::ok()
                    .r#type(Purged::TYPE)
                    .moniker_regex("./destroy_and_recreate/coll:trigger"),
            ],
            // The previous instance can be purged before/after the new instance is started.
            // That is why this sequence is unordered.
            Ordering::Unordered,
        )
        .expect(event_stream)
        .await
        .unwrap();
}
