#[cfg(test)]
use {
    crate::agent::earcons_agent::EarconsAgent,
    crate::agent::restore_agent::RestoreAgent,
    crate::audio::default_audio_info,
    crate::fidl_clone::FIDLClone,
    crate::registry::device_storage::testing::{InMemoryStorageFactory, StorageAccessContext},
    crate::registry::device_storage::DeviceStorage,
    crate::switchboard::base::{AudioInfo, SettingType},
    crate::tests::fakes::audio_core_service::AudioCoreService,
    crate::tests::fakes::input_device_registry_service::InputDeviceRegistryService,
    crate::tests::fakes::service_registry::ServiceRegistry,
    crate::tests::fakes::sound_player_service::SoundPlayerService,
    crate::tests::fakes::usage_reporter_service::UsageReporterService,
    crate::EnvironmentBuilder,
    fidl_fuchsia_media::{
        AudioRenderUsage, Usage,
        UsageState::{Ducked, Muted, Unadjusted},
        UsageStateDucked, UsageStateMuted, UsageStateUnadjusted,
    },
    fidl_fuchsia_settings::{
        AudioMarker, AudioProxy, AudioSettings, AudioStreamSettingSource, AudioStreamSettings,
        Volume,
    },
    fidl_fuchsia_ui_input::MediaButtonsEvent,
    fuchsia_component::server::NestedEnvironment,
    futures::lock::Mutex,
    std::sync::Arc,
};

const ENV_NAME: &str = "earcons_agent_test_environment";
const INITIAL_VOLUME_LEVEL: f32 = 0.5;
const CHANGED_VOLUME_LEVEL_2: f32 = 0.8;
const MAX_VOLUME_LEVEL: f32 = 1.0;
const CHANGED_VOLUME_MUTED: bool = true;
const CHANGED_VOLUME_UNMUTED: bool = false;

const INITIAL_MEDIA_STREAM_SETTINGS: AudioStreamSettings = AudioStreamSettings {
    stream: Some(fidl_fuchsia_media::AudioRenderUsage::Media),
    source: Some(fidl_fuchsia_settings::AudioStreamSettingSource::User),
    user_volume: Some(Volume { level: Some(INITIAL_VOLUME_LEVEL), muted: Some(false) }),
};

const INITIAL_COMMUNICATION_STREAM_SETTINGS: AudioStreamSettings = AudioStreamSettings {
    stream: Some(fidl_fuchsia_media::AudioRenderUsage::Communication),
    source: Some(fidl_fuchsia_settings::AudioStreamSettingSource::User),
    user_volume: Some(Volume { level: Some(INITIAL_VOLUME_LEVEL), muted: Some(false) }),
};

const INITIAL_SYSTEM_AGENT_STREAM_SETTINGS: AudioStreamSettings = AudioStreamSettings {
    stream: Some(fidl_fuchsia_media::AudioRenderUsage::SystemAgent),
    source: Some(fidl_fuchsia_settings::AudioStreamSettingSource::User),
    user_volume: Some(Volume { level: Some(INITIAL_VOLUME_LEVEL), muted: Some(false) }),
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
    user_volume: Some(Volume {
        level: Some(MAX_VOLUME_LEVEL),
        muted: Some(CHANGED_VOLUME_UNMUTED),
    }),
};

const CHANGED_COMMUNICATION_STREAM_SETTINGS_MAX: AudioStreamSettings = AudioStreamSettings {
    stream: Some(fidl_fuchsia_media::AudioRenderUsage::Communication),
    source: Some(AudioStreamSettingSource::User),
    user_volume: Some(Volume {
        level: Some(MAX_VOLUME_LEVEL),
        muted: Some(CHANGED_VOLUME_UNMUTED),
    }),
};

const CHANGED_SYSTEM_AGENT_STREAM_SETTINGS_MAX: AudioStreamSettings = AudioStreamSettings {
    stream: Some(fidl_fuchsia_media::AudioRenderUsage::SystemAgent),
    source: Some(AudioStreamSettingSource::User),
    user_volume: Some(Volume {
        level: Some(MAX_VOLUME_LEVEL),
        muted: Some(CHANGED_VOLUME_UNMUTED),
    }),
};

// Used to store fake services for mocking dependencies and checking input/outputs.
// To add a new fake to these tests, add here, in create_services, and then use
// in your test.
struct FakeServices {
    input_device_registry: Arc<Mutex<InputDeviceRegistryService>>,
    sound_player: Arc<Mutex<SoundPlayerService>>,
    usage_reporter: Arc<Mutex<UsageReporterService>>,
}

