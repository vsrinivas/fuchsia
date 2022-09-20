// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use common::{VolumeChangeEarconsTest, DEFAULT_VOLUME_LEVEL, DEFAULT_VOLUME_MUTED};
use fidl_fuchsia_media::AudioRenderUsage;
use fidl_fuchsia_settings::{AudioStreamSettingSource, AudioStreamSettings, Volume};
use futures::StreamExt;

/// A volume level of 0.7, which is different from the default of 0.5;
const CHANGED_VOLUME_LEVEL: f32 = DEFAULT_VOLUME_LEVEL + 0.2;

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

// Stream settings when background is changed to match Media.
const CHANGED_MEDIA_BACKGROUND_STREAM_SETTINGS: AudioStreamSettings = AudioStreamSettings {
    stream: Some(AudioRenderUsage::Background),
    source: Some(AudioStreamSettingSource::System),
    user_volume: Some(Volume {
        level: Some(CHANGED_VOLUME_LEVEL),
        muted: Some(CHANGED_VOLUME_UNMUTED),
        ..Volume::EMPTY
    }),
    ..AudioStreamSettings::EMPTY
};

const CHANGED_VOLUME_LEVEL_2: f32 = 0.8;
const MAX_VOLUME_LEVEL: f32 = 1.0;
const CHANGED_VOLUME_UNMUTED: bool = false;

const MAX_VOLUME_EARCON_ID: u32 = 0;
const VOLUME_EARCON_ID: u32 = 1;

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

// Stream settings when background is changed to match Media for max volume.
const CHANGED_MEDIA_BACKGROUND_STREAM_SETTINGS_MAX: AudioStreamSettings = AudioStreamSettings {
    stream: Some(fidl_fuchsia_media::AudioRenderUsage::Background),
    source: Some(AudioStreamSettingSource::System),
    user_volume: Some(Volume {
        level: Some(MAX_VOLUME_LEVEL),
        muted: Some(CHANGED_VOLUME_UNMUTED),
        ..Volume::EMPTY
    }),
    ..AudioStreamSettings::EMPTY
};

const CHANGED_MEDIA_STREAM_SETTINGS_SYSTEM: AudioStreamSettings = AudioStreamSettings {
    stream: Some(fidl_fuchsia_media::AudioRenderUsage::Media),
    source: Some(AudioStreamSettingSource::System),
    user_volume: Some(Volume {
        level: Some(CHANGED_VOLUME_LEVEL_2),
        muted: Some(CHANGED_VOLUME_MUTED),
        ..Volume::EMPTY
    }),
    ..AudioStreamSettings::EMPTY
};

const CHANGED_MEDIA_STREAM_SETTINGS_SYSTEM_WITH_FEEDBACK: AudioStreamSettings =
    AudioStreamSettings {
        stream: Some(fidl_fuchsia_media::AudioRenderUsage::Media),
        source: Some(AudioStreamSettingSource::SystemWithFeedback),
        user_volume: Some(Volume {
            level: Some(CHANGED_VOLUME_LEVEL_2),
            muted: Some(CHANGED_VOLUME_MUTED),
            ..Volume::EMPTY
        }),
        ..AudioStreamSettings::EMPTY
    };

const CHANGED_INTERRUPTION_STREAM_SETTINGS_MAX: AudioStreamSettings = AudioStreamSettings {
    stream: Some(fidl_fuchsia_media::AudioRenderUsage::Interruption),
    source: Some(AudioStreamSettingSource::User),
    user_volume: Some(Volume {
        level: Some(MAX_VOLUME_LEVEL),
        muted: Some(CHANGED_VOLUME_UNMUTED),
        ..Volume::EMPTY
    }),
    ..AudioStreamSettings::EMPTY
};

