// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::restore_agent;
use crate::audio::types::{AudioInfo, AudioSettingSource, AudioStream, AudioStreamType};
use crate::audio::{create_default_modified_counters, default_audio_info};
use crate::base::SettingType;
use crate::ingress::fidl::Interface;
use crate::storage::testing::InMemoryStorageFactory;
use crate::tests::fakes::audio_core_service::{self, AudioCoreService};
use crate::tests::fakes::service_registry::ServiceRegistry;
use crate::tests::test_failure_utils::create_test_env_with_failures;
use crate::EnvironmentBuilder;
use assert_matches::assert_matches;
use fidl::Error::ClientChannelClosed;
use fidl_fuchsia_media::AudioRenderUsage;
use fidl_fuchsia_settings::*;
use fuchsia_component::server::ProtocolConnector;
use fuchsia_zircon::Status;
use futures::lock::Mutex;
use settings_storage::device_storage::DeviceStorage;
use std::collections::HashMap;
use std::sync::Arc;

const ENV_NAME: &str = "settings_service_audio_test_environment";

const CHANGED_VOLUME_LEVEL: f32 = 0.7;
const CHANGED_VOLUME_MUTED: bool = true;

const CHANGED_MEDIA_STREAM_SETTINGS: AudioStreamSettings = AudioStreamSettings {
    stream: Some(fidl_fuchsia_media::AudioRenderUsage::Media),
    source: Some(AudioStreamSettingSource::User),
    user_volume: Some(Volume {
        level: Some(CHANGED_VOLUME_LEVEL),
        muted: Some(CHANGED_VOLUME_MUTED),
        ..Volume::EMPTY
    }),
    ..AudioStreamSettings::EMPTY
};

/// Creates an environment that will fail on a get request.
async fn create_audio_test_env_with_failures(
    storage_factory: Arc<InMemoryStorageFactory>,
) -> AudioProxy {
    create_test_env_with_failures(storage_factory, ENV_NAME, Interface::Audio, SettingType::Audio)
        .await
        .connect_to_protocol::<AudioMarker>()
        .unwrap()
}

// Used to store fake services for mocking dependencies and checking input/outputs.
// To add a new fake to these tests, add here, in create_services, and then use
// in your test.
struct FakeServices {
    audio_core: Arc<Mutex<AudioCoreService>>,
}

fn get_default_stream(stream_type: AudioStreamType) -> AudioStream {
    *default_audio_info()
        .streams
        .iter()
        .find(|x| x.stream_type == stream_type)
        .expect("contains stream")
}

// Verifies that a stream equal to |stream| is inside of |settings|.
fn verify_audio_stream(settings: &AudioSettings, stream: AudioStreamSettings) {
    let _ = settings
        .streams
        .as_ref()
        .expect("audio settings contain streams")
        .iter()
        .find(|x| **x == stream)
        .expect("contains stream");
}

// Returns a registry and audio related services it is populated with
async fn create_services() -> (Arc<Mutex<ServiceRegistry>>, FakeServices) {
    let service_registry = ServiceRegistry::create();
    let audio_core_service_handle = audio_core_service::Builder::new().build();
    service_registry.lock().await.register_service(audio_core_service_handle.clone());

    (service_registry, FakeServices { audio_core: audio_core_service_handle })
}

async fn create_environment(
    service_registry: Arc<Mutex<ServiceRegistry>>,
) -> (ProtocolConnector, Arc<DeviceStorage>) {
    let storage_factory =
        Arc::new(InMemoryStorageFactory::with_initial_data(&default_audio_info()));

    let connector = EnvironmentBuilder::new(Arc::clone(&storage_factory))
        .service(ServiceRegistry::serve(service_registry))
        .fidl_interfaces(&[Interface::Audio])
        .spawn_and_get_protocol_connector(ENV_NAME)
        .await
        .unwrap();
    let store = storage_factory.get_device_storage().await;
    (connector, store)
}

// Test that the audio settings are restored correctly.
#[fuchsia_async::run_until_stalled(test)]
async fn test_volume_restore() {
    let (service_registry, fake_services) = create_services().await;
    let expected_info = (0.9, false);
    let mut stored_info = default_audio_info();
    for stream in stored_info.streams.iter_mut() {
        if stream.stream_type == AudioStreamType::Media {
            stream.user_volume_level = expected_info.0;
            stream.user_volume_muted = expected_info.1;
        }
    }

    let storage_factory = InMemoryStorageFactory::with_initial_data(&stored_info);
    assert!(EnvironmentBuilder::new(Arc::new(storage_factory))
        .service(Box::new(ServiceRegistry::serve(service_registry)))
        .agents(&[restore_agent::blueprint::create()])
        .fidl_interfaces(&[Interface::Audio])
        .spawn_nested(ENV_NAME)
        .await
        .is_ok());

    let stored_info =
        fake_services.audio_core.lock().await.get_level_and_mute(AudioRenderUsage::Media).unwrap();
    assert_eq!(stored_info, expected_info);
}

