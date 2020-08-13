// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_media::*;
use std::rc::Rc;
use stream_processor_decoder_factory::*;
use stream_processor_test::*;

pub struct AudioDecoderTestCase {
    pub hash_tests: Vec<AudioDecoderHashTest>,
}

/// A hash test runs audio through the encoder and checks that all that data emitted when hashed
/// sequentially results in the expected digest. Oob bytes are hashed first.
pub struct AudioDecoderHashTest {
    /// If provided, the output will also be written to this file. Use this to verify new files
    /// with a decoder before using their digest in tests.
    pub output_file: Option<&'static str>,
    pub stream: Rc<dyn ElementaryStream>,
    pub output_packet_count: usize,
    pub expected_digests: Vec<ExpectedDigest>,
    pub expected_output_format: FormatDetails,
}

impl AudioDecoderTestCase {
    pub async fn run(self) -> Result<()> {
        self.test_hashes().await
    }

    async fn test_hashes(self) -> Result<()> {
        let mut cases = vec![];
        for (hash_test, stream_lifetime_ordinal) in
            self.hash_tests.into_iter().zip(OrdinalPattern::Odd.into_iter())
        {
            cases.push(TestCase {
                name: "Audio decoder hash test",
                stream: hash_test.stream,
                validators: vec![
                    Rc::new(TerminatesWithValidator {
                        expected_terminal_output: Output::Eos { stream_lifetime_ordinal },
                    }),
                    Rc::new(OutputPacketCountValidator {
                        expected_output_packet_count: hash_test.output_packet_count,
                    }),
                    Rc::new(FormatValidator { expected_format: hash_test.expected_output_format }),
                    Rc::new(BytesValidator {
                        output_file: hash_test.output_file,
                        expected_digests: hash_test.expected_digests,
                    }),
                ],
                stream_options: Some(StreamOptions {
                    queue_format_details: false,
                    ..StreamOptions::default()
                }),
            });
        }

        let spec = TestSpec {
            cases,
            relation: CaseRelation::Serial,
            stream_processor_factory: Rc::new(DecoderFactory),
        };

        spec.run().await
    }
}
