// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::restore_agent;
use crate::base::SettingType;
use crate::event::{self as event, restore, Event};
use crate::handler::base::Request;
use crate::handler::device_storage::testing::InMemoryStorageFactory;
use crate::handler::setting_handler::{ControllerError, SettingHandlerResult};
use crate::ingress::fidl::Interface;
use crate::service;
use crate::service::message::Receptor;
use crate::tests::fakes::base::create_setting_handler;
use crate::tests::fakes::service_registry::ServiceRegistry;
use crate::tests::scaffold::event::subscriber::Blueprint;
use crate::EnvironmentBuilder;
use futures::future::BoxFuture;
use futures::lock::Mutex;
use std::sync::Arc;

const ENV_NAME: &str = "restore_agent_test_environment";

// Create an environment that includes Event handling.
async fn create_event_environment() -> Arc<Mutex<Option<Receptor>>> {
    let event_receptor: Arc<Mutex<Option<Receptor>>> = Arc::new(Mutex::new(None));
    let receptor_capture = event_receptor.clone();

    // Upon environment initialization, the subscriber will capture the event receptor.
    let create_subscriber =
        Arc::new(move |delegate: service::message::Delegate| -> BoxFuture<'static, ()> {
            let event_receptor = receptor_capture.clone();
            Box::pin(async move {
                *event_receptor.lock().await = Some(service::build_event_listener(&delegate).await);
            })
        });

    let _env = EnvironmentBuilder::new(Arc::new(InMemoryStorageFactory::new()))
        .service(ServiceRegistry::serve(ServiceRegistry::create()))
        .event_subscribers(&[Blueprint::create(create_subscriber)])
        .fidl_interfaces(&[Interface::Setup])
        .agents(&[restore_agent::blueprint::create()])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    event_receptor
}

// Helper function for bringing up an environment with a single handler for a
// single SettingType and validating the environment initialization result.
async fn verify_restore_handling(
    response_generate: Box<dyn Fn() -> SettingHandlerResult + Send + Sync + 'static>,
    success: bool,
) {
    let counter: Arc<Mutex<u64>> = Arc::new(Mutex::new(0));

    let counter_clone = counter.clone();
    assert_eq!(
        success,
        EnvironmentBuilder::new(Arc::new(InMemoryStorageFactory::new()))
            .handler(
                SettingType::Unknown,
                create_setting_handler(Box::new(move |request| {
                    let counter = counter_clone.clone();
                    if request == Request::Restore {
                        let result = (response_generate)();
                        return Box::pin(async move {
                            let mut counter_lock = counter.lock().await;
                            *counter_lock += 1;
                            return result;
                        });
                    } else {
                        return Box::pin(async { Ok(None) });
                    }
                })),
            )
            .agents(&[restore_agent::blueprint::create()])
            .settings(&[SettingType::Unknown])
            .spawn_nested(ENV_NAME)
            .await
            .is_ok()
    );

    assert_eq!(*counter.lock().await, 1);
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_restore() {
    // Should succeed when the restore command is handled.
    verify_restore_handling(Box::new(|| Ok(None)), true).await;

    // Snould succeed when the restore command is explicitly not handled.
    verify_restore_handling(
        Box::new(|| {
            Err(ControllerError::UnimplementedRequest(SettingType::Unknown, Request::Restore))
        }),
        true,
    )
    .await;

    // Should succeed when the setting is not available.
    verify_restore_handling(
        Box::new(|| Err(ControllerError::UnhandledType(SettingType::Unknown))),
        true,
    )
    .await;

    // Snould fail when any other error is introduced.
    verify_restore_handling(
        Box::new(|| Err(ControllerError::UnexpectedError("foo".into()))),
        false,
    )
    .await;
}

// Verifies the no-op event was properly passed through and matches.
#[fuchsia_async::run_until_stalled(test)]
async fn test_unimplemented() {
    // The environment uses SettingType::Setup, whose controller, setup_controller, does not
    // implement Restore.
    let receptor = create_event_environment().await;
    let mut event_receptor = receptor.lock().await.take().expect("Should have captured receptor");

    loop {
        let payload = event_receptor.next_of::<event::Payload>().await;
        if let Ok((
            event::Payload::Event(Event::Restore(restore::Event::NoOp(SettingType::Setup))),
            _,
        )) = payload
        {
            return;
        }

        // Else, test will stall and fail if the above event is never received.
    }
}
