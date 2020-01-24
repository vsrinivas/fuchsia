// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::audio::default_audio_info,
    crate::create_environment,
    crate::fidl_clone::FIDLClone,
    crate::registry::device_storage::testing::*,
    crate::registry::device_storage::{DeviceStorage, DeviceStorageFactory},
    crate::service_context::ServiceContext,
    crate::switchboard::base::{
        AudioInfo, AudioInputInfo, AudioSettingSource, AudioStream, AudioStreamType, SettingType,
    },
    crate::tests::fakes::audio_core_service::AudioCoreService,
    crate::tests::fakes::input_device_registry_service::InputDeviceRegistryService,
    crate::tests::fakes::service_registry::ServiceRegistry,
    crate::tests::fakes::sound_player_service::SoundPlayerService,
    fidl_fuchsia_media::AudioRenderUsage,
    fidl_fuchsia_settings::*,
    fidl_fuchsia_ui_input::MediaButtonsEvent,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::lock::Mutex,
    futures::prelude::*,
    parking_lot::RwLock,
    std::sync::Arc,
};

const ENV_NAME: &str = "settings_service_audio_test_environment";

const CHANGED_VOLUME_LEVEL: f32 = 0.7;
const CHANGED_VOLUME_LEVEL_2: f32 = 0.8;
const MAX_VOLUME_LEVEL: f32 = 1.0;
const CHANGED_VOLUME_MUTED: bool = true;
const CHANGED_VOLUME_UNMUTED: bool = false;

const CHANGED_MEDIA_STREAM: AudioStream = AudioStream {
    stream_type: AudioStreamType::Media,
    source: AudioSettingSource::User,
    user_volume_level: CHANGED_VOLUME_LEVEL,
    user_volume_muted: CHANGED_VOLUME_MUTED,
};

const CHANGED_MEDIA_STREAM_SETTINGS: AudioStreamSettings = AudioStreamSettings {
    stream: Some(fidl_fuchsia_media::AudioRenderUsage::Media),
    source: Some(AudioStreamSettingSource::User),
    user_volume: Some(Volume {
        level: Some(CHANGED_VOLUME_LEVEL),
        muted: Some(CHANGED_VOLUME_MUTED),
    }),
};

const CHANGED_MEDIA_STREAM_SETTINGS_2: AudioStreamSettings = AudioStreamSettings {
    stream: Some(fidl_fuchsia_media::AudioRenderUsage::Media),
    source: Some(AudioStreamSettingSource::User),
    user_volume: Some(Volume {
        level: Some(CHANGED_VOLUME_LEVEL_2),
        muted: Some(CHANGED_VOLUME_MUTED),
    }),
};

const CHANGED_MEDIA_STREAM_SETTINGS_MAX: AudioStreamSettings = AudioStreamSettings {
    stream: Some(fidl_fuchsia_media::AudioRenderUsage::Media),
    source: Some(AudioStreamSettingSource::User),
    user_volume: Some(Volume { level: Some(MAX_VOLUME_LEVEL), muted: Some(CHANGED_VOLUME_UNMUTED) }),
};

fn get_default_stream(stream_type: AudioStreamType) -> AudioStream {
    *default_audio_info()
        .streams
        .iter()
        .find(|x| x.stream_type == stream_type)
        .expect("contains stream")
}

async fn set_volume(proxy: &AudioProxy, streams: Vec<AudioStreamSettings>) {
    let mut audio_settings = AudioSettings::empty();
    audio_settings.streams = Some(streams);
    proxy.set(audio_settings).await.expect("set completed").expect("set successful");
}

// Verifies that a stream equal to |stream| is inside of |settings|.
fn verify_audio_stream(settings: AudioSettings, stream: AudioStreamSettings) {
    settings
        .streams
        .expect("audio settings contain streams")
        .into_iter()
        .find(|x| *x == stream)
        .expect("contains stream");
}

// Gets the store from |factory| and populate it with default values.
async fn create_storage(
    factory: &Box<InMemoryStorageFactory>,
) -> Arc<Mutex<DeviceStorage<AudioInfo>>> {
    let store = factory.get_store::<AudioInfo>();
    {
        let mut store_lock = store.lock().await;
        let audio_info = default_audio_info();
        store_lock.write(&audio_info, false).await.unwrap();
    }
    store
}

