// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_media::*;
use fidl_fuchsia_sysmem as sysmem;
use fuchsia_zircon as zx;
use std::rc::Rc;
use stream_processor_encoder_factory::*;
use stream_processor_test::*;

use crate::video_frame::*;

pub struct H264EncoderTestCase {
    pub num_frames: usize,
    pub input_format: sysmem::ImageFormat2,
    // This is a function because FIDL unions are not Copy or Clone.
    pub settings: Rc<dyn Fn() -> EncoderSettings>,
}

impl H264EncoderTestCase {
    pub async fn run(self) -> Result<()> {
        self.test_termination().await
    }

    async fn test_termination(&self) -> Result<()> {
        let stream = self.create_test_stream()?;
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
