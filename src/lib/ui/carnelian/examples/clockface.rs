// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh::FromArgs,
    carnelian::{
        make_app_assistant, render::*, AnimationMode, App, AppAssistant, Color, Point, Size,
        ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey, ViewMode,
    },
    chrono::{Local, Timelike},
    euclid::{Angle, Point2D, Rect, Size2D, Transform2D, Vector2D},
    fuchsia_trace::{self, duration},
    fuchsia_trace_provider,
    fuchsia_zircon::{AsHandleRef, Event, Signals},
    std::{collections::BTreeMap, f32},
};

const BACKGROUND_COLOR: Color = Color { r: 235, g: 213, b: 179, a: 255 };

/// Clockface.
#[derive(Debug, FromArgs)]
#[argh(name = "clockface_rs")]
struct Args {
    /// use mold (software rendering back-end)
    #[argh(switch, short = 'm')]
    use_mold: bool,
}

#[derive(Default)]
struct ClockfaceAppAssistant {
    use_mold: bool,
}

impl AppAssistant for ClockfaceAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        let args: Args = argh::from_env();
        self.use_mold = args.use_mold;
        Ok(())
    }

    fn create_view_assistant_render(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(ClockfaceViewAssistant::new()))
    }

    fn get_mode(&self) -> ViewMode {
        ViewMode::Render { use_mold: self.use_mold }
    }
}

fn line(path_builder: &mut PathBuilder, p0: Point, p1: Point) {
    path_builder.move_to(p0);
    path_builder.line_to(p1);
}

fn cubic(path_builder: &mut PathBuilder, p0: Point, p1: Point, p2: Point, p3: Point) {
    path_builder.move_to(p0);
    path_builder.cubic_to(p1, p2, p3);
}

struct RoundedLine {
    path: Path,
}

impl RoundedLine {
    fn new(mut path_builder: PathBuilder, pos: Point, length: f32, thickness: f32) -> Self {
        let dist = 4.0 / 3.0 * (f32::consts::PI / 8.0).tan();
        let radius = thickness / 2.0;
        let control_dist = dist * radius;
        let tl = pos.to_vector();
        let tr = pos.to_vector() + Point::new(length, 0.0).to_vector();
        let br = pos.to_vector() + Point::new(length, thickness).to_vector();
        let bl = pos.to_vector() + Point::new(0.0, thickness).to_vector();
        let rt = Point::new(0.0, radius).to_vector();
        let rr = Point::new(-radius, 0.0).to_vector();
        let rb = Point::new(0.0, -radius).to_vector();
        let rl = Point::new(radius, 0.0).to_vector();
        let ct = Point::new(0.0, -control_dist).to_vector();
        let cr = Point::new(control_dist, 0.0).to_vector();
        let cb = Point::new(0.0, control_dist).to_vector();
        let cl = Point::new(-control_dist, 0.0).to_vector();

        let path = {
            macro_rules! c {
                ( $v:expr ) => {
                    Point::new($v.x, $v.y)
                };
            }

            line(&mut path_builder, c!(tl + rl), c!(tr + rr));
            cubic(&mut path_builder, c!(tr + rr), c!(tr + rr + cr), c!(tr + rt + ct), c!(tr + rt));
            cubic(&mut path_builder, c!(br + rb), c!(br + rb + cb), c!(br + rr + cr), c!(br + rr));
            line(&mut path_builder, c!(br + rr), c!(bl + rl));
            cubic(&mut path_builder, c!(bl + rl), c!(bl + rl + cl), c!(bl + rb + cb), c!(bl + rb));
            cubic(&mut path_builder, c!(tl + rt), c!(tl + rt + ct), c!(tl + rl + cl), c!(tl + rl));

            path_builder.build()
        };

        Self { path }
    }
}

struct Hand {
    line: RoundedLine,
    raster: Option<Raster>,
    color: Color,
}

impl Hand {
    fn new(
        path_builder: PathBuilder,
        thickness: f32,
        length: f32,
        offset: f32,
        color: Color,
    ) -> Self {
        let line = RoundedLine::new(
            path_builder,
            Point::new(-(thickness / 2.0 + offset), -thickness / 2.0),
            length,
            thickness,
        );

        Self { line, raster: None, color }
    }

    fn update(&mut self, context: &mut Context, scale: f32, angle: f32) {
        let rotation = Transform2D::create_rotation(Angle::radians(angle)).post_scale(scale, scale);
        let mut raster_builder = context.raster_builder().unwrap();
        raster_builder.add(&self.line.path, Some(&rotation));
        self.raster.replace(raster_builder.build());
    }
}

