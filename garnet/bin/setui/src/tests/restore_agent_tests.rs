// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::restore_agent;
use crate::internal::event::{self as event, message::Receptor, restore, Event};
use crate::message::base::{MessageEvent, MessengerType};
use crate::registry::base::SettingHandlerResult;
use crate::registry::device_storage::testing::InMemoryStorageFactory;
use crate::registry::setting_handler::ControllerError;
use crate::switchboard::base::{SettingRequest, SettingType};
use crate::tests::fakes::base::create_setting_handler;
use crate::tests::fakes::service_registry::ServiceRegistry;
use crate::tests::scaffold::event::subscriber::Blueprint;
use crate::EnvironmentBuilder;
use fuchsia_component::server::NestedEnvironment;
use futures::future::BoxFuture;
use futures::lock::Mutex;
use futures::StreamExt;
use std::sync::Arc;

const ENV_NAME: &str = "restore_agent_test_environment";

// Create an environment that includes Event handling.
async fn create_event_environment(
) -> (NestedEnvironment, Arc<Mutex<Option<event::message::Receptor>>>) {
    let event_receptor: Arc<Mutex<Option<Receptor>>> = Arc::new(Mutex::new(None));
    let receptor_capture = event_receptor.clone();

    // Upon environment initialization, the subscriber will capture the event receptor.
    let create_subscriber =
        Arc::new(move |factory: event::message::Factory| -> BoxFuture<'static, ()> {
            let event_receptor = receptor_capture.clone();
            Box::pin(async move {
                let (_, receptor) = factory
                    .create(MessengerType::Unbound)
                    .await
                    .expect("Should be able to retrieve messenger for publisher");
                *event_receptor.lock().await = Some(receptor);
            })
        });

    let env = EnvironmentBuilder::new(InMemoryStorageFactory::create())
        .service(ServiceRegistry::serve(ServiceRegistry::create()))
        .event_subscribers(&[Blueprint::create(create_subscriber)])
        .settings(&[SettingType::Setup])
        .agents(&[restore_agent::blueprint::create()])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    (env, event_receptor)
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
        EnvironmentBuilder::new(InMemoryStorageFactory::create())
            .handler(
                SettingType::Unknown,
                create_setting_handler(Box::new(move |request| {
                    let counter = counter_clone.clone();
                    if request == SettingRequest::Restore {
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
            Err(ControllerError::UnimplementedRequest(
                SettingType::Unknown,
                SettingRequest::Restore,
            ))
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

#[fuchsia_async::run_until_stalled(test)]
async fn test_unimplemented() {
    let (_, receptor) = create_event_environment().await;
    let mut event_receptor = receptor.lock().await.take().expect("Should have captured receptor");
    if let Some(MessageEvent::Message(event::Payload::Event(received_event), ..)) =
        event_receptor.next().await
    {
        assert_eq!(received_event, Event::Restore(restore::Event::NoOp(SettingType::Setup)));
    }
}
