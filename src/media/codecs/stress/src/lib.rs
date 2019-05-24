// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]
#![cfg(test)]

#[macro_use]
mod log_macros;
mod buffer_collection_constraints;
mod buffer_set;
mod codecs;
mod elementary_stream;
mod input_packet_stream;
mod output_validator;
mod stream;
mod stream_runner;
mod test_spec;

use crate::codecs::h264::*;
use crate::elementary_stream::*;
use crate::output_validator::*;
use crate::stream::*;
use crate::test_spec::*;
use failure::{Error, Fail};
use fuchsia_async as fasync;
use lazy_static::lazy_static;
use parking_lot::Mutex;
use std::{fmt, rc::Rc, sync::Arc};

type Result<T> = std::result::Result<T, Error>;

lazy_static! {
    // We use a test lock to prevent any tests from running in parallel.
    //
    // Running in parallel is something we want to control for specific tests cases especially
    // when testing hardware stream processors.
    //
    // This can be removed in the future if we get a static way to specify environment variables
    // for a component, so we can set `RUST_TEST_THREADS=1` for this component.
    static ref TEST_LOCK: Arc<Mutex<()>> = Arc::new(Mutex::new(()));
    static ref LOGGER: () = fuchsia_syslog::init().expect("Initializing syslog");
}

#[derive(Debug)]
pub struct FatalError(String);

impl fmt::Display for FatalError {
    fn fmt(&self, w: &mut fmt::Formatter) -> fmt::Result {
        write!(w, "FatalError: {}", self.0)
    }
}

impl Fail for FatalError {}

// TODO(turnage): Add test spec for buffers released between streams.
// TODO(turnage): Add hash validator for NV12 and YV12.

#[fasync::run_singlethreaded]
#[test]
async fn serial_bear_new_codec_for_each() -> Result<()> {
    let _lock = TEST_LOCK.lock();
    *LOGGER;

    eprintln!("reading stream file...");

    let stream = Rc::new(TimestampedStream {
        source: H264Stream::from_file(BEAR_TEST_FILE)?,
        timestamps: 0..,
    });

    eprintln!("got file");

    let frame_count_validator = Rc::new(OutputPacketCountValidator {
        expected_output_packet_count: stream.video_frame_count(),
    });

    let spec1 = TestSpec {
        cases: vec![TestCase {
            name: "Simple bear test run 1 on new channel",
            stream: stream.clone(),
            validators: vec![
                Rc::new(TerminatesWithValidator {
                    expected_terminal_output: Output::Eos { stream_lifetime_ordinal: 1 },
                }),
                frame_count_validator.clone(),
            ],
            stream_options: None,
        }],
        relation: CaseRelation::Serial,
    };

    let spec2 = TestSpec {
        cases: vec![TestCase {
            name: "Simple bear test run 2 on new channel",
            stream,
            validators: vec![
                Rc::new(TerminatesWithValidator {
                    expected_terminal_output: Output::Eos { stream_lifetime_ordinal: 1 },
                }),
                frame_count_validator,
            ],
            stream_options: None,
        }],
        relation: CaseRelation::Serial,
    };

    await!(spec1.run())?;
    await!(spec2.run())?;

    Ok(())
}

#[fasync::run_singlethreaded]
#[test]
async fn serial_bear_no_release_buffers() -> Result<()> {
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
    };

    await!(spec.run())
}
