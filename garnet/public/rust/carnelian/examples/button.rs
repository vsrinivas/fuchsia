// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use carnelian::{
    make_app_assistant, make_message, set_node_color, App, AppAssistant, Color, Label, Message,
    Paint, Point, Rect, Size, ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey,
    ViewMessages,
};
use fidl_fuchsia_ui_input::{FocusEvent, PointerEvent, PointerEventPhase};
use fuchsia_scenic::{EntityNode, Rectangle, SessionPtr, ShapeNode};
use fuchsia_zircon::{ClockId, Time};

const BACKGROUND_Z: f32 = 0.0;
const INDICATOR_Z: f32 = BACKGROUND_Z - 0.01;
const BUTTON_Z: f32 = BACKGROUND_Z - 0.01;
const BUTTON_BACKGROUND_Z: f32 = BUTTON_Z - 0.01;
const BUTTON_LABEL_Z: f32 = BUTTON_BACKGROUND_Z - 0.01;

/// enum that defines all messages sent with `App::queue_message` that
/// the button view assistant will understand and process.
pub enum ButtonMessages {
    Pressed(Time),
}

#[derive(Default)]
struct ButtonAppAssistant;

impl AppAssistant for ButtonAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(
        &mut self,
        _: ViewKey,
        session: &SessionPtr,
    ) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(ButtonViewAssistant::new(session)?))
    }
}

struct Button {
    label: Label,
    background_node: ShapeNode,
    container: EntityNode,
    bounds: Rect,
    bg_color: Color,
    bg_color_active: Color,
    bg_color_disabled: Color,
    fg_color: Color,
    fg_color_disabled: Color,
    tracking: bool,
    active: bool,
    focused: bool,
}

impl Button {
    pub fn new(session: &SessionPtr, text: &str) -> Result<Button, Error> {
        let mut button = Button {
            label: Label::new(session, text)?,
            background_node: ShapeNode::new(session.clone()),
            container: EntityNode::new(session.clone()),
            bounds: Rect::zero(),
            fg_color: Color::white(),
            bg_color: Color::from_hash_code("#B7410E")?,
            bg_color_active: Color::from_hash_code("#f0703c")?,
            fg_color_disabled: Color::from_hash_code("#A0A0A0")?,
            bg_color_disabled: Color::from_hash_code("#C0C0C0")?,
            tracking: false,
            active: false,
            focused: false,
        };

        // set up the button background
        button.container.add_child(&button.background_node);
        set_node_color(session, &button.background_node, &button.bg_color);

        // Add the label
        button.container.add_child(button.label.node());
        Ok(button)
    }

    pub fn set_focused(&mut self, focused: bool) {
        self.focused = focused;
        if !focused {
            self.active = false;
            self.tracking = false;
        }
    }

    pub fn update(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        // set up paint with different backgrounds depending on whether the button
        // is active. The active state is true when a pointer has gone down in the
        // button's bounds and the pointer has not moved outside the bounds since.
        let paint = if self.focused {
            Paint {
                fg: self.fg_color,
                bg: if self.active { self.bg_color_active } else { self.bg_color },
            }
        } else {
            Paint { fg: self.fg_color_disabled, bg: self.bg_color_disabled }
        };

        // center the button in the Scenic view by translating the
        // container node. All child nodes will be positioned relative
        // to this container
        let center_x = context.size.width * 0.5;
        let center_y = context.size.height * 0.5;

        // pick font size and padding based on the available space
        let min_dimension = context.size.width.min(context.size.height);
        let font_size = (min_dimension / 5.0).ceil().min(64.0) as u32;
        let padding = (min_dimension / 20.0).ceil().max(8.0);
        self.container.set_translation(center_x, center_y, BUTTON_Z);

        set_node_color(context.session(), &self.background_node, &paint.bg);

        // calculate button size based on label's text size
        // plus padding.
        let button_size = self.label.dimensions(font_size);
        let button_w = button_size.width + 2.0 * padding;
        let button_h = button_size.height + 2.0 * padding;

        // record bounds for hit testing
        self.bounds = Rect::new(
            Point::new(center_x - button_w / 2.0, center_y - button_h / 2.0),
            Size::new(button_w, button_h),
        )
        .round_out();

        self.background_node.set_shape(&Rectangle::new(
            context.session().clone(),
            self.bounds.size.width,
            self.bounds.size.height,
        ));
        self.background_node.set_translation(0.0, 0.0, BUTTON_BACKGROUND_Z);

        self.label.update(font_size, &paint)?;
        self.label.node().set_translation(0.0, 0.0, BUTTON_LABEL_Z);

        Ok(())
    }

