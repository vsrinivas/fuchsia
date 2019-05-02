// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]
#![deny(warnings)]

use carnelian::{Canvas, Color, Coord, IntSize, PixelSink, Point, Rect, Size};
use failure::{bail, Error, ResultExt};
use fuchsia_async as fasync;
use fuchsia_framebuffer::{Frame, FrameBuffer, PixelFormat};
use futures::{channel::oneshot::Canceled, prelude::*};
use std::io::{self, Read};
use std::thread;

/// Convenience function that can be called from main and causes the Fuchsia process being
/// run over ssh to be terminated when the user hits control-C.
pub fn wait_for_close() -> impl Future<Output = ()> {
    let (sender, receiver) = futures::channel::oneshot::channel();
    thread::spawn(move || loop {
        let mut input = [0; 1];
        match io::stdin().read_exact(&mut input) {
            Ok(()) => {}
            Err(_) => {
                let _ = sender.send(());
                break;
            }
        }
    });
    receiver.map(|_: Result<(), Canceled>| ())
}

struct FramePixelSink {
    frame: Frame,
}

impl PixelSink for FramePixelSink {
    fn write_pixel_at_offset(&mut self, offset: usize, value: &[u8]) {
        self.frame.write_pixel_at_offset(offset, &value);
    }
}

fn main() -> Result<(), Error> {
    println!("canvas: started");
    let close = wait_for_close();
    let mut executor = fasync::Executor::new().context("Failed to create executor")?;
    let fb = FrameBuffer::new(None, &mut executor).context("Failed to create framebuffer")?;
    let config = fb.get_config();
    if config.format != PixelFormat::Argb8888 && config.format != PixelFormat::Rgb565 {
        bail!("Unsupported pixel format {:#?}", config.format);
    }

    let display_size = IntSize::new(config.width as i32, config.height as i32);

    let frame = fb.new_frame(&mut executor)?;

    frame.present(&fb)?;

    let sink = FramePixelSink { frame };
    let mut canvas = Canvas::new_with_sink(
        display_size,
        sink,
        config.linear_stride_bytes() as u32,
        config.pixel_size_bytes,
    );

    let r = Rect::new(Point::new(0.0, 0.0), Size::new(config.width as f32, config.height as f32));

    let bg = Color::from_hash_code("#EBD5B3")?;
    canvas.fill_rect(&r, bg);

    let min_dimension = config.width.min(config.height);
    let grid_size = (min_dimension / 8) as Coord;

    let r = Rect::new(Point::new(grid_size, grid_size), Size::new(grid_size, grid_size));
    let c1 = Color::from_hash_code("#B7410E")?;
    canvas.fill_rect(&r, c1);

    let v = grid_size * 3.0;
    let pt = Point::new(v, v);
    let c2 = Color::from_hash_code("#008080")?;
    canvas.fill_circle(&pt, grid_size / 2.0, c2);

    let rr =
        Rect::new(Point::new(3.0 * grid_size, grid_size), Size::new(grid_size * 2.0, grid_size));
    let c3 = Color::from_hash_code("#FF00FF")?;
    canvas.fill_roundrect(&rr, grid_size / 8.0, c3);

    let r_clipped =
        Rect::new(Point::new(-grid_size, grid_size * 3.0), Size::new(grid_size * 2.0, grid_size));
    let c4 = Color::from_hash_code("#00008B")?;
    canvas.fill_roundrect(&r_clipped, grid_size / 6.0, c4);

    let r_clipped2 = Rect::new(
        Point::new(
            display_size.width as Coord - grid_size,
            display_size.height as Coord - grid_size / 3.0,
        ),
        Size::new(grid_size * 4.0, grid_size * 2.0),
    );
    let c5 = Color::from_hash_code("#7B68EE")?;
    canvas.fill_roundrect(&r_clipped2, grid_size / 5.0, c5);

    executor.run_singlethreaded(close);

    Ok(())
}
