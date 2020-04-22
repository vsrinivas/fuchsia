// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use carnelian::{
    color::Color,
    drawing::{path_for_rectangle, GlyphMap, Paint, Text},
    input::{self},
    make_app_assistant, make_font_description, make_message,
    render::{
        BlendMode, Composition, Context as RenderContext, Fill, FillRule, Layer, PreClear, Raster,
        RenderExt, Style,
    },
    App, AppAssistant, Message, Point, Rect, RenderOptions, Size, ViewAssistant,
    ViewAssistantContext, ViewAssistantPtr, ViewKey, ViewMessages, ViewMode,
};
use fuchsia_zircon::{AsHandleRef, ClockId, Event, Signals, Time};

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

    fn create_view_assistant_render(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(ButtonViewAssistant::new()?))
    }

    fn get_mode(&self) -> ViewMode {
        ViewMode::Render(RenderOptions::default())
    }
}

fn raster_for_rectangle(bounds: &Rect, render_context: &mut RenderContext) -> Raster {
    let mut raster_builder = render_context.raster_builder().expect("raster_builder");
    raster_builder.add(&path_for_rectangle(bounds, render_context), None);
    raster_builder.build()
}

struct Button {
    bounds: Rect,
    bg_color: Color,
    bg_color_active: Color,
    bg_color_disabled: Color,
    fg_color: Color,
    fg_color_disabled: Color,
    tracking_pointer: Option<input::pointer::PointerId>,
    active: bool,
    focused: bool,
    glyphs: GlyphMap,
    label_text: String,
    label: Option<Text>,
}

impl Button {
    pub fn new(text: &str) -> Result<Button, Error> {
        let button = Button {
            bounds: Rect::zero(),
            fg_color: Color::white(),
            bg_color: Color::from_hash_code("#B7410E")?,
            bg_color_active: Color::from_hash_code("#f0703c")?,
            fg_color_disabled: Color::from_hash_code("#A0A0A0")?,
            bg_color_disabled: Color::from_hash_code("#C0C0C0")?,
            tracking_pointer: None,
            active: false,
            focused: false,
            glyphs: GlyphMap::new(),
            label_text: text.to_string(),
            label: None,
        };

        Ok(button)
    }

    pub fn set_focused(&mut self, focused: bool) {
        self.focused = focused;
        if !focused {
            self.active = false;
            self.tracking_pointer = None;
        }
    }

    fn render(
        &mut self,
        render_context: &mut RenderContext,
        context: &ViewAssistantContext<'_>,
    ) -> Result<(Layer, Layer), Error> {
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

        let font_description = make_font_description(font_size, 0);
        self.label = Some(Text::new(
            render_context,
            &self.label_text,
            font_size as f32,
            100,
            font_description.face,
            &mut self.glyphs,
        ));

        let label = self.label.as_ref().expect("label");

        // calculate button size based on label's text size
        // plus padding.
        let button_size = label.bounding_box.size;
        let button_w = button_size.width + 2.0 * padding;
        let button_h = button_size.height + 2.0 * padding;

        // record bounds for hit testing
        self.bounds = Rect::new(
            Point::new(center_x - button_w / 2.0, center_y - button_h / 2.0),
            Size::new(button_w, button_h),
        )
        .round_out();

        let center = self.bounds.center();

        let label_offet =
            (center - (label.bounding_box.size / 2.0).to_vector()).to_vector().to_i32();

        let raster = raster_for_rectangle(&self.bounds, render_context);
        let button_layer = Layer {
            raster,
            style: Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(paint.bg),
                blend_mode: BlendMode::Over,
            },
        };
        let label_layer = Layer {
            raster: label.raster.clone().translate(label_offet),
            style: Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(paint.fg),
                blend_mode: BlendMode::Over,
            },
        };
        Ok((button_layer, label_layer))
    }

    pub fn handle_pointer_event(
        &mut self,
        context: &mut ViewAssistantContext<'_>,
        pointer_event: &input::pointer::Event,
    ) {
        if !self.focused {
            return;
        }

        if self.tracking_pointer.is_none() {
            match pointer_event.phase {
                input::pointer::Phase::Down(location) => {
                    self.active = self.bounds.contains(location.to_f32());
                    if self.active {
                        self.tracking_pointer = Some(pointer_event.pointer_id.clone());
                    }
                }
                _ => (),
            }
        } else {
            let tracking_pointer = self.tracking_pointer.as_ref().expect("tracking_pointer");
            if tracking_pointer == &pointer_event.pointer_id {
                match pointer_event.phase {
                    input::pointer::Phase::Moved(location) => {
                        self.active = self.bounds.contains(location.to_f32());
                    }
                    input::pointer::Phase::Up => {
                        if self.active {
                            context.queue_message(make_message(ButtonMessages::Pressed(
                                Time::get(ClockId::Monotonic),
                            )));
                        }
                        self.tracking_pointer = None;
                        self.active = false;
                    }
                    input::pointer::Phase::Remove => {
                        self.active = false;
                        self.tracking_pointer = None;
                    }
                    input::pointer::Phase::Cancel => {
                        self.active = false;
                        self.tracking_pointer = None;
                    }
                    _ => (),
                }
            }
        }
    }
}