    pub fn node(&mut self) -> &EntityNode {
        &self.container
    }

    pub fn handle_pointer_event(
        &mut self,
        context: &mut ViewAssistantContext<'_>,
        pointer_event: &PointerEvent,
    ) {
        if !self.focused {
            return;
        }
        // TODO: extend this to support multiple pointers
        match pointer_event.phase {
            PointerEventPhase::Down => {
                self.active = self.bounds.contains(&Point::new(pointer_event.x, pointer_event.y));
                self.tracking = self.active;
            }
            PointerEventPhase::Add => {}
            PointerEventPhase::Hover => {}
            PointerEventPhase::Move => {
                if self.tracking {
                    self.active =
                        self.bounds.contains(&Point::new(pointer_event.x, pointer_event.y));
                }
            }
            PointerEventPhase::Up => {
                if self.active {
                    context.queue_message(make_message(ButtonMessages::Pressed(Time::get(
                        ClockId::Monotonic,
                    ))));
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
}

impl ViewAssistant for ButtonViewAssistant {
    // Called once by Carnelian when the view is first created. Good for setup
    // that isn't concerned with the size of the view.
    fn setup(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        set_node_color(
            context.session(),
            &self.background_node,
            &Color::from_hash_code("#EBD5B3")?,
        );
        context.root_node().add_child(&self.background_node);
        context.root_node().add_child(&self.indicator);
        context.root_node().add_child(self.button.node());

        Ok(())
    }

    // Called  by Carnelian when the view is resized, after input events are processed
    // or if sent an explicit Update message.
    fn update(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        // Position and size the background
        let center_x = context.size.width * 0.5;
        let center_y = context.size.height * 0.5;
        self.background_node.set_shape(&Rectangle::new(
            context.session().clone(),
            context.size.width,
            context.size.height,
        ));
        self.background_node.set_translation(center_x, center_y, BACKGROUND_Z);

        // Position and size the indicator
        let indicator_y = context.size.height / 5.0;
        let indicator_size = context.size.height.min(context.size.width) / 8.0;
        self.indicator.set_shape(&Rectangle::new(
            context.session().clone(),
            indicator_size,
            indicator_size,
        ));
        self.indicator.set_translation(center_x, indicator_y, INDICATOR_Z);

        let indicator_color = if self.red_light {
            Color::from_hash_code("#ff0000")?
        } else {
            Color::from_hash_code("#00ff00")?
        };

        set_node_color(context.session(), &self.indicator, &indicator_color);

        // Update and position the button
        self.button.update(context)?;
        self.button.node().set_translation(center_x, center_y, BUTTON_Z);

        Ok(())
    }

    fn handle_message(&mut self, message: Message) {
        if let Some(button_message) = message.downcast_ref::<ButtonMessages>() {
            match button_message {
                ButtonMessages::Pressed(value) => {
                    println!("value = {:#?}", value);
                    self.red_light = !self.red_light
                }
            }
        }
    }

    fn handle_pointer_event(
        &mut self,
        context: &mut ViewAssistantContext<'_>,
        pointer_event: &PointerEvent,
    ) -> Result<(), Error> {
        self.button.handle_pointer_event(context, pointer_event);
        context.queue_message(make_message(ViewMessages::Update));
        Ok(())
    }

    fn handle_focus_event(
        &mut self,
        context: &mut ViewAssistantContext<'_>,
        focus_event: &FocusEvent,
    ) -> Result<(), Error> {
        self.button.set_focused(focus_event.focused);
        context.queue_message(make_message(ViewMessages::Update));
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    App::run(make_app_assistant::<ButtonAppAssistant>())
}