// Verify that |streams| contain |stream|.
fn verify_contains_stream(streams: &[AudioStream; 5], stream: &AudioStream) {
    streams.into_iter().find(|x| *x == stream).expect("contains changed media stream");
}

// Returns a registry and audio related services it is populated with
fn create_services() -> (
    Arc<RwLock<ServiceRegistry>>,
    Arc<RwLock<AudioCoreService>>,
    Arc<RwLock<InputDeviceRegistryService>>,
    Arc<RwLock<SoundPlayerService>>,
) {
    let service_registry = ServiceRegistry::create();
    let audio_core_service_handle = Arc::new(RwLock::new(AudioCoreService::new()));
    service_registry.write().register_service(audio_core_service_handle.clone());

    let input_device_registry_service_handle =
        Arc::new(RwLock::new(InputDeviceRegistryService::new()));
    service_registry.write().register_service(input_device_registry_service_handle.clone());

    let sound_player_service_handle = Arc::new(RwLock::new(SoundPlayerService::new()));
    service_registry.write().register_service(sound_player_service_handle.clone());

    return (
        service_registry,
        audio_core_service_handle,
        input_device_registry_service_handle,
        sound_player_service_handle,
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_audio() {
    let (service_registry, audio_core_service_handle, _input_service_handle, _sound_player_handle) =
        create_services();

    let storage_factory = Box::new(InMemoryStorageFactory::create());
    let store = create_storage(&storage_factory).await;

    let mut fs = ServiceFs::new();

    assert!(create_environment(
        fs.root_dir(),
        [SettingType::Audio].iter().cloned().collect(),
        vec![],
        ServiceContext::create(ServiceRegistry::serve(service_registry.clone())),
        storage_factory,
    )
    .await
    .unwrap()
    .is_ok());

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

    let audio_proxy = env.connect_to_service::<AudioMarker>().unwrap();

    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    verify_audio_stream(
        settings.clone(),
        AudioStreamSettings::from(get_default_stream(AudioStreamType::Media)),
    );

    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS]).await;
    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    verify_audio_stream(settings.clone(), CHANGED_MEDIA_STREAM_SETTINGS);

    assert_eq!(
        (CHANGED_VOLUME_LEVEL, CHANGED_VOLUME_MUTED),
        audio_core_service_handle.read().get_level_and_mute(AudioRenderUsage::Media).unwrap()
    );

    // Check to make sure value wrote out to store correctly.
    let stored_streams: [AudioStream; 5];
    {
        let mut store_lock = store.lock().await;
        stored_streams = store_lock.get().await.streams;
    }
    verify_contains_stream(&stored_streams, &CHANGED_MEDIA_STREAM);
}

// Test to ensure mic input change events are received.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_audio_input() {
    let (service_registry, _audio_core_service_handle, input_service_handle, _sound_player_handle) =
        create_services();

    let mut fs = ServiceFs::new();

    assert!(create_environment(
        fs.root_dir(),
        [SettingType::Audio].iter().cloned().collect(),
        vec![],
        ServiceContext::create(ServiceRegistry::serve(service_registry.clone())),
        Box::new(InMemoryStorageFactory::create()),
    )
    .await
    .unwrap()
    .is_ok());

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

    let audio_proxy = env.connect_to_service::<AudioMarker>().unwrap();

    let buttons_event = MediaButtonsEvent { volume: Some(1), mic_mute: Some(true) };
    input_service_handle.read().send_media_button_event(buttons_event.clone());

    let updated_settings =
        audio_proxy.watch().await.expect("watch completed").expect("watch successful");

    let input = updated_settings.input.expect("Should have input settings");
    let mic_mute = input.muted.expect("Should have mic mute value");
    assert!(mic_mute);
}

