// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::earcons;
use crate::agent::earcons::bluetooth_handler::BLUETOOTH_DOMAIN;
use crate::agent::earcons::sound_ids::{
    BLUETOOTH_CONNECTED_SOUND_ID, BLUETOOTH_DISCONNECTED_SOUND_ID,
};
use crate::agent::restore_agent;
use crate::audio::default_audio_info;
use crate::ingress::fidl::Interface;
use crate::storage::testing::InMemoryStorageFactory;
use crate::tests::fakes::discovery_service::{DiscoveryService, SessionId};
use crate::tests::fakes::service_registry::ServiceRegistry;
use crate::tests::fakes::sound_player_service::{SoundEventReceiver, SoundPlayerService};
use crate::EnvironmentBuilder;
use anyhow::{format_err, Error};
use fidl_fuchsia_media::AudioRenderUsage;
use fidl_fuchsia_settings::{
    AudioMarker, AudioSettings, AudioStreamSettingSource, AudioStreamSettings, Volume,
};
use fuchsia_component::server::NestedEnvironment;
use futures::lock::Mutex;
use futures::StreamExt;
use std::sync::Arc;

use super::fakes::audio_core_service::AudioCoreService;

const ID_1: SessionId = 1;
const ID_2: SessionId = 2;
const ID_3: SessionId = 3;
const NON_BLUETOOTH_DOMAIN_1: &str = "Cast App";
const NON_BLUETOOTH_DOMAIN_2: &str = "Cast App Helper";
const ENV_NAME: &str = "bluetooth_earcons_test_environment";
const DEFAULT_VOLUME_LEVEL: f32 = 0.5;
const DEFAULT_VOLUME_MUTED: bool = false;

/// A volume level of 0.7, which is different from the default of 0.5.
const CHANGED_VOLUME_LEVEL: f32 = DEFAULT_VOLUME_LEVEL + 0.2;
/// A volume level of 0.95, which is different from both CHANGED_VOLUME_LEVEL and the default.
const CHANGED_VOLUME_LEVEL_2: f32 = CHANGED_VOLUME_LEVEL + 0.25;
/// A mute state of true, which is different from the default of false.
const CHANGED_VOLUME_MUTED: bool = !DEFAULT_VOLUME_MUTED;

const CHANGED_MEDIA_STREAM_SETTINGS: AudioStreamSettings = AudioStreamSettings {
    stream: Some(AudioRenderUsage::Media),
    source: Some(AudioStreamSettingSource::User),
    user_volume: Some(Volume {
        level: Some(CHANGED_VOLUME_LEVEL),
        muted: Some(CHANGED_VOLUME_MUTED),
        ..Volume::EMPTY
    }),
    ..AudioStreamSettings::EMPTY
};

const CHANGED_INTERRUPTION_STREAM_SETTINGS: AudioStreamSettings = AudioStreamSettings {
    stream: Some(fidl_fuchsia_media::AudioRenderUsage::Interruption),
    source: Some(AudioStreamSettingSource::User),
    user_volume: Some(Volume {
        level: Some(CHANGED_VOLUME_LEVEL_2),
        muted: Some(CHANGED_VOLUME_MUTED),
        ..Volume::EMPTY
    }),
    ..AudioStreamSettings::EMPTY
};

/// Used to store fake services for mocking dependencies and checking input/outputs.
/// To add a new fake to these tests, add here, in create_services, and then use
/// in your test.
#[allow(dead_code)]
struct FakeServices {
    sound_player: Arc<Mutex<SoundPlayerService>>,
    discovery: Arc<Mutex<DiscoveryService>>,
    audio_core: Arc<Mutex<AudioCoreService>>,
}

/// Verifies that the stream with the specified [usage] has a volume equal to
/// [expected_level] inside [settings].
fn verify_audio_level_for_usage(
    settings: &AudioSettings,
    usage: AudioRenderUsage,
    expected_level: Option<Volume>,
) {
    let stream = settings
        .streams
        .as_ref()
        .expect("audio settings contain streams")
        .iter()
        .find(|x| x.stream == Some(usage))
        .expect("contains stream");
    assert_eq!(stream.user_volume, expected_level);
}

/// Builds the test environment.
async fn create_environment(service_registry: Arc<Mutex<ServiceRegistry>>) -> NestedEnvironment {
    let initial_audio_info = default_audio_info();
    let storage_factory = Arc::new(InMemoryStorageFactory::with_initial_data(&initial_audio_info));
    let env = EnvironmentBuilder::new(storage_factory)
        .service(ServiceRegistry::serve(service_registry))
        .fidl_interfaces(&[Interface::Audio])
        .agents(&[restore_agent::blueprint::create(), earcons::agent::blueprint::create()])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    env
}

