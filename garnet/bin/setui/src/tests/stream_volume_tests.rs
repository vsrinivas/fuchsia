// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::audio::types::AudioStreamType;
#[cfg(test)]
use crate::audio::{create_default_audio_stream, StreamVolumeControl};
use crate::event;
use crate::message::base::MessengerType;
use crate::message::MessageHubUtil;
use crate::service;
use crate::service_context::ServiceContext;
use crate::tests::fakes::audio_core_service;
use crate::tests::fakes::service_registry::ServiceRegistry;
use futures::lock::Mutex;
use futures::StreamExt;
use std::sync::Arc;

use matches::assert_matches;

// Returns a registry populated with the AudioCore service.
async fn create_service() -> Arc<Mutex<ServiceRegistry>> {
    let service_registry = ServiceRegistry::create();
    let audio_core_service_handle =
        audio_core_service::Builder::new().set_suppress_client_errors(true).build();
    service_registry.lock().await.register_service(audio_core_service_handle.clone());
    service_registry
}

// Tests that the volume event stream thread exits when the StreamVolumeControl is deleted.
#[fuchsia_async::run_until_stalled(test)]
async fn test_drop_thread() {
    let delegate = service::MessageHub::create_hub();

    let mut receptor = service::build_event_listener(&delegate).await;

    let publisher = event::Publisher::create(&delegate, MessengerType::Unbound).await;

    let service_context =
        ServiceContext::new(Some(ServiceRegistry::serve(create_service().await)), None);

    let audio_proxy = service_context
        .connect::<fidl_fuchsia_media::AudioCoreMarker>()
        .await
        .expect("service should be present");

    // Scoped to cause the object to be dropped.
    {
        StreamVolumeControl::create(
            0,
            &audio_proxy,
            create_default_audio_stream(AudioStreamType::Media),
            None,
            Some(publisher),
        )
        .await
        .ok();
    }

    assert_matches!(
        receptor
            .next_of::<event::Payload>()
            .await
            .expect("First message should have been the closed event")
            .0,
        event::Payload::Event(event::Event::Closed("volume_control_events"))
    );
}

/// Ensures that the StreamVolumeControl properly fires the provided early exit
/// closure when the underlying AudioCoreService closes unexpectedly.
#[fuchsia_async::run_until_stalled(test)]
async fn test_detect_early_exit() {
    let service_registry = ServiceRegistry::create();
    let audio_core_service_handle =
        audio_core_service::Builder::new().set_suppress_client_errors(true).build();
    service_registry.lock().await.register_service(audio_core_service_handle.clone());

    let service_context = ServiceContext::new(Some(ServiceRegistry::serve(service_registry)), None);

    let audio_proxy = service_context
        .connect::<fidl_fuchsia_media::AudioCoreMarker>()
        .await
        .expect("proxy should be present");
    let (tx, mut rx) = futures::channel::mpsc::unbounded::<()>();

    // Create StreamVolumeControl, specifying firing an event as the early exit
    // action. Note that we must store the returned value or else the normal
    // drop behavior will clean up it before the AudioCoreService's exit can
    // be detected.
    let _stream_volume_control = StreamVolumeControl::create(
        0,
        &audio_proxy,
        create_default_audio_stream(AudioStreamType::Media),
        Some(Arc::new(move || {
            tx.unbounded_send(()).unwrap();
        })),
        None,
    )
    .await
    .expect("should successfully build");

    // Trigger AudioCoreService exit.
    audio_core_service_handle.lock().await.exit();

    // Check to make sure early exit event was received.
    assert!(matches!(rx.next().await, Some(..)));
}
