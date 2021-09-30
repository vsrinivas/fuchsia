// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::restore_agent;
use crate::audio::types::{
    AudioInfo, AudioInputInfo, AudioSettingSource, AudioStream, AudioStreamType,
};
use crate::audio::{create_default_modified_counters, default_audio_info};
use crate::base::SettingType;
use crate::handler::device_storage::testing::InMemoryStorageFactory;
use crate::handler::device_storage::DeviceStorage;
use crate::ingress::fidl::Interface;
use crate::input::common::MediaButtonsEventBuilder;
use crate::tests::fakes::audio_core_service::{self, AudioCoreService};
use crate::tests::fakes::input_device_registry_service::InputDeviceRegistryService;
use crate::tests::fakes::service_registry::ServiceRegistry;
use crate::tests::fakes::sound_player_service::SoundPlayerService;
use crate::tests::test_failure_utils::create_test_env_with_failures;
use crate::AgentType;
use crate::EnvironmentBuilder;
use fidl::Error::ClientChannelClosed;
use fidl_fuchsia_media::AudioRenderUsage;
use fidl_fuchsia_settings::*;
use fuchsia_component::server::NestedEnvironment;
use fuchsia_zircon::Status;
use futures::lock::Mutex;
use matches::assert_matches;
use std::collections::HashMap;
use std::sync::Arc;

const ENV_NAME: &str = "settings_service_audio_test_environment";

const CHANGED_VOLUME_LEVEL: f32 = 0.7;
const CHANGED_VOLUME_LEVEL_2: f32 = 0.95;
const CHANGED_VOLUME_MUTED: bool = true;

const CHANGED_MEDIA_STREAM: AudioStream = AudioStream {
    stream_type: AudioStreamType::Media,
    source: AudioSettingSource::User,
    user_volume_level: CHANGED_VOLUME_LEVEL,
    user_volume_muted: CHANGED_VOLUME_MUTED,
};

const CHANGED_MEDIA_STREAM_2: AudioStream = AudioStream {
    stream_type: AudioStreamType::Media,
    source: AudioSettingSource::User,
    user_volume_level: CHANGED_VOLUME_LEVEL_2,
    user_volume_muted: CHANGED_VOLUME_MUTED,
};

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

