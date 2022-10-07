// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use common::VolumeChangeEarconsTest;
use fidl_fuchsia_media::AudioRenderUsage;
use fidl_fuchsia_settings::{AudioStreamSettingSource, AudioStreamSettings, Volume};
use fidl_fuchsia_ui_policy::MediaButtonsListenerProxy;
use futures::StreamExt;
use utils::MediaButtonsEventBuilder;

const MAX_VOLUME_LEVEL: f32 = 1.0;
const CHANGED_VOLUME_UNMUTED: bool = false;

const MAX_VOLUME_EARCON_ID: u32 = 0;

const CHANGED_MEDIA_STREAM_SETTINGS_MAX: AudioStreamSettings = AudioStreamSettings {
    stream: Some(fidl_fuchsia_media::AudioRenderUsage::Media),
    source: Some(AudioStreamSettingSource::User),
    user_volume: Some(Volume {
        level: Some(MAX_VOLUME_LEVEL),
        muted: Some(CHANGED_VOLUME_UNMUTED),
        ..Volume::EMPTY
    }),
    ..AudioStreamSettings::EMPTY
};

// Test to ensure that when the volume is increased while already at max volume, the earcon for
// max volume plays.
#[fuchsia::test]
async fn test_max_volume_sound_on_press() {
    let (media_button_sender, mut media_button_receiver) =
        futures::channel::mpsc::channel::<MediaButtonsListenerProxy>(0);

    let test_instance =
        VolumeChangeEarconsTest::create_realm_with_input_and_init(Some(media_button_sender))
            .await
            .expect("Failed to set up test realm");

    // Create channel to receive notifications for when sounds are played. Used to know when to
    // check the sound player fake that the sound has been played.
    let mut sound_event_receiver = test_instance.create_sound_played_listener().await;

    // Set volume to max.
    test_instance.set_volume(vec![CHANGED_MEDIA_STREAM_SETTINGS_MAX]).await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        MAX_VOLUME_EARCON_ID,
        AudioRenderUsage::Background,
    )
    .await;

    // Try to increase volume. Only serves to set the "last volume button press" event
    // to 1 (volume up).
    let buttons_event = MediaButtonsEventBuilder::new().set_volume(1).build();
    let listener_proxy = media_button_receiver.next().await.unwrap();
    let _ = listener_proxy.on_event(buttons_event.clone()).await;

    // Sets volume max again.
    test_instance.set_volume(vec![CHANGED_MEDIA_STREAM_SETTINGS_MAX]).await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        MAX_VOLUME_EARCON_ID,
        AudioRenderUsage::Background,
    )
    .await;

    // Set volume to max again, to simulate holding button.
    test_instance.set_volume(vec![CHANGED_MEDIA_STREAM_SETTINGS_MAX]).await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        MAX_VOLUME_EARCON_ID,
        AudioRenderUsage::Background,
    )
    .await;

    // Check that the sound played the correct number of times.
    assert_eq!(test_instance.play_counts().lock().await.get(&0).copied(), Some(3));

    let _ = test_instance.destroy().await;
}