struct Scene {
    size: Size,
    hour_hand: Hand,
    minute_hand: Hand,
    second_hand: Hand,
    hour_index: usize,
    minute_index: usize,
    second_index: usize,
}

impl Scene {
    fn new(context: &mut Context) -> Self {
        const HOUR_HAND_COLOR: Color = Color { r: 254, g: 72, b: 100, a: 255 };
        const MINUTE_HAND_COLOR: Color = Color { r: 254, g: 72, b: 100, a: 127 };
        const SECOND_HAND_COLOR: Color = Color::white();
        const RADIUS: f32 = 0.4;

        let thickness = RADIUS / 20.0;
        let offset = RADIUS / 5.0;
        let hour_hand = Hand::new(
            context.path_builder().unwrap(),
            thickness * 2.0,
            RADIUS,
            offset,
            HOUR_HAND_COLOR,
        );
        let minute_hand =
            Hand::new(context.path_builder().unwrap(), thickness, RADIUS, 0.0, MINUTE_HAND_COLOR);
        let second_hand = Hand::new(
            context.path_builder().unwrap(),
            thickness / 2.0,
            RADIUS + offset,
            offset,
            SECOND_HAND_COLOR,
        );

        Self {
            size: Size::new(1.0, 1.0),
            hour_hand,
            minute_hand,
            second_hand,
            hour_index: std::usize::MAX,
            minute_index: std::usize::MAX,
            second_index: std::usize::MAX,
        }
    }

    fn update(&mut self, context: &mut Context, size: &Size, scale: f32) {
        if self.size != *size {
            self.size = *size;
            self.hour_index = std::usize::MAX;
            self.minute_index = std::usize::MAX;
            self.second_index = std::usize::MAX;
        }
        const MICROSECONDS_PER_SECOND: f32 = 1e+6;
        let now = Local::now();
        let (_is_pm, hour12) = now.hour12();
        let us = now.nanosecond() as f32 / 1000.0;
        let second = now.second() as f32 + us / MICROSECONDS_PER_SECOND;
        let minute = now.minute() as f32 + second / 60.0;
        let hour = hour12 as f32 + minute / 60.0;
        const R0: f32 = -0.25; // Rotate from 3 to 12.
        const STEPS: usize = 60 * 60; // Enough steps to ensure smooth movement
                                      // of second hand each frame on a 60hz display.
        let index = ((R0 + hour / 12.0).rem_euclid(1.0) * STEPS as f32) as usize;
        if index != self.hour_index {
            let angle = index as f32 * 2.0 * f32::consts::PI / STEPS as f32;
            self.hour_hand.update(context, scale, -angle);
            self.hour_index = index;
        }
        let index = ((R0 + minute / 60.0).rem_euclid(1.0) * STEPS as f32) as usize;
        if index != self.minute_index {
            let angle = index as f32 * 2.0 * f32::consts::PI / STEPS as f32;
            self.minute_hand.update(context, scale, -angle);
            self.minute_index = index;
        }
        let index = ((R0 + second / 60.0).rem_euclid(1.0) * STEPS as f32) as usize;
        if index != self.second_index {
            let angle = index as f32 * 2.0 * f32::consts::PI / STEPS as f32;
            self.second_hand.update(context, scale, -angle);
            self.second_index = index;
        }
    }
}

struct Contents {
    image: Image,
    composition: Composition,
    size: Size,
    previous_rasters: Vec<Raster>,
}

impl Contents {
    fn new(image: Image) -> Self {
        let composition = Composition::new(BACKGROUND_COLOR);

        Self { image, composition, size: Size::zero(), previous_rasters: Vec::new() }
    }

