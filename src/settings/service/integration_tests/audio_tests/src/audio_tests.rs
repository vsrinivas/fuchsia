// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::{
    AudioCoreRequest, AudioTest, DEFAULT_MEDIA_STREAM_SETTINGS, DEFAULT_VOLUME_LEVEL,
    DEFAULT_VOLUME_MUTED,
};
use fidl_fuchsia_media::AudioRenderUsage;
use fidl_fuchsia_settings::{
    AudioProxy, AudioSettings, AudioStreamSettingSource, AudioStreamSettings, Error, Volume,
};
use test_case::test_case;

mod common;
mod mock_audio_core_service;

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

const CHANGED_MEDIA_STREAM_SETTINGS_2: AudioStreamSettings = AudioStreamSettings {
    stream: Some(AudioRenderUsage::Media),
    source: Some(AudioStreamSettingSource::User),
    user_volume: Some(Volume {
        level: Some(CHANGED_VOLUME_LEVEL_2),
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

/// Verifies that a stream equal to |stream| is inside of |settings|.
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
    let mut audio_test = AudioTest::create_and_init(&[AudioRenderUsage::Media])
        .await
        .expect("setting up test realm");

    // Spawn a client that we'll use for a watch call later on to verify that Set calls are observed
    // by all clients.
    let watch_client = audio_test.connect_to_audio_marker();

    // Verify that the settings matches the default on start.
    let settings = watch_client.watch().await.expect("watch completed");
    verify_audio_stream(&settings, DEFAULT_MEDIA_STREAM_SETTINGS);

    // Verify that a Set call changes the audio settings.
    set_volume(audio_test.proxy(), vec![CHANGED_MEDIA_STREAM_SETTINGS]).await;
    let settings = audio_test.proxy().watch().await.expect("watch completed");
    verify_audio_stream(&settings, CHANGED_MEDIA_STREAM_SETTINGS);

    // Verify that audio core received the changed audio settings.
    audio_test
        .verify_audio_requests(&[
            AudioCoreRequest::SetVolume(AudioRenderUsage::Media, CHANGED_VOLUME_LEVEL),
            AudioCoreRequest::SetMute(AudioRenderUsage::Media, CHANGED_VOLUME_MUTED),
        ])
        .await
        .expect("changed audio requests received");

    // Verify that a separate client receives the changed settings on a Watch call.
    let settings = watch_client.watch().await.expect("watch completed");
    verify_audio_stream(&settings, CHANGED_MEDIA_STREAM_SETTINGS);

    audio_test.clean_up().await;
}

// Verifies that the correct value is returned after two successive watch calls.
#[fuchsia::test]
async fn test_consecutive_volume_changes() {
    let mut audio_test = AudioTest::create_and_init(&[AudioRenderUsage::Media])
        .await
        .expect("setting up test realm");

    // Spawn a client that we'll use for a watch call later on to verify that Set calls are observed
    // by all clients.
    let watch_client = audio_test.connect_to_audio_marker();

    // Make a set call and verify the value returned from watch changes.
    set_volume(audio_test.proxy(), vec![CHANGED_MEDIA_STREAM_SETTINGS]).await;
    let settings = audio_test.proxy().watch().await.expect("watch completed");
    verify_audio_stream(&settings, CHANGED_MEDIA_STREAM_SETTINGS);

    audio_test
        .verify_audio_requests(&[
            AudioCoreRequest::SetVolume(AudioRenderUsage::Media, CHANGED_VOLUME_LEVEL),
            AudioCoreRequest::SetMute(AudioRenderUsage::Media, CHANGED_VOLUME_MUTED),
        ])
        .await
        .expect("changed audio requests received");

    // Make a second set call and verify the value returned from watch changes again.
    set_volume(audio_test.proxy(), vec![CHANGED_MEDIA_STREAM_SETTINGS_2]).await;
    let settings = audio_test.proxy().watch().await.expect("watch completed");
    verify_audio_stream(&settings, CHANGED_MEDIA_STREAM_SETTINGS_2);

    // There won't be a muted request since the mute state doesn't change.
    audio_test
        .verify_audio_requests(&[AudioCoreRequest::SetVolume(
            AudioRenderUsage::Media,
            CHANGED_VOLUME_LEVEL_2,
        )])
        .await
        .expect("second changed audio requests received");

    // Verify that a separate client receives the changed settings on a Watch call.
    let settings = watch_client.watch().await.expect("watch completed");
    verify_audio_stream(&settings, CHANGED_MEDIA_STREAM_SETTINGS_2);

    audio_test.clean_up().await;
}

// Verifies that clients aren't notified for duplicate audio changes.
#[fuchsia::test]
async fn test_deduped_volume_changes() {
    let audio_test = AudioTest::create_and_init(&[AudioRenderUsage::Media])
        .await
        .expect("setting up test realm");

    {
        let set_client = audio_test.proxy();

        // Get the initial value.
        let _ = set_client.watch().await;
        let fut = set_client.watch();

        // Make a second identical request. This should do nothing.
        set_volume(&set_client, vec![DEFAULT_MEDIA_STREAM_SETTINGS]).await;

        // Make a third, different request. This should show up in the watch.
        set_volume(&set_client, vec![CHANGED_MEDIA_STREAM_SETTINGS_2]).await;

        let settings = fut.await.expect("watch completed");
        verify_audio_stream(&settings, CHANGED_MEDIA_STREAM_SETTINGS_2);
    }

    audio_test.clean_up().await;
}

// Verifies that changing one stream's volume does not affect the volume of other streams.
#[fuchsia::test]
async fn test_volume_not_overwritten() {
    let mut audio_test =
        AudioTest::create_and_init(&[AudioRenderUsage::Media, AudioRenderUsage::Background])
            .await
            .expect("setting up test realm");

    // Change the media stream and verify a watch call returns the updated value.
    set_volume(audio_test.proxy(), vec![CHANGED_MEDIA_STREAM_SETTINGS]).await;
    let settings = audio_test.proxy().watch().await.expect("watch completed");
    verify_audio_stream(&settings, CHANGED_MEDIA_STREAM_SETTINGS);

    audio_test
        .verify_audio_requests(&[
            AudioCoreRequest::SetVolume(AudioRenderUsage::Media, CHANGED_VOLUME_LEVEL),
            AudioCoreRequest::SetMute(AudioRenderUsage::Media, CHANGED_VOLUME_MUTED),
        ])
        .await
        .expect("changed audio requests received");

    // Change the background stream and verify a watch call returns the updated value.
    const CHANGED_BACKGROUND_STREAM_SETTINGS: AudioStreamSettings = AudioStreamSettings {
        stream: Some(AudioRenderUsage::Background),
        source: Some(AudioStreamSettingSource::User),
        user_volume: Some(Volume { level: Some(0.3), muted: Some(true), ..Volume::EMPTY }),
        ..AudioStreamSettings::EMPTY
    };

    set_volume(audio_test.proxy(), vec![CHANGED_BACKGROUND_STREAM_SETTINGS]).await;
    let settings = audio_test.proxy().watch().await.expect("watch completed");

    // Changing the background volume should not affect media volume.
    verify_audio_stream(&settings, CHANGED_MEDIA_STREAM_SETTINGS);
    verify_audio_stream(&settings, CHANGED_BACKGROUND_STREAM_SETTINGS);

    audio_test.clean_up().await;
}

// Tests that the volume level gets rounded to two decimal places.
#[fuchsia::test]
async fn test_volume_rounding() {
    let mut audio_test = AudioTest::create_and_init(&[AudioRenderUsage::Media])
        .await
        .expect("setting up test realm");

    // Set the volume to a level that's slightly more than CHANGED_VOLUME_LEVEL, but small enough
    // that it should be rounded away.
    set_volume(
        audio_test.proxy(),
        vec![AudioStreamSettings {
            stream: Some(AudioRenderUsage::Media),
            source: Some(AudioStreamSettingSource::User),
            user_volume: Some(Volume {
                level: Some(CHANGED_VOLUME_LEVEL + 0.0015),
                muted: Some(CHANGED_VOLUME_MUTED),
                ..Volume::EMPTY
            }),
            ..AudioStreamSettings::EMPTY
        }],
    )
    .await;

    let settings = audio_test.proxy().watch().await.expect("watch completed");
    verify_audio_stream(&settings, CHANGED_MEDIA_STREAM_SETTINGS);

    audio_test
        .verify_audio_requests(&[
            AudioCoreRequest::SetVolume(AudioRenderUsage::Media, CHANGED_VOLUME_LEVEL),
            AudioCoreRequest::SetMute(AudioRenderUsage::Media, CHANGED_VOLUME_MUTED),
        ])
        .await
        .expect("changed audio requests received");

    audio_test.clean_up().await;
}

// Verifies that invalid inputs return an error for Set calls.
#[test_case(AudioStreamSettings {
    stream: Some(AudioRenderUsage::Media),
    source: Some(AudioStreamSettingSource::User),
    user_volume: None,
    ..AudioStreamSettings::EMPTY
} ; "missing user volume")]
#[test_case(AudioStreamSettings {
    stream: Some(AudioRenderUsage::Media),
    source: Some(AudioStreamSettingSource::User),
    user_volume: Some(Volume {
        level: None,
        muted: None,
        ..Volume::EMPTY
    }),