const CHANGED_MEDIA_STREAM_SETTINGS_2: AudioStreamSettings = AudioStreamSettings {
    stream: Some(fidl_fuchsia_media::AudioRenderUsage::Media),
    source: Some(AudioStreamSettingSource::User),
    user_volume: Some(Volume {
        level: Some(CHANGED_VOLUME_LEVEL_2),
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
    input_device_registry: Arc<Mutex<InputDeviceRegistryService>>,
}

fn get_default_stream(stream_type: AudioStreamType) -> AudioStream {
    *default_audio_info()
        .streams
        .iter()
        .find(|x| x.stream_type == stream_type)
        .expect("contains stream")
}

async fn set_volume(proxy: &AudioProxy, streams: Vec<AudioStreamSettings>) {
    let mut audio_settings = AudioSettings::EMPTY;
    audio_settings.streams = Some(streams);
    proxy.set(audio_settings).await.expect("set completed").expect("set successful");
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

// Verify that |streams| contain |stream|.
fn verify_contains_stream(streams: &[AudioStream; 5], stream: &AudioStream) {
    let _ = streams.iter().find(|x| *x == stream).expect("contains changed media stream");
}

// Returns a registry and audio related services it is populated with
async fn create_services() -> (Arc<Mutex<ServiceRegistry>>, FakeServices) {
    let service_registry = ServiceRegistry::create();
    let audio_core_service_handle = audio_core_service::Builder::new().build();
    service_registry.lock().await.register_service(audio_core_service_handle.clone());

    let input_device_registry_service_handle =
        Arc::new(Mutex::new(InputDeviceRegistryService::new()));
    service_registry.lock().await.register_service(input_device_registry_service_handle.clone());

    let sound_player_service_handle = Arc::new(Mutex::new(SoundPlayerService::new()));
    service_registry.lock().await.register_service(sound_player_service_handle.clone());

    (
        service_registry,
        FakeServices {
            audio_core: audio_core_service_handle,
            input_device_registry: input_device_registry_service_handle,
        },
    )
}

async fn create_environment(
    service_registry: Arc<Mutex<ServiceRegistry>>,
) -> (NestedEnvironment, Arc<DeviceStorage>) {
    create_environment_with_agent(true, service_registry).await
}

async fn create_environment_with_agent(
    with_agent: bool,
    service_registry: Arc<Mutex<ServiceRegistry>>,
) -> (NestedEnvironment, Arc<DeviceStorage>) {
    let storage_factory =
        Arc::new(InMemoryStorageFactory::with_initial_data(&default_audio_info()));

    let env_builder = EnvironmentBuilder::new(Arc::clone(&storage_factory))
        .service(ServiceRegistry::serve(service_registry))
        .fidl_interfaces(&[Interface::Audio]);
    let env = if with_agent {
        env_builder.agents(&[AgentType::MediaButtons.into()])
    } else {
        env_builder
    }
    .spawn_and_get_nested_environment(ENV_NAME)
    .await
    .unwrap();
    let store = storage_factory.get_device_storage().await;
    (env, store)
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_audio() {
    let (service_registry, fake_services) = create_services().await;
    let (env, store) = create_environment(service_registry).await;

    let audio_proxy = env.connect_to_protocol::<AudioMarker>().unwrap();

    let settings = audio_proxy.watch().await.expect("watch completed");
    verify_audio_stream(
        &settings,
        AudioStreamSettings::from(get_default_stream(AudioStreamType::Media)),
    );

    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS]).await;
    let settings = audio_proxy.watch().await.expect("watch completed");
    verify_audio_stream(&settings, CHANGED_MEDIA_STREAM_SETTINGS);

    assert_eq!(
        (CHANGED_VOLUME_LEVEL, CHANGED_VOLUME_MUTED),
        fake_services.audio_core.lock().await.get_level_and_mute(AudioRenderUsage::Media).unwrap()
    );

    // Check to make sure value wrote out to store correctly.
    let stored_streams = store.get::<AudioInfo>().await.streams;
    verify_contains_stream(&stored_streams, &CHANGED_MEDIA_STREAM);
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_consecutive_volume_changes() {
    let (service_registry, fake_services) = create_services().await;
    let (env, store) = create_environment(service_registry).await;

    let audio_proxy = env.connect_to_protocol::<AudioMarker>().unwrap();

    let settings = audio_proxy.watch().await.expect("watch completed");
    verify_audio_stream(
        &settings,
        AudioStreamSettings::from(get_default_stream(AudioStreamType::Media)),
    );

    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS]).await;
    let settings = audio_proxy.watch().await.expect("watch completed");
    verify_audio_stream(&settings, CHANGED_MEDIA_STREAM_SETTINGS);

    assert_eq!(
        (CHANGED_VOLUME_LEVEL, CHANGED_VOLUME_MUTED),
        fake_services.audio_core.lock().await.get_level_and_mute(AudioRenderUsage::Media).unwrap()
    );

    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS_2]).await;
    let settings = audio_proxy.watch().await.expect("watch completed");
    verify_audio_stream(&settings, CHANGED_MEDIA_STREAM_SETTINGS_2);

    assert_eq!(
        (CHANGED_VOLUME_LEVEL_2, CHANGED_VOLUME_MUTED),
        fake_services.audio_core.lock().await.get_level_and_mute(AudioRenderUsage::Media).unwrap()
    );

    // Check to make sure value wrote out to store correctly.
    let stored_streams = store.get::<AudioInfo>().await.streams;
    verify_contains_stream(&stored_streams, &CHANGED_MEDIA_STREAM_2);
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_multiple_changes_on_stream() {
    let (service_registry, _) = create_services().await;
    let (env, store) = create_environment(service_registry).await;

    let audio_proxy = env.connect_to_protocol::<AudioMarker>().unwrap();

    let settings = audio_proxy.watch().await.expect("watch completed");
    verify_audio_stream(
        &settings,
        AudioStreamSettings::from(get_default_stream(AudioStreamType::Media)),
    );

    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS]).await;
    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS_2]).await;
    let settings = audio_proxy.watch().await.expect("watch completed");
    verify_audio_stream(&settings, CHANGED_MEDIA_STREAM_SETTINGS_2);

    // Check to make sure value wrote out to store correctly.
    let stored_streams = store.get::<AudioInfo>().await.streams;
    verify_contains_stream(&stored_streams, &CHANGED_MEDIA_STREAM_2);
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_volume_overwritten() {
    let (service_registry, fake_services) = create_services().await;
    let (env, store) = create_environment(service_registry).await;

    let audio_proxy = env.connect_to_protocol::<AudioMarker>().unwrap();

    let settings = audio_proxy.watch().await.expect("watch completed");
    verify_audio_stream(
        &settings,
        AudioStreamSettings::from(get_default_stream(AudioStreamType::Media)),
    );

    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS]).await;
    let settings = audio_proxy.watch().await.expect("watch completed");
    verify_audio_stream(&settings, CHANGED_MEDIA_STREAM_SETTINGS);

    assert_eq!(
        (CHANGED_VOLUME_LEVEL, CHANGED_VOLUME_MUTED),
        fake_services.audio_core.lock().await.get_level_and_mute(AudioRenderUsage::Media).unwrap()
    );

    // Check to make sure value wrote out to store correctly.
    let stored_streams = store.get::<AudioInfo>().await.streams;
    verify_contains_stream(&stored_streams, &CHANGED_MEDIA_STREAM);

    const CHANGED_BACKGROUND_STREAM_SETTINGS: AudioStreamSettings = AudioStreamSettings {
        stream: Some(fidl_fuchsia_media::AudioRenderUsage::Background),
        source: Some(AudioStreamSettingSource::User),
        user_volume: Some(Volume { level: Some(0.3), muted: Some(true), ..Volume::EMPTY }),
        ..AudioStreamSettings::EMPTY
    };

    set_volume(&audio_proxy, vec![CHANGED_BACKGROUND_STREAM_SETTINGS]).await;
    let settings = audio_proxy.watch().await.expect("watch completed");

    // Changing the background volume should not affect media volume.
    verify_audio_stream(&settings, CHANGED_MEDIA_STREAM_SETTINGS);
    verify_audio_stream(&settings, CHANGED_BACKGROUND_STREAM_SETTINGS);
}

