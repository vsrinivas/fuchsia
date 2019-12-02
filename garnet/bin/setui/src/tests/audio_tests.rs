// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::audio::{default_audio_info, spawn_audio_controller, spawn_audio_fidl_handler},
    crate::create_fidl_service,
    crate::fidl_clone::FIDLClone,
    crate::registry::base::Registry,
    crate::registry::device_storage::testing::*,
    crate::registry::device_storage::{DeviceStorage, DeviceStorageFactory},
    crate::registry::registry_impl::RegistryImpl,
    crate::service_context::ServiceContext,
    crate::switchboard::base::{
        AudioInfo, AudioSettingSource, AudioStream, AudioStreamType, SettingAction, SettingType,
    },
    crate::switchboard::switchboard_impl::SwitchboardImpl,
    crate::tests::fakes::audio_core_service::AudioCoreService,
    crate::tests::fakes::input_device_registry_service::InputDeviceRegistryService,
    crate::tests::fakes::service_registry::ServiceRegistry,
    fidl_fuchsia_media::AudioRenderUsage,
    fidl_fuchsia_settings::*,
    fidl_fuchsia_ui_input::MediaButtonsEvent,
    fuchsia_async as fasync,
    fuchsia_component::server::{ServiceFs, ServiceFsDir, ServiceObj},
    futures::lock::Mutex,
    futures::prelude::*,
    parking_lot::RwLock,
    std::sync::Arc,
};

const ENV_NAME: &str = "settings_service_audio_test_environment";

const CHANGED_VOLUME_LEVEL: f32 = 0.7;
const CHANGED_VOLUME_MUTED: bool = true;

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

fn get_default_stream(stream_type: AudioStreamType) -> AudioStream {
    *default_audio_info()
        .streams
        .into_iter()
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
) {
    let service_registry = ServiceRegistry::create();
    let audio_core_service_handle = Arc::new(RwLock::new(AudioCoreService::new()));
    service_registry.write().register_service(audio_core_service_handle.clone());
    let input_device_registry_service_handle =
        Arc::new(RwLock::new(InputDeviceRegistryService::new()));
    service_registry.write().register_service(input_device_registry_service_handle.clone());

    return (service_registry, audio_core_service_handle, input_device_registry_service_handle);
}