// Stream settings when background is changed to match Interruption for max volume.
const CHANGED_INTERRUPTION_BACKGROUND_STREAM_SETTINGS_MAX: AudioStreamSettings =
    AudioStreamSettings {
        stream: Some(fidl_fuchsia_media::AudioRenderUsage::Background),
        source: Some(AudioStreamSettingSource::System),
        user_volume: Some(Volume {
            level: Some(MAX_VOLUME_LEVEL),
            muted: Some(CHANGED_VOLUME_UNMUTED),
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

// Stream settings when background is changed to match Interruption.
const CHANGED_INTERRUPTION_BACKGROUND_STREAM_SETTINGS: AudioStreamSettings = AudioStreamSettings {
    stream: Some(fidl_fuchsia_media::AudioRenderUsage::Background),
    source: Some(AudioStreamSettingSource::System),
    user_volume: Some(Volume {
        level: Some(CHANGED_VOLUME_LEVEL_2),
        muted: Some(CHANGED_VOLUME_UNMUTED),
        ..Volume::EMPTY
    }),
    ..AudioStreamSettings::EMPTY
};

const CHANGED_COMMUNICATION_STREAM_SETTINGS_MAX: AudioStreamSettings = AudioStreamSettings {
    stream: Some(fidl_fuchsia_media::AudioRenderUsage::Communication),
    source: Some(AudioStreamSettingSource::User),
    user_volume: Some(Volume {
        level: Some(MAX_VOLUME_LEVEL),
        muted: Some(CHANGED_VOLUME_UNMUTED),
        ..Volume::EMPTY
    }),
    ..AudioStreamSettings::EMPTY
};

const CHANGED_SYSTEM_AGENT_STREAM_SETTINGS_MAX: AudioStreamSettings = AudioStreamSettings {
    stream: Some(fidl_fuchsia_media::AudioRenderUsage::SystemAgent),
    source: Some(AudioStreamSettingSource::User),
    user_volume: Some(Volume {
        level: Some(MAX_VOLUME_LEVEL),
        muted: Some(CHANGED_VOLUME_UNMUTED),
        ..Volume::EMPTY
    }),
    ..AudioStreamSettings::EMPTY
};

// Test to ensure that when the volume changes, the SoundPlayer receives requests to play the sounds
// with the correct ids for the media stream.
#[fuchsia::test]
async fn test_media_sounds() {
    let volume_change_earcons_test = VolumeChangeEarconsTest::create();
    let (signal_sender, mut signal_receiver) = futures::channel::mpsc::channel::<()>(0);
    let instance = volume_change_earcons_test
        .create_realm_without_input_device_registry(signal_sender)
        .await
        .expect("setting up test realm");
    let audio_proxy = VolumeChangeEarconsTest::connect_to_audio_marker(&instance);
    // Verify that audio core receives the initial request on start.
    let _ = signal_receiver.next().await;

    // Create channel to receive notifications for when sounds are played. Used to know when to
    // check the sound player fake that the sound has been played.
    let mut sound_event_receiver =
        VolumeChangeEarconsTest::create_sound_played_listener(&volume_change_earcons_test).await;

    // Test that the volume-changed sound gets played on the soundplayer for media.
    VolumeChangeEarconsTest::set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS]).await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        VOLUME_EARCON_ID,
        AudioRenderUsage::Background,
    )
    .await;

    // Test that the volume-max sound gets played on the soundplayer for media.
    VolumeChangeEarconsTest::set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS_MAX])
        .await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        MAX_VOLUME_EARCON_ID,
        AudioRenderUsage::Background,
    )
    .await;

    let _ = instance.destroy().await;
}

// Test to ensure that when the media volume changes via a system update, the SoundPlayer does
// not receive a request to play the volume changed sound.
#[should_panic]
// TODO(fxbug.dev/88496): delete the below
#[cfg_attr(feature = "variant_asan", ignore)]
#[fuchsia::test(allow_stalls = false)]
async fn test_media_sounds_system_source() {
    let volume_change_earcons_test = VolumeChangeEarconsTest::create();
    let (signal_sender, mut signal_receiver) = futures::channel::mpsc::channel::<()>(0);
    let instance = volume_change_earcons_test
        .create_realm_without_input_device_registry(signal_sender)
        .await
        .expect("setting up test realm");
    let audio_proxy = VolumeChangeEarconsTest::connect_to_audio_marker(&instance);
    // Verify that audio core receives the initial request on start.
    let _ = signal_receiver.next().await;

    // Create channel to receive notifications for when sounds are played. Used to know when to
    // check the sound player fake that the sound has been played.
    let mut sound_event_receiver =
        VolumeChangeEarconsTest::create_sound_played_listener(&volume_change_earcons_test).await;

    VolumeChangeEarconsTest::set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS_SYSTEM])
        .await;

    // There should be no next sound event to receive, this is expected to panic.
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        VOLUME_EARCON_ID,
        AudioRenderUsage::Background,
    )
    .await;

    let _ = instance.destroy().await;
}