// Tests that the volume level gets rounded to two decimal places.
#[fuchsia_async::run_until_stalled(test)]
async fn test_volume_rounding() {
    let (service_registry, fake_services) = create_services().await;

    let (env, store) = create_environment(service_registry).await;

    let audio_proxy = env.connect_to_protocol::<AudioMarker>().unwrap();

    let settings = audio_proxy.watch().await.expect("watch completed");
    verify_audio_stream(
        &settings,
        AudioStreamSettings::from(get_default_stream(AudioStreamType::Media)),
    );

    set_volume(
        &audio_proxy,
        vec![AudioStreamSettings {
            stream: Some(fidl_fuchsia_media::AudioRenderUsage::Media),
            source: Some(AudioStreamSettingSource::User),
            user_volume: Some(Volume {
                level: Some(0.7015),
                muted: Some(CHANGED_VOLUME_MUTED),
                ..Volume::EMPTY
            }),
            ..AudioStreamSettings::EMPTY
        }],
    )
    .await;

    let settings = audio_proxy.watch().await.expect("watch completed");
    verify_audio_stream(&settings, CHANGED_MEDIA_STREAM_SETTINGS);

    assert_eq!(
        (CHANGED_VOLUME_LEVEL, CHANGED_VOLUME_MUTED),
        fake_services.audio_core.lock().await.get_level_and_mute(AudioRenderUsage::Media).unwrap()
    );

    // Check to make sure value wrote out to store correctly.
    let stored_streams = store.get::<AudioInfo>().await.streams;
    verify_contains_stream(&stored_streams, &CHANGED_MEDIA_STREAM);
}

