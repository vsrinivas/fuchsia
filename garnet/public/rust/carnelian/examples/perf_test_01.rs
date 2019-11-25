// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use carnelian::{
    set_node_color, AnimationMode, App, AppAssistant, Color, Coord, Point, Rect, Size,
    ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey,
};
use euclid::Vector2D;
use failure::Error;
use fidl_fuchsia_ui_input::{InputEvent::Pointer, PointerEvent, PointerEventPhase};
use fuchsia_scenic::{Circle, Rectangle, RoundedRectangle, SessionPtr, ShapeNode};
use rand::{thread_rng, Rng};
use std::collections::BTreeMap;

const SHAPE_Z: Coord = -10.0;

fn make_bounds(context: &ViewAssistantContext<'_>) -> Rect {
    Rect::new(Point::zero(), context.size)
}

struct ShapeDropAppAssistant;

impl AppAssistant for ShapeDropAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(
        &mut self,
        _: ViewKey,
        session: &SessionPtr,
    ) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(ShapeDropViewAssistant::new(session)?))
    }
}

struct ShapeAnimator {
    shape: ShapeNode,
    location: Point,
    accel: Vector2D<f32>,
    velocity: Vector2D<f32>,
    running: bool,
}

impl ShapeAnimator {
    pub fn new(touch_handler: TouchHandler, location: Point) -> ShapeAnimator {
        ShapeAnimator {
            shape: touch_handler.shape,
            location: location,
            accel: Vector2D::new(0.0, 0.2),
            velocity: Vector2D::zero(),
            running: true,
        }
    }

    pub fn animate(&mut self, bounds: &Rect) {
        self.location += self.velocity;
        self.velocity += self.accel;
        self.shape.set_translation(self.location.x, self.location.y, SHAPE_Z);
        if self.location.y > bounds.max_y() {
            self.running = false;
            self.shape.detach();
        }
    }
}

enum ShapeType {
    Rectangle,
    RoundedRectangle,
    Circle,
}

struct TouchHandler {
    shape: ShapeNode,
    size: Size,
    shape_type: ShapeType,
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
    pub fn new(context: &mut ViewAssistantContext<'_>) -> TouchHandler {
        let mut rng = thread_rng();
        let shape_type = match rng.gen_range(0, 3) {
            0 => ShapeType::Rectangle,
            1 => ShapeType::RoundedRectangle,
            _ => ShapeType::Circle,
        };
        let mut t = TouchHandler {
            shape: ShapeNode::new(context.session().clone()),
            size: Size::new(60.0, 60.0),
            shape_type,
        };
        t.setup(context);
        t
    }

    fn setup(&mut self, context: &mut ViewAssistantContext<'_>) {
        set_node_color(context.session(), &self.shape, &random_color());
        match self.shape_type {
            ShapeType::Rectangle => {
                self.shape.set_shape(&Rectangle::new(
                    context.session().clone(),
                    self.size.width,
                    self.size.height,
                ));
            }

            ShapeType::RoundedRectangle => {
                let corner_radius = (self.size.width / 8.0).ceil();
                self.shape.set_shape(&RoundedRectangle::new(
                    context.session().clone(),
                    self.size.width,
                    self.size.height,
                    corner_radius,
                    corner_radius,
                    corner_radius,
                    corner_radius,
                ));
            }

            ShapeType::Circle => {
                self.shape
                    .set_shape(&Circle::new(context.session().clone(), self.size.width / 2.0));
            }
        }
        context.root_node().add_child(&self.shape);
    }

    fn update(&mut self, context: &mut ViewAssistantContext<'_>, pointer_event: &PointerEvent) {
        let bounds = make_bounds(context);
        let location = Point::new(pointer_event.x, pointer_event.y)
            .clamp(bounds.origin, bounds.bottom_right());
        self.shape.set_translation(location.x, location.y, SHAPE_Z);
    }
}

struct ShapeDropViewAssistant {
    background_node: ShapeNode,
    touch_handlers: BTreeMap<(u32, u32), TouchHandler>,
    animators: Vec<ShapeAnimator>,
}

fn make_pointer_event_key(pointer_event: &PointerEvent) -> (u32, u32) {
    (pointer_event.device_id, pointer_event.pointer_id)
}

impl ShapeDropViewAssistant {
    fn new(session: &SessionPtr) -> Result<ShapeDropViewAssistant, Error> {
        Ok(ShapeDropViewAssistant {
            background_node: ShapeNode::new(session.clone()),
            touch_handlers: BTreeMap::new(),
            animators: Vec::new(),
        })
    }

    fn start_animating(
        &mut self,
        context: &mut ViewAssistantContext<'_>,
        pointer_event: &PointerEvent,
    ) {
        if let Some(handler) = self.touch_handlers.remove(&make_pointer_event_key(pointer_event)) {
            let bounds = make_bounds(context);
            let location = Point::new(pointer_event.x, pointer_event.y)
                .clamp(bounds.origin, bounds.bottom_right());
            let animator = ShapeAnimator::new(handler, Point::new(location.x, location.y));
            self.animators.push(animator);
        }
    }

    fn handle_pointer_event(
        &mut self,
        context: &mut ViewAssistantContext<'_>,
        pointer_event: &PointerEvent,
    ) {
        match pointer_event.phase {
            PointerEventPhase::Down => {
                let mut t = TouchHandler::new(context);
                t.update(context, pointer_event);
                self.touch_handlers.insert(make_pointer_event_key(pointer_event), t);
            }
            PointerEventPhase::Add => {}
            PointerEventPhase::Hover => {}
            PointerEventPhase::Move => {
                if let Some(handler) =
                    self.touch_handlers.get_mut(&make_pointer_event_key(pointer_event))
                {
                    handler.update(context, pointer_event);
                }
            }
            PointerEventPhase::Up => {
                self.start_animating(context, pointer_event);
            }
            PointerEventPhase::Remove => {
                self.start_animating(context, pointer_event);
            }
            PointerEventPhase::Cancel => {
                self.start_animating(context, pointer_event);
            }
        }
    }
}

impl ViewAssistant for ShapeDropViewAssistant {
    fn setup(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        context.root_node().add_child(&self.background_node);

        set_node_color(
            context.session(),
            &self.background_node,
            &Color::from_hash_code("#2F4F4F")?,
        );

        Ok(())
    }

    fn update(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        let center_x = context.size.width * 0.5;
        let center_y = context.size.height * 0.5;
        self.background_node.set_shape(&Rectangle::new(
            context.session().clone(),
            context.size.width,
            context.size.height,
        ));
        self.background_node.set_translation(center_x, center_y, 0.0);
        let bounds = make_bounds(context);
        for animator in &mut self.animators {
            animator.animate(&bounds);
        }
        self.animators.retain(|animator| animator.running);
        Ok(())
    }

    fn initial_animation_mode(&mut self) -> AnimationMode {
        return AnimationMode::EveryFrame;
    }

    fn handle_input_event(
        &mut self,
        context: &mut ViewAssistantContext<'_>,
        event: &fidl_fuchsia_ui_input::InputEvent,
    ) -> Result<(), Error> {
        match event {
            Pointer(pointer_event) => {
                self.handle_pointer_event(context, &pointer_event);
            }
            _ => (),
        }
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    let assistant = ShapeDropAppAssistant {};
    App::run(Box::new(assistant))
}
