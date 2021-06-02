// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::camera_watcher::CameraWatcherAgent;
use crate::agent::{Context, Invocation, Lifespan, Payload};
use crate::event::{self, Event};
use crate::message::base::{Audience, MessengerType};
use crate::service;
use crate::service_context::ServiceContext;
use crate::tests::fakes::camera3_service::Camera3Service;
use crate::tests::fakes::service_registry::ServiceRegistry;
use futures::lock::Mutex;
use std::collections::HashSet;
use std::sync::Arc;

struct FakeServices {
    camera3_service: Arc<Mutex<Camera3Service>>,
}

// Returns a registry and input related services with which it is populated.
async fn create_services() -> (Arc<Mutex<ServiceRegistry>>, FakeServices) {
    let service_registry = ServiceRegistry::create();

    let camera3_service_handle = Arc::new(Mutex::new(Camera3Service::new()));
    service_registry.lock().await.register_service(camera3_service_handle.clone());

    (service_registry, FakeServices { camera3_service: camera3_service_handle })
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_camera_agent_proxy() {
    let service_hub = service::message::create_hub();

    // Create the agent receptor for use by the agent.
    let agent_receptor = service_hub
        .create(MessengerType::Unbound)
        .await
        .expect("Unable to create agent messenger")
        .1;
    let signature = agent_receptor.get_signature();

    // Create the messenger where we will send the invocations.
    let (agent_messenger, _) =
        service_hub.create(MessengerType::Unbound).await.expect("Unable to create agent messenger");

    // Create the receptor which will receive the broadcast events.
    let mut event_receptor = service::build_event_listener(&service_hub).await;

    // Create the agent context and agent.
    let context =
        Context::new(agent_receptor, service_hub, HashSet::new(), HashSet::new(), None).await;
    // Setup the fake services.
    let (service_registry, fake_services) = create_services().await;

    fake_services.camera3_service.lock().await.set_camera_sw_muted(true);
    CameraWatcherAgent::create(context).await;

    let service_context =
        Arc::new(ServiceContext::new(Some(ServiceRegistry::serve(service_registry)), None));

    // Create and send the invocation with faked services.
    let invocation = Invocation { lifespan: Lifespan::Service, service_context };
    let mut reply_receptor = agent_messenger
        .message(Payload::Invocation(invocation).into(), Audience::Messenger(signature))
        .send();
    let completion_result =
        if let Ok((Payload::Complete(result), _)) = reply_receptor.next_of::<Payload>().await {
            Some(result)
        } else {
            None
        };

    // Validate that the setup is complete.
    assert!(
        matches!(completion_result, Some(Ok(()))),
        "Did not receive a completion event from the invocation message"
    );

    // Track the events to make sure they came in.
    let mut camera_state = false;
    while let Ok((payload, _)) = event_receptor.next_of::<event::Payload>().await {
        if let event::Payload::Event(Event::CameraUpdate(event)) = payload {
            match event {
                event::camera_watcher::Event::OnSWMuteState(muted) => {
                    camera_state = muted;
                    break;
                }
            }
        }
    }

    // Validate that we received all expected events.
    assert!(camera_state);
}
