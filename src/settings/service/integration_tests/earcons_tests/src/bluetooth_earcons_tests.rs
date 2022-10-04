// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use common::{SessionId, VolumeChangeEarconsTest, DEFAULT_VOLUME_LEVEL, DEFAULT_VOLUME_MUTED};
use fidl_fuchsia_media::AudioRenderUsage;
use fidl_fuchsia_settings::{AudioSettings, AudioStreamSettingSource, AudioStreamSettings, Volume};
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
    let volume_change_earcons_test = VolumeChangeEarconsTest::create();
    let (signal_sender, mut signal_receiver) = futures::channel::mpsc::channel::<()>(0);
    let (watcher_sender, mut watcher_receiver) = futures::channel::mpsc::channel::<()>(0);
    let instance = volume_change_earcons_test
        .create_realm_with_real_discovery(signal_sender, watcher_sender)
        .await
        .expect("setting up test realm");
    let _audio_proxy = VolumeChangeEarconsTest::connect_to_audio_marker(&instance);
    // Verify that audio core receives the initial request on start.
    let _ = signal_receiver.next().await;

    // Verify that the discovery service received a WatchSessions request.
    let _ = watcher_receiver
        .next()
        .await
        .expect("Failed to receive signal that WatchSessions succeded");

    // Create channel to receive notifications for when sounds are played. Used to verify when
    // sounds have been played.
    let mut sound_event_receiver =
        VolumeChangeEarconsTest::create_sound_played_listener(&volume_change_earcons_test).await;

    // Add a bluetooth connection and verify an earcon plays.
    volume_change_earcons_test.update_session(ID_1, BLUETOOTH_DOMAIN).await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        BLUETOOTH_CONNECTED_SOUND_ID,
        AudioRenderUsage::Background,
    )
    .await;
    assert_eq!(
        Some(&1),
        volume_change_earcons_test.play_counts().lock().await.get(&BLUETOOTH_CONNECTED_SOUND_ID)
    );

    // Add another bluetooth connection and verify an earcon plays.
    volume_change_earcons_test.update_session(ID_2, BLUETOOTH_DOMAIN).await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        BLUETOOTH_CONNECTED_SOUND_ID,
        AudioRenderUsage::Background,
    )
    .await;
    assert_eq!(
        Some(&2),
        volume_change_earcons_test.play_counts().lock().await.get(&BLUETOOTH_CONNECTED_SOUND_ID)
    );

    // Disconnect the first bluetooth connection and verify an earcon plays.
    volume_change_earcons_test.remove_session(ID_1).await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        BLUETOOTH_DISCONNECTED_SOUND_ID,
        AudioRenderUsage::Background,
    )
    .await;
    assert_eq!(
        Some(&1),
        volume_change_earcons_test.play_counts().lock().await.get(&BLUETOOTH_DISCONNECTED_SOUND_ID)
    );

    // Disconnect the second bluetooth connection and verify an earcon plays.
    volume_change_earcons_test.remove_session(ID_2).await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        BLUETOOTH_DISCONNECTED_SOUND_ID,
        AudioRenderUsage::Background,
    )
    .await;
    assert_eq!(
        Some(&2),
        volume_change_earcons_test.play_counts().lock().await.get(&BLUETOOTH_DISCONNECTED_SOUND_ID)
    );

    let _ = instance.destroy().await;
}

