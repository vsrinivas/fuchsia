// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use common::{SessionId, VolumeChangeEarconsTest, DEFAULT_VOLUME_LEVEL, DEFAULT_VOLUME_MUTED};
use fidl_fuchsia_media::AudioRenderUsage;
use fidl_fuchsia_settings::{AudioStreamSettingSource, AudioStreamSettings, Volume};
use futures::StreamExt;

const VOLUME_EARCON_ID: u32 = 1;
const BLUETOOTH_CONNECTED_SOUND_ID: u32 = 2;
const BLUETOOTH_DISCONNECTED_SOUND_ID: u32 = 3;

const ID_1: SessionId = 1;
const ID_2: SessionId = 2;
const ID_3: SessionId = 3;
const BLUETOOTH_DOMAIN: &str = "Bluetooth";
const NON_BLUETOOTH_DOMAIN_1: &str = "Cast App";
const NON_BLUETOOTH_DOMAIN_2: &str = "Cast App Helper";

const CHANGED_MEDIA_STREAM_SETTINGS: AudioStreamSettings = AudioStreamSettings {
    stream: Some(AudioRenderUsage::Media),
    source: Some(AudioStreamSettingSource::User),
    user_volume: Some(Volume {
        level: Some(DEFAULT_VOLUME_LEVEL + 0.2),
        muted: Some(DEFAULT_VOLUME_MUTED),
        ..Volume::EMPTY
    }),
    ..AudioStreamSettings::EMPTY
};

const CHANGED_INTERRUPTION_STREAM_SETTINGS: AudioStreamSettings = AudioStreamSettings {
    stream: Some(fidl_fuchsia_media::AudioRenderUsage::Interruption),
    source: Some(AudioStreamSettingSource::User),
    user_volume: Some(Volume {
        level: Some(DEFAULT_VOLUME_LEVEL + 0.25),
        muted: Some(DEFAULT_VOLUME_MUTED),
        ..Volume::EMPTY
    }),
    ..AudioStreamSettings::EMPTY
};

// Tests to ensure that when the bluetooth connections change, the SoundPlayer receives requests
// to play the sounds with the correct ids.
#[fuchsia::test]
async fn test_multiple_earcons() {
    let mut test_instance = VolumeChangeEarconsTest::create_realm_and_init()
        .await
        .expect("Failed to set up test realm");

    // Verify that the discovery service received a WatchSessions request.
    let _ = test_instance
        .discovery_watcher_receiver()
        .next()
        .await
        .expect("Failed to receive signal that WatchSessions succeded");

    // Create channel to receive notifications for when sounds are played. Used to verify when
    // sounds have been played.
    let mut sound_event_receiver = test_instance.create_sound_played_listener().await;

    // Add a bluetooth connection and verify an earcon plays.
    test_instance.update_session(ID_1, BLUETOOTH_DOMAIN).await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        BLUETOOTH_CONNECTED_SOUND_ID,
        AudioRenderUsage::Background,
    )
    .await;
    assert_eq!(
        Some(&1),
        test_instance.play_counts().lock().await.get(&BLUETOOTH_CONNECTED_SOUND_ID)
    );

    // Add another bluetooth connection and verify an earcon plays.
    test_instance.update_session(ID_2, BLUETOOTH_DOMAIN).await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        BLUETOOTH_CONNECTED_SOUND_ID,
        AudioRenderUsage::Background,
    )
    .await;
    assert_eq!(
        Some(&2),
        test_instance.play_counts().lock().await.get(&BLUETOOTH_CONNECTED_SOUND_ID)
    );

    // Disconnect the first bluetooth connection and verify an earcon plays.
    test_instance.remove_session(ID_1).await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        BLUETOOTH_DISCONNECTED_SOUND_ID,
        AudioRenderUsage::Background,
    )
    .await;
    assert_eq!(
        Some(&1),
        test_instance.play_counts().lock().await.get(&BLUETOOTH_DISCONNECTED_SOUND_ID)
    );

    // Disconnect the second bluetooth connection and verify an earcon plays.
    test_instance.remove_session(ID_2).await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        BLUETOOTH_DISCONNECTED_SOUND_ID,
        AudioRenderUsage::Background,
    )
    .await;
    assert_eq!(
        Some(&2),
        test_instance.play_counts().lock().await.get(&BLUETOOTH_DISCONNECTED_SOUND_ID)
    );

    let _ = test_instance.destroy().await;
}

