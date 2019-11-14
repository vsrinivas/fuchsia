// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

mod pcm_audio;
mod test_suite;
mod timestamp_validator;

use crate::test_suite::*;
use fidl_fuchsia_media::*;
use fuchsia_async as fasync;
use std::rc::Rc;
use stream_processor_test::*;

// INSTRUCTIONS FOR ADDING HASH TESTS
//
// 1. If adding a new pcm input configuration, write the saw wave to file and check it in in the
//    `test_data` directory. It should only be a few thousand PCM frames.
// 2. Set the `output_file` field to write the encoded output into `/tmp/` so you can copy it to
//    host.
// 3. Create an encoded stream with the same settings using another encoder (for sbc, use sbcenc or
//    ffmpeg; for aac use faac) on the reference saw wave.
// 4. Verify the output
//      a. If the codec should produce the exact same bytes for the same settings, `diff` the two
//         files.
//      b. If the codec is permitted to produce different encoded bytes for the same settings, do a
//         similarity check:
//           b1. Decode both the reference and our encoded stream (sbcdec, faad, etc)
//           b2. Import both tracks into Audacity
//           b3. Apply Effect > Invert to one track
//           b4. Select both tracks and Tracks > Mix > Mix and Render to New Track
//           b5. On the resulting track use Effect > Amplify and observe the new peak amplitude
// 5. If all looks good, commit the hash.

#[test]
fn sbc_test_suite() -> Result<()> {
    with_large_stack(|| {
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
                output_packet_count: 94,
                expected_digest: ExpectedDigest::new(
                    "Sbc: 44.1kHz/Loudness/Mono/bitpool 56/blocks 8/subbands 4",
                    "5c65a88bda3f132538966d87df34aa8675f85c9892b7f9f5571f76f3c7813562",
                ),
            }],
        };

        fasync::Executor::new().unwrap().run_singlethreaded(sbc_tests.run())
    })
}

#[test]
fn aac_test_suite() -> Result<()> {
    with_large_stack(|| {
        let sbc_tests = AudioEncoderTestCase {
            input_framelength: 1024,
            settings: Rc::new(move || -> EncoderSettings {
                EncoderSettings::Aac(AacEncoderSettings {
                    transport: AacTransport::Raw(AacTransportRaw {}),
                    channel_mode: AacChannelMode::Mono,
                    bit_rate: AacBitRate::Variable(AacVariableBitRate::V5),
                    aot: AacAudioObjectType::Mpeg2AacLc,
                })
            }),
            channel_count: 1,
            hash_tests: vec![AudioEncoderHashTest {
                input_format: PcmFormat {
                    pcm_mode: AudioPcmMode::Linear,
                    bits_per_sample: 16,
                    frames_per_second: 44100,
                    channel_map: vec![AudioChannelId::Cf],
                },
                output_packet_count: 5,
                output_file: None,
                expected_digest: ExpectedDigest::new(
                    "Aac: 44.1kHz/Mono/V5/Mpeg2 LC/Raw",
                    "3457e04babe80c5364215f66e74f51f871850c0a1c45f8cd69c76ad5fe54f97d",
                ),
            }],
        };

        fasync::Executor::new().unwrap().run_singlethreaded(sbc_tests.run())
    })
}
