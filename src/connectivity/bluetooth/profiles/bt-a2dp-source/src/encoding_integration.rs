// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use bt_a2dp as a2dp;
use bt_avdtp as avdtp;
use fidl_fuchsia_media::{AudioChannelId, AudioPcmMode, PcmFormat};
use fuchsia_async as fasync;
use std::convert::TryFrom;

mod encoding;

use crate::encoding::EncodedStream;

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
