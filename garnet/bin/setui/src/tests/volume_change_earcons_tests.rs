#[cfg(test)]
use {
    crate::agent::earcons,
    crate::agent::restore_agent,
    crate::audio::default_audio_info,
    crate::fidl_clone::FIDLClone,
    crate::handler::device_storage::testing::{InMemoryStorageFactory, StorageAccessContext},
    crate::handler::device_storage::DeviceStorage,
    crate::internal::event,
    crate::message::base::MessengerType,
    crate::switchboard::base::{
        AudioInfo, AudioSettingSource, AudioStream, AudioStreamType, SettingType,
    },
    crate::tests::fakes::audio_core_service::AudioCoreService,
    crate::tests::fakes::input_device_registry_service::InputDeviceRegistryService,
    crate::tests::fakes::service_registry::ServiceRegistry,
    crate::tests::fakes::sound_player_service::{SoundEventReceiver, SoundPlayerService},
    crate::tests::scaffold,
    crate::EnvironmentBuilder,
    fidl_fuchsia_media::AudioRenderUsage,
    fidl_fuchsia_settings::{
        AudioMarker, AudioProxy, AudioSettings, AudioStreamSettingSource, AudioStreamSettings,
        Volume,
    },
    fidl_fuchsia_ui_input::MediaButtonsEvent,
    fuchsia_component::server::NestedEnvironment,
    futures::future::BoxFuture,
    futures::lock::Mutex,
    futures::StreamExt,
    std::sync::Arc,
};

const ENV_NAME: &str = "volume_change_earcons_test_environment";
const CONTEXT_ID: u64 = 0;
const INITIAL_VOLUME_LEVEL: f32 = 0.5;
const CHANGED_VOLUME_LEVEL_2: f32 = 0.8;
const MAX_VOLUME_LEVEL: f32 = 1.0;
const CHANGED_VOLUME_MUTED: bool = true;
const CHANGED_VOLUME_UNMUTED: bool = false;

const MAX_VOLUME_EARCON_ID: u32 = 0;
const VOLUME_EARCON_ID: u32 = 1;

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

const INITIAL_INTERRUPTION_STREAM_SETTINGS: AudioStreamSettings = AudioStreamSettings {
    stream: Some(fidl_fuchsia_media::AudioRenderUsage::Interruption),
    source: Some(fidl_fuchsia_settings::AudioStreamSettingSource::User),
    user_volume: Some(Volume { level: Some(INITIAL_VOLUME_LEVEL), muted: Some(false) }),
};

