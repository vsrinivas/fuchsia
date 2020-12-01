// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod h265;
mod test_suite;

use crate::test_suite::*;
use fidl_fuchsia_media::*;
use fidl_fuchsia_sysmem as sysmem;
use fuchsia_async as fasync;
use lazy_static::lazy_static;
use std::rc::Rc;
use stream_processor_test::*;

lazy_static! {
    static ref LOGGER: () = ::fuchsia_syslog::init().expect("Initializing syslog");
}

// Instructions for capturing output of encoder:
// 1. Set the `output_file` field to write the encoded output into "/tmp/".
// 2. Add exec.run_singlethreaded(future::pending()) at the end of the test so it doesn't exit.
//    This is so the tmp file doesn't get cleaned up.
// 3. File will be written to isolated tmp directory for the test environment. Look under:
//    `/tmp/r/sys/r/test_env_XXXXXX`
// 4. Scp from `<isolated tmp>/fuchsia.com:h265_encoder_test:0#meta:h265_encoder_test.cmx` to host.

#[test]
fn h265_stream_output_generated() -> Result<()> {
    *LOGGER;
    const WIDTH: u32 = 320;
    const HEIGHT: u32 = 240;
    let mut exec = fasync::Executor::new()?;

    let test_case = H265EncoderTestCase {
        input_format: sysmem::ImageFormat2 {
            pixel_format: sysmem::PixelFormat {
                type_: sysmem::PixelFormatType::Nv12,
                has_format_modifier: false,
                format_modifier: sysmem::FormatModifier { value: 0 },
            },
            coded_width: WIDTH,
            coded_height: HEIGHT,
            bytes_per_row: WIDTH,
            display_width: WIDTH,
            display_height: HEIGHT,
            layers: 0,
            color_space: sysmem::ColorSpace { type_: sysmem::ColorSpaceType::Rec601Pal },
            has_pixel_aspect_ratio: false,
            pixel_aspect_ratio_width: 0,
            pixel_aspect_ratio_height: 0,
        },
        num_frames: 6,
        settings: Rc::new(move || -> EncoderSettings {
            EncoderSettings::Hevc(HevcEncoderSettings {
                bit_rate: Some(1000000),
                frame_rate: Some(30),
                gop_size: Some(2),
                ..HevcEncoderSettings::EMPTY
            })
        }),
        expected_key_frames: Some(3),
        output_file: None,
    };
    exec.run_singlethreaded(test_case.run())?;
    Ok(())
}
