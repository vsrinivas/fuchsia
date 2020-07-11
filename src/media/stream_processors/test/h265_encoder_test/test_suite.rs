// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::format_err;
use async_trait::async_trait;
use fidl_fuchsia_media::*;
use fidl_fuchsia_sysmem as sysmem;
use fuchsia_zircon as zx;
use std::io::Write;
use std::rc::Rc;
use stream_processor_encoder_factory::*;
use stream_processor_test::*;
use video_frame_stream::*;

use crate::h265::*;

pub struct H265NalValidator {
    pub expected_nals: Option<Vec<H265NalKind>>,
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

        if let None = self.expected_nals {
            return Ok(());
        }
        let expected = self.expected_nals.as_ref().unwrap();

        let mut current = 0;
        for nal in stream.nal_iter() {
            if current >= expected.len() {
                // ok to get more, we only validate the expected list
                break;
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

pub struct H265EncoderTestCase {
    pub num_frames: usize,
    pub input_format: sysmem::ImageFormat2,
    // This is a function because FIDL unions are not Copy or Clone.
    pub settings: Rc<dyn Fn() -> EncoderSettings>,
    pub expected_nals: Option<Vec<H265NalKind>>,
    pub output_file: Option<&'static str>,
}

impl H265EncoderTestCase {
    pub async fn run(self) -> Result<()> {
        let stream = self.create_test_stream()?;
        let mut validators: Vec<Rc<dyn OutputValidator>> = vec![Rc::new(H265NalValidator {
            expected_nals: self.expected_nals.clone(),
            output_file: self.output_file,
        })];

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

    fn create_test_stream(&self) -> Result<Rc<VideoFrameStream>> {
        Ok(Rc::new(VideoFrameStream::create(
            self.input_format,
            self.num_frames,
            self.settings.clone(),
            /*frames_per_second=*/ 30,
            /*timebase=*/ Some(zx::Duration::from_seconds(1).into_nanos() as u64),
            /*mime_type=*/ "video/h265",
        )?))
    }
}