..AudioStreamSettings::EMPTY
} ; "missing user volume and muted")]
#[test_case(AudioStreamSettings {
    stream: None,
    source: Some(AudioStreamSettingSource::User),
    user_volume: Some(Volume {
        level: Some(CHANGED_VOLUME_LEVEL),
        muted: Some(CHANGED_VOLUME_MUTED),
        ..Volume::EMPTY
    }),
    ..AudioStreamSettings::EMPTY
} ; "missing stream")]
#[test_case(AudioStreamSettings {
    stream: Some(AudioRenderUsage::Media),
    source: None,
    user_volume: Some(Volume {
        level: Some(CHANGED_VOLUME_LEVEL),
        muted: Some(CHANGED_VOLUME_MUTED),
        ..Volume::EMPTY
    }),
    ..AudioStreamSettings::EMPTY
} ; "missing source")]
#[fuchsia::test]
async fn invalid_missing_input_tests(setting: AudioStreamSettings) {
    let mut audio_test = AudioTest::create_and_init(&[AudioRenderUsage::Media])
        .await
        .expect("setting up test realm");

    let result = audio_test
        .proxy()
        .set(AudioSettings { streams: Some(vec![setting]), ..AudioSettings::EMPTY })
        .await
        .expect("set completed");
    assert_eq!(result, Err(Error::Failed));

    // The best way to test that audio core *didn't* receive an event is to
    // trigger another request and verify that it shows up next.
    set_volume(audio_test.proxy(), vec![CHANGED_MEDIA_STREAM_SETTINGS]).await;

    // Verify that audio core received the changed audio settings.
    audio_test
        .verify_audio_requests(&[
            AudioCoreRequest::SetVolume(AudioRenderUsage::Media, CHANGED_VOLUME_LEVEL),
            AudioCoreRequest::SetMute(AudioRenderUsage::Media, CHANGED_VOLUME_MUTED),
        ])
        .await
        .expect("changed audio requests received");

    audio_test.clean_up().await;
}

