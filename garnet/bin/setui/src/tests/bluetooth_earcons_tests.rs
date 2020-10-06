// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::agent::earcons,
    crate::agent::earcons::bluetooth_handler::BLUETOOTH_DOMAIN,
    crate::agent::earcons::sound_ids::{
        BLUETOOTH_CONNECTED_SOUND_ID, BLUETOOTH_DISCONNECTED_SOUND_ID,
    },
    crate::agent::restore_agent,
    crate::handler::device_storage::testing::InMemoryStorageFactory,
    crate::tests::fakes::discovery_service::{DiscoveryService, SessionId},
    crate::tests::fakes::service_registry::ServiceRegistry,
    crate::tests::fakes::sound_player_service::{SoundEventReceiver, SoundPlayerService},
    crate::EnvironmentBuilder,
    anyhow::{format_err, Error},
    fidl_fuchsia_media::AudioRenderUsage,
    fuchsia_component::server::NestedEnvironment,
    futures::lock::Mutex,
    futures::StreamExt,
    std::sync::Arc,
};

const ID_1: SessionId = 1;
const ID_2: SessionId = 2;
const ID_3: SessionId = 3;
const NON_BLUETOOTH_DOMAIN_1: &str = "Cast App";
const NON_BLUETOOTH_DOMAIN_2: &str = "Cast App Helper";
const ENV_NAME: &str = "bluetooth_earcons_test_environment";

/// Used to store fake services for mocking dependencies and checking input/outputs.
/// To add a new fake to these tests, add here, in create_services, and then use
/// in your test.
#[allow(dead_code)]
struct FakeServices {
    sound_player: Arc<Mutex<SoundPlayerService>>,
    discovery: Arc<Mutex<DiscoveryService>>,
}

/// Builds the test environment.
async fn create_environment(service_registry: Arc<Mutex<ServiceRegistry>>) -> NestedEnvironment {
    let env = EnvironmentBuilder::new(InMemoryStorageFactory::create())
        .service(ServiceRegistry::serve(service_registry))
        .settings(&[])
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
    service_registry.lock().await.register_service(sound_player_service_handle.clone());
    service_registry.lock().await.register_service(discovery_service_handle.clone());

    (
        service_registry,
        FakeServices {
            sound_player: sound_player_service_handle,
            discovery: discovery_service_handle,
        },
    )
}

/// Tests to ensure that when the bluetooth connections change, the SoundPlayer receives requests
/// to play the sounds with the correct ids.
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
    watch_for_next_sound_played(&mut sound_played_receiver).await.ok();
    assert!(fake_services.sound_player.lock().await.id_exists(BLUETOOTH_CONNECTED_SOUND_ID).await);
    assert_eq!(
        fake_services.sound_player.lock().await.get_play_count(BLUETOOTH_CONNECTED_SOUND_ID).await,
        Some(1)
    );

    // Add second connection.
    fake_services.discovery.lock().await.update_session(ID_2, BLUETOOTH_DOMAIN).await;
    watch_for_next_sound_played(&mut sound_played_receiver).await.ok();
    assert_eq!(
        fake_services.sound_player.lock().await.get_play_count(BLUETOOTH_CONNECTED_SOUND_ID).await,
        Some(2)
    );

    // Disconnect the first connection.
    fake_services.discovery.lock().await.remove_session(ID_1).await;
    watch_for_next_sound_played(&mut sound_played_receiver).await.ok();
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
    watch_for_next_sound_played(&mut sound_played_receiver).await.ok();
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

/// Tests to ensure that only bluetooth domains play sounds, and that when others
/// are present, they do not duplicate the sounds.
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
    watch_for_next_sound_played(&mut sound_played_receiver).await.ok();
    assert!(fake_services.sound_player.lock().await.id_exists(BLUETOOTH_CONNECTED_SOUND_ID).await);

    // Ensure the connection sound only played once.
    assert_eq!(
        fake_services.sound_player.lock().await.get_play_count(BLUETOOTH_CONNECTED_SOUND_ID).await,
        Some(1)
    );

    // Disconnect the bluetooth connection.
    fake_services.discovery.lock().await.remove_session(ID_1).await;
    watch_for_next_sound_played(&mut sound_played_receiver).await.ok();
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
    assert!(!fake_services.sound_player.lock().await.id_exists(BLUETOOTH_CONNECTED_SOUND_ID).await);
    assert_eq!(
        fake_services.sound_player.lock().await.get_play_count(BLUETOOTH_CONNECTED_SOUND_ID).await,
        None
    );

    // Disconnect the oobe blueooth connection.
    fake_services.discovery.lock().await.remove_session(ID_1).await;
    assert!(
        !fake_services.sound_player.lock().await.id_exists(BLUETOOTH_DISCONNECTED_SOUND_ID).await
    );
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
    watch_for_next_sound_played(&mut sound_played_receiver).await.ok();
    assert_eq!(
        fake_services.sound_player.lock().await.get_play_count(BLUETOOTH_CONNECTED_SOUND_ID).await,
        Some(1)
    );

    // Disconnect the regular bluetooth connection.
    fake_services.discovery.lock().await.remove_session(ID_2).await;
    watch_for_next_sound_played(&mut sound_played_receiver).await.ok();
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
