// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{constants::constants::*, font};
use carnelian::{
    color::Color,
    drawing::measure_text_width,
    input::{self},
    make_message,
    scene::{
        facets::{Facet, FacetId, RiveFacet, SetColorMessage, TextFacetOptions},
        facets::{TextHorizontalAlignment, TextVerticalAlignment},
        layout::{Alignment, CrossAxisAlignment, Flex, FlexOptions},
        layout::{MainAxisAlignment, MainAxisSize, Stack, StackOptions},
        scene::{Scene, SceneBuilder},
    },
    Coord, Point, Size, ViewAssistantContext,
};
use derivative::Derivative;
use euclid::{size2, Size2D, UnknownUnit};
use fuchsia_zircon::Time;
use std::ops::Add;

/// enum that defines all messages sent with `App::queue_message` that
/// the button view assistant will understand and process.
pub enum ButtonMessages {
    Pressed(Time, String),
}

// These values and the text and padding sizes are used to calculate the corners.
#[derive(Debug, Clone)]
#[allow(unused)]
pub enum ButtonShape {
    Square = 0,
    Rounded = 15,
    Oval = 50,
}

impl Default for ButtonShape {
    fn default() -> Self {
        ButtonShape::Square
    }
}

#[derive(Debug, Derivative)]
#[derivative(Default)]
pub struct ButtonOptions {
    #[derivative(Default(value = "SMALL_BUTTON_FONT_SIZE"))]
    pub font_size: f32,
    #[derivative(Default(value = "BUTTON_BORDER"))]
    pub padding: f32,
    // Whether the background and foreground colours should be swapped
    #[derivative(Default(value = "false"))]
    pub bg_fg_swapped: bool,
    #[derivative(Default(value = "BUTTON_COLOR"))]
    pub bg_color: Color,
    #[derivative(Default(value = "ACTIVE_BUTTON_COLOR"))]
    pub bg_color_active: Color,
    #[derivative(Default(value = "ACTIVE_BUTTON_COLOR_SWAPPED"))]
    pub bg_color_active_if_bg_fg_swapped: Color,
    #[derivative(Default(value = "BUTTON_DISABLED_COLOR"))]
    pub bg_color_disabled: Color,
    #[derivative(Default(value = "BUTTON_TEXT_COLOR"))]
    pub fg_color: Color,
    #[derivative(Default(value = "BUTTON_TEXT_COLOR"))]
    pub fg_color_disabled: Color,
    #[derivative(Default(value = "BUTTON_BORDER_COLOR"))]
    pub border_color: Color,
    #[derivative(Default(value = "false"))]
    pub draw_border: bool,
    #[derivative(Default(value = "ButtonShape::Square"))]
    pub shape: ButtonShape,
    #[derivative(Default(value = "None"))]
    pub bg_size: Option<Size2D<f32, UnknownUnit>>,
    #[derivative(Default(value = "None"))]
    pub text_alignment: Option<Alignment>,
    #[derivative(Default(value = "false"))]
    pub hide_text: bool,
}

pub struct Button {
    tracking_pointer: Option<input::pointer::PointerId>,
    active: bool,
    focused: bool,
    label_text: String,
    background: FacetId,
    label: FacetId,
    button_options: ButtonOptions,
}

impl Button {
    pub fn new(
        text: &str,
        icon: Option<RiveFacet>,
        mut button_options: ButtonOptions,
        builder: &mut SceneBuilder,
    ) -> Button {
        if button_options.bg_fg_swapped {
            std::mem::swap(&mut button_options.fg_color, &mut button_options.bg_color);
            button_options.bg_color_active = button_options.bg_color_active_if_bg_fg_swapped;
        }
        let mut alignment = match button_options.bg_size {
            Some(_) => Alignment::center_left(),
            _ => Alignment::center(),
        };
        alignment = button_options.text_alignment.unwrap_or(alignment);
        let options = StackOptions { alignment, ..StackOptions::default() };

        builder.start_group("button", Stack::with_options_ptr(options));
        let font_size = button_options.font_size;
        let (label_text, mut label_width) = if button_options.hide_text || font_size == 0.0 {
            ("", 0.0)
        } else {
            (text, measure_text_width(font::get_default_font_face(), font_size, text))
        };

        builder.start_group(
            &("Button icon text row"),
            Flex::with_options_ptr(FlexOptions::row(
                MainAxisSize::Min,
                MainAxisAlignment::Start,
                CrossAxisAlignment::Start,
            )),
        );
        if let Some(icon) = icon {
            label_width += icon.calculate_size(size2(0.0, 0.0)).width;
            builder.facet(Box::new(icon));
        }
        builder.space(size2(5.0, 1.0));
        let label = builder.text(
            font::get_default_font_face().clone(),
            label_text,
            font_size,
            Point::zero(),
            TextFacetOptions {
                color: Color::blue(),
                horizontal_alignment: TextHorizontalAlignment::Right,
                vertical_alignment: TextVerticalAlignment::Center,
                ..TextFacetOptions::default()
            },
        );
        builder.end_group(); // Button icon text row

        let padding = button_options.padding;
        let bg_size = button_options
            .bg_size
            .unwrap_or(size2(label_width + padding * 2.0, font_size + padding * 2.0));
        let corner: Coord =
            Coord::from(bg_size.height * (button_options.shape.clone() as i32 as f32) / 100.0);
        let background =
            builder.rounded_rectangle(bg_size.clone(), corner, button_options.bg_color.clone());
        if button_options.draw_border {
            let bg_size = bg_size.add(size2(2.0, 2.0));
            builder.rounded_rectangle(bg_size, corner, button_options.border_color);
        }
        builder.end_group(); // button

        Button {
            button_options,
            tracking_pointer: None,
            active: false,
            focused: false,
            label_text: text.to_string(),
            background,
            label,
        }
    }

