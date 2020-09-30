// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use bt_a2dp::{
    self as a2dp, codec::MediaCodecConfig, inspect::DataStreamInspect, media_task::*,
    media_types::*,
};

use bt_avdtp::{self as avdtp, MediaCodecType, MediaStream};
use fidl_fuchsia_media::{AudioChannelId, AudioPcmMode, PcmFormat};
use fuchsia_async as fasync;
use fuchsia_bluetooth::types::Channel;
use fuchsia_bluetooth::types::PeerId;
use fuchsia_inspect as inspect;
use fuchsia_inspect_derive::WithInspect;
use futures::StreamExt;
use parking_lot::Mutex;
use std::convert::TryFrom;
use std::sync::{Arc, RwLock};
use test_util::*;

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

#[test]
fn source_media_stream_stats() {
    let mut exec = fasync::Executor::new().expect("executor should build");
    let builder = SourceTaskBuilder::new(sources::AudioSourceType::BigBen);

    let inspector = inspect::component::inspector();
    let root = inspector.root();
    let d = DataStreamInspect::default().with_inspect(root, "stream").expect("attach to tree");

    // Minimum SBC requirements are mono, 48kHz
    let mono_config = MediaCodecConfig::min_sbc();
    let mut task = builder.configure_task(&PeerId(1), &mono_config, d).expect("should build okay");

    let (mut remote, local) = Channel::create();
    let local = Arc::new(RwLock::new(local));
    let weak_local = Arc::downgrade(&local);
    let stream = MediaStream::new(Arc::new(Mutex::new(true)), weak_local);

    task.start(stream).expect("started");

    let _ = exec.run_singlethreaded(remote.next()).expect("some packet");

    let hierarchy = exec
        .run_singlethreaded(inspect::reader::read_from_inspector(&inspector))
        .expect("got hierarchy");

    // We don't know exactly how many were sent at this point, but make sure we got at
    // least some recorded.
    let total_bytes =
        hierarchy.get_property_by_path(&vec!["stream", "total_bytes"]).expect("missing property");
    assert_gt!(total_bytes.uint().expect("uint"), &0);

    let bytes_per_second_current = hierarchy
        .get_property_by_path(&vec!["stream", "bytes_per_second_current"])
        .expect("missing property");
    assert_gt!(bytes_per_second_current.uint().expect("uint"), &0);
}
