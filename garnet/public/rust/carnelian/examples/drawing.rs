// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]
#![deny(warnings)]

use carnelian::{Canvas, Color, Coord, IntSize, PixelSink, Point, Rect, Size};
use failure::{bail, Error, ResultExt};
use fuchsia_async::{self as fasync, Interval};
use fuchsia_framebuffer::{Frame, FrameBuffer, PixelFormat};
use fuchsia_zircon::{ClockId, Duration, Time};
use futures::{channel::oneshot::Canceled, prelude::*};
use std::{
    f32::consts::PI,
    io::{self, Read},
    thread,
};

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

struct DrawingExample {
    bounds: Rect,
    start: Time,
    bg_color: Color,
    color1: Color,
    color2: Color,
    color3: Color,
    color4: Color,
    color5: Color,
    last_radius: Option<Coord>,
}

impl DrawingExample {
    pub fn new(bounds: Rect) -> Result<DrawingExample, Error> {
        let bg_color = Color::from_hash_code("#EBD5B3")?;
        let color1 = Color::from_hash_code("#B7410E")?;
        let color2 = Color::from_hash_code("#008080")?;
        let color3 = Color::from_hash_code("#FF00FF")?;
        let color4 = Color::from_hash_code("#00008B")?;
        let color5 = Color::from_hash_code("#7B68EE")?;

        let example = DrawingExample {
            bounds,
            start: Time::get(ClockId::Monotonic),
            bg_color,
            color1,
            color2,
            color3,
            color4,
            color5,
            last_radius: None,
        };
        Ok(example)
    }

    pub fn update<T: PixelSink>(&mut self, canvas: &mut Canvas<T>) {
        const SPEED: f32 = 0.25;
        const SECONDS_PER_NANOSECOND: f32 = 1e-9;

        let min_dimension = self.bounds.size.width.min(self.bounds.size.height);
        let grid_size = min_dimension / 8.0;

        let v = grid_size * 3.0;
        let pt = Point::new(v, v);

        if let Some(last_radius) = self.last_radius {
            Self::inval_circle(&pt, last_radius, canvas);
        }

        canvas.fill_rect(&self.bounds, self.bg_color);

        let r = Rect::new(Point::new(grid_size, grid_size), Size::new(grid_size, grid_size));
        canvas.fill_rect(&r, self.color1);

        let time_now = Time::get(ClockId::Monotonic);
        let t = ((time_now.into_nanos() - self.start.into_nanos()) as f32
            * SECONDS_PER_NANOSECOND
            * SPEED)
            % 1.0;
        let angle = t * PI * 2.0;
        let radius = angle.cos() * grid_size / 2.0 + grid_size / 2.0;
        self.last_radius = Some(radius);
        Self::inval_circle(&pt, radius, canvas);
        canvas.fill_circle(&pt, radius, self.color2);

        let rr = Rect::new(
            Point::new(3.0 * grid_size, grid_size),
            Size::new(grid_size * 2.0, grid_size),
        );
        canvas.fill_roundrect(&rr, grid_size / 8.0, self.color3);

        let r_clipped = Rect::new(
            Point::new(-grid_size, grid_size * 3.0),
            Size::new(grid_size * 2.0, grid_size),
        );
        canvas.fill_roundrect(&r_clipped, grid_size / 6.0, self.color4);

        let r_clipped2 = Rect::new(
            Point::new(
                self.bounds.size.width - grid_size,
                self.bounds.size.height - grid_size / 3.0,
            ),
            Size::new(grid_size * 4.0, grid_size * 2.0),
        );
        canvas.fill_roundrect(&r_clipped2, grid_size / 5.0, self.color5);

        canvas.reset_update_area();
    }

    pub fn inval_circle<T: PixelSink>(pt: &Point, radius: Coord, canvas: &mut Canvas<T>) {
        let diameter = radius * 2.0;
        let top_left = *pt - Point::new(radius, radius);
        let circle_bounds = Rect::new(top_left.to_point(), Size::new(diameter, diameter));
        canvas.add_to_update_area(&circle_bounds);
    }
}

fn main() -> Result<(), Error> {
    println!("drawing: started");
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
    let timer = Interval::new(Duration::from_millis(1000 / 60));
    let mut example = DrawingExample::new(r)?;
    example.update(&mut canvas);
    let f = timer
        .map(move |_| {
            example.update(&mut canvas);
        })
        .collect::<()>();
    fasync::spawn_local(f);

    executor.run_singlethreaded(close);

    Ok(())
}