async fn create_environment(
    service_registry: Arc<Mutex<ServiceRegistry>>,
) -> (NestedEnvironment, Arc<Mutex<DeviceStorage<AudioInfo>>>) {
    let storage_factory = InMemoryStorageFactory::create_handle();
    let store = create_storage(storage_factory.clone()).await;

    let env = EnvironmentBuilder::new(storage_factory)
        .service(ServiceRegistry::serve(service_registry))
        .settings(&[SettingType::Audio])
        .agents(&[
            Arc::new(Mutex::new(EarconsAgent::new())),
            Arc::new(Mutex::new(RestoreAgent::new())),
        ])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    (env, store)
}

// Gets the store from |factory| and populate it with default values.
async fn create_storage(
    factory: Arc<Mutex<InMemoryStorageFactory>>,
) -> Arc<Mutex<DeviceStorage<AudioInfo>>> {
    let store = factory.lock().await.get_device_storage::<AudioInfo>(StorageAccessContext::Test);
    {
        let mut store_lock = store.lock().await;
        let audio_info = default_audio_info();
        store_lock.write(&audio_info, false).await.unwrap();
    }
    store
}

// Returns a registry and audio related services it is populated with.
async fn create_services() -> (Arc<Mutex<ServiceRegistry>>, FakeServices) {
    let service_registry = ServiceRegistry::create();

    // Used for some functions inside the audio_controller and other dependencies.
    let audio_core_service_handle = Arc::new(Mutex::new(AudioCoreService::new()));
    service_registry.lock().await.register_service(audio_core_service_handle.clone());

    let input_device_registry_service_handle =
        Arc::new(Mutex::new(InputDeviceRegistryService::new()));
    service_registry.lock().await.register_service(input_device_registry_service_handle.clone());

    let sound_player_service_handle = Arc::new(Mutex::new(SoundPlayerService::new()));
    service_registry.lock().await.register_service(sound_player_service_handle.clone());

    let usage_reporter_service_handle = Arc::new(Mutex::new(UsageReporterService::new()));
    service_registry.lock().await.register_service(usage_reporter_service_handle.clone());

    (
        service_registry,
        FakeServices {
            input_device_registry: input_device_registry_service_handle,
            sound_player: sound_player_service_handle,
            usage_reporter: usage_reporter_service_handle,
        },
    )
}

/// Helper function to set volume streams on the proxy.
async fn set_volume(proxy: &AudioProxy, streams: Vec<AudioStreamSettings>) {
    let mut audio_settings = AudioSettings::empty();
    audio_settings.streams = Some(streams);
    proxy.set(audio_settings).await.expect("set completed").expect("set successful");
}

/// Verifies that a stream equal to |stream| is inside of |settings|.
fn verify_audio_stream(settings: AudioSettings, stream: AudioStreamSettings) {
    settings
        .streams
        .expect("audio settings contain streams")
        .into_iter()
        .find(|x| *x == stream)
        .expect("contains stream");
}

// Test to ensure that when the volume changes, the SoundPlayer receives requests to play the sounds
// with the correct ids.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_sounds() {
    let (service_registry, fake_services) = create_services().await;
    let (env, _) = create_environment(service_registry).await;
    let audio_proxy = env.connect_to_service::<AudioMarker>().unwrap();

    // Simulate initial restoration of media volume.
    set_volume(&audio_proxy, vec![INITIAL_MEDIA_STREAM_SETTINGS]).await;

    // Test that the volume-changed sound gets played on the soundplayer.
    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS_2]).await;
    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    verify_audio_stream(settings.clone(), CHANGED_MEDIA_STREAM_SETTINGS_2);
    assert!(fake_services.sound_player.lock().await.id_exists(1));
    assert_eq!(
        fake_services.sound_player.lock().await.get_usage_by_id(1),
        Some(AudioRenderUsage::Media)
    );

    // Test that the volume-max sound gets played on the soundplayer.
    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS_MAX]).await;
    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    verify_audio_stream(settings.clone(), CHANGED_MEDIA_STREAM_SETTINGS_MAX);
    assert!(fake_services.sound_player.lock().await.id_exists(0));
    assert_eq!(
        fake_services.sound_player.lock().await.get_usage_by_id(0),
        Some(AudioRenderUsage::Media)
    );
}

// Test to ensure that when the volume is increased while already at max volume, the earcon for
// max volume plays.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_max_volume_sound_on_press() {
    let (service_registry, fake_services) = create_services().await;
    let (env, _) = create_environment(service_registry).await;
    let audio_proxy = env.connect_to_service::<AudioMarker>().unwrap();

    // Simulate initial restoration of media volume.
    set_volume(&audio_proxy, vec![INITIAL_MEDIA_STREAM_SETTINGS]).await;

    // Set volume to max.
    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS_MAX]).await;

    audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    assert!(fake_services.sound_player.lock().await.id_exists(0));
    assert_eq!(fake_services.sound_player.lock().await.get_play_count(0), Some(1));

    // Try to increase volume. Only serves to set the "last volume button press" event
    // to 1 (volume up).
    let buttons_event =
        MediaButtonsEvent { volume: Some(1), mic_mute: Some(false), pause: Some(false) };
    fake_services.input_device_registry.lock().await.send_media_button_event(buttons_event.clone());

    // Sets volume max again.
    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS_MAX]).await;

    audio_proxy.watch().await.expect("watch completed").expect("watch successful");

    // Check that the sound played more than once.
    assert!(fake_services.sound_player.lock().await.id_exists(0));
    assert!(fake_services.sound_player.lock().await.get_play_count(0).unwrap() > 1);
}