// This function is created so that we can manipulate the |pair_media_and_system_agent| flag.
// TODO(go/fxb/37493): Remove this function and the related tests when the hack is removed.
fn create_audio_fidl_service<'a>(
    mut service_dir: ServiceFsDir<'_, ServiceObj<'a, ()>>,
    service_registry_handle: Arc<RwLock<ServiceRegistry>>,
    storage: Arc<Mutex<DeviceStorage<AudioInfo>>>,
    pair_media_and_system_agent: bool,
) {
    let (action_tx, action_rx) = futures::channel::mpsc::unbounded::<SettingAction>();

    // Creates switchboard, handed to interface implementations to send messages
    // to handlers.
    let (switchboard_handle, event_tx) = SwitchboardImpl::create(action_tx);

    let service_context_handle = Arc::new(RwLock::new(ServiceContext::new(
        ServiceRegistry::serve(service_registry_handle.clone()),
    )));

    // Creates registry, used to register handlers for setting types.
    let registry_handle = RegistryImpl::create(event_tx, action_rx);
    registry_handle
        .write()
        .register(
            SettingType::Audio,
            spawn_audio_controller(
                service_context_handle.clone(),
                storage,
                pair_media_and_system_agent,
            ),
        )
        .unwrap();

    let switchboard_handle_clone = switchboard_handle.clone();
    service_dir.add_fidl_service(move |stream: AudioRequestStream| {
        spawn_audio_fidl_handler(switchboard_handle_clone.clone(), stream);
    });
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_audio() {
    let (service_registry, audio_core_service_handle, _) = create_services();

    let storage_factory = Box::new(InMemoryStorageFactory::create());
    let store = create_storage(&storage_factory).await;

    let mut fs = ServiceFs::new();

    create_fidl_service(
        fs.root_dir(),
        [SettingType::Audio].iter().cloned().collect(),
        Arc::new(RwLock::new(ServiceContext::new(ServiceRegistry::serve(
            service_registry.clone(),
        )))),
        storage_factory,
    );

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
    let (service_registry, _, input_service_handle) = create_services();

    let mut fs = ServiceFs::new();

    create_fidl_service(
        fs.root_dir(),
        [SettingType::Audio].iter().cloned().collect(),
        Arc::new(RwLock::new(ServiceContext::new(ServiceRegistry::serve(
            service_registry.clone(),
        )))),
        Box::new(InMemoryStorageFactory::create()),
    );

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

    let audio_proxy = env.connect_to_service::<AudioMarker>().unwrap();

    let buttons_event = MediaButtonsEvent { volume: Some(1), mic_mute: Some(true) };
    input_service_handle.read().send_media_button_event(buttons_event.clone());

    let updated_settings =
        audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    if let Some(input) = updated_settings.input {
        if let Some(mic_mute) = input.muted {
            assert!(mic_mute);
        } else {
            panic!("should have mic mute value");
        }
    } else {
        panic!("should have input settings");
    }
}

// Test to ensure mic input change events are received.
// TODO(fxb/41006): Add a request.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_bringup_without_input_registry() {
    let service_registry = ServiceRegistry::create();
    let audio_core_service_handle = Arc::new(RwLock::new(AudioCoreService::new()));
    service_registry.write().register_service(audio_core_service_handle.clone());

    let mut fs = ServiceFs::new();

    create_fidl_service(
        fs.root_dir(),
        [SettingType::Audio].iter().cloned().collect(),
        Arc::new(RwLock::new(ServiceContext::new(ServiceRegistry::serve(
            service_registry.clone(),
        )))),
        Box::new(InMemoryStorageFactory::create()),
    );

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

    create_fidl_service(
        fs.root_dir(),
        [SettingType::Audio].iter().cloned().collect(),
        Arc::new(RwLock::new(ServiceContext::new(ServiceRegistry::serve(
            service_registry.clone(),
        )))),
        Box::new(InMemoryStorageFactory::create()),
    );

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

// Test to ensure that when |pair_media_and_system_agent| is enabled, setting the media volume
// without a system agent volume will change the system agent volume.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_audio_pair_media_system() {
    let mut changed_system_stream = CHANGED_MEDIA_STREAM.clone();
    changed_system_stream.stream_type = AudioStreamType::SystemAgent;

    let (service_registry, audio_core_service_handle, _) = create_services();

    let mut fs = ServiceFs::new();

    let storage_factory = Box::new(InMemoryStorageFactory::create());
    let store = create_storage(&storage_factory).await;

    create_audio_fidl_service(fs.root_dir(), service_registry.clone(), store.clone(), true);

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

    let audio_proxy = env.connect_to_service::<AudioMarker>().unwrap();

    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    verify_audio_stream(
        settings.clone(),
        AudioStreamSettings::from(get_default_stream(AudioStreamType::Media)),
    );
    verify_audio_stream(
        settings.clone(),
        AudioStreamSettings::from(get_default_stream(AudioStreamType::SystemAgent)),
    );

    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS]).await;

    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    verify_audio_stream(settings.clone(), CHANGED_MEDIA_STREAM_SETTINGS);
    verify_audio_stream(settings.clone(), AudioStreamSettings::from(changed_system_stream));

    assert_eq!(
        (CHANGED_VOLUME_LEVEL, CHANGED_VOLUME_MUTED),
        audio_core_service_handle.read().get_level_and_mute(AudioRenderUsage::Media).unwrap()
    );
    assert_eq!(
        (CHANGED_VOLUME_LEVEL, CHANGED_VOLUME_MUTED),
        audio_core_service_handle.read().get_level_and_mute(AudioRenderUsage::SystemAgent).unwrap()
    );

    // Check to make sure value wrote out to store correctly.
    let stored_streams: [AudioStream; 5];
    {
        let mut store_lock = store.lock().await;
        stored_streams = store_lock.get().await.streams;
    }
    verify_contains_stream(&stored_streams, &CHANGED_MEDIA_STREAM);
    verify_contains_stream(&stored_streams, &changed_system_stream);
}

