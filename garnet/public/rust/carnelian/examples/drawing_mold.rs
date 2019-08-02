// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

#![allow(dead_code)]
#![allow(unused_imports)]

use carnelian::{
    AnimationMode, App, AppAssistant, PixelSink, Point,
    ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey, ViewMode,
};
use failure::Error;
use fuchsia_zircon::{ClockId, Time};
use mold::{
    Path, Raster,
    tile::{ColorBuffer, Layer, Map, PixelFormat, TileOp}
};
use rusttype::{Contour, Font, Scale, Segment};
use std::{
    env,
    f32,
    io::{self, Read},
    slice,
    thread,
};

static FONT_DATA: &'static [u8] =
    include_bytes!("../../../../../prebuilt/third_party/fonts/robotoslab/RobotoSlab-Regular.ttf");

/// Convenience function that can be called from main and causes the Fuchsia process being
/// run over ssh to be terminated when the user hits control-C.
pub fn wait_for_close() {
    if let Some(argument) = env::args().next() {
        if !argument.starts_with("/tmp") {
            return;
        }
    }

    thread::spawn(move || loop {
        let mut input = [0; 1];
        match io::stdin().read_exact(&mut input) {
            Ok(()) => {}
            Err(_) => {
                std::process::exit(0);
            }
        }
    });
}

struct RoundedRect {
    path: Path,
}

impl RoundedRect {
    fn new(pos: Point, size: Point, radius: f32) -> Self {
        let dist = 4.0 / 3.0 * (f32::consts::PI / 8.0).tan();
        let control_dist = dist * radius;

        let tl = pos.to_vector();
        let tr = pos.to_vector() + Point::new(size.x, 0.0).to_vector();
        let br = pos.to_vector() + size.to_vector();
        let bl = pos.to_vector() + Point::new(0.0, size.y).to_vector();

        let rt = Point::new(0.0, radius).to_vector();
        let rr = Point::new(-radius, 0.0).to_vector();
        let rb = Point::new(0.0, -radius).to_vector();
        let rl = Point::new(radius, 0.0).to_vector();

        let ct = Point::new(0.0, -control_dist).to_vector();
        let cr = Point::new(control_dist, 0.0).to_vector();
        let cb = Point::new(0.0, control_dist).to_vector();
        let cl = Point::new(-control_dist, 0.0).to_vector();

        macro_rules! c {
            ( $v:expr ) => (mold::Point::new($v.x, $v.y));
        }

        let mut path = Path::new();

        path.line(c!(tl + rl), c!(tr + rr));
        path.cubic(c!(tr + rr), c!(tr + rr + cr), c!(tr + rt + ct), c!(tr + rt));
        path.line(c!(tr + rt), c!(br + rb));
        path.cubic(c!(br + rb), c!(br + rb + cb), c!(br + rr + cr), c!(br + rr));
        path.line(c!(br + rr), c!(bl + rl));
        path.cubic(c!(bl + rl), c!(bl + rl + cl), c!(bl + rb + cb), c!(bl + rb));
        path.line(c!(bl + rb), c!(tl + rt));
        path.cubic(c!(tl + rt), c!(tl + rt + ct), c!(tl + rl + cl), c!(tl + rl));

        Self { path }
    }
}

struct Glyph {
    path: Path,
}

impl Glyph {
    fn new(contours: &[Contour], scale: f32) -> Self {
        let mut path = Path::new();

        let flip_scale = |x: f32, y: f32| {
            let x = x * scale;
            let y = (-y + 1.0) * scale;
            mold::Point::new(x, y)
        };

        for contour in contours {
            for segment in &contour.segments {
                match segment {
                    Segment::Line(line) => {
                        path.line(
                            flip_scale(line.p[1].x, line.p[1].y),
                            flip_scale(line.p[0].x, line.p[0].y),
                        );
                    },
                    Segment::Curve(curve) => {
                        path.quad(
                            flip_scale(curve.p[2].x, curve.p[2].y),
                            flip_scale(curve.p[1].x, curve.p[1].y),
                            flip_scale(curve.p[0].x, curve.p[0].y),
                        );
                    },
                }
            }
        }

        Self { path }
    }
}

struct DrawingAppAssistant;