// Test to ensure mic input change events are received.
#[fuchsia_async::run_until_stalled(test)]
async fn test_audio_input() {
    let (service_registry, fake_services) = create_services().await;

    let (env, _) = create_environment(service_registry).await;

    let audio_proxy = env.connect_to_protocol::<AudioMarker>().unwrap();

    let buttons_event = MediaButtonsEventBuilder::new().set_volume(1).set_mic_mute(true).build();

    fake_services
        .input_device_registry
        .lock()
        .await
        .send_media_button_event(buttons_event.clone())
        .await;

    let updated_settings = audio_proxy.watch().await.expect("watch completed");

    let input = updated_settings.input.expect("Should have input settings");
    let mic_mute = input.muted.expect("Should have mic mute value");
    assert!(mic_mute);
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

// Test to ensure the audio protocol still works without device input.
// TODO(fxbug.dev/56537): Remove with switchover to input interface.
#[fuchsia_async::run_until_stalled(test)]
async fn test_bringup_without_input_registry() {
    let service_registry = ServiceRegistry::create();
    let audio_core_service_handle = audio_core_service::Builder::new().build();
    service_registry.lock().await.register_service(audio_core_service_handle.clone());

    // Create an environment without the media_buttons_agent since it will fail environment creation
    // without the input registry.
    let (env, _) = create_environment_with_agent(false, service_registry).await;

    // At this point we should not crash.
    assert!(env.connect_to_protocol::<AudioMarker>().is_ok());
}

// Ensure that we won't crash if audio core fails.
#[fuchsia_async::run_until_stalled(test)]
async fn test_bringup_without_audio_core() {
    let service_registry = ServiceRegistry::create();
    let input_registry_service_handle = Arc::new(Mutex::new(InputDeviceRegistryService::new()));
    service_registry.lock().await.register_service(input_registry_service_handle.clone());

    let (env, _) = create_environment(service_registry).await;

    // At this point we should not crash.
    let audio_proxy = env.connect_to_protocol::<AudioMarker>().unwrap();

    let settings = audio_proxy.watch().await.expect("watch completed");
    verify_audio_stream(
        &settings,
        AudioStreamSettings::from(get_default_stream(AudioStreamType::Media)),
    );
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_audio_info_copy() {
    let audio_info = default_audio_info();
    let copy_audio_info = audio_info.clone();
    assert_eq!(audio_info, copy_audio_info);
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
        input: AudioInputInfo { mic_mute: true },
        modified_counters: Some(create_default_modified_counters()),
    };

    let storage_factory = InMemoryStorageFactory::with_initial_data(&test_audio_info);

    let env = EnvironmentBuilder::new(Arc::new(storage_factory))
        .service(ServiceRegistry::serve(service_registry))
        .agents(&[restore_agent::blueprint::create()])
        .fidl_interfaces(&[Interface::Audio])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    let audio_proxy = env.connect_to_protocol::<AudioMarker>().unwrap();

    let settings = audio_proxy.watch().await.expect("watch completed");

    // Check to make sure mic mute value is loaded properly.
    let mut audio_input = AudioInput::EMPTY;
    audio_input.muted = Some(test_audio_info.input.mic_mute);

    assert_eq!(settings.input, Some(audio_input));
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

// Test each of the failure conditions for validating the fidl input.
async_property_test!(test_missing_input_returns_failed => [
    missing_user_volume(AudioStreamSettings {
        stream: Some(fidl_fuchsia_media::AudioRenderUsage::Media),
        source: Some(AudioStreamSettingSource::User),
        user_volume: None,
        ..AudioStreamSettings::EMPTY
    }),
    missing_user_volume_level_and_muted(AudioStreamSettings {
        stream: Some(fidl_fuchsia_media::AudioRenderUsage::Media),
        source: Some(AudioStreamSettingSource::User),
        user_volume: Some(Volume {
            level: None,
            muted: None,
            ..Volume::EMPTY
        }),
        ..AudioStreamSettings::EMPTY
    }),
    missing_stream(AudioStreamSettings {
        stream: None,
        source: Some(AudioStreamSettingSource::User),
        user_volume: Some(Volume {
            level: Some(CHANGED_VOLUME_LEVEL),
            muted: Some(CHANGED_VOLUME_MUTED),
            ..Volume::EMPTY
        }),
        ..AudioStreamSettings::EMPTY
    }),
    missing_source(AudioStreamSettings {
        stream: Some(fidl_fuchsia_media::AudioRenderUsage::Media),
        source: None,
        user_volume: Some(Volume {
            level: Some(CHANGED_VOLUME_LEVEL),
            muted: Some(CHANGED_VOLUME_MUTED),
            ..Volume::EMPTY
        }),
        ..AudioStreamSettings::EMPTY
    }),
]);

async fn test_missing_input_returns_failed(setting: AudioStreamSettings) {
    let (service_registry, _) = create_services().await;
    let (env, _) = create_environment(service_registry).await;

    let audio_proxy = env.connect_to_protocol::<AudioMarker>().unwrap();

    let settings = audio_proxy.watch().await.expect("watch completed");
    verify_audio_stream(
        &settings,
        AudioStreamSettings::from(get_default_stream(AudioStreamType::Media)),
    );

    let result = audio_proxy
        .set(AudioSettings { streams: Some(vec![setting]), ..AudioSettings::EMPTY })
        .await
        .expect("set completed");
    assert_eq!(result, Err(Error::Failed));
}

// Test each of the failure conditions for validating the fidl input.
async_property_test!(test_missing_one_returns_ok => [
    missing_user_volume_level(AudioStreamSettings {
        stream: Some(fidl_fuchsia_media::AudioRenderUsage::Media),
        source: Some(AudioStreamSettingSource::User),
        user_volume: Some(Volume {
            level: None,
            muted: Some(CHANGED_VOLUME_MUTED),
            ..Volume::EMPTY
        }),
        ..AudioStreamSettings::EMPTY
    }),
    missing_user_volume_muted(AudioStreamSettings {
        stream: Some(fidl_fuchsia_media::AudioRenderUsage::Media),
        source: Some(AudioStreamSettingSource::User),
        user_volume: Some(Volume {
            level: Some(CHANGED_VOLUME_LEVEL),
            muted: None,
            ..Volume::EMPTY
        }),
        ..AudioStreamSettings::EMPTY
    }),
]);

async fn test_missing_one_returns_ok(setting: AudioStreamSettings) {
    let (service_registry, _) = create_services().await;
    let (env, _) = create_environment(service_registry).await;

    let audio_proxy = env.connect_to_protocol::<AudioMarker>().unwrap();

    let settings = audio_proxy.watch().await.expect("watch completed");
    verify_audio_stream(
        &settings,
        AudioStreamSettings::from(get_default_stream(AudioStreamType::Media)),
    );

    let result = audio_proxy
        .set(AudioSettings { streams: Some(vec![setting]), ..AudioSettings::EMPTY })
        .await
        .expect("set completed");
    assert_eq!(result, Ok(()));
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

    let input_device_registry_service_handle =
        Arc::new(Mutex::new(InputDeviceRegistryService::new()));
    service_registry.lock().await.register_service(input_device_registry_service_handle.clone());

    let sound_player_service_handle = Arc::new(Mutex::new(SoundPlayerService::new()));
    service_registry.lock().await.register_service(sound_player_service_handle.clone());

    // AudioInfo has to have 5 streams, but make them all the same stream type so that we can
    // perform a set call with a stream that isn't in the AudioInfo.
    let counters: HashMap<_, _> =
        std::array::IntoIter::new([(AudioStreamType::Background, 0)]).collect();

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
        input: AudioInputInfo { mic_mute: true },
        modified_counters: Some(counters),
    };

    // Start the environment with the hand-crafted data.
    let storage_factory = InMemoryStorageFactory::with_initial_data(&test_audio_info);
    let env = EnvironmentBuilder::new(Arc::new(storage_factory))
        .service(ServiceRegistry::serve(service_registry))
        .agents(&[restore_agent::blueprint::create()])
        .fidl_interfaces(&[Interface::Audio])
        .spawn_and_get_nested_environment(ENV_NAME)
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