// Test to ensure that when the volume changes, the SoundPlayer receives requests to play the sounds
// with the correct ids.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_sounds() {
    let (service_registry, _audio_core_service_handle, _input_service_handle, sound_player_handle) =
        create_services();

    let mut fs = ServiceFs::new();

    assert!(create_environment(
        fs.root_dir(),
        [SettingType::Audio].iter().cloned().collect(),
        vec![],
        ServiceContext::create(ServiceRegistry::serve(service_registry.clone())),
        Box::new(InMemoryStorageFactory::create()),
    )
    .await
    .unwrap()
    .is_ok());

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());
    let audio_proxy = env.connect_to_service::<AudioMarker>().unwrap();

    // Test that the volume-max sound gets played on the soundplayer.
    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS_2]).await;
    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    verify_audio_stream(settings.clone(), CHANGED_MEDIA_STREAM_SETTINGS_2);
    assert!(sound_player_handle.read().id_exists(1));
    assert_eq!(sound_player_handle.read().get_mapping(1), Some(AudioRenderUsage::Media));

    // Test that the volume-max sound gets played on the soundplayer.
    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS_MAX]).await;
    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    verify_audio_stream(settings.clone(), CHANGED_MEDIA_STREAM_SETTINGS_MAX);
    assert!(sound_player_handle.read().id_exists(0));
    assert_eq!(sound_player_handle.read().get_mapping(0), Some(AudioRenderUsage::Media));
}

// Test to ensure mic input change events are received.
// TODO(fxb/41006): Add a request.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_bringup_without_input_registry() {
    let service_registry = ServiceRegistry::create();
    let audio_core_service_handle = Arc::new(RwLock::new(AudioCoreService::new()));
    service_registry.write().register_service(audio_core_service_handle.clone());

    let mut fs = ServiceFs::new();

    assert!(create_environment(
        fs.root_dir(),
        [SettingType::Audio].iter().cloned().collect(),
        vec![],
        ServiceContext::create(ServiceRegistry::serve(service_registry.clone())),
        Box::new(InMemoryStorageFactory::create()),
    )
    .await
    .unwrap()
    .is_ok());

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

    // At this point we should not crash.
    assert!(env.connect_to_service::<AudioMarker>().is_ok());
}

// Ensure that we won't crash if audio core fails.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_bringup_without_audio_core() {
    let service_registry = ServiceRegistry::create();
    let input_registry_service_handle = Arc::new(RwLock::new(InputDeviceRegistryService::new()));
    service_registry.write().register_service(input_registry_service_handle.clone());

    let mut fs = ServiceFs::new();

    assert!(create_environment(
        fs.root_dir(),
        [SettingType::Audio].iter().cloned().collect(),
        vec![],
        ServiceContext::create(ServiceRegistry::serve(service_registry.clone())),
        Box::new(InMemoryStorageFactory::create()),
    )
    .await
    .unwrap()
    .is_ok());

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

    // At this point we should not crash.
    let audio_proxy = env.connect_to_service::<AudioMarker>().unwrap();

    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    verify_audio_stream(
        settings.clone(),
        AudioStreamSettings::from(get_default_stream(AudioStreamType::Media)),
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_audio_info_copy() {
    let audio_info = default_audio_info();
    let copy_audio_info = audio_info;
    assert_eq!(audio_info, copy_audio_info);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_persisted_values_applied_at_start() {
    let (service_registry, audio_core_service_handle, _input_service_handle, _sound_player_handle) =
        create_services();

    let storage_factory = Box::new(InMemoryStorageFactory::create());
    let store = create_storage(&storage_factory).await;

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
    };

    // Write values in the store.
    {
        let mut store_lock = store.lock().await;
        store_lock.write(&test_audio_info, false).await.expect("write audio info in store");
    }

    let mut fs = ServiceFs::new();

    assert!(create_environment(
        fs.root_dir(),
        [SettingType::Audio].iter().cloned().collect(),
        vec![],
        ServiceContext::create(ServiceRegistry::serve(service_registry.clone())),
        storage_factory,
    )
    .await
    .unwrap()
    .is_ok());

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

    let audio_proxy = env.connect_to_service::<AudioMarker>().unwrap();

    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");

    // Check that the stored values were returned from watch() and applied to the audio core
    // service.
    for stream in test_audio_info.streams.iter() {
        verify_audio_stream(settings.clone(), AudioStreamSettings::from(*stream));
        assert_eq!(
            (stream.user_volume_level, stream.user_volume_muted),
            audio_core_service_handle
                .read()
                .get_level_and_mute(AudioRenderUsage::from(stream.stream_type))
                .unwrap()
        );
    }
}
