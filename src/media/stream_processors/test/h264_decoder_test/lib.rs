// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This test is run with --test-threasds=1 to prevent any tests from running in parallel.
//
// Running in parallel is something we want to control for specific tests cases especially
// when testing hardware stream processors.

#![cfg(test)]

use anyhow;
use h264_stream::*;
use lazy_static::lazy_static;
use std::{fs::File, io::Read, rc::Rc, result::Result};
use stream_processor_decoder_factory::*;
use stream_processor_test::*;
use video_frame_hasher::*;

pub const BEAR_TEST_FILE: &str = "/pkg/data/bear.h264";

lazy_static! {
    static ref LOGGER: () = ::fuchsia_syslog::init().expect("Initializing syslog");
    static ref BEAR_DIGEST: ExpectedDigest = ExpectedDigest::new_with_per_frame_digest(
        "bear.h264 decoded digest",
        "1dc4d1510fc4d26173480f5e689e38dca7c1fa2df1894085f1bcee9c0d19acf7",
        vec!(
            "0f1d46e5b13b0bb96b42d0c3ead24f656e3e000b3f287714be85572b11fa747f",
            "a1c64f7db1ffcc90f493494fd66a49fd90581e7b4b9cf01e44cc3d1b48f1b2ba",
            "0bce4a93ed09cda3967d193a46d3dac770852849059b63e58c59ca37ec02dae0",
            "a3dd10db3320fc8679c522e4638c56623cb4cc04763147ab263a014158a4de83",
            "32ee2c4e9d7efe149871a5bad5fc3a965353976b7b09f6ca402cd20b768cf512",
            "f65970e9ac1fe36cc8d4652ca49be6bc936a39f44102b24353280f3de1567937",
            "4f2921b3304608f4fdbbddbb20f1415321deb1a5ba06df25e0860025340bcb52",
            "9e8becc472fbec2ac3c1f7beb17737bdd46b824f0198fe14f5bfc15cc55f86a1",
            "97d49f5ca990970317890c15106deb55fc8fbe98602621953293a8384983ff73",
            "4bc568295e668a245242961c98c78ac4a2068973c941b99fc9ba8e24f05badcf",
            "52dc2c4a765617b2d2e8675bf1da3cb7275a44a1efa792833e049aedd330ebdf",
            "6b4136c0f952aceec0ccfd27ac3e7ae8f2de7164145520593ae634c40bce2874",
            "2894c4c09598098cba30be9721063e7d5354fccf41a301788d0dda25e1e9df3a",
            "e2ba87ad32ea7429093c594e74465bc3b33f5a22c1d08f9c6534ff027e7d7124",
            "b3ea3627c5f7a61ca327bf79377d296273fddb98d0cd18a64cef699640a1b28a",
            "c60813c1be8abe568a20e802ddf6997dd77f673ddc9847d5655ee107f8b16934",
            "b5d01e7f4012ff375231a2dc66c35109f852adbd90e63704653a6c9bd4f012a7",
            "aed3b808d3cbe0b37209ebe8c84c6e7c95b7b690b5bf4ce5484b182c9e56d4e0",
            "de7483a21de0d78b6c9cf4ae31f487dd7da22296eb1dbecd201fe07b5d5ed0e6",
            "219a6d4029d0f75501dd8b4f8a16c31240d1491d623d2c26bca096615820de48",
            "8efa6fbe7b3479e43537e7226b3e079e778bb1442de52c69122b55df6e4c5e8c",
            "2930e0b080180312a6add0478161f93009b683bdafcfafc20e6205ebc8b9d4b5",
            "52193a2d808e2b7c7b82bf2833d5a6c1da8e6344ec1c7759f59db1cad8d02dd0",
            "2edf5bcb1825e50d8e5871c68b322e47196b5087b165b3d11af502084bd3363a",
            "542e0d748ee037034ab360822f65df2edc5ef1d10a37a3f6ac9f35b09eb49593",
            "6ffacaad59095ba5c2e8e9d727386fdb55d618ffc2a3fbe039d8dd9189cd9d2c",
            "8c537610171663df07ab7a87de3598cd63cb18eb2852f1184088aeb9ef0a70d6",
            "00ddc6d257a5223a30c0d652d37cc7a37c3e65de922f10a6e8a8a5cda61c2365",
            "8c25f9fbe01f491c343d9ab7c0bd3ffb6fc7f0fed546fbf7f49ad1f661f53d50",
            "1dc4d1510fc4d26173480f5e689e38dca7c1fa2df1894085f1bcee9c0d19acf7",
        )
    );
}