/// Creates and returns a registry and bluetooth related services it is populated with.
async fn create_services() -> (Arc<Mutex<ServiceRegistry>>, FakeServices) {
    let service_registry = ServiceRegistry::create();

    let sound_player_service_handle = Arc::new(Mutex::new(SoundPlayerService::new()));
    let discovery_service_handle = Arc::new(Mutex::new(DiscoveryService::new()));
    let audio_core_service_handle = Arc::new(Mutex::new(AudioCoreService::new(false)));
    service_registry.lock().await.register_service(sound_player_service_handle.clone());
    service_registry.lock().await.register_service(discovery_service_handle.clone());
    service_registry.lock().await.register_service(audio_core_service_handle.clone());

    (
        service_registry,
        FakeServices {
            sound_player: sound_player_service_handle,
            discovery: discovery_service_handle,
            audio_core: audio_core_service_handle,
        },
    )
}

// Tests to ensure that when the bluetooth connections change, the SoundPlayer receives requests
// to play the sounds with the correct ids.
#[fuchsia_async::run_until_stalled(test)]
async fn test_sounds() {
    let (service_registry, fake_services) = create_services().await;
    let _env = create_environment(service_registry).await;

    // Create channel to receive notifications for when sounds are played. Used to know when to
    // check the sound player fake that the sound has been played.
    let mut sound_played_receiver =
        fake_services.sound_player.lock().await.create_sound_played_listener().await;

    // Add first connection.
    fake_services.discovery.lock().await.update_session(ID_1, BLUETOOTH_DOMAIN).await;
    let _ = watch_for_next_sound_played(&mut sound_played_receiver).await;
    assert!(fake_services.sound_player.lock().await.id_exists(BLUETOOTH_CONNECTED_SOUND_ID).await);
    assert_eq!(
        fake_services.sound_player.lock().await.get_play_count(BLUETOOTH_CONNECTED_SOUND_ID).await,
        Some(1)
    );

    // Add second connection.
    fake_services.discovery.lock().await.update_session(ID_2, BLUETOOTH_DOMAIN).await;
    let _ = watch_for_next_sound_played(&mut sound_played_receiver).await;
    assert_eq!(
        fake_services.sound_player.lock().await.get_play_count(BLUETOOTH_CONNECTED_SOUND_ID).await,
        Some(2)
    );

    // Disconnect the first connection.
    fake_services.discovery.lock().await.remove_session(ID_1).await;
    let _ = watch_for_next_sound_played(&mut sound_played_receiver).await;
    assert!(
        fake_services.sound_player.lock().await.id_exists(BLUETOOTH_DISCONNECTED_SOUND_ID).await
    );
    assert_eq!(
        fake_services
            .sound_player
            .lock()
            .await
            .get_play_count(BLUETOOTH_DISCONNECTED_SOUND_ID)
            .await,
        Some(1)
    );

    // Disconnect the second connection.
    fake_services.discovery.lock().await.remove_session(ID_2).await;
    let _ = watch_for_next_sound_played(&mut sound_played_receiver).await;
    assert_eq!(
        fake_services
            .sound_player
            .lock()
            .await
            .get_play_count(BLUETOOTH_DISCONNECTED_SOUND_ID)
            .await,
        Some(2)
    );
}

// Tests to ensure that the bluetooth earcons sound plays at the media volume level.
#[fuchsia_async::run_until_stalled(test)]
async fn test_volume_level() {
    let (service_registry, fake_services) = create_services().await;
    let env = create_environment(service_registry).await;

    // Create channel to receive notifications for when sounds are played. Used to know when to
    // check the sound player fake that the sound has been played.
    let mut sound_played_receiver =
        fake_services.sound_player.lock().await.create_sound_played_listener().await;

    // Set both the media and interruption streams to different volumes. The background stream
    // should match the media stream when played.
    let audio_proxy = env.connect_to_protocol::<AudioMarker>().unwrap();

    let mut audio_settings_media = AudioSettings::EMPTY;
    audio_settings_media.streams = Some(vec![CHANGED_MEDIA_STREAM_SETTINGS]);
    let _ =
        audio_proxy.set(audio_settings_media).await.expect("set completed").expect("set succeeded");

    let mut audio_settings_interruption = AudioSettings::EMPTY;
    audio_settings_interruption.streams = Some(vec![CHANGED_INTERRUPTION_STREAM_SETTINGS]);
    let _ = audio_proxy
        .set(audio_settings_interruption)
        .await
        .expect("set completed")
        .expect("set succeeded");

    // Add connection.
    fake_services.discovery.lock().await.update_session(ID_1, BLUETOOTH_DOMAIN).await;
    let _ = watch_for_next_sound_played(&mut sound_played_receiver).await;
    assert!(fake_services.sound_player.lock().await.id_exists(BLUETOOTH_CONNECTED_SOUND_ID).await);
    assert_eq!(
        fake_services.sound_player.lock().await.get_play_count(BLUETOOTH_CONNECTED_SOUND_ID).await,
        Some(1)
    );

    // Ensure background volume was matched before sound was played.
    let settings: AudioSettings = audio_proxy.watch().await.expect("watch completed");
    verify_audio_level_for_usage(
        &settings,
        AudioRenderUsage::Background,
        CHANGED_MEDIA_STREAM_SETTINGS.user_volume,
    );
}

