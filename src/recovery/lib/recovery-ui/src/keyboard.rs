// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::keys;
use crate::keys::{get_accent, Accent, Key, SpecialKey};
use crate::proxy_view_assistant::ProxyMessages;
use anyhow::Error;
use carnelian::{
    color::Color,
    drawing::{measure_text_width, FontFace},
    input::{self},
    make_message,
    render::Context as RenderContext,
    scene::{
        facets::{
            FacetId, SetColorMessage, SetTextMessage, TextFacetOptions, TextHorizontalAlignment,
            TextVerticalAlignment,
        },
        layout::{
            Alignment, CrossAxisAlignment, Flex, FlexOptions, MainAxisAlignment, MainAxisSize,
            Stack, StackOptions,
        },
        scene::{Scene, SceneBuilder},
    },
    AppSender, Coord, Message, MessageTarget, Point, Size, ViewAssistant, ViewAssistantContext,
    ViewKey,
};
use euclid::{size2, Size2D};
use fuchsia_zircon::{Duration, Event, Time};
use std::collections::VecDeque;
use std::hash::{Hash, Hasher};

/// enum that defines all messages sent with `App::queue_message` that
/// the button view assistant will understand and process.
pub enum ButtonMessages {
    Pressed(&'static keys::Key, Time, String),
}

pub enum KeyboardMessages {
    NoInput,
    // Result of input field name and entered text
    Result(&'static str, String),
}

pub struct KeyButton {
    pub font_size: f32,
    pub padding: f32,
    bg_color: Color,
    bg_color_active: Color,
    bg_color_disabled: Color,
    bg_color_special: Color,
    fg_color: Color,
    fg_color_disabled: Color,
    tracking_pointer: Option<input::pointer::PointerId>,
    active: bool,
    focused: bool,
    // Used for shifted and accent keys
    special_action: bool,
    key: &'static keys::Key,
    label_text: String,
    background: FacetId,
    label: FacetId,
}

impl KeyButton {
    pub fn new(
        key: &'static keys::Key,
        font_face: FontFace,
        font_size: f32,
        padding: f32,
        builder: &mut SceneBuilder,
    ) -> Result<KeyButton, Error> {
        let options = StackOptions { alignment: Alignment::center(), ..StackOptions::default() };
        builder.start_group("key_button", Stack::with_options_ptr(options));
        let text = match key {
            Key::Letter(letter_key) => &letter_key.lower,
            Key::Special(_special_key, text) => text,
        };
        let label = builder.text(
            font_face.clone(),
            text,
            font_size,
            Point::zero(),
            TextFacetOptions {
                color: Color::white(),
                horizontal_alignment: TextHorizontalAlignment::Left,
                vertical_alignment: TextVerticalAlignment::Top,
                ..TextFacetOptions::default()
            },
        );
        let bg_color = Color::from_hash_code("#B7410E")?;
        let bg_size = match key {
            Key::Letter(_letter_key) => size2(font_size + padding, font_size + padding),
            Key::Special(_special_key, text) => {
                let label_width = measure_text_width(&font_face, font_size, text);
                size2(label_width + padding * 1.5, font_size + padding)
            }
        };
        let corner: Coord = Coord::from(5.0);
        let background = builder.rounded_rectangle(bg_size, corner, bg_color);
        builder.end_group();
        let button = KeyButton {
            key,
            font_size,
            padding,
            fg_color: Color::white(),
            bg_color,
            bg_color_active: Color::from_hash_code("#f0703c")?,
            fg_color_disabled: Color::from_hash_code("#A0A0A0")?,
            bg_color_disabled: Color::from_hash_code("#C0C0C0")?,
            bg_color_special: Color::green(),
            tracking_pointer: None,
            active: false,
            focused: false,
            special_action: false,
            label_text: text.to_string(),
            background,
            label,
        };

        Ok(button)
    }