// TODO(turnage): Add test spec for buffers released between streams.
// TODO(turnage): Add hash validator for NV12 and YV12.

#[test]
fn test_bear() -> std::result::Result<(), ::anyhow::Error> {
    with_large_stack(|| {
        *LOGGER;

        let stream = Rc::new(TimestampedStream {
            source: H264Stream::from_file(BEAR_TEST_FILE)?,
            timestamps: 0..,
        });

        let frame_count_validator = Rc::new(OutputPacketCountValidator {
            expected_output_packet_count: stream.video_frame_count(),
        });

        let hash_validator = Rc::new(VideoFrameHasher { expected_digest: BEAR_DIGEST.clone() });

        let spec = TestSpec {
            cases: vec![TestCase {
                name: "Simple bear test run 1",
                stream: stream.clone(),
                validators: vec![
                    Rc::new(TerminatesWithValidator {
                        expected_terminal_output: Output::Eos { stream_lifetime_ordinal: 1 },
                    }),
                    frame_count_validator.clone(),
                    hash_validator.clone(),
                ],
                stream_options: None,
            }],
            relation: CaseRelation::Serial,
            stream_processor_factory: Rc::new(DecoderFactory),
        };

        fuchsia_async::TestExecutor::new()?.run_singlethreaded(spec.run())
    })
}

#[test]
fn test_serial_bear_on_same_codec() -> std::result::Result<(), ::anyhow::Error> {
    with_large_stack(|| {
        *LOGGER;

        let stream = Rc::new(TimestampedStream {
            source: H264Stream::from_file(BEAR_TEST_FILE)?,
            timestamps: 0..,
        });

        let frame_count_validator = Rc::new(OutputPacketCountValidator {
            expected_output_packet_count: stream.video_frame_count(),
        });

        let hash_validator = Rc::new(VideoFrameHasher { expected_digest: BEAR_DIGEST.clone() });

        let spec = TestSpec {
            cases: vec![
                TestCase {
                    name: "Simple bear test run 1 on same channel",
                    stream: stream.clone(),
                    validators: vec![
                        Rc::new(TerminatesWithValidator {
                            expected_terminal_output: Output::Eos { stream_lifetime_ordinal: 1 },
                        }),
                        frame_count_validator.clone(),
                        hash_validator.clone(),
                    ],
                    stream_options: None,
                },
                TestCase {
                    name: "Simple bear test run 2 on same channel",
                    stream: stream.clone(),
                    validators: vec![
                        Rc::new(TerminatesWithValidator {
                            expected_terminal_output: Output::Eos { stream_lifetime_ordinal: 3 },
                        }),
                        frame_count_validator.clone(),
                        hash_validator.clone(),
                    ],
                    stream_options: Some(StreamOptions {
                        queue_format_details: false,
                        ..StreamOptions::default()
                    }),
                },
            ],
            relation: CaseRelation::Serial,
            stream_processor_factory: Rc::new(DecoderFactory),
        };

        fuchsia_async::TestExecutor::new()?.run_singlethreaded(spec.run())
    })
}

#[test]
fn bear_with_sei_itu_t35() -> Result<(), anyhow::Error> {
    with_large_stack(|| {
        *LOGGER;

        let mut nal_stream = H264SeiItuT35 {
            country_code: H264SeiItuT35::COUNTRY_CODE_UNITED_STATES,
            country_code_extension: 0,
            payload: vec![0xde, 0xad, 0xbe, 0xef],
        }
        .as_bytes()?;
        File::open(BEAR_TEST_FILE)?.read_to_end(&mut nal_stream)?;

        let stream =
            Rc::new(TimestampedStream { source: H264Stream::from(nal_stream), timestamps: 0.. });

        let frame_count_validator = Rc::new(OutputPacketCountValidator {
            expected_output_packet_count: stream.video_frame_count(),
        });

        let hash_validator = Rc::new(VideoFrameHasher { expected_digest: BEAR_DIGEST.clone() });

        let spec = TestSpec {
            cases: vec![TestCase {
                name: "Modified Bear with SEI ITU-T T.35 data test run",
                stream: stream,
                validators: vec![
                    Rc::new(TerminatesWithValidator {
                        expected_terminal_output: Output::Eos { stream_lifetime_ordinal: 1 },
                    }),
                    frame_count_validator,
                    hash_validator,
                ],
                stream_options: None,
            }],
            relation: CaseRelation::Serial,
            stream_processor_factory: Rc::new(DecoderFactory),
        };

        fuchsia_async::TestExecutor::new()?.run_singlethreaded(spec.run())
    })
}