// Tests to ensure that only bluetooth domains play sounds, and that when others
// are present, they do not duplicate the sounds.
#[fuchsia_async::run_until_stalled(test)]
async fn test_bluetooth_domain() {
    let (service_registry, fake_services) = create_services().await;
    let _env = create_environment(service_registry).await;

    // Create channel to receive notifications for when sounds are played. Used to know when to
    // check the sound player fake that the sound has been played.
    let mut sound_played_receiver =
        fake_services.sound_player.lock().await.create_sound_played_listener().await;

    // Add multiple updates, only one of which is the Bluetooth domain.
    fake_services.discovery.lock().await.update_session(ID_1, BLUETOOTH_DOMAIN).await;
    fake_services.discovery.lock().await.update_session(ID_2, NON_BLUETOOTH_DOMAIN_1).await;
    fake_services.discovery.lock().await.update_session(ID_3, NON_BLUETOOTH_DOMAIN_2).await;
    let _ = watch_for_next_sound_played(&mut sound_played_receiver).await;
    assert!(fake_services.sound_player.lock().await.id_exists(BLUETOOTH_CONNECTED_SOUND_ID).await);

    // Ensure the connection sound only played once.
    assert_eq!(
        fake_services.sound_player.lock().await.get_play_count(BLUETOOTH_CONNECTED_SOUND_ID).await,
        Some(1)
    );

    // Disconnect the bluetooth connection.
    fake_services.discovery.lock().await.remove_session(ID_1).await;
    let _ = watch_for_next_sound_played(&mut sound_played_receiver).await;
    assert!(
        fake_services.sound_player.lock().await.id_exists(BLUETOOTH_DISCONNECTED_SOUND_ID).await
    );
    assert_eq!(
        fake_services
            .sound_player
            .lock()
            .await
            .get_play_count(BLUETOOTH_DISCONNECTED_SOUND_ID)
            .await,
        Some(1)
    );
}

// Test that the bluetooth earcons aren't played for oobe connections.
#[fuchsia_async::run_until_stalled(test)]
async fn test_oobe_connection() {
    let (service_registry, fake_services) = create_services().await;
    let _env = create_environment(service_registry).await;

    // Create channel to receive notifications for when sounds are played. Used to know when to
    // check the sound player fake that the sound has been played.
    let mut sound_played_receiver =
        fake_services.sound_player.lock().await.create_sound_played_listener().await;

    // Add oobe bluetooth connection.
    fake_services.discovery.lock().await.update_session(ID_1, BLUETOOTH_DOMAIN).await;
    #[allow(clippy::bool_assert_comparison)]
    {
        assert_eq!(
            fake_services.sound_player.lock().await.id_exists(BLUETOOTH_CONNECTED_SOUND_ID).await,
            false
        );
    }
    assert_eq!(
        fake_services.sound_player.lock().await.get_play_count(BLUETOOTH_CONNECTED_SOUND_ID).await,
        None
    );

    // Disconnect the oobe blueooth connection.
    fake_services.discovery.lock().await.remove_session(ID_1).await;
    #[allow(clippy::bool_assert_comparison)]
    {
        assert_eq!(
            fake_services
                .sound_player
                .lock()
                .await
                .id_exists(BLUETOOTH_DISCONNECTED_SOUND_ID)
                .await,
            false
        );
    }
    assert_eq!(
        fake_services
            .sound_player
            .lock()
            .await
            .get_play_count(BLUETOOTH_DISCONNECTED_SOUND_ID)
            .await,
        None
    );

    // Add regular bluetooth connection.
    fake_services.discovery.lock().await.update_session(ID_2, BLUETOOTH_DOMAIN).await;
    let _ = watch_for_next_sound_played(&mut sound_played_receiver).await;
    assert_eq!(
        fake_services.sound_player.lock().await.get_play_count(BLUETOOTH_CONNECTED_SOUND_ID).await,
        Some(1)
    );

    // Disconnect the regular bluetooth connection.
    fake_services.discovery.lock().await.remove_session(ID_2).await;
    let _ = watch_for_next_sound_played(&mut sound_played_receiver).await;
    assert_eq!(
        fake_services
            .sound_player
            .lock()
            .await
            .get_play_count(BLUETOOTH_DISCONNECTED_SOUND_ID)
            .await,
        Some(1)
    );
}

/// Perform a watch on the sound player fake to wait until a sound has been played.
async fn watch_for_next_sound_played(
    sound_played_receiver: &mut SoundEventReceiver,
) -> Result<(u32, AudioRenderUsage), Error> {
    sound_played_receiver.next().await.ok_or_else(|| format_err!("No next event found in stream"))
}
