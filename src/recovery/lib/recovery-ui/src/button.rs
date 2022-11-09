// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::font;
use anyhow::Error;
use carnelian::{
    color::Color,
    drawing::{measure_text_width, FontFace},
    input::{self},
    make_message,
    scene::{
        facets::{
            FacetId, SetColorMessage, TextFacetOptions, TextHorizontalAlignment,
            TextVerticalAlignment,
        },
        layout::{Alignment, Stack, StackOptions},
        scene::{Scene, SceneBuilder},
    },
    Coord, Point, Size, ViewAssistantContext,
};
use euclid::{size2, Size2D, UnknownUnit};
use fuchsia_zircon::Time;

/// enum that defines all messages sent with `App::queue_message` that
/// the button view assistant will understand and process.
pub enum ButtonMessages {
    Pressed(Time, String),
}

#[allow(unused)]
pub struct Button {
    pub font_size: f32,
    pub padding: f32,
    bg_color: Color,
    bg_color_active: Color,
    bg_color_disabled: Color,
    fg_color: Color,
    fg_color_disabled: Color,
    tracking_pointer: Option<input::pointer::PointerId>,
    active: bool,
    focused: Option<bool>,
    label_text: String,
    face: FontFace,
    background: FacetId,
    label: FacetId,
    bg_size: Size2D<f32, UnknownUnit>,
}

impl Button {
    pub fn new(
        text: &str,
        font_size: f32,
        padding: f32,
        builder: &mut SceneBuilder,
    ) -> Result<Button, Error> {
        let options = StackOptions { alignment: Alignment::center(), ..StackOptions::default() };
        builder.start_group("button", Stack::with_options_ptr(options));
        let face = font::load_default_font_face()?;
        let label_width = measure_text_width(&face, font_size, text);
        let label = builder.text(
            face.clone(),
            text,
            font_size,
            Point::zero(),
            TextFacetOptions {
                color: Color::white(),
                horizontal_alignment: TextHorizontalAlignment::Center,
                vertical_alignment: TextVerticalAlignment::Center,
                ..TextFacetOptions::default()
            },
        );
        let bg_color = Color::from_hash_code("#B7410E")?;
        let bg_size = size2(label_width + padding * 2.0, font_size + padding * 2.0);
        let corner: Coord = Coord::from(5.0);
        let background = builder.rounded_rectangle(bg_size, corner, bg_color);
        builder.end_group();
        let button = Button {
            font_size: font_size,
            padding: padding,
            fg_color: Color::white(),
            bg_color,
            bg_color_active: Color::from_hash_code("#f0703c")?,
            fg_color_disabled: Color::from_hash_code("#A0A0A0")?,
            bg_color_disabled: Color::from_hash_code("#C0C0C0")?,
            tracking_pointer: None,
            active: false,
            focused: None,
            label_text: text.to_string(),
            face,
            background,
            label,
            bg_size,
        };

        Ok(button)
    }

    #[allow(unused)]
    pub fn get_size(&self, scene: &mut Scene) -> Size {
        scene.get_facet_size(&self.background)
    }

    fn update_button_bg_color(&mut self, scene: &mut Scene) {
        let (label_color, color) = if let Some(true) = self.focused {
            if self.active {
                (self.fg_color, self.bg_color_active)
            } else {
                (self.fg_color, self.bg_color)
            }
        } else {
            (self.fg_color_disabled, self.bg_color_disabled)
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
        if self.focused.is_none() || focused != self.focused.unwrap() {
            self.focused = Some(focused);
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
        if self.focused.is_none() || !self.focused.unwrap() {
            return;
        }

        let bounds = scene.get_facet_bounds(&self.background);

        if self.tracking_pointer.is_none() {
            match pointer_event.phase {
                input::pointer::Phase::Down(location) => {
                    self.set_active(scene, bounds.contains(location.to_f32()));
                    if self.active {
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