    fn update(&mut self, context: &mut Context, scene: &Scene, size: &Size, scale: f32) {
        const SHADOW_COLOR: Color = Color { r: 0, g: 0, b: 0, a: 13 };
        const ELEVATION: f32 = 0.01;

        let center = Vector2D::new(size.width as i32 / 2, size.height as i32 / 2);
        let elevation = (ELEVATION * scale) as i32;
        let shadow_offset = center + Vector2D::new(elevation, elevation * 2);

        let clip = Rect::new(
            Point2D::new(0, 0),
            Size2D::new(size.width.floor() as u32, size.height.floor() as u32),
        );

        let ext = if self.size != *size {
            self.size = *size;
            RenderExt {
                pre_clear: Some(PreClear { color: BACKGROUND_COLOR }),
                ..Default::default()
            }
        } else {
            RenderExt::default()
        };

        let hands = [&scene.hour_hand, &scene.minute_hand, &scene.second_hand];

        let layers = hands
            .iter()
            .map(|hand| Layer {
                raster: hand.raster.clone().unwrap().translate(center),
                style: Style {
                    fill_rule: FillRule::NonZero,
                    fill: Fill::Solid(hand.color),
                    blend_mode: BlendMode::Over,
                },
            })
            .chain(std::iter::once(Layer {
                raster: hands
                    .iter()
                    .fold(None, |raster_union: Option<Raster>, hand| {
                        let raster = hand.raster.clone().unwrap().translate(shadow_offset);
                        if let Some(raster_union) = raster_union {
                            Some(raster_union + raster)
                        } else {
                            Some(raster)
                        }
                    })
                    .unwrap(),
                style: Style {
                    fill_rule: FillRule::NonZero,
                    fill: Fill::Solid(SHADOW_COLOR),
                    blend_mode: BlendMode::Over,
                },
            }))
            .chain(std::iter::once(Layer {
                raster: hands
                    .iter()
                    .enumerate()
                    .fold(None, |raster_union: Option<Raster>, (i, hand)| {
                        if i != 1 {
                            let raster = hand.raster.clone().unwrap().translate(shadow_offset);
                            if let Some(raster_union) = raster_union {
                                Some(raster_union + raster)
                            } else {
                                Some(raster)
                            }
                        } else {
                            raster_union
                        }
                    })
                    .unwrap(),
                style: Style {
                    fill_rule: FillRule::NonZero,
                    fill: Fill::Solid(SHADOW_COLOR),
                    blend_mode: BlendMode::Over,
                },
            }))
            .chain(self.previous_rasters.drain(..).map(|raster| Layer {
                raster,
                style: Style {
                    fill_rule: FillRule::WholeTile,
                    fill: Fill::Solid(BACKGROUND_COLOR),
                    blend_mode: BlendMode::Over,
                },
            }));
        self.composition.replace(.., layers);

        context.render(&self.composition, Some(clip), self.image, &ext);
        context.flush_image(self.image);

        // Keep reference to rasters for clearing.
        self.previous_rasters.extend(
            hands.iter().map(|hand| hand.raster.clone().unwrap().translate(center)).chain(
                hands.iter().map(|hand| hand.raster.clone().unwrap().translate(shadow_offset)),
            ),
        );
    }
}

struct Clockface {
    scene: Scene,
    contents: BTreeMap<u64, Contents>,
}

impl Clockface {
    pub fn new(context: &mut Context) -> Self {
        let scene = Scene::new(context);

        Self { scene, contents: BTreeMap::new() }
    }

    fn update(
        &mut self,
        render_context: &mut Context,
        context: &ViewAssistantContext<'_>,
    ) -> Result<(), Error> {
        duration!("gfx", "update");

        let size = &context.logical_size;
        let image_id = context.image_id;
        let scale = size.width.min(size.height);

        self.scene.update(render_context, size, scale);

        let image = render_context.get_current_image(context);
        let content = self.contents.entry(image_id).or_insert_with(|| Contents::new(image));

        content.update(render_context, &self.scene, size, scale);

        Ok(())
    }
}

struct ClockfaceViewAssistant {
    size: Size,
    clockface: Option<Clockface>,
}

impl ClockfaceViewAssistant {
    pub fn new() -> Self {
        Self { size: Size::zero(), clockface: None }
    }
}

impl ViewAssistant for ClockfaceViewAssistant {
    fn setup(&mut self, _context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        Ok(())
    }

    fn update(&mut self, _: &ViewAssistantContext<'_>) -> Result<(), Error> {
        Ok(())
    }

    fn render(
        &mut self,
        render_context: &mut Context,
        ready_event: Event,
        context: &ViewAssistantContext<'_>,
    ) -> Result<(), Error> {
        if context.logical_size != self.size || self.clockface.is_none() {
            self.size = context.logical_size;
            self.clockface = Some(Clockface::new(render_context));
        }

        if let Some(clockface) = self.clockface.as_mut() {
            clockface.update(render_context, context).expect("clockface.update");
        }

        ready_event.as_handle_ref().signal(Signals::NONE, Signals::EVENT_SIGNALED)?;

        Ok(())
    }

    fn initial_animation_mode(&mut self) -> AnimationMode {
        return AnimationMode::EveryFrame;
    }
}

fn main() -> Result<(), Error> {
    fuchsia_trace_provider::trace_provider_create_with_fdio();

    println!("Clockface Example");
    App::run(make_app_assistant::<ClockfaceAppAssistant>())
}