// Test to ensure that when |pair_media_and_system_agent| is disabled, setting the media volume will
// not affect the system agent volume.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_audio_pair_media_system_off() {
    let (service_registry, audio_core_service_handle, _) = create_services();

    let mut fs = ServiceFs::new();

    let storage_factory = Box::new(InMemoryStorageFactory::create());
    let store = create_storage(&storage_factory).await;

    create_audio_fidl_service(fs.root_dir(), service_registry.clone(), store.clone(), false);

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

    let audio_proxy = env.connect_to_service::<AudioMarker>().unwrap();

    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    verify_audio_stream(
        settings.clone(),
        AudioStreamSettings::from(get_default_stream(AudioStreamType::Media)),
    );
    verify_audio_stream(
        settings.clone(),
        AudioStreamSettings::from(get_default_stream(AudioStreamType::SystemAgent)),
    );

    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS]).await;

    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    verify_audio_stream(settings.clone(), CHANGED_MEDIA_STREAM_SETTINGS);
    verify_audio_stream(
        settings.clone(),
        AudioStreamSettings::from(get_default_stream(AudioStreamType::SystemAgent)),
    );

    assert_eq!(
        (CHANGED_VOLUME_LEVEL, CHANGED_VOLUME_MUTED),
        audio_core_service_handle.read().get_level_and_mute(AudioRenderUsage::Media).unwrap()
    );

    let default_system_stream = get_default_stream(AudioStreamType::SystemAgent);
    assert_eq!(
        (default_system_stream.user_volume_level, default_system_stream.user_volume_muted),
        audio_core_service_handle.read().get_level_and_mute(AudioRenderUsage::SystemAgent).unwrap()
    );

    // Check to make sure value wrote out to store correctly.
    let stored_streams: [AudioStream; 5];
    {
        let mut store_lock = store.lock().await;
        stored_streams = store_lock.get().await.streams;
    }
    verify_contains_stream(&stored_streams, &CHANGED_MEDIA_STREAM);
    verify_contains_stream(&stored_streams, &get_default_stream(AudioStreamType::SystemAgent));
}

// Test to ensure that when |pair_media_and_system_agent| is enabled, setting the media volume
// with the system agent volume will not set to the system agent volume to the media volume.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_audio_pair_media_system_with_system_agent_change() {
    let (service_registry, audio_core_service_handle, _) = create_services();

    let mut fs = ServiceFs::new();

    let storage_factory = Box::new(InMemoryStorageFactory::create());
    let store = create_storage(&storage_factory).await;

    create_audio_fidl_service(fs.root_dir(), service_registry.clone(), store.clone(), true);

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

    let audio_proxy = env.connect_to_service::<AudioMarker>().unwrap();

    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    verify_audio_stream(
        settings.clone(),
        AudioStreamSettings::from(get_default_stream(AudioStreamType::Media)),
    );
    verify_audio_stream(
        settings.clone(),
        AudioStreamSettings::from(get_default_stream(AudioStreamType::SystemAgent)),
    );

    const CHANGED_SYSTEM_LEVEL: f32 = 0.2;
    const CHANGED_SYSTEM_MUTED: bool = false;
    const CHANGED_SYSTEM_STREAM: AudioStream = AudioStream {
        stream_type: AudioStreamType::SystemAgent,
        source: AudioSettingSource::User,
        user_volume_level: CHANGED_SYSTEM_LEVEL,
        user_volume_muted: CHANGED_SYSTEM_MUTED,
    };
    let changed_system_stream_settings = AudioStreamSettings::from(CHANGED_SYSTEM_STREAM);

    set_volume(
        &audio_proxy,
        vec![CHANGED_MEDIA_STREAM_SETTINGS, changed_system_stream_settings.clone()],
    )
    .await;

    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    verify_audio_stream(settings.clone(), CHANGED_MEDIA_STREAM_SETTINGS);
    verify_audio_stream(settings.clone(), changed_system_stream_settings);

    assert_eq!(
        (CHANGED_VOLUME_LEVEL, CHANGED_VOLUME_MUTED),
        audio_core_service_handle.read().get_level_and_mute(AudioRenderUsage::Media).unwrap()
    );

    assert_eq!(
        (CHANGED_SYSTEM_LEVEL, CHANGED_SYSTEM_MUTED),
        audio_core_service_handle.read().get_level_and_mute(AudioRenderUsage::SystemAgent).unwrap()
    );

    // Check to make sure value wrote out to store correctly.
    let stored_streams: [AudioStream; 5];
    {
        let mut store_lock = store.lock().await;
        stored_streams = store_lock.get().await.streams;
    }
    verify_contains_stream(&stored_streams, &CHANGED_MEDIA_STREAM);
    verify_contains_stream(&stored_streams, &CHANGED_SYSTEM_STREAM);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_audio_info_copy() {
    let audio_info = default_audio_info();
    let copy_audio_info = audio_info;
    assert_eq!(audio_info, copy_audio_info);
}
