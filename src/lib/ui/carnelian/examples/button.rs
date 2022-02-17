// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is the button sample.

use anyhow::Error;
use argh::FromArgs;
use carnelian::{
    app::Config,
    color::Color,
    drawing::{load_font, measure_text_width, DisplayRotation, FontFace},
    input::{self},
    make_app_assistant, make_message,
    scene::{
        facets::{
            FacetId, SetColorMessage, SetSizeMessage, TextFacetOptions, TextHorizontalAlignment,
            TextVerticalAlignment,
        },
        layout::{
            Alignment, CrossAxisAlignment, Flex, FlexOptions, MainAxisAlignment, MainAxisSize,
            Stack, StackOptions,
        },
        scene::{Scene, SceneBuilder},
    },
    App, AppAssistant, AppSender, Message, MessageTarget, Point, Size, ViewAssistant,
    ViewAssistantContext, ViewAssistantPtr, ViewKey,
};
use euclid::size2;
use fuchsia_zircon::Time;
use std::{
    f32::consts::PI,
    path::PathBuf,
    thread,
    time::{Duration, Instant},
};

/// Button Sample
#[derive(Debug, FromArgs)]
#[argh(name = "recovery")]
struct Args {
    /// rotate
    #[argh(option)]
    rotation: Option<DisplayRotation>,
}

/// enum that defines all messages sent with `App::queue_message` that
/// the button view assistant will understand and process.
pub enum ButtonMessages {
    Pressed(Time),
    Resize,
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

    fn create_view_assistant_with_sender(
        &mut self,
        view_key: ViewKey,
        app_sender: AppSender,
    ) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(ButtonViewAssistant::new(app_sender, view_key)?))
    }

    fn filter_config(&mut self, config: &mut Config) {
        config.display_rotation = self.display_rotation;
    }
}

#[allow(unused)]
struct Button {
    pub font_size: f32,
    pub padding: f32,
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
        builder: &mut SceneBuilder,
    ) -> Result<Button, Error> {
        let options = StackOptions { alignment: Alignment::center(), ..StackOptions::default() };
        builder.start_group("button", Stack::with_options_ptr(options));
        let face = load_font(PathBuf::from("/pkg/data/fonts/RobotoSlab-Regular.ttf"))?;
        let label_width = measure_text_width(&face, font_size, text);
        let label = builder.text(
            face.clone(),
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
        let bg_size = size2(label_width + padding * 2.0, font_size + padding * 2.0);
        let background = builder.rounded_rectangle(bg_size, padding / 2.0, bg_color);
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
    app_sender: AppSender,
    view_key: ViewKey,
    focused: bool,
    bg_color: Color,
    red_light: bool,
    original_indicator_size: Size,
    indicator_size: Size,
    animation_start: Instant,
    main_alignment_index: usize,
    cross_alignment_index: usize,
    column_main_alignment_index: usize,
    scene_details: Option<SceneDetails>,
}

const BUTTON_LABEL: &'static str = "Depress Me";
const CROSS_AXIS_ALIGNMENTS: &'static [CrossAxisAlignment] =
    &[CrossAxisAlignment::Start, CrossAxisAlignment::Center, CrossAxisAlignment::End];

const MAIN_AXIS_ALIGNMENTS: &'static [MainAxisAlignment] = &[
    MainAxisAlignment::Start,
    MainAxisAlignment::Center,
    MainAxisAlignment::End,
    MainAxisAlignment::SpaceBetween,
    MainAxisAlignment::SpaceAround,
    MainAxisAlignment::SpaceEvenly,
];

impl ButtonViewAssistant {
    fn new(app_sender: AppSender, view_key: ViewKey) -> Result<ButtonViewAssistant, Error> {
        let bg_color = Color::from_hash_code("#EBD5B3")?;
        let mut b = ButtonViewAssistant {
            app_sender,
            view_key,
            focused: false,
            bg_color,
            red_light: false,
            original_indicator_size: Size::default(),
            indicator_size: Size::default(),
            animation_start: Instant::now(),
            main_alignment_index: 5,
            cross_alignment_index: 1,
            column_main_alignment_index: 5,
            scene_details: None,
        };
        b.schedule_resize_timer();
        Ok(b)
    }