const CHANGED_MEDIA_STREAM_SETTINGS: AudioStreamSettings = AudioStreamSettings {
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

const CHANGED_INTERRUPTION_STREAM_SETTINGS: AudioStreamSettings = AudioStreamSettings {
    stream: Some(fidl_fuchsia_media::AudioRenderUsage::Interruption),
    source: Some(AudioStreamSettingSource::User),
    user_volume: Some(Volume {
        level: Some(CHANGED_VOLUME_LEVEL_2),
        muted: Some(CHANGED_VOLUME_MUTED),
    }),
};

const CHANGED_INTERRUPTION_STREAM_SETTINGS_MAX: AudioStreamSettings = AudioStreamSettings {
    stream: Some(fidl_fuchsia_media::AudioRenderUsage::Interruption),
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

impl From<AudioStreamSettings> for AudioStream {
    fn from(stream: AudioStreamSettings) -> Self {
        AudioStream {
            stream_type: AudioStreamType::from(stream.stream.unwrap()),
            source: AudioSettingSource::from(stream.source.unwrap()),
            user_volume_level: stream.user_volume.as_ref().unwrap().level.unwrap(),
            user_volume_muted: stream.user_volume.as_ref().unwrap().muted.unwrap(),
        }
    }
}

// Used to store fake services for mocking dependencies and checking input/outputs.
// To add a new fake to these tests, add here, in create_services, and then use
// in your test.
struct FakeServices {
    input_device_registry: Arc<Mutex<InputDeviceRegistryService>>,
    sound_player: Arc<Mutex<SoundPlayerService>>,
}

async fn create_environment(
    service_registry: Arc<Mutex<ServiceRegistry>>,
    overridden_initial_streams: Vec<AudioStreamSettings>,
) -> (NestedEnvironment, Arc<Mutex<DeviceStorage<AudioInfo>>>, event::message::Receptor) {
    let storage_factory = InMemoryStorageFactory::create();
    let mut initial_audio_info = default_audio_info();

    for stream in overridden_initial_streams {
        initial_audio_info.replace_stream(AudioStream::from(stream));
    }

    let store = create_storage(storage_factory.clone(), initial_audio_info).await;

    let (event_tx, mut event_rx) = futures::channel::mpsc::unbounded::<event::message::Factory>();

    // Upon instantiation, the subscriber will capture the event message
    // factory.
    let create_subscriber =
        Arc::new(move |factory: event::message::Factory| -> BoxFuture<'static, ()> {
            let event_tx = event_tx.clone();
            Box::pin(async move {
                event_tx.unbounded_send(factory).ok();
            })
        });

    let env = EnvironmentBuilder::new(storage_factory)
        .service(ServiceRegistry::serve(service_registry))
        .event_subscribers(&[scaffold::event::subscriber::Blueprint::create(create_subscriber)])
        .settings(&[SettingType::Audio])
        .agents(&[restore_agent::blueprint::create(), earcons::agent::blueprint::create()])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    let event_factory = event_rx.next().await.expect("should return a factory");

    let (_, receptor) = event_factory
        .create(MessengerType::Unbound)
        .await
        .expect("Should be able to retrieve messenger for publisher");

    (env, store, receptor)
}

// Gets the store from |factory| and populate it with default values.
async fn create_storage(
    factory: Arc<Mutex<InMemoryStorageFactory>>,
    audio_info: AudioInfo,
) -> Arc<Mutex<DeviceStorage<AudioInfo>>> {
    let store = factory
        .lock()
        .await
        .get_device_storage::<AudioInfo>(StorageAccessContext::Test, CONTEXT_ID);
    {
        let mut store_lock = store.lock().await;
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

    (
        service_registry,
        FakeServices {
            input_device_registry: input_device_registry_service_handle,
            sound_player: sound_player_service_handle,
        },
    )
}

/// Helper function to set volume streams on the proxy.
async fn set_volume(proxy: &AudioProxy, streams: Vec<AudioStreamSettings>) {
    let mut audio_settings = AudioSettings::empty();
    audio_settings.streams = Some(streams);
    proxy.set(audio_settings).await.expect("set completed").expect("set successful");
}

async fn verify_earcon(receiver: &mut SoundEventReceiver, id: u32, usage: AudioRenderUsage) {
    assert_eq!(receiver.next().await.unwrap(), (id, usage));
}

// Test to ensure that when the volume changes, the SoundPlayer receives requests to play the sounds
// with the correct ids for the media stream.
#[fuchsia_async::run_until_stalled(test)]
async fn test_media_sounds() {
    let (service_registry, fake_services) = create_services().await;
    let (env, ..) = create_environment(service_registry, vec![INITIAL_MEDIA_STREAM_SETTINGS]).await;
    let audio_proxy = env.connect_to_service::<AudioMarker>().unwrap();

    // Create channel to receive notifications for when sounds are played. Used to know when to
    // check the sound player fake that the sound has been played.
    let mut sound_event_receiver =
        fake_services.sound_player.lock().await.create_sound_played_listener().await;

    // Test that the volume-changed sound gets played on the soundplayer for media.
    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS]).await;
    verify_earcon(&mut sound_event_receiver, VOLUME_EARCON_ID, AudioRenderUsage::Background).await;

    // Test that the volume-max sound gets played on the soundplayer for media.
    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS_MAX]).await;
    verify_earcon(&mut sound_event_receiver, MAX_VOLUME_EARCON_ID, AudioRenderUsage::Background)
        .await;
}

