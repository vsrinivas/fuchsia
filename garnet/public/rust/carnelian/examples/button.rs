// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use carnelian::{
    measure_text, App, AppAssistant, Canvas, Color, FontDescription, FontFace, Paint, Point, Rect,
    SharedBufferPixelSink, ViewAssistant, ViewAssistantContext, ViewAssistantPtr,
};
use failure::{Error, ResultExt};
use fidl_fuchsia_images as images;
use fidl_fuchsia_ui_gfx as gfx;
use fidl_fuchsia_ui_input::{InputEvent::Pointer, PointerEvent, PointerEventPhase};
use fuchsia_scenic::{EntityNode, HostImageCycler, Material, Rectangle, SessionPtr, ShapeNode};
use lazy_static::lazy_static;
use parking_lot::Mutex;
use std::{any::Any, cell::RefCell};

// This font creation method isn't ideal. The correct method would be to ask the Fuchsia
// font service for the font data.
static FONT_DATA: &'static [u8] =
    include_bytes!("../../../../bin/fonts/third_party/robotoslab/RobotoSlab-Regular.ttf");

lazy_static! {
    pub static ref FONT_FACE: FontFace<'static> =
        FontFace::new(&FONT_DATA).expect("Failed to create font");
}

const BASELINE: i32 = 0;

/// enum that defines all messages sent with `App::send_message` that
/// the button view assistant will understand and process.
pub enum ButtonMessages {
    Pressed,
}

// Utility routine to create the FontDescription structure that
// canvas wants for filling text.
fn make_font_description<'a, 'b>(size: u32, baseline: i32) -> FontDescription<'a, 'b> {
    FontDescription { face: &FONT_FACE, size: size, baseline: baseline }
}

// Utility routine to create the FontDescription structure that
// canvas wants for filling text.
fn set_node_color(session: &SessionPtr, node: &ShapeNode, color: &Color) {
    let material = Material::new(session.clone());
    material.set_color(color.make_color_rgba());
    node.set_material(&material);
}

struct ButtonAppAssistant {}

impl AppAssistant for ButtonAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, session: &SessionPtr) -> Result<ViewAssistantPtr, Error> {
        Ok(Mutex::new(RefCell::new(Box::new(ButtonViewAssistant::new(session)?))))
    }
}

struct Label {
    text: String,
    image_cycler: HostImageCycler,
}

impl Label {
    pub fn new(session: &SessionPtr, text: &str) -> Result<Label, Error> {
        Ok(Label { text: text.to_string(), image_cycler: HostImageCycler::new(session.clone()) })
    }

    pub fn update(&mut self, font_size: u32, paint: &Paint) -> Result<(), Error> {
        // Figure out the pixel dimension of the label
        let (w, h) = self.dimensions(font_size);

        // Fuchsia is uniformly 4 bytes per pixel but ideally this would
        // come from the graphics environment.
        let stride = w * 4;

        // Create a description of this pixel buffer that
        // Scenic can understand.
        let info = images::ImageInfo {
            transform: images::Transform::Normal,
            width: w,
            height: h,
            stride: stride,
            pixel_format: images::PixelFormat::Bgra8,
            color_space: images::ColorSpace::Srgb,
            tiling: images::Tiling::Linear,
            alpha_format: images::AlphaFormat::Opaque,
        };

        // Grab an image buffer from the cycler
        let guard = self.image_cycler.acquire(info).context("failed to allocate buffer")?;

        // Create a canvas to render into the buffer
        let mut canvas = Canvas::<SharedBufferPixelSink>::new(guard.image().buffer(), stride);

        // since the label buffer is sized to fit the text, allways draw at 0,0
        let location = Point { x: 0, y: 0 };

        // fill the buffer with the bg color, since fill_text ignore pixels that
        // aren't covered by glyphs
        let bounds = Rect { left: 0, top: 0, right: w, bottom: h };
        canvas.fill_rect(&bounds, paint.bg);

        // Fill the text
        canvas.fill_text(
            &self.text,
            location,
            &mut make_font_description(font_size, BASELINE),
            &paint,
        );

        Ok(())
    }

    // Use canvas's measure_text() to find the pixel size of the label
    pub fn dimensions(&self, font_size: u32) -> (u32, u32) {
        let mut font_description = make_font_description(font_size, BASELINE);
        let (w, h) = measure_text(&self.text, &mut font_description);
        (w as u32, h as u32)
    }

    // Expose the image cycler's node so that it can be added to the
    // button's container
    pub fn node(&mut self) -> &EntityNode {
        self.image_cycler.node()
    }
}

struct Button {
    label: Label,
    background_node: ShapeNode,
    container: EntityNode,
    bounds: Rect,
    bg_color: Color,
    bg_color_active: Color,
    fg_color: Color,
    tracking: bool,
    active: bool,
}

// Soon this sort of thing will be replaced with function from the
// Euclid crate.
fn is_point_in_rect(x: f32, y: f32, r: &Rect) -> bool {
    x > r.left as f32 && x < r.right as f32 && y > r.top as f32 && y < r.bottom as f32
}

impl Button {
    pub fn new(session: &SessionPtr, text: &str) -> Result<Button, Error> {
        let mut button = Button {
            label: Label::new(session, text)?,
            background_node: ShapeNode::new(session.clone()),
            container: EntityNode::new(session.clone()),
            bounds: Default::default(),
            fg_color: Color::white(),
            bg_color: Color::from_hash_code("#404040")?,
            bg_color_active: Color::from_hash_code("#808080")?,
            tracking: false,
            active: false,
        };

        // set up the button background
        button.container.add_child(&button.background_node);
        set_node_color(session, &button.background_node, &button.bg_color);

        // Add the label
        button.container.add_child(button.label.node());
        Ok(button)
    }