#[test]
fn bear_with_large_sei_itu_t35() -> Result<(), anyhow::Error> {
    with_large_stack(|| {
        *LOGGER;

        let mut nal_stream = H264SeiItuT35 {
            country_code: H264SeiItuT35::COUNTRY_CODE_UNITED_STATES,
            country_code_extension: 0,
            payload: vec![0xde, 0xad, 0xbe, 0xef],
        }
        .as_bytes()?;

        // Appending 0s to an annex-B NAL shouldn't change the behavior.
        nal_stream.resize(428, 0);
        File::open(BEAR_TEST_FILE)?.read_to_end(&mut nal_stream)?;

        let stream =
            Rc::new(TimestampedStream { source: H264Stream::from(nal_stream), timestamps: 0.. });

        let frame_count_validator = Rc::new(OutputPacketCountValidator {
            expected_output_packet_count: stream.video_frame_count(),
        });

        let hash_validator = Rc::new(VideoFrameHasher { expected_digest: BEAR_DIGEST.clone() });

        let spec = TestSpec {
            cases: vec![TestCase {
                name: "Modified Bear with Large SEI ITU-T T.35 data test run",
                stream: stream,
                validators: vec![
                    Rc::new(TerminatesWithValidator {
                        expected_terminal_output: Output::Eos { stream_lifetime_ordinal: 1 },
                    }),
                    frame_count_validator,
                    hash_validator,
                ],
                stream_options: None,
            }],
            relation: CaseRelation::Serial,
            stream_processor_factory: Rc::new(DecoderFactory),
        };

        fuchsia_async::TestExecutor::new()?.run_singlethreaded(spec.run())
    })
}

#[test]
fn bear_with_gaps() -> Result<(), anyhow::Error> {
    with_large_stack(|| {
        *LOGGER;

        let mut nal_stream = Vec::new();
        let mut bear = Vec::new();
        File::open(BEAR_TEST_FILE)?.read_to_end(&mut bear)?;

        // Append bear up till somewhere in middle, but then drop a NonIDR frame. Index of NonIDR in bear found by adding logging to H264NalIter.
        nal_stream.extend_from_slice(&bear[0..7635]);
        nal_stream.extend(&bear[8858..]);

        let stream =
            Rc::new(TimestampedStream { source: H264Stream::from(nal_stream), timestamps: 0.. });

        let frame_count_validator =
            Rc::new(OutputPacketCountValidator { expected_output_packet_count: 29 });

        let spec = TestSpec {
            cases: vec![TestCase {
                name: "Bear with gaps",
                stream: stream,
                validators: vec![
                    Rc::new(TerminatesWithValidator {
                        expected_terminal_output: Output::Eos { stream_lifetime_ordinal: 1 },
                    }),
                    frame_count_validator,
                ],
                stream_options: None,
            }],
            relation: CaseRelation::Serial,
            stream_processor_factory: Rc::new(DecoderFactory),
        };

        fuchsia_async::TestExecutor::new()?.run_singlethreaded(spec.run())
    })
}

#[test]
fn test_bear_avcc() -> std::result::Result<(), ::anyhow::Error> {
    with_large_stack(|| {
        *LOGGER;

        let stream = Rc::new(TimestampedStream {
            source: H264AVCCStream::from_annexb_stream(H264Stream::from_file(BEAR_TEST_FILE)?)?,
            timestamps: 0..,
        });

        let frame_count_validator = Rc::new(OutputPacketCountValidator {
            expected_output_packet_count: stream.video_frame_count(),
        });

        let hash_validator = Rc::new(VideoFrameHasher { expected_digest: BEAR_DIGEST.clone() });

        let spec = TestSpec {
            cases: vec![TestCase {
                name: "Simple bear test run with AVCC",
                stream: stream.clone(),
                validators: vec![
                    Rc::new(TerminatesWithValidator {
                        expected_terminal_output: Output::Eos { stream_lifetime_ordinal: 1 },
                    }),
                    frame_count_validator.clone(),
                    hash_validator.clone(),
                ],
                stream_options: None,
            }],
            relation: CaseRelation::Serial,
            stream_processor_factory: Rc::new(DecoderFactory),
        };

        fuchsia_async::TestExecutor::new()?.run_singlethreaded(spec.run())
    })
}
