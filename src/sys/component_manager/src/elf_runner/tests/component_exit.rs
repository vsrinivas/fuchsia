// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    async_trait::async_trait,
    fidl_fidl_test_components as test_protocol, fuchsia_async as fasync,
    fuchsia_component::client::ScopedInstance,
    fuchsia_syslog::{self as fxlog, fx_log_info},
    futures_util::stream::TryStreamExt,
    std::sync::{Arc, Mutex},
    test_utils_lib::{
        events::{self as events, Event, EventSource},
        injectors::*,
        matcher::EventMatcher,
        sequence::EventSequence,
    },
};

#[fasync::run_singlethreaded(test)]
async fn test_exit_detection() {
    fxlog::init().unwrap();

    let event_source = EventSource::new_sync().unwrap();
    let event_stream = event_source.subscribe(vec![events::Stopped::NAME]).await.unwrap();
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

    let target_moniker = format!("./{}:{}:*", collection_name, instance.child_name());

    EventSequence::new()
        .then(EventMatcher::ok().r#type(events::Stopped::TYPE).moniker(&target_moniker))
        .expect(event_stream)
        .await
        .unwrap();
}

#[fasync::run_singlethreaded(test)]
async fn test_exit_after_rendezvous() {
    fxlog::init().unwrap();

    // Get the event source, install our service injector, and then start the
    // component tree.
    let event_source = EventSource::new_sync().unwrap();
    let rendezvous_service = Arc::new(RendezvousService { call_count: Mutex::new(0) });
    rendezvous_service.inject(&event_source, EventMatcher::ok()).await;
    let event_stream = event_source.subscribe(vec![events::Stopped::NAME]).await.unwrap();
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

    // Wait to get confirmation that the component under test exited.
    let target_moniker = format!("./{}:{}:*", collection_name, instance.child_name());
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

#[async_trait]
impl ProtocolInjector for RendezvousService {
    type Marker = test_protocol::TriggerMarker;

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
