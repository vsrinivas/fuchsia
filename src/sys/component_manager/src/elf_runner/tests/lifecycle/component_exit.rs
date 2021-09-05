// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    component_events::{
        events::{
            self as events, CapabilityRequested, Event, EventMode, EventSource, EventStream,
            EventStreamError, EventSubscription,
        },
        matcher::EventMatcher,
        sequence::EventSequence,
    },
    fidl_fidl_test_components as test_protocol, fuchsia_async as fasync,
    fuchsia_component_test::ScopedInstance,
    fuchsia_syslog::fx_log_info,
    futures_util::stream::TryStreamExt,
    std::sync::{Arc, Mutex},
};

#[fuchsia::test]
async fn test_exit_detection() {
    let event_source = EventSource::new().unwrap();
    let event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![events::Stopped::NAME], EventMode::Async)])
        .await
        .unwrap();
    event_source.start_component_tree().await;

    let collection_name = String::from("test-collection");

    let instance = ScopedInstance::new(
        collection_name.clone(),
        String::from(
            "fuchsia-pkg://fuchsia.com/elf_runner_lifecycle_test#meta/immediate_exit_component.cm",
        ),
    )
    .await
    .unwrap();

    let _ = instance.connect_to_binder().unwrap();

    let target_moniker = format!("./{}:{}:*", collection_name, instance.child_name());

    EventSequence::new()
        .then(EventMatcher::ok().r#type(events::Stopped::TYPE).moniker(&target_moniker))
        .expect(event_stream)
        .await
        .unwrap();
}

#[fuchsia::test]
async fn test_exit_after_rendezvous() {
    // Get the event source, install our service injector, and then start the
    // component tree.
    let event_source = EventSource::new().unwrap();
    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(
            vec![events::Started::NAME, events::Stopped::NAME],
            EventMode::Async,
        )])
        .await
        .unwrap();

    // tests don't have access to output directory so using capability requested event to offer a
    // protocol.
    let capability_requested_event_stream =
        event_source.take_static_event_stream("EventStream").await.unwrap();
    let rendezvous_service = RendezvousService::new(capability_requested_event_stream);
    event_source.start_component_tree().await;

    // Launch the component under test.
    let collection_name = String::from("test-collection");
    let instance = ScopedInstance::new(
        collection_name.clone(),
        String::from(
            "fuchsia-pkg://fuchsia.com/elf_runner_lifecycle_test#meta/rendezvous_exit_component.cm",
        ),
    )
    .await
    .unwrap();

    let _ = instance.connect_to_binder().unwrap();

    let target_moniker = format!("./{}:{}:*", collection_name, instance.child_name());

    // First, ensure that component has started.
    let _ = EventMatcher::ok()
        .moniker(&target_moniker)
        .wait::<events::Started>(&mut event_stream)
        .await
        .expect("failed to observe events");

    // Then, wait to get confirmation that the component under test exited.
    EventSequence::new()
        .then(EventMatcher::ok().r#type(events::Stopped::TYPE).moniker(&target_moniker))
        .expect(event_stream)
        .await
        .unwrap();

    // Check that we received a request from the component under test.
    assert_eq!(*rendezvous_service.call_count.lock().unwrap(), 1);
}

struct RendezvousService {
    call_count: Mutex<u32>,
}

impl RendezvousService {
    // Right now we are using simple event stream. If this is used by multiple tests cases,
    // this struct should use mock component from RealmBuilder.
    fn new(mut event_stream: EventStream) -> Arc<Self> {
        let obj = Arc::new(RendezvousService { call_count: Mutex::new(0) });
        let obj_clone = obj.clone();
        fasync::Task::spawn(async move {
            loop {
                let mut event =
                    match EventMatcher::ok().wait::<CapabilityRequested>(&mut event_stream).await {
                        Ok(e) => e,
                        Err(e) => match e.downcast::<EventStreamError>() {
                            Ok(EventStreamError::StreamClosed) => return,
                            Err(e) => panic!("Unknown error! {:?}", e),
                        },
                    };

                let stream: test_protocol::TriggerRequestStream =
                    event.take_capability::<test_protocol::TriggerMarker>().unwrap();
                obj_clone.clone().serve(stream).await.expect("error serving trigger");
            }
        })
        .detach();
        obj
    }

    async fn serve(
        self: Arc<Self>,
        mut stream: test_protocol::TriggerRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await.unwrap() {
            match request {
                test_protocol::TriggerRequest::Run { responder } => {
                    {
                        let mut count = self.call_count.lock().unwrap();
                        *count += 1;
                    }
                    fx_log_info!("Received rendezvous from target");
                    responder.send("").unwrap();
                }
            }
        }
        Ok(())
    }
}
