// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::format_err;
use async_trait::async_trait;
use fidl_fuchsia_media::*;
use fidl_fuchsia_sysmem as sysmem;
use fuchsia_zircon as zx;
use h264_stream::*;
use std::io::Write;
use std::rc::Rc;
use stream_processor_decoder_factory::*;
use stream_processor_encoder_factory::*;
use stream_processor_test::*;
use video_frame_stream::*;

pub struct H264NalValidator {
    pub expected_nals: Option<Vec<H264NalKind>>,
    pub output_file: Option<&'static str>,
}

impl H264NalValidator {
    fn output_file(&self) -> Result<impl Write> {
        Ok(if let Some(file) = self.output_file {
            Box::new(std::fs::File::create(file)?) as Box<dyn Write>
        } else {
            Box::new(std::io::sink()) as Box<dyn Write>
        })
    }
}

#[async_trait(?Send)]
impl OutputValidator for H264NalValidator {
    async fn validate(&self, output: &[Output]) -> Result<()> {
        let packets: Vec<&OutputPacket> = output_packets(output).collect();
        let mut file = self.output_file()?;
        let mut stream = H264Stream::from(vec![]);
        for packet in packets {
            file.write_all(&packet.data)?;
            stream.append(&mut packet.data.clone());
        }

        if let None = self.expected_nals {
            return Ok(());
        }
        let expected = self.expected_nals.as_ref().unwrap();

        let mut current = 0;
        for nal in stream.nal_iter() {
            if current >= expected.len() {
                return Err(format_err!("Too many NAL received"));
            }

            if nal.kind != expected[current] {
                return Err(format_err!(
                    "Expected NAL kind {:?} got {:?} at index {}",
                    expected[current],
                    nal.kind,
                    current
                ));
            }
            current += 1;
        }

        if current != expected.len() {
            return Err(format_err!("Too few NAL received"));
        }

        Ok(())
    }
}

pub struct H264DecoderValidator {
    num_frames: usize,
    input_stream: Rc<VideoFrameStream>,
    normalized_sad_threshold: f64,
}

#[async_trait(?Send)]
impl OutputValidator for H264DecoderValidator {
    async fn validate(&self, output: &[Output]) -> Result<()> {
        let decoder_factory = Rc::new(DecoderFactory);
        let packets: Vec<&OutputPacket> = output_packets(output).collect();
        let mut stream = H264Stream::from(Vec::new());
        for p in packets {
            stream.append(&mut p.data.clone());
        }

        let stream = Rc::new(stream);
        let decoder = decoder_factory
            .connect_to_stream_processor(
                stream.as_ref(),
                /* format_details_version_ordinal */ 1,
            )
            .await?;
        let mut stream_runner = StreamRunner::new(decoder);
        let stream_options = None;
        let decoded_output =
            stream_runner.run_stream(stream, stream_options.unwrap_or_default()).await?;

        let decoded_frames: Vec<&OutputPacket> = output_packets(&decoded_output).collect();
        if decoded_frames.len() != self.num_frames {
            return Err(format_err!(
                "Wrong number of frames received {} {}",
                decoded_frames.len(),
                self.num_frames
            ));
        }

        let input_frames = self.input_stream.stream();
        for (frame_number, (input_frame, output_frame)) in
            input_frames.zip(decoded_frames).enumerate()
        {
            let sad = Self::normalized_sad(&input_frame.data, &output_frame.data);
            if sad > self.normalized_sad_threshold {
                return Err(format_err!(
                    "SAD threshold {} exceeded: {}, frame {}",
                    self.normalized_sad_threshold,
                    sad,
                    frame_number
                ));
            }
        }

        Ok(())
    }
}

impl H264DecoderValidator {
    fn normalized_sad(input: &[u8], output: &[u8]) -> f64 {
        let mut sad = 0.0;
        for (i_p, o_p) in input.iter().zip(output.iter()) {
            sad += (*i_p as f64 - *o_p as f64).abs();
        }

        sad / input.len() as f64
    }
}

pub struct H264EncoderTestCase {
    pub num_frames: usize,
    pub input_format: sysmem::ImageFormat2,
    // This is a function because FIDL unions are not Copy or Clone.
    pub settings: Rc<dyn Fn() -> EncoderSettings>,
    pub expected_nals: Option<Vec<H264NalKind>>,
    // If set, computes the Sum of Absolute Differences of each input to output frame and fails
    // validation of the normalized value is greater than this threshold.
    pub normalized_sad_threshold: Option<f64>,
    pub decode_output: bool,
    pub output_file: Option<&'static str>,
}

impl H264EncoderTestCase {
    pub async fn run(self) -> Result<()> {
        // This threshold is not meant to be hit.
        const MAX_NORMALIZED_SAD: f64 = 0xffff as f64;

        let stream = self.create_test_stream()?;
        let mut validators: Vec<Rc<dyn OutputValidator>> = vec![
            Rc::new(H264NalValidator {
                expected_nals: self.expected_nals.clone(),
                output_file: self.output_file,
            }),
            Rc::new(TimestampValidator { generator: stream.timestamp_generator() }),
        ];

        if self.decode_output {
            validators.push(Rc::new(H264DecoderValidator {
                num_frames: self.num_frames,
                input_stream: stream.clone(),
                normalized_sad_threshold: self
                    .normalized_sad_threshold
                    .unwrap_or(MAX_NORMALIZED_SAD),
            }));
        }

        validators.push(Rc::new(TerminatesWithValidator {
            expected_terminal_output: Output::Eos { stream_lifetime_ordinal: 1 },
        }));

        let case =
            TestCase { name: "Terminates with EOS test", stream, validators, stream_options: None };

        let spec = TestSpec {
            cases: vec![case],
            relation: CaseRelation::Serial,
            stream_processor_factory: Rc::new(EncoderFactory),
        };

        spec.run().await
    }

    fn get_frame_rate(&self) -> usize {
        const DEFAULT_FRAMERATE: usize = 30;
        match (self.settings)() {
            EncoderSettings::H264(H264EncoderSettings { frame_rate: Some(frame_rate), .. }) => {
                frame_rate as usize
            }
            _ => DEFAULT_FRAMERATE,
        }
    }

    fn get_timebase(&self) -> u64 {
        zx::Duration::from_seconds(1).into_nanos() as u64
    }

    fn create_test_stream(&self) -> Result<Rc<VideoFrameStream>> {
        Ok(Rc::new(VideoFrameStream::create(
            self.input_format,
            self.num_frames,
            self.settings.clone(),
            self.get_frame_rate(),
            Some(self.get_timebase()),
            /*mime_type=*/ "video/h264",
        )?))
    }
}
