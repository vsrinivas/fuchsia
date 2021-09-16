// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use carnelian::{
    color::Color,
    drawing::{path_for_circle, path_for_polygon, path_for_rectangle, path_for_rounded_rectangle},
    geometry::{Corners, IntPoint},
    input::{self},
    make_app_assistant,
    render::{BlendMode, Context as RenderContext, Fill, FillRule, Raster, Style},
    scene::{
        facets::{FacetId, RasterFacet},
        scene::{Scene, SceneBuilder},
    },
    App, AppAssistant, Coord, Point, Rect, Size, ViewAssistant, ViewAssistantContext,
    ViewAssistantPtr, ViewKey,
};
use euclid::{default::Vector2D, point2, size2, vec2};
use fuchsia_trace::duration;
use fuchsia_zircon::Event;
use rand::{thread_rng, Rng};
use std::{collections::HashMap, mem};

fn make_bounds(context: &ViewAssistantContext) -> Rect {
    Rect::from_size(context.size)
}

const SHAPE_SIZE: Size = size2(60.0, 60.0);

#[derive(Default)]
struct ShapeDropAppAssistant;

impl AppAssistant for ShapeDropAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(ShapeDropViewAssistant::new()?))
    }
}

struct ShapeAnimator {
    location: Point,
    accel: Vector2D<f32>,
    velocity: Vector2D<f32>,
    running: bool,
    facet_id: FacetId,
}

impl ShapeAnimator {
    pub fn new(touch_handler: TouchHandler, location: Point) -> Self {
        Self {
            location: location,
            accel: vec2(0.0, 1.0),
            velocity: Vector2D::zero(),
            running: true,
            facet_id: touch_handler.facet_id,
        }
    }

