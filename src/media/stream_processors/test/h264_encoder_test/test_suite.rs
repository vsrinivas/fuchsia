// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::format_err;
use fidl_fuchsia_media::*;
use fidl_fuchsia_sysmem as sysmem;
use fuchsia_zircon as zx;
use std::rc::Rc;
use stream_processor_encoder_factory::*;
use stream_processor_test::*;

use crate::h264::*;
use crate::video_frame::*;

pub struct H264NalValidator {
    pub expected_nals: Option<Vec<H264NalKind>>,
}

impl OutputValidator for H264NalValidator {
    fn validate(&self, output: &[Output]) -> Result<()> {
        if let None = self.expected_nals {
            return Ok(());
        }

        let expected = self.expected_nals.as_ref().unwrap();

        let packets: Vec<&OutputPacket> = output_packets(output).collect();
        let mut stream = H264Stream::from(vec![]);
        for packet in packets {
            stream.append(&mut packet.data.clone());
        }

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

pub struct H264EncoderTestCase {
    pub num_frames: usize,
    pub input_format: sysmem::ImageFormat2,
    // This is a function because FIDL unions are not Copy or Clone.
    pub settings: Rc<dyn Fn() -> EncoderSettings>,
    pub expected_nals: Option<Vec<H264NalKind>>,
}

impl H264EncoderTestCase {
    pub async fn run(self) -> Result<()> {
        let stream = self.create_test_stream()?;
        let nal_validator = Rc::new(H264NalValidator { expected_nals: self.expected_nals.clone() });
        let eos_validator = Rc::new(TerminatesWithValidator {
            expected_terminal_output: Output::Eos { stream_lifetime_ordinal: 1 },
        });

        let case = TestCase {
            name: "Terminates with EOS test",
            stream,
            validators: vec![nal_validator, eos_validator],
            stream_options: None,
        };

        let spec = TestSpec {
            cases: vec![case],
            relation: CaseRelation::Concurrent,
            stream_processor_factory: Rc::new(EncoderFactory),
        };

        spec.run().await
    }

    fn create_test_stream(&self) -> Result<Rc<VideoFrameStream>> {
        Ok(Rc::new(VideoFrameStream::create(
            self.input_format,
            self.num_frames,
            self.settings.clone(),
            /*frames_per_second=*/ 60,
            /*timebase=*/ Some(zx::Duration::from_seconds(1).into_nanos() as u64),
        )?))
    }
}