    fn update_button_bg_color(&mut self, scene: &mut Scene) {
        let (label_color, color) = if self.focused {
            if self.special_action {
                (self.fg_color, self.bg_color_special)
            } else if self.active {
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

    fn set_label(&mut self, scene: &mut Scene, shift: bool, alt: bool, _symbol: bool) {
        if let keys::Key::Letter(key) = self.key {
            self.label_text = if shift {
                key.upper
            } else if alt {
                key.alt
            } else {
                key.lower
            }
            .to_string();
            scene.send_message(
                &self.label,
                Box::new(SetTextMessage { text: self.label_text.clone() }),
            );
        }
    }

    pub fn set_accented_char(
        &mut self,
        scene: &mut Scene,
        accent_char: &Accent,
        state_shift: bool,
    ) {
        self.label_text =
            if state_shift { accent_char.upper } else { accent_char.lower }.to_string();
        scene.send_message(&self.label, Box::new(SetTextMessage { text: self.label_text.clone() }));
    }

    pub fn set_special_action(&mut self, scene: &mut Scene, special_action: bool) {
        if self.special_action != special_action {
            self.special_action = special_action;
            self.update_button_bg_color(scene);
        }
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
                                self.key,
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

impl PartialEq for KeyButton {
    fn eq(&self, other: &Self) -> bool {
        self.label_text == other.label_text
    }
}

impl Eq for KeyButton {}

impl Hash for KeyButton {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.label_text.hash(state);
    }
}

pub struct SceneDetails {
    buttons: VecDeque<KeyButton>,
    user_text: FacetId,
    pub(crate) scene: Scene,
}

pub struct KeyboardViewAssistant {
    font_face: FontFace,
    app_sender: AppSender,
    view_key: ViewKey,
    focused: bool,
    bg_color: Color,
    user_text: String,
    field_name: &'static str,
    scene_details: Option<SceneDetails>,
    state_shift: bool,
    shift_time: Time,
    sticky_shift: bool,
    accent_key: Option<&'static Key>,
    state_alt: bool,
    state_symbols: bool,
}

impl KeyboardViewAssistant {
    pub fn new(
        app_sender: AppSender,
        view_key: ViewKey,
        font_face: FontFace,
    ) -> Result<KeyboardViewAssistant, Error> {
        let bg_color = Color::from_hash_code("#EBD5B3")?;
        Ok(KeyboardViewAssistant {
            font_face,
            app_sender: app_sender.clone(),
            view_key,
            focused: false,
            bg_color,
            user_text: String::new(),
            field_name: "",
            scene_details: None,
            state_shift: false,
            shift_time: Time::ZERO,
            sticky_shift: false,
            state_alt: false,
            accent_key: None,
            state_symbols: false,
        })
    }

    pub fn set_field_name(&mut self, field_name: &'static str) {
        self.field_name = field_name;
    }

    pub fn set_text_field(&mut self, text: String) {
        self.user_text = text;
    }

    fn remove_last_char(&mut self) {
        let mut chars = self.user_text.chars();
        chars.next_back();
        self.user_text = chars.as_str().to_string();
    }

    fn key_press(&mut self, key: &&'static keys::Key, key_cap: &String, time: &Time) {
        let mut keyboard_changed = false;
        match key {
            Key::Letter(letter_key) => {
                // The default is to use the normal key cap character
                // Save it here, we may change it later.
                let mut text = key_cap.as_str();
                if let Some(alt_key) = self.accent_key {
                    // An accent has previously been selected
                    // Are we selecting the same or another accent?
                    if self.state_alt && letter_key.is_alt_accent {
                        if self.accent_key == Some(key) {
                            // We want the accent as a character,
                            // Just leave the alt accent state
                            self.accent_key = None;
                        } else {
                            // We want a different accent character
                            self.accent_key = Some(key);
                        }
                        // The accent key will be added (again) later
                        self.remove_last_char();
                        self.state_alt = false;
                        keyboard_changed = true;
                    } else {
                        self.accent_key = None;
                        // Remove the accent from the input line.
                        self.remove_last_char();
                        // Now we check for a valid accented character otherwise use the normal key cap letter
                        if let Some(accent) = get_accent(alt_key, key) {
                            // add the accented character
                            if self.state_shift {
                                text = accent.upper;
                            } else {
                                text = accent.lower;
                            }
                        }
                    }
                } else {
                    if self.state_alt && letter_key.is_alt_accent {
                        self.accent_key = Some(*key);
                        self.state_alt = false;
                        keyboard_changed = true;
                    }
                }
                if !self.sticky_shift {
                    self.state_shift = false;
                    keyboard_changed = true;
                }
                self.user_text.push_str(text);
            }
            Key::Special(key, _text) => match key {
                SpecialKey::DEL => {
                    let mut chars = self.user_text.chars();
                    chars.next_back();
                    self.user_text = chars.into_iter().collect();
                    if self.accent_key.is_some() {
                        self.accent_key = None;
                        keyboard_changed = true;
                    }
                }
                SpecialKey::ENTER => {
                    // Finish this view and return to the calling view
                    self.app_sender.queue_message(
                        MessageTarget::View(self.view_key),
                        make_message(ProxyMessages::PopViewAssistant),
                    );
                    // Send the calling view the result
                    self.app_sender.queue_message(
                        MessageTarget::View(self.view_key),
                        make_message(KeyboardMessages::Result(
                            self.field_name,
                            self.user_text.clone(),
                        )),
                    );
                }
                SpecialKey::ALT => {
                    self.state_alt = !self.state_alt;
                    if self.state_alt {
                        self.state_shift = false;
                        self.accent_key = None;
                    }
                    keyboard_changed = true;
                }
                SpecialKey::SHIFT => {
                    if !self.state_shift {
                        self.shift_time = time.clone();
                        self.state_shift = true;
                        self.state_alt = false;
                    } else {
                        // Is this a quick double press?
                        if *time - self.shift_time < Duration::from_seconds(1) {
                            self.sticky_shift = true;
                        } else {
                            self.sticky_shift = false;
                            self.state_shift = false;
                        }
                    }
                    keyboard_changed = true;
                }
                SpecialKey::SPACE => {
                    self.user_text.push_str(" ");
                }
            },
        };
        if keyboard_changed {
            if let Some(scene_details) = &mut self.scene_details {
                for button in &mut scene_details.buttons {
                    button.set_label(
                        &mut scene_details.scene,
                        self.state_shift,
                        self.state_alt,
                        self.state_symbols,
                    );

                    if let Key::Special(SpecialKey::SHIFT, _) = button.key {
                        button.set_special_action(&mut scene_details.scene, self.state_shift)
                    }
                    if let Key::Special(SpecialKey::ALT, _) = button.key {
                        button.set_special_action(&mut scene_details.scene, self.state_alt);
                    }

                    if let Key::Letter(letter_key) = button.key {
                        // Reset background first
                        button.set_special_action(&mut scene_details.scene, false);
                        // Highlight accent keys if necessary
                        if letter_key.is_alt_accent {
                            button.set_special_action(&mut scene_details.scene, self.state_alt);
                        }

                        // Highlight possible keys that will go with the selected accent character
                        if let Some(accent_key) = self.accent_key {
                            if let Some(accent_char) = get_accent(accent_key, button.key) {
                                button.set_accented_char(
                                    &mut scene_details.scene,
                                    accent_char,
                                    self.state_shift,
                                );
                                button.set_special_action(&mut scene_details.scene, true);
                            }
                        }
                    }
                }
            }
        }

        if let Some(scene_details) = self.scene_details.as_mut() {
            scene_details.scene.send_message(
                &scene_details.user_text,
                Box::new(SetTextMessage { text: self.user_text.clone() }),
            );
        }
    }

    pub fn keyboard_scene(&mut self, context: &ViewAssistantContext) -> SceneDetails {
        let scene_details = self.scene_details.take().unwrap_or_else(|| {
            let target_size = context.size;
            let min_dimension = target_size.width.min(target_size.height);
            let font_size = (min_dimension / 5.0).ceil().min(40.0);
            let padding = (min_dimension / 20.0).ceil().max(8.0);
            let mut builder = SceneBuilder::new().background_color(self.bg_color);
            let mut user_text = None;
            let mut buttons: VecDeque<KeyButton> = VecDeque::with_capacity(50);
            builder
                .group()
                .column()
                .max_size()
                .main_align(MainAxisAlignment::SpaceEvenly)
                .contents(|builder| {
                    builder.start_group(
                        "text_row",
                        Flex::with_options_ptr(FlexOptions::row(
                            MainAxisSize::Max,
                            MainAxisAlignment::Start,
                            CrossAxisAlignment::End,
                        )),
                    );
                    builder.space(Size2D { width: 10.0, height: 10.0, _unit: Default::default() });
                    builder.text(
                        self.font_face.clone(),
                        &format!("Enter {}: ", self.field_name),
                        35.0,
                        Point::zero(),
                        TextFacetOptions {
                            horizontal_alignment: TextHorizontalAlignment::Left,
                            vertical_alignment: TextVerticalAlignment::Top,
                            color: Color::new(),
                            ..TextFacetOptions::default()
                        },
                    );
                    user_text = Some(builder.text(
                        self.font_face.clone(),
                        &format!("{}", self.user_text),
                        35.0,
                        Point::zero(),
                        TextFacetOptions {
                            horizontal_alignment: TextHorizontalAlignment::Left,
                            vertical_alignment: TextVerticalAlignment::Bottom,
                            color: Color::new(),
                            ..TextFacetOptions::default()
                        },
                    ));
                    builder.end_group();

                    for row in keys::KEYBOARD {
                        builder.start_group(
                            "row",
                            Flex::with_options_ptr(FlexOptions::row(
                                MainAxisSize::Max,
                                MainAxisAlignment::SpaceEvenly,
                                CrossAxisAlignment::Center,
                            )),
                        );
                        for key in *row {
                            let button = KeyButton::new(
                                key,
                                self.font_face.clone(),
                                font_size,
                                padding,
                                builder,
                            )
                            .expect("KeyButton");
                            buttons.push_back(button);
                        }
                        builder.end_group();
                    }
                });
            let mut scene = builder.build();
            scene.layout(target_size);
            for button in buttons.iter_mut() {
                button.set_focused(&mut scene, self.focused);
            }
            SceneDetails { scene, user_text: user_text.expect("user_text"), buttons }
        });
        scene_details
    }
}

impl ViewAssistant for KeyboardViewAssistant {
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
        let mut scene_details = self.keyboard_scene(context);

        scene_details.scene.render(render_context, ready_event, context)?;
        self.scene_details = Some(scene_details);
        context.request_render();
        Ok(())
    }

    fn handle_message(&mut self, message: Message) {
        if let Some(button_message) = message.downcast_ref::<ButtonMessages>() {
            match button_message {
                ButtonMessages::Pressed(key, time, letter) => {
                    self.key_press(key, letter, time);
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
            for button in scene_details.buttons.iter_mut() {
                button.handle_pointer_event(&mut scene_details.scene, context, &pointer_event);
            }
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
            for button in scene_details.buttons.iter_mut() {
                button.set_focused(&mut scene_details.scene, focused);
            }
        }
        context.request_render();
        Ok(())
    }
}
