// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use carnelian::{
    make_app_assistant, set_node_color, AnimationMode, App, AppAssistant, Color, Label, Paint,
    Point, Rect, ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey,
};
use euclid::Vector2D;
use fuchsia_scenic::{EntityNode, Rectangle, SessionPtr, ShapeNode};

fn make_bounds(context: &ViewAssistantContext<'_>) -> Rect {
    Rect::new(Point::zero(), context.size)
}

#[derive(Default)]
struct TextScrollAppAssistant;

const BACKGROUND_Z: f32 = 0.0;
const TEXT_Z: f32 = BACKGROUND_Z - 5.0;

impl AppAssistant for TextScrollAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(
        &mut self,
        _: ViewKey,
        session: &SessionPtr,
    ) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(TextScrollViewAssistant::new(session)?))
    }
}

struct TextLineAnimator {
    label: Label,
    location: Point,
    velocity: Vector2D<f32>,
    running: bool,
}

impl TextLineAnimator {
    pub fn new(location: Point, label: Label) -> TextLineAnimator {
        TextLineAnimator { label, location, velocity: Vector2D::new(0.0, -0.5), running: true }
    }

    pub fn animate(&mut self, bounds: &Rect) {
        self.location += self.velocity;
        self.label.node().set_translation(bounds.size.width / 2.0, self.location.y, TEXT_Z);

        const ARBITRARILY_CHOSE_TOP_Y_LIMIT: f32 = 20.0;
        if self.location.y < bounds.origin.y + ARBITRARILY_CHOSE_TOP_Y_LIMIT {
            self.running = false;
            self.label.node().detach();
        }
    }
}

struct TextScrollViewAssistant {
    background_node: ShapeNode,
    container: EntityNode,
    animators: Vec<TextLineAnimator>,
    bg_color: Color,
    fg_color: Color,
}

impl TextScrollViewAssistant {
    fn new(session: &SessionPtr) -> Result<TextScrollViewAssistant, Error> {
        Ok(TextScrollViewAssistant {
            background_node: ShapeNode::new(session.clone()),
            container: EntityNode::new(session.clone()),
            animators: Vec::new(),
            fg_color: Color::from_hash_code("#A9A9A9")?,
            bg_color: Color::from_hash_code("#0000FF")?,
        })
    }
}

const LINE_FONT_SIZE: u32 = 32;
const STATES: &[&str] = &[
    "Alabama",
    "Alaska",
    "Arizona",
    "Arkansas",
    "California",
    "Colorado",
    "Connecticut",
    "Delaware",
    "Florida",
    "Georgia",
    "Hawaii",
    "Idaho",
    "Illinois",
    "Indiana",
    "Iowa",
    "Kansas",
    "Kentucky",
    "Louisiana",
    "Maine",
    "Maryland",
    "Massachusetts",
    "Michigan",
    "Minnesota",
    "Mississippi",
    "Missouri",
    "Montana",
    "Nebraska",
    "Nevada",
    "New Hampshire",
    "New Jersey",
    "New Mexico",
    "New York",
    "North Carolina",
    "North Dakota",
    "Ohio",
    "Oklahoma",
    "Oregon",
    "Pennsylvania",
    "Rhode Island",
    "South Carolina",
    "South Dakota",
    "Tennessee",
    "Texas",
    "Utah",
    "Vermont",
    "Virginia",
    "Washington",
    "West Virginia",
    "Wisconsin",
    "Wyoming",
];

impl ViewAssistant for TextScrollViewAssistant {
    fn setup(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        context.root_node().add_child(&self.container);
        self.container.add_child(&self.background_node);
        set_node_color(context.session(), &self.background_node, &self.bg_color);

        const AESTHETICALLY_PLEASING_INITAL_Y: f32 = 10.0;
        let mut y = AESTHETICALLY_PLEASING_INITAL_Y;
        for line in STATES.iter() {
            if line.len() > 0 {
                let label = Label::new(context.session(), line)?;
                let size = label.dimensions(LINE_FONT_SIZE);
                y += size.height;
                const AESTHETICALLY_PLEASING_LINE_PADDING: f32 = 4.0;
                y += AESTHETICALLY_PLEASING_LINE_PADDING;
                let mut text_animator =
                    TextLineAnimator::new(Point::new(context.size.width / 2.0, y), label);
                let paint = Paint { fg: self.fg_color, bg: self.bg_color };
                self.container.add_child(text_animator.label.node());
                text_animator.label.update(32, &paint).expect("label update failed");
                self.animators.push(text_animator);
            }
        }

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
        Ok(())
    }

    fn initial_animation_mode(&mut self) -> AnimationMode {
        return AnimationMode::EveryFrame;
    }
}

fn main() -> Result<(), Error> {
    App::run(make_app_assistant::<TextScrollAppAssistant>())
}