// Test to ensure that when the volume changes, the SoundPlayer receives requests to play the sounds
// with the correct ids for the interruption stream.
#[fuchsia_async::run_until_stalled(test)]
async fn test_interruption_sounds() {
    let (service_registry, fake_services) = create_services().await;
    let (env, ..) = create_environment(
        service_registry,
        vec![INITIAL_MEDIA_STREAM_SETTINGS, INITIAL_INTERRUPTION_STREAM_SETTINGS],
    )
    .await;
    let audio_proxy = env.connect_to_service::<AudioMarker>().unwrap();

    // Create channel to receive notifications for when sounds are played. Used to know when to
    // check the sound player fake that the sound has been played.
    let mut sound_event_receiver =
        fake_services.sound_player.lock().await.create_sound_played_listener().await;

    // Test that the volume-changed sound gets played on the soundplayer for interruption.
    set_volume(&audio_proxy, vec![CHANGED_INTERRUPTION_STREAM_SETTINGS]).await;
    verify_earcon(&mut sound_event_receiver, VOLUME_EARCON_ID, AudioRenderUsage::Interruption)
        .await;

    // Test that the volume-max sound gets played on the soundplayer for interruption.
    set_volume(&audio_proxy, vec![CHANGED_INTERRUPTION_STREAM_SETTINGS_MAX]).await;
    verify_earcon(&mut sound_event_receiver, MAX_VOLUME_EARCON_ID, AudioRenderUsage::Interruption)
        .await;
}

// Test to ensure that when the volume is increased while already at max volume, the earcon for
// max volume plays.
#[fuchsia_async::run_until_stalled(test)]
async fn test_max_volume_sound_on_press() {
    let (service_registry, fake_services) = create_services().await;
    let (env, ..) = create_environment(service_registry, vec![INITIAL_MEDIA_STREAM_SETTINGS]).await;
    let mut sound_event_receiver =
        fake_services.sound_player.lock().await.create_sound_played_listener().await;

    let audio_proxy = env.connect_to_service::<AudioMarker>().unwrap();

    // Set volume to max.
    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS_MAX]).await;
    verify_earcon(&mut sound_event_receiver, MAX_VOLUME_EARCON_ID, AudioRenderUsage::Background)
        .await;

    // Try to increase volume. Only serves to set the "last volume button press" event
    // to 1 (volume up).
    let buttons_event =
        MediaButtonsEvent { volume: Some(1), mic_mute: Some(false), pause: Some(false) };
    fake_services.input_device_registry.lock().await.send_media_button_event(buttons_event.clone());

    // Sets volume max again.
    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS_MAX]).await;
    verify_earcon(&mut sound_event_receiver, MAX_VOLUME_EARCON_ID, AudioRenderUsage::Background)
        .await;

    // Set volume to max again, to simulate holding button.
    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS_MAX]).await;
    verify_earcon(&mut sound_event_receiver, MAX_VOLUME_EARCON_ID, AudioRenderUsage::Background)
        .await;

    // Check that the sound played the correct number of times.
    assert_eq!(fake_services.sound_player.lock().await.get_play_count(0).await, Some(3));
}

// Test to ensure that when the volume is changed on multiple channels, the sound only plays once.
#[fuchsia_async::run_until_stalled(test)]
async fn test_earcons_on_multiple_channel_change() {
    let (service_registry, fake_services) = create_services().await;
    let (env, ..) = create_environment(
        service_registry,
        vec![
            INITIAL_MEDIA_STREAM_SETTINGS,
            INITIAL_COMMUNICATION_STREAM_SETTINGS,
            INITIAL_SYSTEM_AGENT_STREAM_SETTINGS,
        ],
    )
    .await;

    let audio_proxy = env.connect_to_service::<AudioMarker>().unwrap();

    let mut sound_event_receiver =
        fake_services.sound_player.lock().await.create_sound_played_listener().await;
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

    verify_earcon(&mut sound_event_receiver, MAX_VOLUME_EARCON_ID, AudioRenderUsage::Background)
        .await;

    // Playing sound right after ensures that only 1 max sound was in the
    // pipeline.
    set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS]).await;
    verify_earcon(&mut sound_event_receiver, VOLUME_EARCON_ID, AudioRenderUsage::Background).await;
}
