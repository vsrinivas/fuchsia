// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::camera_watcher::CameraWatcherAgent;
use crate::agent::{AgentError, Context, Invocation, Lifespan, Payload};
use crate::event::{self, Event};
use crate::input::common::CAMERA_WATCHER_TIMEOUT;
use crate::message::base::{Audience, MessengerType};
use crate::message::MessageHubUtil;
use crate::service;
use crate::service_context::ServiceContext;
use crate::tests::fakes::camera3_service::Camera3Service;
use crate::tests::fakes::service_registry::ServiceRegistry;
use crate::tests::helpers::{move_executor_forward, move_executor_forward_and_get};
use fuchsia_async::{TestExecutor, Time};
use futures::lock::Mutex;
use std::collections::HashSet;
use std::convert::TryInto;
use std::sync::Arc;

struct FakeServices {
    camera3_service: Arc<Mutex<Camera3Service>>,
}

// Returns a registry and input related services with which it is populated. If delay_camera_device
// is true, then has_camera_device is ignored. It sends back the camera device after a delay.
// Otherwise, if has_camera_device is true, it will immediately respond with the populated camera
// device. If has_camera_device is false, it will immediately respond with an empty device list.
async fn create_services(
    has_camera_device: bool,
    delay_camera_device: bool,
) -> (Arc<Mutex<ServiceRegistry>>, FakeServices) {
    let service_registry = ServiceRegistry::create();

    let camera3_service_handle = Arc::new(Mutex::new(if delay_camera_device {
        Camera3Service::new_delayed_devices(delay_camera_device)
    } else {
        Camera3Service::new(has_camera_device)
    }));
    service_registry.lock().await.register_service(camera3_service_handle.clone());

    (service_registry, FakeServices { camera3_service: camera3_service_handle })
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_camera_agent_proxy() {
    let service_hub = service::MessageHub::create_hub();

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
    let (service_registry, fake_services) = create_services(true, false).await;

    let expected_camera_state = true;
    fake_services.camera3_service.lock().await.set_camera_sw_muted(expected_camera_state);
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
    assert_eq!(camera_state, expected_camera_state);
}

// Tests that an error is returned if the camera watcher cannot find a camera device
// after the timeout is reached.
#[test]
fn test_camera_devices_watcher_timeout() {
    // Custom executor for this test so that we can advance the clock arbitrarily and verify the
    // state of the executor at any given point.
    let mut executor = TestExecutor::new_with_fake_time().expect("Failed to create executor");
    executor.set_fake_time(Time::from_nanos(0));

    let service_hub = service::MessageHub::create_hub();

    // Create the agent receptor for use by the agent.
    let agent_receptor_future = service_hub.create(MessengerType::Unbound);
    let agent_receptor = move_executor_forward_and_get(
        &mut executor,
        agent_receptor_future,
        "Unable to get agent receptor",
    )
    .expect("Unable to create agent messenger")
    .1;
    let signature = agent_receptor.get_signature();

    // Create the messenger where we will send the invocations.
    let agent_messenger_future = service_hub.create(MessengerType::Unbound);
    let (agent_messenger, _) = move_executor_forward_and_get(
        &mut executor,
        agent_messenger_future,
        "Unable to create agent messenger",
    )
    .expect("Unable to get agent messenger");

    // Create the agent context and agent.
    let context_future =
        Context::new(agent_receptor, service_hub.clone(), HashSet::new(), HashSet::new(), None);
    let context =
        move_executor_forward_and_get(&mut executor, context_future, "Could not create context");

    // Setup the fake services.
    let services_future = create_services(false, false);
    let (service_registry, fake_services) =
        move_executor_forward_and_get(&mut executor, services_future, "Could not create services");

    // Mute the camera via software.
    let camera_service_future = fake_services.camera3_service.lock();
    move_executor_forward_and_get(
        &mut executor,
        camera_service_future,
        "Unable to get camera service",
    )
    .set_camera_sw_muted(true);

    let camera_watcher_agent_future = CameraWatcherAgent::create(context);
    move_executor_forward(
        &mut executor,
        camera_watcher_agent_future,
        "Unable to create camera watcher agent",
    );

    let service_context =
        Arc::new(ServiceContext::new(Some(ServiceRegistry::serve(service_registry)), None));

    // Create and send the invocation with faked services.
    let invocation = Invocation { lifespan: Lifespan::Service, service_context };
    let mut reply_receptor = agent_messenger
        .message(Payload::Invocation(invocation).into(), Audience::Messenger(signature))
        .send();

    // Advance time past the timeout.
    executor.set_fake_time(
        Time::from_nanos(CAMERA_WATCHER_TIMEOUT * 10_i64.pow(6))
            .try_into()
            .expect("could not convert timeout into nanos"),
    );

    let completion_future = reply_receptor.next_of::<Payload>();
    let completion =
        move_executor_forward_and_get(&mut executor, completion_future, "Could not get next reply");
    let completion_result =
        if let Ok((Payload::Complete(result), _)) = completion { Some(result) } else { None };

    // Validate that the setup is complete.
    assert!(
        matches!(completion_result, Some(Err(AgentError::UnexpectedError))),
        "Did not receive a completion event from the invocation message"
    );
}

// Tests that the camera agent is able to handle an empty device list first, and then
// a second update with the device in it that comes in before the timeout.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_camera_agent_delayed_devices() {
    let service_hub = service::MessageHub::create_hub();

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
    let (service_registry, fake_services) = create_services(false, true).await;

    let expected_camera_state = true;
    fake_services.camera3_service.lock().await.set_camera_sw_muted(expected_camera_state);
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
    assert_eq!(camera_state, expected_camera_state);
}
