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
    static ref BEAR_DIGEST: ExpectedDigest = ExpectedDigest::new(
        "bear.h264 decoded digest",
        "1dc4d1510fc4d26173480f5e689e38dca7c1fa2df1894085f1bcee9c0d19acf7",
    );
}

// TODO(turnage): Add test spec for buffers released between streams.
// TODO(turnage): Add hash validator for NV12 and YV12.

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

        let hash_validator = Rc::new(VideoFrameHasher { expected_digest: *BEAR_DIGEST });

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
                    stream,
                    validators: vec![
                        Rc::new(TerminatesWithValidator {
                            expected_terminal_output: Output::Eos { stream_lifetime_ordinal: 3 },
                        }),
                        frame_count_validator,
                        hash_validator,
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

        fuchsia_async::Executor::new()?.run_singlethreaded(spec.run())
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

        let hash_validator = Rc::new(VideoFrameHasher { expected_digest: *BEAR_DIGEST });

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

        fuchsia_async::Executor::new()?.run_singlethreaded(spec.run())
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

        let hash_validator = Rc::new(VideoFrameHasher { expected_digest: *BEAR_DIGEST });

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

        fuchsia_async::Executor::new()?.run_singlethreaded(spec.run())
    })
}
