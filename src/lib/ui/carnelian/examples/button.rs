// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use argh::FromArgs;
use carnelian::{
    color::Color,
    drawing::{load_font, measure_text_width, DisplayRotation, FontFace},
    facet::{
        FacetId, Scene, SceneBuilder, SetColorMessage, TextFacetOptions, TextHorizontalAlignment,
        TextVerticalAlignment,
    },
    input::{self},
    make_app_assistant, make_message,
    render::Context as RenderContext,
    App, AppAssistant, Message, Point, Rect, Size, ViewAssistant, ViewAssistantContext,
    ViewAssistantPtr, ViewKey,
};
use euclid::{point2, size2, vec2};
use fuchsia_zircon::{Event, Time};
use std::path::PathBuf;

fn display_rotation_from_str(s: &str) -> Result<DisplayRotation, String> {
    match s {
        "0" => Ok(DisplayRotation::Deg0),
        "90" => Ok(DisplayRotation::Deg90),
        "180" => Ok(DisplayRotation::Deg180),
        "270" => Ok(DisplayRotation::Deg270),
        _ => Err(format!("Invalid DisplayRotation {}", s)),
    }
}

/// Button Sample
#[derive(Debug, FromArgs)]
#[argh(name = "recovery")]
struct Args {
    /// rotate
    #[argh(option, from_str_fn(display_rotation_from_str))]
    rotation: Option<DisplayRotation>,
}

/// enum that defines all messages sent with `App::queue_message` that
/// the button view assistant will understand and process.
pub enum ButtonMessages {
    Pressed(Time),
}

struct ButtonAppAssistant {
    display_rotation: DisplayRotation,
}

impl Default for ButtonAppAssistant {
    fn default() -> Self {
        let args: Args = argh::from_env();
        Self { display_rotation: args.rotation.unwrap_or(DisplayRotation::Deg0) }
    }
}

impl AppAssistant for ButtonAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(ButtonViewAssistant::new()?))
    }

    fn get_display_rotation(&self) -> DisplayRotation {
        self.display_rotation
    }
}

#[allow(unused)]
struct Button {
    pub font_size: f32,
    pub padding: f32,
    bounds: Rect,
    bg_color: Color,
    bg_color_active: Color,
    bg_color_disabled: Color,
    fg_color: Color,
    fg_color_disabled: Color,
    tracking_pointer: Option<input::pointer::PointerId>,
    active: bool,
    focused: bool,
    label_text: String,
    face: FontFace,
    background: FacetId,
    label: FacetId,
}

impl Button {
    pub fn new(
        text: &str,
        font_size: f32,
        padding: f32,
        position: Point,
        builder: &mut SceneBuilder,
    ) -> Result<Button, Error> {
        let face = load_font(PathBuf::from("/pkg/data/fonts/RobotoSlab-Regular.ttf"))?;
        let label_width = measure_text_width(&face, font_size, text);
        let label = builder.text(
            face.clone(),
            text,
            font_size,
            position,
            TextFacetOptions {
                color: Color::white(),
                horizontal_alignment: TextHorizontalAlignment::Center,
                vertical_alignment: TextVerticalAlignment::Center,
                ..TextFacetOptions::default()
            },
        );
        let bg_color = Color::from_hash_code("#B7410E")?;
        let bg_bounds = Rect::new(
            position - vec2(label_width, font_size) / 2.0 - vec2(padding, padding),
            size2(label_width + padding * 2.0, font_size + padding * 2.0),
        );
        let background = builder.rectangle(bg_bounds, bg_color);
        let button = Button {
            font_size: font_size,
            padding: padding,
            bounds: bg_bounds,
            fg_color: Color::white(),
            bg_color,
            bg_color_active: Color::from_hash_code("#f0703c")?,
            fg_color_disabled: Color::from_hash_code("#A0A0A0")?,
            bg_color_disabled: Color::from_hash_code("#C0C0C0")?,
            tracking_pointer: None,
            active: false,
            focused: false,
            label_text: text.to_string(),
            face,
            background,
            label,
        };

        Ok(button)
    }

