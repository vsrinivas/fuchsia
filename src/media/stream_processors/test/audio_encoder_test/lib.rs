// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]
#![cfg(test)]

mod pcm_audio;
mod test_suite;
mod timestamp_validator;

use crate::test_suite::*;
use fidl_fuchsia_media::*;
use std::rc::Rc;
use stream_processor_test::*;

#[fuchsia_async::run_singlethreaded]
#[test]
async fn test_suite() -> std::result::Result<(), ::failure::Error> {
    let sub_bands = SbcSubBands::SubBands4;
    let block_count = SbcBlockCount::BlockCount8;

    let sbc_tests = AudioEncoderTestCase {
        input_framelength: (sub_bands.into_primitive() * block_count.into_primitive()) as usize,
        settings: Rc::new(move || -> EncoderSettings {
            EncoderSettings::Sbc(SbcEncoderSettings {
                allocation: SbcAllocation::AllocLoudness,
                sub_bands,
                block_count,
                channel_mode: SbcChannelMode::Mono,
                // Recommended bit pool value for these parameters, from SBC spec.
                bit_pool: 59,
            })
        }),
        channel_count: 1,
        hash_tests: vec![AudioEncoderHashTest {
            output_file: None,
            input_format: PcmFormat {
                pcm_mode: AudioPcmMode::Linear,
                bits_per_sample: 16,
                frames_per_second: 44100,
                channel_map: vec![AudioChannelId::Cf],
            },
            expected_digest: ExpectedDigest::new(
                "Sbc: 44.1kHz/Loudness/Mono/bitpool 56/blocks 8/subbands 4",
                "5c65a88bda3f132538966d87df34aa8675f85c9892b7f9f5571f76f3c7813562",
            ),
        }],
    };

    await!(sbc_tests.run())
}
