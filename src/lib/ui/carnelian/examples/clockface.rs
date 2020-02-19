// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh::FromArgs,
    carnelian::{
        make_app_assistant, render::*, AnimationMode, App, AppAssistant, Color, FrameBufferPtr,
        Point, Size, ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey, ViewMode,
    },
    chrono::{Local, Timelike},
    euclid::{Angle, Point2D, Rect, Size2D, Transform2D, Vector2D},
    fidl::endpoints::create_endpoints,
    fidl_fuchsia_sysmem::BufferCollectionTokenMarker,
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
struct ClockfaceAppAssistant;

impl AppAssistant for ClockfaceAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant_image_pipe(
        &mut self,
        _: ViewKey,
        fb: FrameBufferPtr,
    ) -> Result<ViewAssistantPtr, Error> {
        let args: Args = argh::from_env();
        println!("back-end: {}", if args.use_mold { "mold" } else { "spinel" });

        let (token, token_request) =
            create_endpoints::<BufferCollectionTokenMarker>().expect("create_endpoint");
        fb.borrow()
            .local_token
            .as_ref()
            .unwrap()
            .duplicate(std::u32::MAX, token_request)
            .expect("duplicate");
        let config = &fb.borrow().get_config();
        let size = Size2D::new(config.width, config.height);

        if args.use_mold {
            Ok(Box::new(ClockfaceViewAssistant::new(Mold::new_context(token, size))))
        } else {
            Ok(Box::new(ClockfaceViewAssistant::new(Spinel::new_context(token, size))))
        }
    }

    fn get_mode(&self) -> ViewMode {
        ViewMode::ImagePipe
    }
}

fn line<B: Backend>(path_builder: &mut impl PathBuilder<B>, p0: Point, p1: Point) {
    path_builder.move_to(p0);
    path_builder.line_to(p1);
}

fn cubic<B: Backend>(
    path_builder: &mut impl PathBuilder<B>,
    p0: Point,
    p1: Point,
    p2: Point,
    p3: Point,
) {
    path_builder.move_to(p0);
    path_builder.cubic_to(p1, p2, p3);
}

struct RoundedLine<B: Backend> {
    path: B::Path,
}

