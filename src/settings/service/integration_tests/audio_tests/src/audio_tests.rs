// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::{
    AudioTest, DEFAULT_MEDIA_STREAM_SETTINGS, DEFAULT_VOLUME_LEVEL, DEFAULT_VOLUME_MUTED,
};
use crate::mock_audio_core_service::get_level_and_mute;
use fidl_fuchsia_media::AudioRenderUsage;
use fidl_fuchsia_settings::{
    AudioProxy, AudioSettings, AudioStreamSettingSource, AudioStreamSettings, Volume,
};

mod common;
mod mock_audio_core_service;

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

async fn set_volume(proxy: &AudioProxy, streams: Vec<AudioStreamSettings>) {
    let mut audio_settings = AudioSettings::EMPTY;
    audio_settings.streams = Some(streams);
    proxy.set(audio_settings).await.expect("set completed").expect("set successful");
}

// Verifies that a stream equal to |stream| is inside of |settings|.
fn verify_audio_stream(settings: &AudioSettings, stream: AudioStreamSettings) {
    let _ = settings
        .streams
        .as_ref()
        .expect("audio settings contain streams")
        .iter()
        .find(|x| **x == stream)
        .expect("contains stream");
}

// Verifies that basic get and set functionality works.
#[fuchsia::test]
async fn test_audio() {
    let audio_test = AudioTest::create();
    let instance = audio_test.create_realm().await.expect("setting up test realm");

    // Spawn a client that we'll use for a watch call later on to verify that Set calls are observed
    // by all clients.
    let watch_client = AudioTest::connect_to_audio_marker(&instance);

    // Verify that the settings match the default on start.
    let settings = watch_client.watch().await.expect("watch completed");
    verify_audio_stream(&settings, DEFAULT_MEDIA_STREAM_SETTINGS);

    {
        let set_client = AudioTest::connect_to_audio_marker(&instance);

        // Verify that a Set call changes the audio settings.
        set_volume(&set_client, vec![CHANGED_MEDIA_STREAM_SETTINGS]).await;
        let settings = set_client.watch().await.expect("watch completed");
        verify_audio_stream(&settings, CHANGED_MEDIA_STREAM_SETTINGS);

        // Verify that audio core received the changed audio settings.
        assert_eq!(
            (CHANGED_VOLUME_LEVEL, CHANGED_VOLUME_MUTED),
            get_level_and_mute(AudioRenderUsage::Media, &audio_test.audio_streams())
                .expect("found audio settings in streams")
        );
    }

    // Verify that a separate client receives the changed settings on a Watch call.
    let settings = watch_client.watch().await.expect("watch completed");
    verify_audio_stream(&settings, CHANGED_MEDIA_STREAM_SETTINGS);
}