struct ButtonViewAssistant {
    bg_color: Color,
    button: Button,
    red_light: bool,
    composition: Composition,
}

impl ButtonViewAssistant {
    fn new() -> Result<ButtonViewAssistant, Error> {
        let bg_color = Color::from_hash_code("#EBD5B3")?;
        let composition = Composition::new(bg_color);
        Ok(ButtonViewAssistant {
            bg_color,
            button: Button::new("Touch Me")?,
            red_light: false,
            composition,
        })
    }
}

impl ViewAssistant for ButtonViewAssistant {
    fn render(
        &mut self,
        render_context: &mut RenderContext,
        ready_event: Event,
        context: &ViewAssistantContext<'_>,
    ) -> Result<(), Error> {
        // Position and size the background
        let center_x = context.size.width * 0.5;
        let _center_y = context.size.height * 0.5;

        // Position and size the indicator
        let indicator_y = context.size.height / 5.0;
        let indicator_size = context.size.height.min(context.size.width) / 8.0;
        let indicator_bounds = Rect::new(
            Point::new(center_x - indicator_size / 2.0, indicator_y - indicator_size / 2.0),
            Size::new(indicator_size, indicator_size),
        );

        let indicator_color = if self.red_light {
            Color::from_hash_code("#ff0000")?
        } else {
            Color::from_hash_code("#00ff00")?
        };

        let indicator_layer = Layer {
            raster: raster_for_rectangle(&indicator_bounds, render_context),
            style: Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(indicator_color),
                blend_mode: BlendMode::Over,
            },
        };

        // Update and position the button
        let (button_layer, label_layer) = self.button.render(render_context, context)?;
        self.composition.replace(
            ..,
            std::iter::once(label_layer)
                .chain(std::iter::once(button_layer))
                .chain(std::iter::once(indicator_layer)),
        );

        let image = render_context.get_current_image(context);
        let ext =
            RenderExt { pre_clear: Some(PreClear { color: self.bg_color }), ..Default::default() };
        render_context.render(&self.composition, None, image, &ext);
        ready_event.as_handle_ref().signal(Signals::NONE, Signals::EVENT_SIGNALED)?;

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
        _event: &input::Event,
        pointer_event: &input::pointer::Event,
    ) -> Result<(), Error> {
        self.button.handle_pointer_event(context, pointer_event);
        context.queue_message(make_message(ViewMessages::Update));
        Ok(())
    }

    fn handle_focus_event(&mut self, focused: bool) -> Result<(), Error> {
        self.button.set_focused(focused);
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    App::run(make_app_assistant::<ButtonAppAssistant>())
}