impl<B: Backend> RoundedLine<B> {
    fn new(mut path_builder: impl PathBuilder<B>, pos: Point, length: f32, thickness: f32) -> Self {
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

struct Hand<B: Backend> {
    line: RoundedLine<B>,
    elevation: f32,
    raster: Option<B::Raster>,
    shadow_raster: Option<B::Raster>,
    color: Color,
}

impl<B: Backend> Hand<B> {
    fn new_raster(
        mut raster_builder: impl RasterBuilder<B>,
        path: &B::Path,
        rotation: &Transform2D<f32>,
        txty: &Vector2D<f32>,
    ) -> B::Raster {
        let transform = rotation.post_translate(*txty);
        raster_builder.add(path, Some(&transform));
        raster_builder.build()
    }

    fn new(
        path_builder: impl PathBuilder<B>,
        thickness: f32,
        length: f32,
        offset: f32,
        color: Color,
        elevation: f32,
    ) -> Self {
        let line = RoundedLine::new(
            path_builder,
            Point::new(-(thickness / 2.0 + offset), -thickness / 2.0),
            length,
            thickness,
        );

        Self { line, elevation, raster: None, shadow_raster: None, color }
    }

    fn update(&mut self, context: &mut impl Context<B>, scale: f32, angle: f32, position: Point) {
        let rotation = Transform2D::create_rotation(Angle::radians(angle)).post_scale(scale, scale);

        let txty = Vector2D::new(position.x, position.y);
        let raster =
            Self::new_raster(context.raster_builder().unwrap(), &self.line.path, &rotation, &txty);
        self.raster.replace(raster);

        let shadow_offset = self.elevation * scale;
        let txty = Vector2D::new(position.x + shadow_offset, position.y + shadow_offset * 2.0);
        let raster =
            Self::new_raster(context.raster_builder().unwrap(), &self.line.path, &rotation, &txty);
        self.shadow_raster.replace(raster);
    }
}

struct Scene<B: Backend> {
    size: Size,
    hour_hand: Hand<B>,
    minute_hand: Hand<B>,
    second_hand: Hand<B>,
    hour_index: usize,
    minute_index: usize,
    second_index: usize,
}

impl<B: Backend> Scene<B> {
    fn new(context: &mut impl Context<B>) -> Self {
        const HOUR_HAND_COLOR: Color = Color { r: 254, g: 72, b: 100, a: 255 };
        const MINUTE_HAND_COLOR: Color = Color { r: 254, g: 72, b: 100, a: 127 };
        const SECOND_HAND_COLOR: Color = Color::white();

        let radius = 0.4;
        let thickness = radius / 20.0;
        let offset = radius / 5.0;
        let elevation = 0.01;
        let hour_hand = Hand::new(
            context.path_builder().unwrap(),
            thickness * 2.0,
            radius,
            offset,
            HOUR_HAND_COLOR,
            elevation,
        );
        let minute_hand = Hand::new(
            context.path_builder().unwrap(),
            thickness,
            radius,
            0.0,
            MINUTE_HAND_COLOR,
            elevation,
        );
        let second_hand = Hand::new(
            context.path_builder().unwrap(),
            thickness / 2.0,
            radius + offset,
            offset,
            SECOND_HAND_COLOR,
            elevation,
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

    fn update(&mut self, context: &mut impl Context<B>, size: &Size) {
        if self.size != *size {
            self.size = *size;
            self.hour_index = std::usize::MAX;
            self.minute_index = std::usize::MAX;
            self.second_index = std::usize::MAX;
        }
        let scale = size.width.min(size.height);
        let center = Point::new(size.width / 2.0, size.height / 2.0);
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
            self.hour_hand.update(context, scale, -angle, center);
            self.hour_index = index;
        }
        let index = ((R0 + minute / 60.0).rem_euclid(1.0) * STEPS as f32) as usize;
        if index != self.minute_index {
            let angle = index as f32 * 2.0 * f32::consts::PI / STEPS as f32;
            self.minute_hand.update(context, scale, -angle, center);
            self.minute_index = index;
        }
        let index = ((R0 + second / 60.0).rem_euclid(1.0) * STEPS as f32) as usize;
        if index != self.second_index {
            let angle = index as f32 * 2.0 * f32::consts::PI / STEPS as f32;
            self.second_hand.update(context, scale, -angle, center);
            self.second_index = index;
        }
    }
}

struct Contents<B: Backend> {
    image: B::Image,
    composition: B::Composition,
    size: Size,
    previous_rasters: Vec<B::Raster>,
}

impl<B: Backend> Contents<B> {
    fn new(image: B::Image) -> Self {
        let composition = Composition::new(std::iter::empty(), BACKGROUND_COLOR);

        Self { image, composition, size: Size::zero(), previous_rasters: Vec::new() }
    }

    fn update(&mut self, context: &mut impl Context<B>, scene: &Scene<B>, size: &Size) {
        const SHADOW_COLOR: Color = Color { r: 0, g: 0, b: 0, a: 13 };
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
                raster: hand.raster.clone().unwrap(),
                style: Style {
                    fill_rule: FillRule::NonZero,
                    fill: Fill::Solid(hand.color),
                    blend_mode: BlendMode::Over,
                },
            })
            .chain(std::iter::once(Layer {
                raster: hands
                    .iter()
                    .fold(None, |raster_union: Option<B::Raster>, hand| {
                        if let Some(raster_union) = raster_union {
                            Some(raster_union + hand.shadow_raster.clone().unwrap())
                        } else {
                            hand.shadow_raster.clone()
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
                    .fold(None, |raster_union: Option<B::Raster>, (i, hand)| {
                        if i != 1 {
                            if let Some(raster_union) = raster_union {
                                Some(raster_union + hand.shadow_raster.clone().unwrap())
                            } else {
                                hand.shadow_raster.clone()
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
        self.composition.splice(.., layers);

        context.render(&self.composition, Some(clip), self.image, &ext);
        context.flush_image(self.image);

        // Keep reference to rasters for clearing.
        self.previous_rasters.extend(
            hands
                .iter()
                .map(|hand| hand.raster.clone().unwrap())
                .chain(hands.iter().map(|hand| hand.shadow_raster.clone().unwrap())),
        );
    }
}

struct ClockfaceViewAssistant<B: Backend, C: Context<B>> {
    context: C,
    scene: Scene<B>,
    contents: BTreeMap<u64, Contents<B>>,
}

impl<B: Backend, C: Context<B>> ClockfaceViewAssistant<B, C> {
    pub fn new(mut context: C) -> Self {
        let scene = Scene::new(&mut context);

        Self { context, scene, contents: BTreeMap::new() }
    }
}

impl<B: Backend, C: Context<B>> ViewAssistant for ClockfaceViewAssistant<B, C> {
    fn setup(&mut self, _context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        Ok(())
    }

    fn update(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        let canvas = context.canvas.as_ref().unwrap().borrow();
        let size = &context.size;

        self.scene.update(&mut self.context, size);

        // Temporary hack to deal with the fact that carnelian
        // allocates a new buffer for each frame with the same
        // image ID of zero.
        let mut temp_content;
        let content;
        let image = self.context.get_current_image(context);

        if canvas.id == 0 {
            temp_content = Contents::new(image);
            content = &mut temp_content;
        } else {
            content = self.contents.entry(canvas.id).or_insert_with(|| Contents::new(image));
        }

        content.update(&mut self.context, &self.scene, size);

        Ok(())
    }

    fn initial_animation_mode(&mut self) -> AnimationMode {
        return AnimationMode::EveryFrame;
    }

    fn get_pixel_format(&self) -> fuchsia_framebuffer::PixelFormat {
        self.context.pixel_format()
    }
}

fn main() -> Result<(), Error> {
    App::run(make_app_assistant::<ClockfaceAppAssistant>())
}
