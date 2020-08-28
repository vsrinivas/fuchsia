// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::format_err;
use async_trait::async_trait;
use fidl_fuchsia_media::*;
use fidl_fuchsia_sysmem as sysmem;
use fuchsia_zircon as zx;
use log::*;
use std::io::Write;
use std::rc::Rc;
use stream_processor_encoder_factory::*;
use stream_processor_test::*;
use video_frame_stream::*;

use crate::h265::*;

/// Validates that the expected number of frames and key frames are received,
/// and that all received NAL's are valid.
pub struct H265NalValidator {
    pub expected_frames: Option<usize>,
    pub expected_key_frames: Option<usize>,
    pub output_file: Option<&'static str>,
}

impl H265NalValidator {
    fn output_file(&self) -> Result<impl Write> {
        Ok(if let Some(file) = self.output_file {
            Box::new(std::fs::File::create(file)?) as Box<dyn Write>
        } else {
            Box::new(std::io::sink()) as Box<dyn Write>
        })
    }
}

#[async_trait(?Send)]
impl OutputValidator for H265NalValidator {
    async fn validate(&self, output: &[Output]) -> Result<()> {
        let packets: Vec<&OutputPacket> = output_packets(output).collect();
        let mut file = self.output_file()?;
        let mut stream = H265Stream::from(vec![]);
        for packet in packets {
            file.write_all(&packet.data)?;
            stream.append(&mut packet.data.clone());
        }

        let nals: Vec<H265NalKind> = stream.nal_iter().map(|n| n.kind).collect();
        info!("nals {:?}", nals);

        let mut seen_frames = 0;
        let mut seen_key_frames = 0;
        for nal in stream.nal_iter() {
            if nal.kind == H265NalKind::IRAP || nal.kind == H265NalKind::NonIRAP {
                seen_frames += 1;
                if nal.kind == H265NalKind::IRAP {
                    seen_key_frames += 1;
                }
            }

            if nal.kind == H265NalKind::Unknown {
                return Err(format_err!("unknown NAL received"));
            }
        }

        if let Some(expected_frames) = self.expected_frames {
            if seen_frames < expected_frames {
                return Err(format_err!(
                    "Wrong number of frames received {} {}",
                    seen_frames,
                    expected_frames
                ));
            }
        }

        if let Some(expected_key_frames) = self.expected_key_frames {
            if seen_key_frames < expected_key_frames {
                return Err(format_err!(
                    "Wrong number of key frames received {} {}",
                    seen_key_frames,
                    expected_key_frames
                ));
            }
        }

        Ok(())
    }
}

pub struct H265EncoderTestCase {
    pub num_frames: usize,
    pub input_format: sysmem::ImageFormat2,
    // This is a function because FIDL unions are not Copy or Clone.
    pub settings: Rc<dyn Fn() -> EncoderSettings>,
    pub expected_key_frames: Option<usize>,
    pub output_file: Option<&'static str>,
}

impl H265EncoderTestCase {
    pub async fn run(self) -> Result<()> {
        let stream = self.create_test_stream()?;
        let mut validators: Vec<Rc<dyn OutputValidator>> = vec![
            Rc::new(H265NalValidator {
                expected_frames: Some(self.num_frames),
                expected_key_frames: self.expected_key_frames,
                output_file: self.output_file,
            }),
            Rc::new(TimestampValidator { generator: stream.timestamp_generator() }),
        ];

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
            EncoderSettings::Hevc(HevcEncoderSettings { frame_rate: Some(frame_rate), .. }) => {
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
            /*mime_type=*/ "video/h265",
        )?))
    }
}