    #[allow(unused)]
    pub fn get_size(&self, scene: &mut Scene) -> Size {
        scene.get_facet_size(&self.background)
    }

    fn update_button_bg_color(&mut self, scene: &mut Scene) {
        let (label_color, color) = if self.focused {
            if self.active {
                (self.button_options.fg_color, self.button_options.bg_color_active)
            } else {
                (self.button_options.fg_color, self.button_options.bg_color)
            }
        } else {
            (self.button_options.fg_color_disabled, self.button_options.bg_color_disabled)
        };
        scene.send_message(&self.background, Box::new(SetColorMessage { color }));
        scene.send_message(&self.label, Box::new(SetColorMessage { color: label_color }));
    }

    fn set_active(&mut self, scene: &mut Scene, active: bool) {
        if self.active != active {
            self.active = active;
            self.update_button_bg_color(scene);
        }
    }

    pub fn set_focused(&mut self, scene: &mut Scene, focused: bool) {
        if focused != self.focused {
            self.focused = focused;
            self.active = false;
            self.update_button_bg_color(scene);
            if !focused {
                self.tracking_pointer = None;
            }
        }
    }

    pub fn handle_pointer_event(
        &mut self,
        scene: &mut Scene,
        context: &mut ViewAssistantContext,
        pointer_event: &input::pointer::Event,
    ) {
        if !self.focused {
            return;
        }

        let bounds = scene.get_facet_bounds(&self.background);

        if self.tracking_pointer.is_none() {
            match pointer_event.phase {
                input::pointer::Phase::Down(location) => {
                    self.set_active(scene, bounds.contains(location.to_f32()));
                    if self.active {
                        #[cfg(feature = "debug_logging")]
                        println!("====== Button {} is now active", self.label_text);
                        self.tracking_pointer = Some(pointer_event.pointer_id.clone());
                    }
                }
                _ => (),
            }
        } else {
            if let Some(tracking_pointer) = self.tracking_pointer.as_ref() {
                if tracking_pointer == &pointer_event.pointer_id {
                    match pointer_event.phase {
                        input::pointer::Phase::Moved(location) => {
                            self.set_active(scene, bounds.contains(location.to_f32()));
                        }
                        input::pointer::Phase::Up => {
                            if self.active {
                                #[cfg(feature = "debug_logging")]
                                println!("====== Button {} pressed", self.label_text);
                                context.queue_message(make_message(ButtonMessages::Pressed(
                                    Time::get_monotonic(),
                                    self.label_text.clone(),
                                )));
                            }
                            self.tracking_pointer = None;
                            self.set_active(scene, false);
                        }
                        input::pointer::Phase::Remove => {
                            self.set_active(scene, false);
                            self.tracking_pointer = None;
                        }
                        input::pointer::Phase::Cancel => {
                            self.set_active(scene, false);
                            self.tracking_pointer = None;
                        }
                        _ => (),
                    }
                }
            }
        }
    }
}

pub trait SceneBuilderButtonExt {
    fn button(
        &mut self,
        text: &str,
        icon: Option<RiveFacet>,
        button_options: ButtonOptions,
    ) -> Button;
}

impl SceneBuilderButtonExt for SceneBuilder {
    fn button(
        &mut self,
        text: &str,
        icon: Option<RiveFacet>,
        button_options: ButtonOptions,
    ) -> Button {
        Button::new(text, icon, button_options, self)
    }
}