    fn ensure_scene_built(&mut self, size: Size) {
        if self.scene_details.is_none() {
            let min_dimension = size.width.min(size.height);
            let font_size = (min_dimension / 5.0).ceil().min(64.0);
            let padding = (min_dimension / 20.0).ceil().max(8.0);
            let mut builder = SceneBuilder::new().background_color(self.bg_color);
            let mut button = None;
            let mut indicator_id = None;
            let mut indicator_size_opt = None;
            builder
                .group()
                .column()
                .max_size()
                .main_align(MAIN_AXIS_ALIGNMENTS[self.column_main_alignment_index])
                .contents(|builder| {
                    let indicator_len = size.height.min(size.width) / 8.0;
                    let indicator_size = size2(indicator_len * 1.25, indicator_len);
                    indicator_size_opt = Some(indicator_size);
                    let indicator_side_size = size2(indicator_len / 2.0, indicator_len * 1.5);
                    builder.start_group(
                        "indicator_row",
                        Flex::with_options_ptr(FlexOptions::row(
                            MainAxisSize::Max,
                            MAIN_AXIS_ALIGNMENTS[self.main_alignment_index],
                            CROSS_AXIS_ALIGNMENTS[self.cross_alignment_index],
                        )),
                    );
                    builder.rectangle(indicator_side_size, Color::blue());
                    indicator_id = Some(builder.rectangle(
                        indicator_size,
                        if self.red_light { Color::red() } else { Color::green() },
                    ));
                    builder.rectangle(indicator_side_size, Color::blue());
                    builder.end_group();
                    button = Some(
                        Button::new(BUTTON_LABEL, font_size, padding, builder).expect("button"),
                    );
                });
            let mut button = button.expect("button");
            let mut scene = builder.build();
            self.indicator_size = indicator_size_opt.expect("indicator_size_opt");
            self.original_indicator_size = self.indicator_size;
            button.set_focused(&mut scene, self.focused);
            self.animation_start = Instant::now();
            self.scene_details = Some(SceneDetails {
                scene,
                indicator: indicator_id.expect("indicator_id"),
                button,
            });
        }
    }

    fn schedule_resize_timer(&mut self) {
        // use a thread to drive the button resize timer as a way to demonstrate the cross thread
        // sender.
        let sender = self
            .app_sender
            .create_cross_thread_sender::<ButtonMessages>(MessageTarget::View(self.view_key));
        thread::spawn(move || loop {
            sender.unbounded_send(ButtonMessages::Resize).expect("unbounded_send");
            thread::sleep(Duration::from_millis(16));
        });
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

    fn resize_indicator(&mut self, indicator_size: Size) {
        if indicator_size != self.indicator_size {
            if let Some(scene_details) = self.scene_details.as_mut() {
                self.indicator_size = indicator_size;
                scene_details.scene.send_message(
                    &scene_details.indicator,
                    Box::new(SetSizeMessage { size: self.indicator_size }),
                );
            }
        }
    }

    fn cycle_main_alignment(&mut self) {
        self.main_alignment_index = (self.main_alignment_index + 1) % MAIN_AXIS_ALIGNMENTS.len();
        self.scene_details = None;
    }

    fn cycle_cross_alignment(&mut self) {
        self.cross_alignment_index = (self.cross_alignment_index + 1) % CROSS_AXIS_ALIGNMENTS.len();
        self.scene_details = None;
    }

    fn cycle_column_main_alignment(&mut self) {
        self.column_main_alignment_index =
            (self.column_main_alignment_index + 1) % MAIN_AXIS_ALIGNMENTS.len();
        self.scene_details = None;
    }
}

impl ViewAssistant for ButtonViewAssistant {
    fn resize(&mut self, new_size: &Size) -> Result<(), Error> {
        self.scene_details = None;
        self.ensure_scene_built(*new_size);
        Ok(())
    }

    fn get_scene(&mut self, target_size: Size) -> Option<&mut Scene> {
        self.ensure_scene_built(target_size);
        Some(&mut self.scene_details.as_mut().unwrap().scene)
    }

    fn handle_message(&mut self, message: Message) {
        if let Some(button_message) = message.downcast_ref::<ButtonMessages>() {
            match button_message {
                ButtonMessages::Pressed(value) => {
                    println!("value = {:#?}", value);
                    self.set_red_light(!self.red_light);
                }
                ButtonMessages::Resize => {
                    let t = Instant::now().duration_since(self.animation_start).as_millis() % 3000;
                    let f = t as f32 / 3000.0;
                    let angle = f * PI * 2.0;
                    let ratio = angle.sin();
                    self.resize_indicator(size2(
                        self.original_indicator_size.width + ratio * 80.0,
                        self.indicator_size.height,
                    ));
                    self.app_sender.request_render(self.view_key);
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

    fn handle_keyboard_event(
        &mut self,
        _context: &mut ViewAssistantContext,
        _event: &input::Event,
        keyboard_event: &input::keyboard::Event,
    ) -> Result<(), Error> {
        const C: u32 = 99;
        const M: u32 = 109;
        const R: u32 = 114;
        if let Some(code_point) = keyboard_event.code_point {
            if keyboard_event.phase == input::keyboard::Phase::Pressed
                || keyboard_event.phase == input::keyboard::Phase::Repeat
            {
                match code_point {
                    C => self.cycle_cross_alignment(),
                    M => self.cycle_main_alignment(),
                    R => self.cycle_column_main_alignment(),
                    _ => println!("code_point = {}", code_point),
                }
            }
        }
        Ok(())
    }

    fn ownership_changed(&mut self, owned: bool) -> Result<(), Error> {
        println!("ownership_changed {}", owned);
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    fuchsia_trace_provider::trace_provider_create_with_fdio();
    App::run(make_app_assistant::<ButtonAppAssistant>())
}