// Tests to ensure that the bluetooth earcons sound plays at the media volume level.
#[fuchsia::test]
async fn test_earcons_play_at_media_volume_level() {
    let mut test_instance = VolumeChangeEarconsTest::create_realm_and_init()
        .await
        .expect("Failed to set up test realm");

    // Verify that the discovery service received a WatchSessions request.
    let _ = test_instance
        .discovery_watcher_receiver()
        .next()
        .await
        .expect("Failed to receive signal that WatchSessions succeded");

    // Create channel to receive notifications for when sounds are played. Used to know when to
    // check the sound player fake that the sound has been played.
    let mut sound_event_receiver = test_instance.create_sound_played_listener().await;

    // Set both the media and interruption streams to different volumes. The background stream
    // should match the media stream when played.
    test_instance.set_volume(vec![CHANGED_MEDIA_STREAM_SETTINGS]).await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        VOLUME_EARCON_ID,
        AudioRenderUsage::Background,
    )
    .await;

    test_instance.set_volume(vec![CHANGED_INTERRUPTION_STREAM_SETTINGS]).await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        VOLUME_EARCON_ID,
        AudioRenderUsage::Background,
    )
    .await;

    // Add connection and verify earcon plays.
    test_instance.update_session(ID_1, BLUETOOTH_DOMAIN).await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        BLUETOOTH_CONNECTED_SOUND_ID,
        AudioRenderUsage::Background,
    )
    .await;

    // Ensure background volume was matched when earcon was played.
    let expected_background_stream = AudioStreamSettings {
        stream: Some(AudioRenderUsage::Background),
        source: Some(AudioStreamSettingSource::System),
        user_volume: CHANGED_MEDIA_STREAM_SETTINGS.user_volume,
        ..AudioStreamSettings::EMPTY
    };
    test_instance.verify_volume(AudioRenderUsage::Background, expected_background_stream).await;

    let _ = test_instance.destroy().await;
}

// Tests to ensure that only bluetooth domains play sounds, and that when others
// are present, they do not duplicate the sounds.
#[fuchsia::test]
async fn test_bluetooth_domain() {
    let mut test_instance = VolumeChangeEarconsTest::create_realm_and_init()
        .await
        .expect("Failed to set up test realm");

    // Verify that the discovery service received a WatchSessions request.
    let _ = test_instance
        .discovery_watcher_receiver()
        .next()
        .await
        .expect("Failed to receive signal that WatchSessions succeded");

    // Create channel to receive notifications for when sounds are played. Used to know when to
    // check the sound player fake that the sound has been played.
    let mut sound_event_receiver = test_instance.create_sound_played_listener().await;

    // Add multiple updates, only one of which is the Bluetooth domain.
    test_instance.update_session(ID_1, BLUETOOTH_DOMAIN).await;
    test_instance.update_session(ID_2, NON_BLUETOOTH_DOMAIN_1).await;
    test_instance.update_session(ID_3, NON_BLUETOOTH_DOMAIN_2).await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        BLUETOOTH_CONNECTED_SOUND_ID,
        AudioRenderUsage::Background,
    )
    .await;
    assert_eq!(
        Some(&1),
        test_instance.play_counts().lock().await.get(&BLUETOOTH_CONNECTED_SOUND_ID)
    );

    // Disconnect the bluetooth connection and verify the disconnection earcon is the next one that
    // plays. If any of the non-bluetooth connections had played an earcon, this verify would fail.
    test_instance.remove_session(ID_1).await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        BLUETOOTH_DISCONNECTED_SOUND_ID,
        AudioRenderUsage::Background,
    )
    .await;
    assert_eq!(
        Some(&1),
        test_instance.play_counts().lock().await.get(&BLUETOOTH_DISCONNECTED_SOUND_ID)
    );

    let _ = test_instance.destroy().await;
}