// Test to ensure that when the volume is changed on multiple channels, the sound only plays once.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_earcons_on_multiple_channel_change() {
    let (service_registry, fake_services) = create_services().await;
    let (env, _) = create_environment(service_registry).await;
    let audio_proxy = env.connect_to_service::<AudioMarker>().unwrap();

    // Simulate initial restoration of stream volumes.
    set_volume(
        &audio_proxy,
        vec![
            INITIAL_MEDIA_STREAM_SETTINGS,
            INITIAL_COMMUNICATION_STREAM_SETTINGS,
            INITIAL_SYSTEM_AGENT_STREAM_SETTINGS,
        ],
    )
    .await;

    // Set volume to max on multiple channels.
    set_volume(
        &audio_proxy,
        vec![
            CHANGED_COMMUNICATION_STREAM_SETTINGS_MAX,
            CHANGED_SYSTEM_AGENT_STREAM_SETTINGS_MAX,
            CHANGED_MEDIA_STREAM_SETTINGS_MAX,
        ],
    )
    .await;

    audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    assert!(fake_services.sound_player.lock().await.id_exists(0));
    assert_eq!(fake_services.sound_player.lock().await.get_play_count(0), Some(1));
}

// Test to ensure that when another higher priority stream is playing,
// the earcons sounds don't play.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_earcons_with_active_stream() {
    let (service_registry, fake_services) = create_services().await;
    let (env, _) = create_environment(service_registry).await;
    let audio_proxy = env.connect_to_service::<AudioMarker>().unwrap();

    // Simulate initial restoration of media volume.
    set_volume(&audio_proxy, vec![INITIAL_MEDIA_STREAM_SETTINGS]).await;

    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS_2]).await;
    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    verify_audio_stream(settings.clone(), CHANGED_MEDIA_STREAM_SETTINGS_2);
    assert!(fake_services.sound_player.lock().await.id_exists(1));
    assert_eq!(
        fake_services.sound_player.lock().await.get_usage_by_id(1),
        Some(AudioRenderUsage::Media)
    );

    fake_services
        .usage_reporter
        .lock()
        .await
        .set_usage_state(
            Usage::RenderUsage(AudioRenderUsage::Background {}),
            Muted(UsageStateMuted {}),
        )
        .await;

    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS_MAX]).await;
    let settings = audio_proxy.watch().await.expect("watch completed").expect("watch successful");
    verify_audio_stream(settings.clone(), CHANGED_MEDIA_STREAM_SETTINGS_MAX);

    // With the background stream muted, the sound should not have played.
    assert!(!fake_services.sound_player.lock().await.id_exists(0));
    assert_ne!(
        fake_services.sound_player.lock().await.get_usage_by_id(0),
        Some(AudioRenderUsage::Media)
    );

    fake_services
        .usage_reporter
        .lock()
        .await
        .set_usage_state(
            Usage::RenderUsage(AudioRenderUsage::Background {}),
            Ducked(UsageStateDucked {}),
        )
        .await;

    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS_2]).await;
    audio_proxy.watch().await.expect("watch completed").expect("watch successful");

    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS_MAX]).await;
    audio_proxy.watch().await.expect("watch completed").expect("watch successful");

    // With the background stream ducked, the sound should not have played.
    assert!(!fake_services.sound_player.lock().await.id_exists(0));
    assert_ne!(
        fake_services.sound_player.lock().await.get_usage_by_id(0),
        Some(AudioRenderUsage::Media)
    );

    fake_services
        .usage_reporter
        .lock()
        .await
        .set_usage_state(
            Usage::RenderUsage(AudioRenderUsage::Background {}),
            Unadjusted(UsageStateUnadjusted {}),
        )
        .await;

    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS_2]).await;
    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS_MAX]).await;

    audio_proxy.watch().await.expect("watch completed").expect("watch successful");

    // With the background stream unadjusted, the sound should have played.
    assert!(fake_services.sound_player.lock().await.id_exists(0));
    assert_eq!(
        fake_services.sound_player.lock().await.get_usage_by_id(0),
        Some(AudioRenderUsage::Media)
    );
}
