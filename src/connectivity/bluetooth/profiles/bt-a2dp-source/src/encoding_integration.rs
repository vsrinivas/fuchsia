// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use bt_a2dp::{self as a2dp, codec::MediaCodecConfig, inspect::DataStreamInspect, media_types::*};
use bt_avdtp::{self as avdtp, MediaCodecType};
use fidl_fuchsia_media::{AudioChannelId, AudioPcmMode, PcmFormat};
use fuchsia_async as fasync;
use fuchsia_bluetooth::types::PeerId;
use std::convert::TryFrom;

mod encoding;
mod pcm_audio;
mod source_task;
mod sources;

use crate::encoding::EncodedStream;
use crate::pcm_audio::PcmAudio;
use crate::source_task::SourceTaskBuilder;

pub async fn test_encoding_capability(capability: &avdtp::ServiceCapability) -> Result<(), Error> {
    let config = a2dp::codec::MediaCodecConfig::try_from(capability)?;
    let channel_map = match config.channel_count()? {
        1 => vec![AudioChannelId::Lf],
        2 => vec![AudioChannelId::Lf, AudioChannelId::Rf],
        _ => panic!("More than 2 channels not supported"),
    };
    let input_format = PcmFormat {
        pcm_mode: AudioPcmMode::Linear,
        bits_per_sample: 16,
        frames_per_second: 48000,
        channel_map,
    };
    EncodedStream::test(input_format, &config).await
}

#[test]
fn test_sbc_encodes_correctly() {
    let mut exec = fasync::Executor::new().expect("failed to create an executor");
    let sbc_capability = &avdtp::ServiceCapability::MediaCodec {
        media_type: avdtp::MediaType::Audio,
        codec_type: avdtp::MediaCodecType::AUDIO_SBC,
        codec_extra: vec![0x11, 0x15, 2, 53],
    };
    match exec.run_singlethreaded(test_encoding_capability(sbc_capability)) {
        Ok(()) => {}
        x => panic!("Expected encoding SBC to be Ok but got {:?}", x),
    };
}

#[test]
fn test_aac_encodes_correctly() {
    let mut exec = fasync::Executor::new().expect("failed to create an executor");
    let aac_capability = &avdtp::ServiceCapability::MediaCodec {
        media_type: avdtp::MediaType::Audio,
        codec_type: avdtp::MediaCodecType::AUDIO_AAC,
        codec_extra: vec![128, 1, 4, 4, 226, 0],
    };
    match exec.run_singlethreaded(test_encoding_capability(aac_capability)) {
        Ok(()) => {}
        x => panic!("Expected encoding AAC to be Ok but got {:?}", x),
    };
}

#[test]
fn configures_source_from_codec_config() {
    let _exec = fasync::Executor::new().expect("failed to create an executor");
    let builder = SourceTaskBuilder::new(sources::AudioSourceType::BigBen);

    // Minimum SBC requirements are mono, 48kHz
    let mono_config = MediaCodecConfig::min_sbc();
    let task = builder
        .configure_task(&PeerId(1), &mono_config, DataStreamInspect::default())
        .expect("should build okay");
    assert_eq!(48000, task.pcm_format.frames_per_second);
    assert_eq!(1, task.pcm_format.channel_map.len());

    // A standard SBC audio config which is stereo and 44.1kHz
    let sbc_codec_info = SbcCodecInfo::new(
        SbcSamplingFrequency::FREQ44100HZ,
        SbcChannelMode::JOINT_STEREO,
        SbcBlockCount::SIXTEEN,
        SbcSubBands::EIGHT,
        SbcAllocation::LOUDNESS,
        SbcCodecInfo::BITPOOL_MIN,
        SbcCodecInfo::BITPOOL_MAX,
    )
    .unwrap();
    let stereo_config =
        MediaCodecConfig::build(MediaCodecType::AUDIO_SBC, &sbc_codec_info.to_bytes().to_vec())
            .unwrap();

    let task = builder
        .configure_task(&PeerId(1), &stereo_config, DataStreamInspect::default())
        .expect("should build okay");
    assert_eq!(44100, task.pcm_format.frames_per_second);
    assert_eq!(2, task.pcm_format.channel_map.len());
}