// Ensure that we won't crash if audio core fails.
#[fuchsia_async::run_until_stalled(test)]
async fn test_bringup_without_audio_core() {
    let service_registry = ServiceRegistry::create();

    let (connector, _) = create_environment(service_registry).await;

    // At this point we should not crash.
    let audio_proxy = connector.connect_to_protocol::<AudioMarker>().unwrap();

    let settings = audio_proxy.watch().await.expect("watch completed");
    verify_audio_stream(
        &settings,
        AudioStreamSettings::from(get_default_stream(AudioStreamType::Media)),
    );
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_persisted_values_applied_at_start() {
    let (service_registry, fake_services) = create_services().await;

    let test_audio_info = AudioInfo {
        streams: [
            AudioStream {
                stream_type: AudioStreamType::Background,
                source: AudioSettingSource::User,
                user_volume_level: 0.5,
                user_volume_muted: true,
            },
            AudioStream {
                stream_type: AudioStreamType::Media,
                source: AudioSettingSource::User,
                user_volume_level: 0.6,
                user_volume_muted: true,
            },
            AudioStream {
                stream_type: AudioStreamType::Interruption,
                source: AudioSettingSource::System,
                user_volume_level: 0.3,
                user_volume_muted: false,
            },
            AudioStream {
                stream_type: AudioStreamType::SystemAgent,
                source: AudioSettingSource::User,
                user_volume_level: 0.7,
                user_volume_muted: true,
            },
            AudioStream {
                stream_type: AudioStreamType::Communication,
                source: AudioSettingSource::User,
                user_volume_level: 0.8,
                user_volume_muted: false,
            },
        ],
        modified_counters: Some(create_default_modified_counters()),
    };

    let storage_factory = InMemoryStorageFactory::with_initial_data(&test_audio_info);

    let env = EnvironmentBuilder::new(Arc::new(storage_factory))
        .service(ServiceRegistry::serve(service_registry))
        .agents(&[restore_agent::blueprint::create()])
        .fidl_interfaces(&[Interface::Audio])
        .spawn_and_get_protocol_connector(ENV_NAME)
        .await
        .unwrap();

    let audio_proxy = env.connect_to_protocol::<AudioMarker>().unwrap();

    let settings = audio_proxy.watch().await.expect("watch completed");

    // Check that the stored values were returned from watch() and applied to the audio core
    // service.
    for stream in test_audio_info.streams.iter() {
        verify_audio_stream(&settings, AudioStreamSettings::from(*stream));
        assert_eq!(
            (stream.user_volume_level, stream.user_volume_muted),
            fake_services
                .audio_core
                .lock()
                .await
                .get_level_and_mute(AudioRenderUsage::from(stream.stream_type))
                .unwrap()
        );
    }
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_channel_failure_watch() {
    let audio_proxy =
        create_audio_test_env_with_failures(Arc::new(InMemoryStorageFactory::new())).await;
    let result = audio_proxy.watch().await;
    assert_matches!(result, Err(ClientChannelClosed { status: Status::UNAVAILABLE, .. }));
}

// Tests that a set call for a stream that isn't in the audio settings fails.
#[fuchsia_async::run_until_stalled(test)]
async fn test_invalid_stream_fails() {
    // Create a service registry with a fake audio core service that suppresses client errors, since
    // the invalid set call will cause the connection to close.
    let service_registry = ServiceRegistry::create();
    let audio_core_service_handle =
        audio_core_service::Builder::new().set_suppress_client_errors(true).build();
    service_registry.lock().await.register_service(audio_core_service_handle.clone());

    // AudioInfo has to have 5 streams, but make them all the same stream type so that we can
    // perform a set call with a stream that isn't in the AudioInfo.
    let counters: HashMap<_, _> = [(AudioStreamType::Background, 0)].into();

    let test_audio_info = AudioInfo {
        streams: [
            AudioStream {
                stream_type: AudioStreamType::Background,
                source: AudioSettingSource::User,
                user_volume_level: 0.5,
                user_volume_muted: true,
            },
            AudioStream {
                stream_type: AudioStreamType::Background,
                source: AudioSettingSource::User,
                user_volume_level: 0.5,
                user_volume_muted: true,
            },
            AudioStream {
                stream_type: AudioStreamType::Background,
                source: AudioSettingSource::User,
                user_volume_level: 0.5,
                user_volume_muted: true,
            },
            AudioStream {
                stream_type: AudioStreamType::Background,
                source: AudioSettingSource::User,
                user_volume_level: 0.5,
                user_volume_muted: true,
            },
            AudioStream {
                stream_type: AudioStreamType::Background,
                source: AudioSettingSource::User,
                user_volume_level: 0.5,
                user_volume_muted: true,
            },
        ],
        modified_counters: Some(counters),
    };

    // Start the environment with the hand-crafted data.
    let storage_factory = InMemoryStorageFactory::with_initial_data(&test_audio_info);
    let env = EnvironmentBuilder::new(Arc::new(storage_factory))
        .service(ServiceRegistry::serve(service_registry))
        .agents(&[restore_agent::blueprint::create()])
        .fidl_interfaces(&[Interface::Audio])
        .spawn_and_get_protocol_connector(ENV_NAME)
        .await
        .unwrap();

    // Connect to the service and make a watch call that should succeed.
    let audio_proxy = env.connect_to_protocol::<AudioMarker>().unwrap();
    let _ = audio_proxy.watch().await.expect("watch completed");

    // Make a set call with the media stream, which isn't present and should fail.
    let mut audio_settings = AudioSettings::EMPTY;
    audio_settings.streams = Some(vec![CHANGED_MEDIA_STREAM_SETTINGS]);
    let _ =
        audio_proxy.set(audio_settings).await.expect("set completed").expect_err("set should fail");
}