    pub fn animate(&mut self, bounds: &Rect) {
        self.location += self.velocity;
        self.velocity += self.accel;
        if self.location.y > bounds.max_y() {
            self.running = false;
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
enum ShapeType {
    Rectangle,
    RoundedRectangle,
    Circle,
    Hexagon,
    Triangle,
    Octagon,
    LastShapeType,
}

#[derive(Debug)]
struct TouchHandler {
    location: Point,
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    shape_type: ShapeType,
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    color: Color,
    facet_id: FacetId,
}

fn random_color_element() -> u8 {
    let mut rng = thread_rng();
    let e: u8 = rng.gen_range(0, 128);
    e + 128
}

fn random_color() -> Color {
    Color {
        r: random_color_element(),
        g: random_color_element(),
        b: random_color_element(),
        a: 0xff,
    }
}

impl TouchHandler {
    pub fn new(shapes: &HashMap<ShapeType, Raster>, scene: &mut Scene) -> Self {
        let mut rng = thread_rng();
        let shape_type = match rng.gen_range(0, ShapeType::LastShapeType as usize) {
            0 => ShapeType::Rectangle,
            1 => ShapeType::RoundedRectangle,
            2 => ShapeType::Hexagon,
            3 => ShapeType::Triangle,
            4 => ShapeType::Octagon,
            _ => ShapeType::Circle,
        };
        let color = random_color();
        let raster = shapes.get(&shape_type).expect("shape");
        let raster_facet = RasterFacet::new(
            raster.clone(),
            Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(color),
                blend_mode: BlendMode::Over,
            },
            SHAPE_SIZE,
        );
        let facet_id = scene.add_facet(Box::new(raster_facet));
        Self { location: Point::zero(), color, shape_type, facet_id }
    }

    fn update(
        &mut self,
        context: &mut ViewAssistantContext,
        location: &IntPoint,
        scene: &mut Scene,
    ) {
        let touch_point = location.to_f32();
        let bounds = make_bounds(context);
        self.location =
            point2(touch_point.x, touch_point.y).clamp(bounds.origin, bounds.bottom_right());
        scene.set_facet_location(&self.facet_id, self.location);
    }
}

fn raster_for_rectangle(bounds: &Rect, render_context: &mut RenderContext) -> Raster {
    let mut raster_builder = render_context.raster_builder().expect("raster_builder");
    raster_builder.add(&path_for_rectangle(bounds, render_context), None);
    raster_builder.build()
}

fn raster_for_rounded_rectangle(
    bounds: &Rect,
    corner_radius: Coord,
    render_context: &mut RenderContext,
) -> Raster {
    let path = path_for_rounded_rectangle(bounds, corner_radius, render_context);
    let mut raster_builder = render_context.raster_builder().expect("raster_builder");
    raster_builder.add(&path, None);
    raster_builder.build()
}

fn raster_for_circle(center: Point, radius: Coord, render_context: &mut RenderContext) -> Raster {
    let path = path_for_circle(center, radius, render_context);
    let mut raster_builder = render_context.raster_builder().expect("raster_builder");
    raster_builder.add(&path, None);
    raster_builder.build()
}

fn raster_for_polygon(
    center: Point,
    radius: Coord,
    segment_count: usize,
    render_context: &mut RenderContext,
) -> Raster {
    let path = path_for_polygon(center, radius, segment_count, render_context);
    let mut raster_builder = render_context.raster_builder().expect("raster_builder");
    raster_builder.add(&path, None);
    raster_builder.build()
}

struct ShapeDropViewAssistant {
    touch_handlers: HashMap<input::pointer::PointerId, TouchHandler>,
    animators: Vec<ShapeAnimator>,
    background_color: Color,
    shapes: HashMap<ShapeType, Raster>,
    scene_details: Option<SceneDetails>,
}

impl ShapeDropViewAssistant {
    fn new() -> Result<Self, Error> {
        let background_color = Color::from_hash_code("#2F4F4F")?;
        Ok(Self {
            background_color,
            touch_handlers: HashMap::new(),
            animators: Vec::new(),
            shapes: HashMap::new(),
            scene_details: None,
        })
    }

    fn start_animating(
        &mut self,
        context: &mut ViewAssistantContext,
        location: &IntPoint,
        pointer_id: &input::pointer::PointerId,
    ) {
        if let Some(handler) = self.touch_handlers.remove(&pointer_id) {
            let bounds = make_bounds(context);
            let touch_point = location.to_f32();
            let location =
                point2(touch_point.x, touch_point.y).clamp(bounds.origin, bounds.bottom_right());
            let animator = ShapeAnimator::new(handler, point2(location.x, location.y));
            self.animators.push(animator);
        }
    }

    fn setup_shapes(&mut self, render_context: &mut RenderContext) {
        if self.shapes.is_empty() {
            let size = size2(60.0, 60.0);
            let origin = Point::zero() - size.to_vector() / 2.0;
            let shape_bounds = Rect::new(origin, size);
            let raster = raster_for_rectangle(&shape_bounds, render_context);
            self.shapes.insert(ShapeType::Rectangle, raster);
            let raster = raster_for_circle(Point::zero(), size.width / 2.0, render_context);
            self.shapes.insert(ShapeType::Circle, raster);
            let raster =
                raster_for_rounded_rectangle(&shape_bounds, size.width * 0.25, render_context);
            self.shapes.insert(ShapeType::RoundedRectangle, raster);
            let raster = raster_for_polygon(Point::zero(), size.width / 2.0, 6, render_context);
            self.shapes.insert(ShapeType::Hexagon, raster);
            let raster = raster_for_polygon(Point::zero(), size.width / 2.0, 3, render_context);
            self.shapes.insert(ShapeType::Triangle, raster);
            let raster = raster_for_polygon(Point::zero(), size.width / 2.0, 8, render_context);
            self.shapes.insert(ShapeType::Octagon, raster);
        }
    }
}

struct SceneDetails {
    scene: Scene,
}

impl ViewAssistant for ShapeDropViewAssistant {
    fn render(
        &mut self,
        render_context: &mut RenderContext,
        ready_event: Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        duration!("gfx", "ShapeDropViewAssistant::render");

        let background_color = self.background_color;

        let mut scene_details = self.scene_details.take().unwrap_or_else(|| {
            let builder = SceneBuilder::new().background_color(background_color);
            let scene = builder.build();
            SceneDetails { scene }
        });

        self.setup_shapes(render_context);

        let mut animators = Vec::new();
        mem::swap(&mut animators, &mut self.animators);
        let bounds = make_bounds(context);
        for animator in animators.iter_mut() {
            animator.animate(&bounds);
            scene_details.scene.set_facet_location(&animator.facet_id, animator.location);
        }

        let (mut running, not_running): (Vec<_>, Vec<_>) =
            animators.into_iter().partition(|animator| animator.running);

        for animator in not_running {
            scene_details.scene.remove_facet(animator.facet_id)?;
        }

        mem::swap(&mut running, &mut self.animators);

        scene_details.scene.render(render_context, ready_event, context)?;
        self.scene_details = Some(scene_details);

        if !self.animators.is_empty() {
            context.request_render();
        }

        Ok(())
    }

    fn handle_pointer_event(
        &mut self,
        context: &mut ViewAssistantContext,
        _event: &input::Event,
        pointer_event: &input::pointer::Event,
    ) -> Result<(), Error> {
        match &pointer_event.phase {
            input::pointer::Phase::Down(touch_location) => {
                let scene_details = self.scene_details.as_mut().expect("scene_details");
                let mut t = TouchHandler::new(&mut self.shapes, &mut scene_details.scene);
                t.update(context, &touch_location, &mut scene_details.scene);
                self.touch_handlers.insert(pointer_event.pointer_id.clone(), t);
                context.request_render();
            }
            input::pointer::Phase::Moved(touch_location) => {
                if let Some(handler) = self.touch_handlers.get_mut(&pointer_event.pointer_id) {
                    let scene_details = self.scene_details.as_mut().expect("scene_details");
                    handler.update(context, &touch_location, &mut scene_details.scene);
                }
                context.request_render();
            }
            input::pointer::Phase::Up => {
                let end_location =
                    if let Some(handler) = self.touch_handlers.get_mut(&pointer_event.pointer_id) {
                        handler.location.to_i32()
                    } else {
                        IntPoint::zero()
                    };
                self.start_animating(context, &end_location, &pointer_event.pointer_id);
                context.request_render();
            }
            _ => (),
        }
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    fuchsia_trace_provider::trace_provider_create_with_fdio();
    App::run(make_app_assistant::<ShapeDropAppAssistant>())
}