    pub fn update(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        // set up paint with different backgrounds depending on whether the button
        // is active. The active state is true when a pointer has gone down in the
        // button's bounds and the pointer has not moved outside the bounds since.
        let paint = Paint {
            fg: self.fg_color,
            bg: if self.active { self.bg_color_active } else { self.bg_color },
        };

        // center the button in the Scenic view by translating the
        // container node. All child nodes will be positioned relative
        // to this container
        let center_x = context.width * 0.5;
        let center_y = context.height * 0.5;

        // pick font size and padding based on the available space
        let min_dimension = context.width.min(context.height);
        let font_size = (min_dimension / 5.0).ceil().min(64.0) as u32;
        let padding = (min_dimension / 20.0).ceil().max(8.0);
        self.container.set_translation(center_x, center_y, 0.0);

        set_node_color(context.session, &self.background_node, &paint.bg);

        // calculate button size based on label's text size
        // plus padding.
        let (w, h) = self.label.dimensions(font_size);
        let button_w = w as f32 + 2.0 * padding;
        let button_h = h as f32 + 2.0 * padding;

        // record bounds for hit testing
        self.bounds = Rect {
            top: (center_y - button_h / 2.0) as u32,
            bottom: (center_y + button_h / 2.0) as u32,
            left: (center_x - button_w / 2.0) as u32,
            right: (center_x + button_w / 2.0) as u32,
        };

        self.background_node.set_shape(&Rectangle::new(
            context.session.clone(),
            button_w,
            button_h,
        ));

        self.label.update(font_size, &paint)?;

        Ok(())
    }

    pub fn node(&mut self) -> &EntityNode {
        &self.container
    }

    pub fn handle_pointer_event(
        &mut self,
        context: &mut ViewAssistantContext,
        pointer_event: &PointerEvent,
    ) {
        // TODO: extend this to support multiple pointers
        match pointer_event.phase {
            PointerEventPhase::Down => {
                self.active = is_point_in_rect(pointer_event.x, pointer_event.y, &self.bounds);
                self.tracking = self.active;
            }
            PointerEventPhase::Add => {}
            PointerEventPhase::Hover => {}
            PointerEventPhase::Move => {
                if self.tracking {
                    self.active = is_point_in_rect(pointer_event.x, pointer_event.y, &self.bounds);
                }
            }
            PointerEventPhase::Up => {
                if self.active {
                    context.queue_message(&ButtonMessages::Pressed);
                }
                self.tracking = false;
                self.active = false;
            }
            PointerEventPhase::Remove => {
                self.active = false;
                self.tracking = false;
            }
            PointerEventPhase::Cancel => {
                self.active = false;
                self.tracking = false;
            }
        }
    }
}

struct ButtonViewAssistant {
    background_node: ShapeNode,
    indicator: ShapeNode,
    button: Button,
    red_light: bool,
}

impl ButtonViewAssistant {
    fn new(session: &SessionPtr) -> Result<ButtonViewAssistant, Error> {
        Ok(ButtonViewAssistant {
            background_node: ShapeNode::new(session.clone()),
            indicator: ShapeNode::new(session.clone()),
            button: Button::new(&session, "Touch Me")?,
            red_light: false,
        })
    }

    fn handle_pointer_event(
        &mut self,
        context: &mut ViewAssistantContext,
        pointer_event: &PointerEvent,
    ) {
        self.button.handle_pointer_event(context, pointer_event);
    }
}

impl ViewAssistant for ButtonViewAssistant {
    // Called once by Carnelian when the view is first created. Good for setup
    // that isn't concerned with the size of the view.
    fn setup(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        // Let scenic know we're interested in changes to metrics for this node.
        context.import_node.resource().set_event_mask(gfx::METRICS_EVENT_MASK);

        context.import_node.add_child(&self.background_node);

        set_node_color(context.session, &self.background_node, &Color::from_hash_code("#b7410e")?);

        context.import_node.add_child(&self.indicator);

        context.import_node.add_child(self.button.node());

        Ok(())
    }

    // Called  by Carnelian when the view is resized, after input events are processed
    // or if sent an explicit Update message.
    fn update(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        // Position and size the background
        let center_x = context.width * 0.5;
        let center_y = context.height * 0.5;
        self.background_node.set_shape(&Rectangle::new(
            context.session.clone(),
            context.width,
            context.height,
        ));
        self.background_node.set_translation(center_x, center_y, 0.0);

        // Position and size the indicator
        let indicator_y = context.height / 5.0;
        let indicator_size = context.height.min(context.width) / 8.0;
        self.indicator.set_shape(&Rectangle::new(
            context.session.clone(),
            indicator_size,
            indicator_size,
        ));
        self.indicator.set_translation(center_x, indicator_y, 5.0);

        let indicator_color = if self.red_light {
            Color::from_hash_code("#ff0000")?
        } else {
            Color::from_hash_code("#00ff00")?
        };

        set_node_color(context.session, &self.indicator, &indicator_color);

        // Update and position the button
        self.button.update(context)?;
        self.button.node().set_translation(center_x, center_y, 0.0);

        Ok(())
    }

    fn handle_message(&mut self, _: &dyn Any) {
        // TODO: downcast the message to ensure that it is
        // ButtonMessages::Pressed.
        self.red_light = !self.red_light;
    }

    fn handle_input_event(
        &mut self,
        context: &mut ViewAssistantContext,
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
    let assistant = ButtonAppAssistant {};
    App::run(Box::new(assistant))
}
