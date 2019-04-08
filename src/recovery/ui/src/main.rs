// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use carnelian::{
    measure_text, Canvas, Color, FontDescription, FontFace, Paint, PixelSink, Point, Rect, Size,
};
use failure::{bail, Error, ResultExt};
use fuchsia_async as fasync;
use fuchsia_framebuffer::{Config, Frame, FrameBuffer, PixelFormat};
use futures::future;

static FONT_DATA: &'static [u8] =
    include_bytes!("../../../../garnet/bin/fonts/third_party/robotoslab/RobotoSlab-Regular.ttf");

struct RecoveryUI<'a> {
    face: FontFace<'a>,
    canvas: Canvas<FramePixelSink>,
    config: Config,
    text_size: u32,
}

impl<'a> RecoveryUI<'a> {
    fn draw(&mut self, url: &str, user_code: &str) {
        let r = Rect::new(
            Point::new(0.0, 0.0),
            Size::new(self.config.width as f32, self.config.height as f32),
        );

        let bg = Color { r: 255, g: 0, b: 255, a: 255 };
        self.canvas.fill_rect(&r, bg);

        let mut font_description =
            FontDescription { baseline: 0, face: &mut self.face, size: self.text_size };
        let size = measure_text(url, &mut font_description);
        let paint = Paint { fg: Color { r: 255, g: 255, b: 255, a: 255 }, bg: bg };
        self.canvas.fill_text(
            url,
            Point::new(
                (self.config.width / 2) as f32 - (size.width / 2.0),
                (self.config.height / 4) as f32 - (size.height / 2.0),
            ),
            &mut font_description,
            &paint,
        );

        let size = measure_text(user_code, &mut font_description);

        self.canvas.fill_text(
            user_code,
            Point::new(
                (self.config.width / 2) as f32 - (size.width / 2.0),
                (self.config.height / 2) as f32 + (self.config.height / 4) as f32
                    - (size.height / 2.0),
            ),
            &mut font_description,
            &paint,
        );
    }
}

const BYTES_PER_PIXEL: u32 = 4;

struct FramePixelSink {
    frame: Frame,
}

impl PixelSink for FramePixelSink {
    fn write_pixel_at_location(&mut self, x: u32, y: u32, value: &[u8]) {
        let stride = self.frame.linear_stride_bytes() as u32;
        let offset = (y * stride + x * BYTES_PER_PIXEL) as usize;
        self.frame.write_pixel_at_offset(offset, &value);
    }

    fn write_pixel_at_offset(&mut self, offset: usize, value: &[u8]) {
        self.frame.write_pixel_at_offset(offset, &value);
    }
}

fn main() -> Result<(), Error> {
    println!("Recovery UI");

    let face = FontFace::new(FONT_DATA).unwrap();

    let mut executor = fasync::Executor::new().context("Failed to create executor")?;

    let fb = FrameBuffer::new(None, &mut executor).context("Failed to create framebuffer")?;
    let config = fb.get_config();
    if config.format != PixelFormat::Argb8888 {
        bail!("Unsupported pixel format {:#?}", config.format);
    }

    let frame = fb.new_frame(&mut executor)?;
    frame.present(&fb)?;

    let stride = frame.linear_stride_bytes() as u32;
    let sink = FramePixelSink { frame };
    let canvas = Canvas::new_with_sink(sink, stride);

    let mut ui = RecoveryUI { face: face, canvas, config, text_size: config.height / 12 };

    ui.draw("Verification URL", "User Code");

    executor.run_singlethreaded(future::empty::<()>());
    unreachable!();
}