impl AppAssistant for DrawingAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant_canvas(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(DrawingViewAssistant::new()))
    }

    fn get_mode(&self) -> ViewMode {
        ViewMode::Canvas
    }
}

struct DrawingViewAssistant {
    rect: Raster,
    glyph: Vec<Contour>,
    start: Time,
    map: Option<Map>,
}

impl DrawingViewAssistant {
    pub fn new() -> Self {
        let rounded_rect = RoundedRect::new(
            Point::new(200.0, 700.0),
            Point::new(300.0, 200.0),
            20.0,
        );

        let rect = Raster::new(&rounded_rect.path);

        let font: Font<'static> = Font::from_bytes(FONT_DATA).unwrap();
        let glyph = font.glyph('a').scaled(Scale::uniform(1.0)).shape().unwrap();

        Self { rect, glyph, start: Time::get(ClockId::Monotonic), map: None }
    }
}

#[derive(Clone)]
struct PixelSinkWrapper<P: PixelSink> {
    pixel_sink: P,
    format: PixelFormat
}

impl<P: PixelSink> PixelSinkWrapper<P> {
    pub fn new(pixel_sink: P, col_stride: u32) -> Self {
        Self {
            pixel_sink,
            format: if col_stride == 2 {
                PixelFormat::RGB565
            } else {
                PixelFormat::BGRA8888
            }
        }
    }
}

impl<P: PixelSink> ColorBuffer for PixelSinkWrapper<P> {
    fn pixel_format(&self) -> PixelFormat {
        self.format
    }

    unsafe fn write_at(&mut self, offset: usize, src: *const u8, len: usize) {
        self.pixel_sink.write_pixel_at_offset(offset, slice::from_raw_parts(src, len));
    }
}

impl ViewAssistant for DrawingViewAssistant {
    fn setup(&mut self, _: &ViewAssistantContext) -> Result<(), Error> {
        Ok(())
    }

    fn update(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        if self.map.is_none() {
            let mut map = Map::new(context.size.width as usize, context.size.height as usize);
            map.global(0, vec![TileOp::ColorAccZero]);

            map.print(
                1,
                Layer {
                    raster: self.rect.clone(),
                    ops: vec![
                        TileOp::CoverWipZero,
                        TileOp::CoverWipNonZero,
                        TileOp::ColorWipZero,
                        TileOp::ColorWipFillSolid(0x7B68_EEFF),
                        TileOp::ColorAccBlendOver,
                    ],
                },
            );

            map.global(3, vec![
                TileOp::ColorAccBackground(0xEBD5_B3FF),
            ]);
            self.map = Some(map);
        }

        const SPEED: f32 = 1.2;
        const SECONDS_PER_NANOSECOND: f32 = 1e-9;
        const DELTA: f32 = 7.0;
        const GLYPH_SIZE: f32 = 200.0;

        let time_now = Time::get(ClockId::Monotonic);
        let t = (time_now.into_nanos() - self.start.into_nanos()) as f32
            * SECONDS_PER_NANOSECOND
            * SPEED;

        self.rect.translate(mold::Point::new(DELTA * t.sin(), 0.0));

        let canvas = context.canvas.as_ref().unwrap().borrow();

        let map = self.map.as_mut().unwrap();
        let mut raster = Raster::new(&Glyph::new(&self.glyph, GLYPH_SIZE * t.sin().abs()).path);
        raster.translate(mold::Point::new(100.0, 100.0));

        map.print(
            2,
            Layer {
                raster,
                ops: vec![
                    TileOp::CoverWipZero,
                    TileOp::CoverWipNonZero,
                    TileOp::ColorWipZero,
                    TileOp::ColorWipFillSolid(0x0000_00FF),
                    TileOp::ColorAccBlendOver,
                ],
            },
        );

        map.render(PixelSinkWrapper::new(canvas.pixel_sink.clone(), canvas.col_stride));

        Ok(())
    }

    fn initial_animation_mode(&mut self) -> AnimationMode {
        return AnimationMode::EveryFrame;
    }
}

fn main() -> Result<(), Error> {
    println!("drawing: started");
    wait_for_close();
    let assistant = DrawingAppAssistant {};
    App::run(Box::new(assistant))
}
