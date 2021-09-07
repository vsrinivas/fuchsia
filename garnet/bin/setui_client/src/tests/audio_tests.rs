// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::ExpectedStreamSettingsStruct;
use crate::Services;
use crate::ENV_NAME;
use anyhow::{Context as _, Error};
use fidl_fuchsia_media::AudioRenderUsage;
use fidl_fuchsia_settings::{
    AudioInput, AudioMarker, AudioRequest, AudioSettings, AudioStreamSettings, Volume,
};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;
use setui_client_lib::audio;

fn verify_streams(
    streams: Vec<AudioStreamSettings>,
    expected: &'static ExpectedStreamSettingsStruct,
) {
    let extracted_stream_settings = streams.get(0).unwrap();
    if let (Some(stream), Some(expected_stream)) =
        (extracted_stream_settings.stream, expected.stream)
    {
        assert_eq!(stream, expected_stream);
    }
    if let (Some(source), Some(expected_source)) =
        (extracted_stream_settings.source, expected.source)
    {
        assert_eq!(source, expected_source);
    }
    if let Some(user_volume) = extracted_stream_settings.user_volume.as_ref() {
        if let (Some(level), Some(expected_level)) = (user_volume.level, expected.level) {
            assert_eq!(level, expected_level);
        }
        if let (Some(volume_muted), Some(expected_volume_muted)) =
            (user_volume.muted, expected.volume_muted)
        {
            assert_eq!(volume_muted, expected_volume_muted);
        }
    }
}

pub(crate) async fn validate_audio(
    expected: &'static ExpectedStreamSettingsStruct,
) -> Result<(), Error> {
    let env = create_service!(Services::Audio,
        AudioRequest::Set { settings, responder } => {
            if let Some(streams) = settings.streams {
                verify_streams(streams, expected);
                responder.send(&mut (Ok(())))?;
            } else if let Some(input) = settings.input {
                if let (Some(input_muted), Some(expected_input_muted)) =
                    (input.muted, expected.input_muted) {
                    assert_eq!(input_muted, expected_input_muted);
                    responder.send(&mut (Ok(())))?;
                }
            }
        },
        AudioRequest::Watch { responder } => {
            responder.send(AudioSettings {
                streams: Some(vec![AudioStreamSettings {
                    stream: Some(AudioRenderUsage::Media),
                    source: Some(fidl_fuchsia_settings::AudioStreamSettingSource::User),
                    user_volume: Some(Volume {
                        level: Some(0.6),
                        muted: Some(false),
                        ..Volume::EMPTY
                    }),
                    ..AudioStreamSettings::EMPTY
                }]),
                input: Some(AudioInput {
                    muted: Some(true),
                    ..AudioInput::EMPTY
                }),
                ..AudioSettings::EMPTY
            })?;
        }
    );

    let audio_service =
        env.connect_to_protocol::<AudioMarker>().context("Failed to connect to audio service")?;

    assert_successful!(audio::command(
        audio_service,
        expected.stream,
        expected.source,
        expected.level,
        expected.volume_muted,
        expected.input_muted,
    ));
    Ok(())
}
