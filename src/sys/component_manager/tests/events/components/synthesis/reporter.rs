// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    assert_matches::assert_matches,
    component_events::events::{
        Destroyed, DirectoryReady, Event, EventSource, EventSubscription, Started,
    },
    fidl_fuchsia_sys2 as fsys,
    fuchsia_component_test::ScopedInstance,
    std::convert::TryFrom,
    tracing::*,
};

#[fuchsia::main]
async fn main() {
    let event_source = EventSource::new().unwrap();
    let mut event_stream =
        event_source.subscribe(vec![EventSubscription::new(vec![Started::NAME])]).await.unwrap();

    // Make 4 components: 1 directory ready child and 3 stub children
    let mut instances = vec![];
    let url = "#meta/stub_component.cm".to_string();
    let url_cap_ready = "#meta/directory_ready_child.cm".to_string();
    let scoped_instance =
        ScopedInstance::new("coll".to_string(), url_cap_ready.clone()).await.unwrap();
    let _ = scoped_instance.connect_to_binder().unwrap();
    instances.push(scoped_instance);
    assert_matches!(
        event_stream.next().await.unwrap(),
        fsys::Event { header: Some(fsys::EventHeader { event_type: Some(Started::TYPE), .. }), .. }
    );

    for _ in 0..3 {
        let scoped_instance = ScopedInstance::new("coll".to_string(), url.clone()).await.unwrap();
        let _ = scoped_instance.connect_to_binder().unwrap();
        instances.push(scoped_instance);
        assert_matches!(
            event_stream.next().await.unwrap(),
            fsys::Event {
                header: Some(fsys::EventHeader { event_type: Some(Started::TYPE), .. }),
                ..
            }
        );
    }

    // Destroy one stub child, this shouldn't appear anywhere in the events.
    let mut instance = instances.pop().unwrap();
    let destroy_waiter = instance.take_destroy_waiter();
    drop(instance);
    destroy_waiter.await.unwrap();

    // Subscribe to events.
    let event_source = EventSource::new().unwrap();
    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Destroyed::NAME, DirectoryReady::NAME])])
        .await
        .unwrap();

    let mut directory_ready = vec![];

    while directory_ready.len() < 1 {
        let event = event_stream.next().await.unwrap();
        if let Some(header) = &event.header {
            match header.event_type {
                Some(DirectoryReady::TYPE) => {
                    let event =
                        DirectoryReady::try_from(event).expect("convert to directory ready");
                    info!("Got directory ready event");
                    directory_ready.push(event.target_moniker().to_string());
                }
                other => panic!("unexpected event type: {:?}", other),
            }
        }
    }

    assert_eq!(directory_ready.len(), 1);

    // Dropping instances stops and destroys the children.
    drop(instances);

    // The three instances were destroyed.
    let mut seen_destroyed = 0;
    while seen_destroyed != 3 {
        let event = event_stream.next().await.unwrap();
        if let Some(header) = event.header {
            match header.event_type {
                Some(DirectoryReady::TYPE) => {
                    // ignore. we could get a duplicate here.
                }
                Some(Destroyed::TYPE) => {
                    seen_destroyed += 1;
                }
                event => {
                    panic!("Got unexpected event type: {:?}", event);
                }
            }
        }
    }
}