// Test to ensure that when the media volume changes via a system update, the SoundPlayer does
// not receive a request to play the volume changed sound.
#[fuchsia::test]
async fn test_media_sounds_system_with_feedback_source() {
    let volume_change_earcons_test = VolumeChangeEarconsTest::create();
    let (signal_sender, mut signal_receiver) = futures::channel::mpsc::channel::<()>(0);
    let instance = volume_change_earcons_test
        .create_realm_without_input_device_registry(signal_sender)
        .await
        .expect("setting up test realm");
    let audio_proxy = VolumeChangeEarconsTest::connect_to_audio_marker(&instance);
    // Verify that audio core receives the initial request on start.
    let _ = signal_receiver.next().await;

    // Create channel to receive notifications for when sounds are played. Used to know when to
    // check the sound player fake that the sound has been played.
    let mut sound_event_receiver =
        VolumeChangeEarconsTest::create_sound_played_listener(&volume_change_earcons_test).await;

    // Test that the volume-changed sound gets played on the soundplayer for media.
    VolumeChangeEarconsTest::set_volume(
        &audio_proxy,
        vec![CHANGED_MEDIA_STREAM_SETTINGS_SYSTEM_WITH_FEEDBACK],
    )
    .await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        VOLUME_EARCON_ID,
        AudioRenderUsage::Background,
    )
    .await;

    let _ = instance.destroy().await;
}

// Test to ensure that when the volume changes, the SoundPlayer receives requests to play the sounds
// with the correct ids for the interruption stream.
#[fuchsia::test]
async fn test_interruption_sounds() {
    let volume_change_earcons_test = VolumeChangeEarconsTest::create();
    let (signal_sender, mut signal_receiver) = futures::channel::mpsc::channel::<()>(0);
    let instance = volume_change_earcons_test
        .create_realm_without_input_device_registry(signal_sender)
        .await
        .expect("setting up test realm");
    let audio_proxy = VolumeChangeEarconsTest::connect_to_audio_marker(&instance);
    // Verify that audio core receives the initial request on start.
    let _ = signal_receiver.next().await;

    // Create channel to receive notifications for when sounds are played. Used to know when to
    // check the sound player fake that the sound has been played.
    let mut sound_event_receiver =
        VolumeChangeEarconsTest::create_sound_played_listener(&volume_change_earcons_test).await;

    // Test that the volume-changed sound gets played on the soundplayer for interruption.
    VolumeChangeEarconsTest::set_volume(&audio_proxy, vec![CHANGED_INTERRUPTION_STREAM_SETTINGS])
        .await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        VOLUME_EARCON_ID,
        AudioRenderUsage::Background,
    )
    .await;

    // Test that the volume-max sound gets played on the soundplayer for interruption.
    VolumeChangeEarconsTest::set_volume(
        &audio_proxy,
        vec![CHANGED_INTERRUPTION_STREAM_SETTINGS_MAX],
    )
    .await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        MAX_VOLUME_EARCON_ID,
        AudioRenderUsage::Background,
    )
    .await;

    let _ = instance.destroy().await;
}

// Test to ensure that when the volume is set to max while already at max volume
// via a software change, the earcon for max volume plays.
#[fuchsia::test]
async fn test_max_volume_sound_on_sw_change() {
    let volume_change_earcons_test = VolumeChangeEarconsTest::create();
    let (signal_sender, mut signal_receiver) = futures::channel::mpsc::channel::<()>(0);
    let instance = volume_change_earcons_test
        .create_realm_without_input_device_registry(signal_sender)
        .await
        .expect("setting up test realm");
    let audio_proxy = VolumeChangeEarconsTest::connect_to_audio_marker(&instance);
    // Verify that audio core receives the initial request on start.
    let _ = signal_receiver.next().await;

    // Create channel to receive notifications for when sounds are played. Used to know when to
    // check the sound player fake that the sound has been played.
    let mut sound_event_receiver =
        VolumeChangeEarconsTest::create_sound_played_listener(&volume_change_earcons_test).await;

    // The max volume sound should play the first time it is set to max volume.
    VolumeChangeEarconsTest::set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS_MAX])
        .await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        MAX_VOLUME_EARCON_ID,
        AudioRenderUsage::Background,
    )
    .await;

    // The max volume sound should play again if it was already at max volume.
    VolumeChangeEarconsTest::set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS_MAX])
        .await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        MAX_VOLUME_EARCON_ID,
        AudioRenderUsage::Background,
    )
    .await;

    let _ = instance.destroy().await;
}

