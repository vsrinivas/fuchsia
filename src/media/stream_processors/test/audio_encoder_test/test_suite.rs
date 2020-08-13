// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::pcm_audio::*;
use crate::timestamp_validator::*;
use fidl_fuchsia_media::*;
use fidl_fuchsia_sysmem::*;
use fuchsia_zircon as zx;
use rand::prelude::*;
use std::rc::Rc;
use stream_processor_encoder_factory::*;
use stream_processor_test::*;

pub const TEST_PCM_FRAME_COUNT: usize = 3000;

pub struct AudioEncoderTestCase {
    /// Encoder settings.
    // This is a function because FIDL unions are not Copy or Clone.
    pub settings: Rc<dyn Fn() -> EncoderSettings>,
    /// The number of PCM input frames per encoded frame.
    pub input_framelength: usize,
    pub channel_count: usize,
    pub hash_tests: Vec<AudioEncoderHashTest>,
}

/// A hash test runs audio through the encoder and checks that all that data emitted when hashed
/// sequentially results in the expected digest. Oob bytes are hashed first.
pub struct AudioEncoderHashTest {
    /// If provided, the output will also be written to this file. Use this to verify new files
    /// with a decoder before using their digest in tests.
    pub output_file: Option<&'static str>,
    pub input_format: PcmFormat,
    pub output_packet_count: usize,
    pub expected_digests: Vec<ExpectedDigest>,
}

impl AudioEncoderTestCase {
    pub async fn run(self) -> Result<()> {
        self.test_termination().await?;
        self.test_timestamps().await?;
        self.test_hashes().await
    }

    async fn test_hashes(self) -> Result<()> {
        let mut cases = vec![];
        let easy_framelength = self.input_framelength;
        for (hash_test, stream_lifetime_ordinal) in
            self.hash_tests.into_iter().zip(OrdinalPattern::Odd.into_iter())
        {
            let settings = self.settings.clone();
            let pcm_audio = PcmAudio::create_saw_wave(hash_test.input_format, TEST_PCM_FRAME_COUNT);
            let stream = Rc::new(PcmAudioStream {
                pcm_audio,
                encoder_settings: move || (settings)(),
                frames_per_packet: (0..).map(move |_| easy_framelength),
                timebase: None,
            });

            cases.push(TestCase {
                name: "Audio encoder hash test",
                stream,
                validators: vec![
                    Rc::new(TerminatesWithValidator {
                        expected_terminal_output: Output::Eos { stream_lifetime_ordinal },
                    }),
                    Rc::new(OutputPacketCountValidator {
                        expected_output_packet_count: hash_test.output_packet_count,
                    }),
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
            stream_processor_factory: Rc::new(EncoderFactory),
        };

        spec.run().await
    }

    async fn test_termination(&self) -> Result<()> {
        let easy_framelength = self.input_framelength;
        let stream = self.create_test_stream((0..).map(move |_| easy_framelength));
        let eos_validator = Rc::new(TerminatesWithValidator {
            expected_terminal_output: Output::Eos { stream_lifetime_ordinal: 1 },
        });

        let case = TestCase {
            name: "Terminates with EOS test",
            stream,
            validators: vec![eos_validator],
            stream_options: None,
        };

        let spec = TestSpec {
            cases: vec![case],
            relation: CaseRelation::Concurrent,
            stream_processor_factory: Rc::new(EncoderFactory),
        };

        spec.run().await
    }

    async fn test_timestamps(&self) -> Result<()> {
        let max_framelength = self.input_framelength * 5;

        let fixed_framelength = self.input_framelength + 1;
        let fixed_framelength_stream =
            self.create_test_stream((0..).map(move |_| fixed_framelength));
        let pcm_frame_size = fixed_framelength_stream.pcm_audio.frame_size();

        let stream_options = Some(StreamOptions {
            input_buffer_collection_constraints: Some(BufferCollectionConstraints {
                has_buffer_memory_constraints: true,
                buffer_memory_constraints: BufferMemoryConstraints {
                    min_size_bytes: (max_framelength * pcm_frame_size) as u32,
                    ..BUFFER_MEMORY_CONSTRAINTS_DEFAULT
                },
                ..BUFFER_COLLECTION_CONSTRAINTS_DEFAULT
            }),
            ..StreamOptions::default()
        });

        let fixed_framelength_case = TestCase {
            name: "Timestamp extrapolation test - fixed framelength",
            validators: vec![Rc::new(TimestampValidator::new(
                self.input_framelength,
                pcm_frame_size,
                fixed_framelength_stream.timestamp_generator(),
                fixed_framelength_stream.as_ref(),
            ))],
            stream: fixed_framelength_stream,
            stream_options,
        };

        let variable_framelength_stream = self.create_test_stream((0..).map(move |i| {
            let mut rng = StdRng::seed_from_u64(i as u64);
            rng.gen::<usize>() % max_framelength + 1
        }));
        let variable_framelength_case = TestCase {
            name: "Timestamp extrapolation test - variable framelength",
            validators: vec![Rc::new(TimestampValidator::new(
                self.input_framelength,
                pcm_frame_size,
                variable_framelength_stream.timestamp_generator(),
                variable_framelength_stream.as_ref(),
            ))],
            stream: variable_framelength_stream,
            stream_options,
        };

        let spec = TestSpec {
            cases: vec![fixed_framelength_case, variable_framelength_case],
            relation: CaseRelation::Concurrent,
            stream_processor_factory: Rc::new(EncoderFactory),
        };

        spec.run().await
    }

    fn create_test_stream(
        &self,
        frames_per_packet: impl Iterator<Item = usize> + Clone,
    ) -> Rc<PcmAudioStream<impl Iterator<Item = usize> + Clone, impl Fn() -> EncoderSettings>> {
        let pcm_format = PcmFormat {
            pcm_mode: AudioPcmMode::Linear,
            bits_per_sample: 16,
            frames_per_second: 44100,
            channel_map: match self.channel_count {
                1 => vec![AudioChannelId::Cf],
                2 => vec![AudioChannelId::Lf, AudioChannelId::Rf],
                c => panic!("{} is not a valid channel count", c),
            },
        };
        let pcm_audio = PcmAudio::create_saw_wave(pcm_format.clone(), TEST_PCM_FRAME_COUNT);
        let settings = self.settings.clone();
        Rc::new(PcmAudioStream {
            pcm_audio,
            encoder_settings: move || (settings)(),
            frames_per_packet: frames_per_packet,
            timebase: Some(zx::Duration::from_seconds(1).into_nanos() as u64),
        })
    }
}
