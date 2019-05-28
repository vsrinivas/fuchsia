// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]
#![cfg(test)]

mod h264;

use crate::h264::*;
use lazy_static::lazy_static;
use parking_lot::Mutex;
use std::{rc::Rc, sync::Arc};
use stream_processor_decoder_factory::*;
use stream_processor_test::*;

lazy_static! {
    // We use a test lock to prevent any tests from running in parallel.
    //
    // Running in parallel is something we want to control for specific tests cases especially
    // when testing hardware stream processors.
    //
    // This can be removed in the future if we get a static way to specify environment variables
    // for a component, so we can set `RUST_TEST_THREADS=1` for this component.
    static ref TEST_LOCK: Arc<Mutex<()>> = Arc::new(Mutex::new(()));
    static ref LOGGER: () = ::fuchsia_syslog::init().expect("Initializing syslog");
}

// TODO(turnage): Add test spec for buffers released between streams.
// TODO(turnage): Add hash validator for NV12 and YV12.

#[::fuchsia_async::run_singlethreaded]
#[test]
async fn serial_bear_on_same_codec() -> std::result::Result<(), ::failure::Error> {
    let _lock = TEST_LOCK.lock();
    *LOGGER;

    let stream = Rc::new(TimestampedStream {
        source: H264Stream::from_file(BEAR_TEST_FILE)?,
        timestamps: 0..,
    });

    let frame_count_validator = Rc::new(OutputPacketCountValidator {
        expected_output_packet_count: stream.video_frame_count(),
    });

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

    await!(spec.run())
}