// Test to ensure that when the volume is changed on multiple channels, the sound only plays once.
#[fuchsia::test]
async fn test_earcons_on_multiple_channel_change() {
    let volume_change_earcons_test = VolumeChangeEarconsTest::create();
    let (signal_sender, mut signal_receiver) = futures::channel::mpsc::channel::<()>(0);
    let instance = volume_change_earcons_test
        .create_realm_without_input_device_registry(signal_sender)
        .await
        .expect("setting up test realm");
    let audio_proxy = VolumeChangeEarconsTest::connect_to_audio_marker(&instance);
    // Verify that audio core receives the initial request on start.
    let _ = signal_receiver.next().await;

    let mut sound_event_receiver =
        VolumeChangeEarconsTest::create_sound_played_listener(&volume_change_earcons_test).await;
    // Set volume to max on multiple channels.
    VolumeChangeEarconsTest::set_volume(
        &audio_proxy,
        vec![
            CHANGED_COMMUNICATION_STREAM_SETTINGS_MAX,
            CHANGED_SYSTEM_AGENT_STREAM_SETTINGS_MAX,
            CHANGED_MEDIA_STREAM_SETTINGS_MAX,
        ],
    )
    .await;

    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        MAX_VOLUME_EARCON_ID,
        AudioRenderUsage::Background,
    )
    .await;

    // Playing sound right after ensures that only 1 max sound was in the
    // pipeline.
    VolumeChangeEarconsTest::set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS]).await;
    VolumeChangeEarconsTest::verify_earcon(
        &mut sound_event_receiver,
        VOLUME_EARCON_ID,
        AudioRenderUsage::Background,
    )
    .await;

    let _ = instance.destroy().await;
}

// Test to ensure that when the media stream changes, the settings service matches the background
// stream to match the level.
#[fuchsia::test]
async fn test_media_background_matching() {
    let volume_change_earcons_test = VolumeChangeEarconsTest::create();
    let (signal_sender, mut signal_receiver) = futures::channel::mpsc::channel::<()>(0);
    let instance = volume_change_earcons_test
        .create_realm_without_input_device_registry(signal_sender)
        .await
        .expect("setting up test realm");
    let audio_proxy = VolumeChangeEarconsTest::connect_to_audio_marker(&instance);
    // Verify that audio core receives the initial request on start.
    let _ = signal_receiver.next().await;

    let mut sound_event_receiver =
        VolumeChangeEarconsTest::create_sound_played_listener(&volume_change_earcons_test).await;

    VolumeChangeEarconsTest::set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS]).await;
    let _ = sound_event_receiver.next().await.unwrap();
    VolumeChangeEarconsTest::verify_volume(
        &audio_proxy,
        AudioRenderUsage::Background,
        CHANGED_MEDIA_BACKGROUND_STREAM_SETTINGS,
    )
    .await;

    VolumeChangeEarconsTest::set_volume(&audio_proxy, vec![CHANGED_MEDIA_STREAM_SETTINGS_MAX])
        .await;
    let _ = sound_event_receiver.next().await.unwrap();
    VolumeChangeEarconsTest::verify_volume(
        &audio_proxy,
        AudioRenderUsage::Background,
        CHANGED_MEDIA_BACKGROUND_STREAM_SETTINGS_MAX,
    )
    .await;

    let _ = instance.destroy().await;
}

// Test to ensure that when the interruption stream changes, the settings service matches
// the background stream to match the level.
#[fuchsia::test]
async fn test_interruption_background_matching() {
    let volume_change_earcons_test = VolumeChangeEarconsTest::create();
    let (signal_sender, mut signal_receiver) = futures::channel::mpsc::channel::<()>(0);
    let instance = volume_change_earcons_test
        .create_realm_without_input_device_registry(signal_sender)
        .await
        .expect("setting up test realm");
    let audio_proxy = VolumeChangeEarconsTest::connect_to_audio_marker(&instance);
    // Verify that audio core receives the initial request on start.
    let _ = signal_receiver.next().await;

    let mut sound_event_receiver =
        VolumeChangeEarconsTest::create_sound_played_listener(&volume_change_earcons_test).await;

    // Test that the volume-changed sound gets played on the soundplayer for interruption
    // and the volume is matched on the background.
    VolumeChangeEarconsTest::set_volume(&audio_proxy, vec![CHANGED_INTERRUPTION_STREAM_SETTINGS])
        .await;
    let _ = sound_event_receiver.next().await.unwrap();
    VolumeChangeEarconsTest::verify_volume(
        &audio_proxy,
        AudioRenderUsage::Background,
        CHANGED_INTERRUPTION_BACKGROUND_STREAM_SETTINGS,
    )
    .await;

    // Test that the volume-max sound gets played on the soundplayer for interruption.
    VolumeChangeEarconsTest::set_volume(
        &audio_proxy,
        vec![CHANGED_INTERRUPTION_STREAM_SETTINGS_MAX],
    )
    .await;
    let _ = sound_event_receiver.next().await.unwrap();
    VolumeChangeEarconsTest::verify_volume(
        &audio_proxy,
        AudioRenderUsage::Background,
        CHANGED_INTERRUPTION_BACKGROUND_STREAM_SETTINGS_MAX,
    )
    .await;

    let _ = instance.destroy().await;
}