// Tests to ensure that the bluetooth earcons sound plays at the media volume level.
#[fuchsia::test]
async fn test_earcons_play_at_media_volume_level() {
    let volume_change_earcons_test = VolumeChangeEarconsTest::create();
    let (signal_sender, mut signal_receiver) = futures::channel::mpsc::channel::<()>(0);
    let (watcher_sender, mut watcher_receiver) = futures::channel::mpsc::channel::<()>(0);
    let instance = volume_change_earcons_test
        .create_realm_with_real_discovery(signal_sender, watcher_sender)
        .await
        .expect("setting up test realm");
    let audio_proxy = VolumeChangeEarconsTest::connect_to_audio_marker(&instance);
    // Verify that audio core receives the initial request on start.
    let _ = signal_receiver.next().await;

    // Verify that the discovery service received a WatchSessions request.
    let _ = watcher_receiver
        .next()
        .await
        .expect("Failed to receive signal that WatchSessions succeded");

    // Create channel to receive notifications for when sounds are played. Used to know when to
    // check the sound player fake that the sound has been played.
    let mut sound_event_receiver =
        VolumeChangeEarconsTest::create_sound_played_listener(&volume_change_earcons_test).await;

    // Set both the media and interruption streams to different volumes. The background stream
    // should match the media stream when played.
    let mut audio_settings_media = AudioSettings::EMPTY;
    audio_settings_media.streams = Some(vec![CHANGED_MEDIA_STREAM_SETTINGS]);
    let _ =
        audio_proxy.set(audio_settings_media).await.expect("set completed").expect("set succeeded");
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        VOLUME_EARCON_ID,
        AudioRenderUsage::Background,
    )
    .await;

    let mut audio_settings_interruption = AudioSettings::EMPTY;
    audio_settings_interruption.streams = Some(vec![CHANGED_INTERRUPTION_STREAM_SETTINGS]);
    let _ = audio_proxy
        .set(audio_settings_interruption)
        .await
        .expect("set completed")
        .expect("set succeeded");
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        VOLUME_EARCON_ID,
        AudioRenderUsage::Background,
    )
    .await;

    // Add connection and verify earcon plays.
    volume_change_earcons_test.update_session(ID_1, BLUETOOTH_DOMAIN).await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        BLUETOOTH_CONNECTED_SOUND_ID,
        AudioRenderUsage::Background,
    )
    .await;

    // Ensure background volume was matched when earcon was played.
    let settings: AudioSettings = audio_proxy.watch().await.expect("watch completed");
    verify_audio_level_for_usage(
        &settings,
        AudioRenderUsage::Background,
        CHANGED_MEDIA_STREAM_SETTINGS.user_volume,
    );

    let _ = instance.destroy().await;
}

// Tests to ensure that only bluetooth domains play sounds, and that when others
// are present, they do not duplicate the sounds.
#[fuchsia::test]
async fn test_bluetooth_domain() {
    let volume_change_earcons_test = VolumeChangeEarconsTest::create();
    let (signal_sender, mut signal_receiver) = futures::channel::mpsc::channel::<()>(0);
    let (watcher_sender, mut watcher_receiver) = futures::channel::mpsc::channel::<()>(0);
    let instance = volume_change_earcons_test
        .create_realm_with_real_discovery(signal_sender, watcher_sender)
        .await
        .expect("setting up test realm");
    let _audio_proxy = VolumeChangeEarconsTest::connect_to_audio_marker(&instance);
    // Verify that audio core receives the initial request on start.
    let _ = signal_receiver.next().await;

    // Verify that the discovery service received a WatchSessions request.
    let _ = watcher_receiver
        .next()
        .await
        .expect("Failed to receive signal that WatchSessions succeded");

    // Create channel to receive notifications for when sounds are played. Used to know when to
    // check the sound player fake that the sound has been played.
    let mut sound_event_receiver =
        VolumeChangeEarconsTest::create_sound_played_listener(&volume_change_earcons_test).await;

    // Add multiple updates, only one of which is the Bluetooth domain.
    volume_change_earcons_test.update_session(ID_1, BLUETOOTH_DOMAIN).await;
    volume_change_earcons_test.update_session(ID_2, NON_BLUETOOTH_DOMAIN_1).await;
    volume_change_earcons_test.update_session(ID_3, NON_BLUETOOTH_DOMAIN_2).await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        BLUETOOTH_CONNECTED_SOUND_ID,
        AudioRenderUsage::Background,
    )
    .await;
    assert_eq!(
        Some(&1),
        volume_change_earcons_test.play_counts().lock().await.get(&BLUETOOTH_CONNECTED_SOUND_ID)
    );

    // Disconnect the bluetooth connection and verify the disconnection earcon is the next one that
    // plays. If any of the non-bluetooth connections had played an earcon, this verify would fail.
    volume_change_earcons_test.remove_session(ID_1).await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        BLUETOOTH_DISCONNECTED_SOUND_ID,
        AudioRenderUsage::Background,
    )
    .await;
    assert_eq!(
        Some(&1),
        volume_change_earcons_test.play_counts().lock().await.get(&BLUETOOTH_DISCONNECTED_SOUND_ID)
    );

    let _ = instance.destroy().await;
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
