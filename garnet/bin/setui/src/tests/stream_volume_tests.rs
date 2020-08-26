// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use crate::audio::{create_default_audio_stream, StreamVolumeControl};
use crate::handler::device_storage::testing::*;
use crate::internal::event;
use crate::message::base::{MessageEvent, MessengerType};
use crate::service_context::ExternalServiceProxy;
use crate::switchboard::base::AudioStreamType;
use crate::tests::fakes::audio_core_service::AudioCoreService;
use crate::tests::fakes::service_registry::ServiceRegistry;
use crate::EnvironmentBuilder;
use futures::lock::Mutex;
use futures::StreamExt;
use std::sync::Arc;

const ENV_NAME: &str = "settings_service_event_test_environment";

// Returns a registry populated with the AudioCore service.
async fn create_service() -> Arc<Mutex<ServiceRegistry>> {
    let service_registry = ServiceRegistry::create();
    let audio_core_service_handle = Arc::new(Mutex::new(AudioCoreService::new()));
    service_registry.lock().await.register_service(audio_core_service_handle.clone());
    service_registry
}

// Tests that the volume event stream thread exits when the StreamVolumeControl is deleted.
#[fuchsia_async::run_until_stalled(test)]
async fn test_drop_thread() {
    let env = EnvironmentBuilder::new(InMemoryStorageFactory::create())
        .service(ServiceRegistry::serve(create_service().await))
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    let factory = event::message::create_hub();

    let (_, mut receptor) =
        factory.create(MessengerType::Unbound).await.expect("Should be able to retrieve receptor");

    let publisher = event::Publisher::create(&factory, MessengerType::Unbound).await;

    let audio_proxy = env.connect_to_service::<fidl_fuchsia_media::AudioCoreMarker>().unwrap();

    // Scoped to cause the object to be dropped.
    {
        StreamVolumeControl::create(
            &ExternalServiceProxy::new(audio_proxy, None),
            create_default_audio_stream(AudioStreamType::Media),
            Some(publisher),
        )
        .await
        .ok();
    }

    let received_event =
        receptor.next().await.expect("First message should have been the closed event");
    match received_event {
        MessageEvent::Message(event::Payload::Event(broadcasted_event), _) => {
            assert_eq!(broadcasted_event, event::Event::Closed("volume_control_events"));
        }
        _ => {
            panic!("Should have received an event payload");
        }
    }
}