// Verifies that the input to Set calls can be missing certain parts and still be valid.
#[test_case(AudioStreamSettings {
    stream: Some(AudioRenderUsage::Media),
    source: Some(AudioStreamSettingSource::User),
    user_volume: Some(Volume {
        level: None,
        muted: Some(CHANGED_VOLUME_MUTED),
        ..Volume::EMPTY
    }),
    ..AudioStreamSettings::EMPTY
},  AudioCoreRequest::SetMute(AudioRenderUsage::Media, CHANGED_VOLUME_MUTED)
; "missing user volume")]
#[test_case(AudioStreamSettings {
    stream: Some(AudioRenderUsage::Media),
    source: Some(AudioStreamSettingSource::User),
    user_volume: Some(Volume {
        level: Some(CHANGED_VOLUME_LEVEL),
        muted: None,
        ..Volume::EMPTY
    }),
    ..AudioStreamSettings::EMPTY
}, AudioCoreRequest::SetVolume(AudioRenderUsage::Media, CHANGED_VOLUME_LEVEL)
; "missing muted")]
#[fuchsia::test]
async fn valid_missing_input_tests(
    setting: AudioStreamSettings,
    expected_request: AudioCoreRequest,
) {
    let mut audio_test = AudioTest::create_and_init(&[AudioRenderUsage::Media])
        .await
        .expect("setting up test realm");

    let result = audio_test
        .proxy()
        .set(AudioSettings { streams: Some(vec![setting]), ..AudioSettings::EMPTY })
        .await
        .expect("set completed");

    // Verify that the valid request comes through.
    audio_test
        .verify_audio_requests(&[expected_request])
        .await
        .expect("changed audio requests received");

    assert_eq!(result, Ok(()));
    audio_test.clean_up().await;
}
