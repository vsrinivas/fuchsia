// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod h264;
mod test_suite;
mod video_frame;

use crate::h264::*;
use crate::test_suite::*;
use fidl_fuchsia_media::*;
use fidl_fuchsia_sysmem as sysmem;
use fuchsia_async as fasync;
use std::rc::Rc;
use stream_processor_test::*;

// Instructions for capturing output of encoder:
// 1. Set the `output_file` field to write the encoded output into "/tmp/".
// 2. Add exec.run_singlethreaded(future::pending()) at the end of the test so it doesn't exit.
//    This is so the tmp file doesn't get cleaned up.
// 3. File will be written to isolated tmp directory for the test environment. Look under:
//    `/tmp/r/sys/r/test_env_XXXXXX`
// 4. Scp from `<isolated tmp>/fuchsia.com:h264_encoder_test:0#meta:h264_encoder_test.cmx` to host.

#[test]
fn h264_stream_output_generated() -> Result<()> {
    let test_case = H264EncoderTestCase {
        input_format: sysmem::ImageFormat2 {
            pixel_format: sysmem::PixelFormat {
                type_: sysmem::PixelFormatType::Nv12,
                has_format_modifier: false,
                format_modifier: sysmem::FormatModifier { value: 0 },
            },
            coded_width: 320,
            coded_height: 240,
            bytes_per_row: 320,
            display_width: 320,
            display_height: 240,
            layers: 0,
            color_space: sysmem::ColorSpace { type_: sysmem::ColorSpaceType::Rec709 },
            has_pixel_aspect_ratio: false,
            pixel_aspect_ratio_width: 0,
            pixel_aspect_ratio_height: 0,
        },
        num_frames: 1,
        settings: Rc::new(move || -> EncoderSettings {
            EncoderSettings::H264(H264EncoderSettings {})
        }),
        expected_nals: Some(vec![
            H264NalKind::NotPicture,
            H264NalKind::NotPicture,
            H264NalKind::Picture,
        ]),
        output_file: None,
    };

    let mut exec = fasync::Executor::new()?;
    exec.run_singlethreaded(test_case.run())
}