    fn update_button_bg_color(&mut self, scene: &mut Scene) {
        let (label_color, color) = if self.focused {
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

        let bounds = self.bounds;

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
            let tracking_pointer = self.tracking_pointer.as_ref().expect("tracking_pointer");
            if tracking_pointer == &pointer_event.pointer_id {
                match pointer_event.phase {
                    input::pointer::Phase::Moved(location) => {
                        self.set_active(scene, bounds.contains(location.to_f32()));
                    }
                    input::pointer::Phase::Up => {
                        if self.active {
                            context.queue_message(make_message(ButtonMessages::Pressed(
                                Time::get_monotonic(),
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

#[allow(unused)]
struct SceneDetails {
    button: Button,
    indicator: FacetId,
    scene: Scene,
}

struct ButtonViewAssistant {
    focused: bool,
    bg_color: Color,
    red_light: bool,
    scene_details: Option<SceneDetails>,
}

const BUTTON_LABEL: &'static str = "Depress Me";

impl ButtonViewAssistant {
    fn new() -> Result<ButtonViewAssistant, Error> {
        let bg_color = Color::from_hash_code("#EBD5B3")?;
        Ok(ButtonViewAssistant { focused: false, bg_color, red_light: false, scene_details: None })
    }

    fn set_red_light(&mut self, red_light: bool) {
        if red_light != self.red_light {
            if let Some(scene_details) = self.scene_details.as_mut() {
                let color = if red_light { Color::red() } else { Color::green() };
                scene_details
                    .scene
                    .send_message(&scene_details.indicator, Box::new(SetColorMessage { color }));
            }
            self.red_light = red_light;
        }
    }
}

impl ViewAssistant for ButtonViewAssistant {
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
            let target_size = context.size;
            let min_dimension = target_size.width.min(target_size.height);
            let font_size = (min_dimension / 5.0).ceil().min(64.0);
            let padding = (min_dimension / 20.0).ceil().max(8.0);
            let center_x = target_size.width * 0.5;
            let button_y = target_size.height * 0.7;
            let mut builder = SceneBuilder::new(self.bg_color);
            let indicator_y = target_size.height / 5.0;
            let indicator_len = target_size.height.min(target_size.width) / 8.0;
            let indicator_size = size2(indicator_len * 2.0, indicator_len);
            let indicator_pos = point2(center_x - indicator_len, indicator_y - indicator_len / 2.0);
            let indicator_bounds = Rect::new(indicator_pos, indicator_size);
            let indicator = builder.rectangle(indicator_bounds, Color::green());
            let mut button = Button::new(
                BUTTON_LABEL,
                font_size,
                padding,
                point2(center_x, button_y),
                &mut builder,
            )
            .expect("button");
            let mut scene = builder.build();
            button.set_focused(&mut scene, self.focused);
            SceneDetails { scene, indicator, button }
        });

        scene_details.scene.render(render_context, ready_event, context)?;
        self.scene_details = Some(scene_details);
        context.request_render();
        Ok(())
    }

    fn handle_message(&mut self, message: Message) {
        if let Some(button_message) = message.downcast_ref::<ButtonMessages>() {
            match button_message {
                ButtonMessages::Pressed(value) => {
                    println!("value = {:#?}", value);
                    self.set_red_light(!self.red_light);
                }
            }
        }
    }

    fn handle_pointer_event(
        &mut self,
        context: &mut ViewAssistantContext,
        _event: &input::Event,
        pointer_event: &input::pointer::Event,
    ) -> Result<(), Error> {
        if let Some(scene_details) = self.scene_details.as_mut() {
            scene_details.button.handle_pointer_event(
                &mut scene_details.scene,
                context,
                &pointer_event,
            );
            context.request_render();
        }
        Ok(())
    }

    fn handle_focus_event(
        &mut self,
        context: &mut ViewAssistantContext,
        focused: bool,
    ) -> Result<(), Error> {
        self.focused = focused;
        if let Some(scene_details) = self.scene_details.as_mut() {
            scene_details.button.set_focused(&mut scene_details.scene, focused);
        }
        context.request_render();
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    fuchsia_trace_provider::trace_provider_create_with_fdio();
    App::run(make_app_assistant::<ButtonAppAssistant>())
}
