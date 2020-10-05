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
// 2. Set the `output_file` field to write the encoded output into
//    `/tmp/r/sys/fuchsia.com:audio_encoder_test:0#meta:audio_encoder_test.cmx ` so you can copy
//    it to host.
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
                expected_digests: vec![ExpectedDigest::new(
                    "Sbc: 44.1kHz/Loudness/Mono/bitpool 56/blocks 8/subbands 4",
                    "5c65a88bda3f132538966d87df34aa8675f85c9892b7f9f5571f76f3c7813562",
                )],
            }],
        };

        fasync::Executor::new().unwrap().run_singlethreaded(sbc_tests.run())
    })
}

#[test]
fn aac_test_suite() -> Result<()> {
    with_large_stack(|| {
        let aac_raw_tests = AudioEncoderTestCase {
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
                expected_digests: vec![
                    ExpectedDigest::new(
                        "Aac: 44.1kHz/Mono/V5/Mpeg2 LC/Raw Arm",
                        "11fe39d40b09c3158172adf86ecb715d98f5e0ca9d5b541629ac80922f79fc1c",
                    ),
                    ExpectedDigest::new(
                        "Aac: 44.1kHz/Mono/V5/Mpeg2 LC/Raw x64",
                        "5be551b15b856508a186daa008e06b5ea2d7c2b18ae7977c5037ddee92d4ef9b",
                    ),
                ],
            }],
        };

        fasync::Executor::new().unwrap().run_singlethreaded(aac_raw_tests.run())?;

        // Test the MPEG4 AAC_LC variant. This affects encoder behavior but in this test case the
        // resulting bit streams are identical.
        let aac_raw_tests = AudioEncoderTestCase {
            input_framelength: 1024,
            settings: Rc::new(move || -> EncoderSettings {
                EncoderSettings::Aac(AacEncoderSettings {
                    transport: AacTransport::Raw(AacTransportRaw {}),
                    channel_mode: AacChannelMode::Mono,
                    bit_rate: AacBitRate::Variable(AacVariableBitRate::V5),
                    aot: AacAudioObjectType::Mpeg4AacLc,
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
                expected_digests: vec![
                    ExpectedDigest::new(
                        "Aac: 44.1kHz/Mono/V5/Mpeg2 LC/Raw Arm",
                        "11fe39d40b09c3158172adf86ecb715d98f5e0ca9d5b541629ac80922f79fc1c",
                    ),
                    ExpectedDigest::new(
                        "Aac: 44.1kHz/Mono/V5/Mpeg2 LC/Raw x64",
                        "5be551b15b856508a186daa008e06b5ea2d7c2b18ae7977c5037ddee92d4ef9b",
                    ),
                ],
            }],
        };

        fasync::Executor::new().unwrap().run_singlethreaded(aac_raw_tests.run())
    })
}

#[test]
fn aac_adts_test_suite() -> Result<()> {
    with_large_stack(|| {
        let aac_adts_tests = AudioEncoderTestCase {
            input_framelength: 1024,
            settings: Rc::new(move || -> EncoderSettings {
                EncoderSettings::Aac(AacEncoderSettings {
                    transport: AacTransport::Adts(AacTransportAdts {}),
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
                expected_digests: vec![
                    ExpectedDigest::new(
                        "Aac: 44.1kHz/Mono/V5/Mpeg2 LC/Adts Arm",
                        "c9d1ebb5844b9d90c09b0a26db14ddcf4189e77087efc064061f1c88df51e296",
                    ),
                    ExpectedDigest::new(
                        "Aac: 44.1kHz/Mono/V5/Mpeg2 LC/Adts x64",
                        "e88afc9130dc3cf429719f4e66fa7c60a17161c5ac30b37c527ab98e83f30750",
                    ),
                ],
            }],
        };

        fasync::Executor::new().unwrap().run_singlethreaded(aac_adts_tests.run())
    })
}

#[test]
fn aac_latm_test_suite() -> Result<()> {
    with_large_stack(|| {
        let aac_latm_with_mux_config_test = AudioEncoderTestCase {
            input_framelength: 1024,
            settings: Rc::new(move || -> EncoderSettings {
                EncoderSettings::Aac(AacEncoderSettings {
                    transport: AacTransport::Latm(AacTransportLatm { mux_config_present: true }),
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
                expected_digests: vec![
                    ExpectedDigest::new(
                        "Aac: 44.1kHz/Mono/V5/Mpeg2 LC/Latm/MuxConfig Arm",
                        "85ce565087981c36e47c873be7df2d57d3c0e8273e6641477e1b6d20c41c29b4",
                    ),
                    ExpectedDigest::new(
                        "Aac: 44.1kHz/Mono/V5/Mpeg2 LC/Latm/MuxConfig x64",
                        "6f2eadfe6dd88b189a38b00b9711160fea4b2d8a6acc24ea9008708d2a355735",
                    ),
                ],
            }],
        };

        fasync::Executor::new().unwrap().run_singlethreaded(aac_latm_with_mux_config_test.run())?;

        let aac_latm_without_mux_config_test = AudioEncoderTestCase {
            input_framelength: 1024,
            settings: Rc::new(move || -> EncoderSettings {
                EncoderSettings::Aac(AacEncoderSettings {
                    transport: AacTransport::Latm(AacTransportLatm { mux_config_present: false }),
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
                expected_digests: vec![
                    ExpectedDigest::new(
                        "Aac: 44.1kHz/Mono/V5/Mpeg2 LC/Latm/NoMuxConfig Arm",
                        "09f7e4a6c55873f21772a8ef6d28d96eab287a93290d6d3cd612a11bc2abe6e3",
                    ),
                    ExpectedDigest::new(
                        "Aac: 44.1kHz/Mono/V5/Mpeg2 LC/Latm/NoMuxConfig x64",
                        "a139f287f77c06e3f0a318a8712ea2cabf93c94b7b7106825747f3dd752fc7c0",
                    ),
                ],
            }],
        };

        fasync::Executor::new().unwrap().run_singlethreaded(aac_latm_without_mux_config_test.run())
    })
}
