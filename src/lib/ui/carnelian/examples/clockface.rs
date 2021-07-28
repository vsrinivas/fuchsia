// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    carnelian::{
        color::Color,
        make_app_assistant,
        render::{
            BlendMode, Context as RenderContext, Fill, FillRule, Layer, Path, PathBuilder, Raster,
            Style,
        },
        scene::{
            facets::Facet,
            scene::{Scene, SceneBuilder},
            LayerGroup,
        },
        App, AppAssistant, Point, Size, ViewAssistant, ViewAssistantContext, ViewAssistantPtr,
        ViewKey,
    },
    chrono::{Local, Timelike},
    euclid::{point2, size2, vec2, Angle, Transform2D},
    fuchsia_trace_provider,
    fuchsia_zircon::Event,
    std::f32,
};

const BACKGROUND_COLOR: Color = Color { r: 235, g: 213, b: 179, a: 255 };

#[derive(Default)]
struct ClockfaceAppAssistant;

impl AppAssistant for ClockfaceAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(ClockfaceViewAssistant::new()))
    }
}

struct RoundedLine {
    path: Path,
}

impl RoundedLine {
    fn new(mut path_builder: PathBuilder, pos: Point, length: f32, thickness: f32) -> Self {
        let radius = thickness / 2.0;
        let tl = pos.to_vector();
        let tr = pos.to_vector() + vec2(length, 0.0);
        let br = pos.to_vector() + vec2(length, thickness);
        let bl = pos.to_vector() + vec2(0.0, thickness);
        let radiush = vec2(radius, 0.0);
        let radiusv = vec2(0.0, radius);

        let path = {
            macro_rules! c {
                ( $v:expr ) => {
                    point2($v.x, $v.y)
                };
            }

            path_builder.move_to(c!(tl + radiush));
            path_builder.line_to(c!(tr - radiush));
            path_builder.rat_quad_to(c!(tr), c!(tr + radiusv), 0.7071);
            path_builder.rat_quad_to(c!(br), c!(br - radiush), 0.7071);
            path_builder.line_to(c!(bl + radiush));
            path_builder.rat_quad_to(c!(bl), c!(bl - radiusv), 0.7071);
            path_builder.rat_quad_to(c!(tl), c!(tl + radiush), 0.7071);

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
            point2(-(thickness / 2.0 + offset), -thickness / 2.0),
            length,
            thickness,
        );

        Self { line, raster: None, color }
    }

    fn update(&mut self, context: &mut RenderContext, scale: f32, angle: f32) {
        let rotation = Transform2D::rotation(Angle::radians(angle)).then_scale(scale, scale);
        let mut raster_builder = context.raster_builder().unwrap();
        raster_builder.add(&self.line.path, Some(&rotation));
        self.raster.replace(raster_builder.build());
    }
}

struct ClockFaceFacet {
    size: Size,
    hour_hand: Hand,
    minute_hand: Hand,
    second_hand: Hand,
    hour_index: usize,
    minute_index: usize,
    second_index: usize,
}

impl ClockFaceFacet {
    fn new(context: &mut RenderContext) -> Self {
        const HOUR_HAND_COLOR: Color = Color { r: 254, g: 72, b: 100, a: 255 };
        const MINUTE_HAND_COLOR: Color = Color { r: 255, g: 114, b: 132, a: 127 };
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
            size: size2(1.0, 1.0),
            hour_hand,
            minute_hand,
            second_hand,
            hour_index: std::usize::MAX,
            minute_index: std::usize::MAX,
            second_index: std::usize::MAX,
        }
    }

    fn update(&mut self, context: &mut RenderContext, size: &Size, scale: f32) {
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
            self.hour_hand.update(context, scale, angle);
            self.hour_index = index;
        }
        let index = ((R0 + minute / 60.0).rem_euclid(1.0) * STEPS as f32) as usize;
        if index != self.minute_index {
            let angle = index as f32 * 2.0 * f32::consts::PI / STEPS as f32;
            self.minute_hand.update(context, scale, angle);
            self.minute_index = index;
        }
        let index = ((R0 + second / 60.0).rem_euclid(1.0) * STEPS as f32) as usize;
        if index != self.second_index {
            let angle = index as f32 * 2.0 * f32::consts::PI / STEPS as f32;
            self.second_hand.update(context, scale, angle);
            self.second_index = index;
        }
    }
}

impl Facet for ClockFaceFacet {
    fn update_layers(
        &mut self,
        size: Size,
        layer_group: &mut dyn LayerGroup,
        render_context: &mut RenderContext,
        _view_context: &ViewAssistantContext,
    ) -> std::result::Result<(), anyhow::Error> {
        const SHADOW_COLOR: Color = Color { r: 0, g: 0, b: 0, a: 13 };
        const ELEVATION: f32 = 0.01;

        let scale = size.width.min(size.height);
        self.size = size;

        self.update(render_context, &size, scale);

        let elevation = (ELEVATION * scale) as i32;
        let center = vec2(size.width as i32 / 2, size.height as i32 / 2);
        let shadow_offset = center + vec2(elevation, elevation * 2);

        let hands = [&self.second_hand, &self.minute_hand, &self.hour_hand];

        let layers = std::iter::once(Layer {
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
            clip: None,
            style: Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(SHADOW_COLOR),
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
            clip: None,
            style: Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(SHADOW_COLOR),
                blend_mode: BlendMode::Over,
            },
        }))
        .chain(hands.iter().map(|hand| Layer {
            raster: hand.raster.clone().unwrap().translate(center),
            clip: None,
            style: Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(hand.color),
                blend_mode: BlendMode::Over,
            },
        }));
        layer_group.clear();
        for (i, layer) in layers.enumerate() {
            layer_group.insert(i as u16, layer);
        }
        Ok(())
    }

    fn calculate_size(&self, _available: Size) -> Size {
        self.size
    }
}

struct SceneDetails {
    scene: Scene,
}

struct ClockfaceViewAssistant {
    scene_details: Option<SceneDetails>,
}

impl ClockfaceViewAssistant {
    pub fn new() -> Self {
        Self { scene_details: None }
    }
}

impl ViewAssistant for ClockfaceViewAssistant {
    fn resize(&mut self, _new_size: &Size) -> Result<(), Error> {
        self.scene_details = None;
        Ok(())
    }

    fn render(
        &mut self,
        render_context: &mut RenderContext,
        ready_event: Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        let mut scene_details = self.scene_details.take().unwrap_or_else(|| {
            let mut builder = SceneBuilder::new().background_color(BACKGROUND_COLOR);
            let clock_face_facet = ClockFaceFacet::new(render_context);
            let _ = builder.facet(Box::new(clock_face_facet));
            SceneDetails { scene: builder.build() }
        });

        scene_details.scene.render(render_context, ready_event, context)?;

        self.scene_details = Some(scene_details);

        context.request_render();

        Ok(())
    }
}

fn main() -> Result<(), Error> {
    fuchsia_trace_provider::trace_provider_create_with_fdio();

    println!("Clockface Example");
    App::run(make_app_assistant::<ClockfaceAppAssistant>())
}
