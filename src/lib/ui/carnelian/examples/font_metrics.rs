// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use carnelian::{
    color::Color,
    drawing::{load_font, FontFace},
    input::{self},
    make_app_assistant,
    scene::{
        facets::{
            FacetId, SetBackgroundColorMessage, SetColorMessage, SetTextMessage, TextFacetOptions,
            TextVerticalAlignment,
        },
        layout::{Alignment, StackMemberDataBuilder},
        scene::{Scene, SceneBuilder},
    },
    App, AppAssistant, AppSender, Coord, Point, Size, ViewAssistant, ViewAssistantContext,
    ViewAssistantPtr, ViewKey,
};
use euclid::point2;
use lipsum::lipsum_words;
use std::path::PathBuf;

const LINE_THICKNESS: Coord = 2.0;
const BASELINE_INDENT: Coord = 0.15;
const PADDING: Coord = 0.005;

#[derive(Default)]
struct FontAppAssistant;

impl AppAssistant for FontAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant_with_sender(
        &mut self,
        view_key: ViewKey,
        app_sender: AppSender,
    ) -> Result<ViewAssistantPtr, Error> {
        Ok(FontMetricsViewAssistant::new(app_sender, view_key)?)
    }
}

struct SceneDetails {
    scene: Scene,
    sample_title: FacetId,
    sample_paragraph: FacetId,
    lines: Vec<FacetId>,
}

struct FontMetricsViewAssistant {
    app_sender: AppSender,
    view_key: ViewKey,
    sample_title: String,
    sample_paragraph: String,
    sample_faces: Vec<FontFace>,
    sample_index: usize,
    sample_size_divisor: f32,
    label_face: FontFace,
    scene_details: Option<SceneDetails>,
    line_color: Color,
    text_background_color: Color,
    round_scene_corners: bool,
    show_text_background_color: bool,
}

impl FontMetricsViewAssistant {
    fn new(app_sender: AppSender, view_key: ViewKey) -> Result<ViewAssistantPtr, Error> {
        let ss = load_font(PathBuf::from("/pkg/data/fonts/ShortStack-Regular.ttf"))?;
        let ds = load_font(PathBuf::from("/pkg/data/fonts/DroidSerif-Regular.ttf"))?;
        let qr = load_font(PathBuf::from("/pkg/data/fonts/Quintessential-Regular.ttf"))?;
        let sample_faces = vec![ss, ds, qr];
        let label_face = load_font(PathBuf::from("/pkg/data/fonts/Roboto-Regular.ttf"))?;
        let (title, para) = Self::new_sample_pair();
        Ok(Box::new(Self {
            app_sender: app_sender.clone(),
            view_key,
            sample_title: title,
            sample_paragraph: para,
            sample_faces,
            sample_index: 0,
            sample_size_divisor: 6.0,
            label_face,
            scene_details: None,
            line_color: Color::new(),
            text_background_color: Color::from_hash_code("#0000ff60").expect("from_hash_code"),
            round_scene_corners: true,
            show_text_background_color: false,
        }))
    }

    fn new_sample_pair() -> (String, String) {
        (lipsum_words(2), lipsum_words(25))
    }

    fn update(&mut self) {
        self.scene_details = None;
        self.app_sender.request_render(self.view_key);
    }

    fn update_text(&mut self, title: String, para: String) {
        self.sample_title = title.to_string();
        self.sample_paragraph = para.to_string();
        if let Some(scene_details) = self.scene_details.as_mut() {
            scene_details.scene.send_message(
                &scene_details.sample_title.clone(),
                Box::new(SetTextMessage { text: self.sample_title.clone() }),
            );
            scene_details.scene.send_message(
                &scene_details.sample_paragraph.clone(),
                Box::new(SetTextMessage { text: self.sample_paragraph.clone() }),
            );
        }
        self.app_sender.request_render(self.view_key);
    }

    fn new_sample_text(&mut self) {
        let (title, para) = Self::new_sample_pair();
        self.update_text(title, para);
    }

    fn new_blank_text(&mut self) {
        let title = "".to_string();
        let para = "\nnow is the time\n\n".to_string();
        self.update_text(title, para);
    }

    fn show_next_face(&mut self) {
        self.sample_index = (self.sample_index + 1) % self.sample_faces.len();
        self.update();
    }

    fn increase_sample_size(&mut self) {
        self.sample_size_divisor = (self.sample_size_divisor - 0.5).max(1.0);
        self.update();
    }

    fn decrease_sample_size(&mut self) {
        self.sample_size_divisor = (self.sample_size_divisor + 0.5).min(10.0);
        self.update();
    }

    fn toggle_line_color(&mut self) {
        let black = Color::new();
        if self.line_color == black {
            self.line_color = Color::white();
        } else {
            self.line_color = black;
        }
        if let Some(scene_details) = self.scene_details.as_mut() {
            let targets: Vec<FacetId> = scene_details.lines.iter().cloned().collect();
            for target in targets {
                scene_details
                    .scene
                    .send_message(&target, Box::new(SetColorMessage { color: self.line_color }));
            }
        }
        self.app_sender.request_render(self.view_key);
    }

    fn toggle_round_scene_corners(&mut self) {
        self.round_scene_corners = !self.round_scene_corners;
        if let Some(scene_details) = self.scene_details.as_mut() {
            scene_details.scene.round_scene_corners(self.round_scene_corners);
        }
        self.app_sender.request_render(self.view_key);
    }

    fn optional_bg_color(&self) -> Option<Color> {
        self.show_text_background_color.then(|| self.text_background_color)
    }

    fn toggle_text_background_color(&mut self) {
        self.show_text_background_color = !self.show_text_background_color;
        let optional_bg_color = self.optional_bg_color();
        if let Some(scene_details) = self.scene_details.as_mut() {
            scene_details.scene.send_message(
                &scene_details.sample_title.clone(),
                Box::new(SetBackgroundColorMessage { color: optional_bg_color }),
            );
            scene_details.scene.send_message(
                &scene_details.sample_paragraph.clone(),
                Box::new(SetBackgroundColorMessage { color: optional_bg_color }),
            );
        }
        self.app_sender.request_render(self.view_key);
    }
}

impl ViewAssistant for FontMetricsViewAssistant {
    fn resize(&mut self, _new_size: &Size) -> Result<(), Error> {
        self.scene_details = None;
        Ok(())
    }

    fn get_scene(&mut self, size: Size) -> Option<&mut Scene> {
        let scene_details = self.scene_details.take().unwrap_or_else(|| {
            let mut sample_title_ref = None;
            let mut sample_paragraph_ref = None;
            let mut lines_ref = None;
            let mut root_builder = SceneBuilder::new()
                .background_color(Color::white())
                .round_scene_corners(self.round_scene_corners);
            root_builder.group().stack().expand().align(Alignment::top_center()).contents(
                |stack_builder| {
                    let text_size = size.height.min(size.width) / self.sample_size_divisor;
                    let ascent = self.sample_faces[self.sample_index].ascent(text_size);
                    let descent = self.sample_faces[self.sample_index].descent(text_size);
                    let sample_text_size = text_size / 3.0;
                    let baseline_location = size.height / 3.0;
                    let baseline_left = size.width * BASELINE_INDENT;
                    let baseline_width = size.width * (1.0 - BASELINE_INDENT * 2.0);
                    let padding = PADDING * size.width;
                    let label_right = size.width - baseline_left + padding;
                    let optional_bg_color = self.optional_bg_color();

                    let sample_title = stack_builder.text_with_data(
                        self.sample_faces[self.sample_index].clone(),
                        &self.sample_title,
                        text_size,
                        Point::zero(),
                        TextFacetOptions {
                            background_color: optional_bg_color,
                            ..TextFacetOptions::default()
                        },
                        StackMemberDataBuilder::new().top(baseline_location - ascent).build(),
                    );
                    sample_title_ref = Some(sample_title);

                    let ascent_x = baseline_left + size.width * BASELINE_INDENT / 5.0;
                    let paragraph_baseline_location = baseline_location + sample_text_size * 2.0;

                    let sample_paragraph = stack_builder.text_with_data(
                        self.sample_faces[self.sample_index].clone(),
                        &self.sample_paragraph,
                        sample_text_size,
                        Point::zero(),
                        TextFacetOptions {
                            max_width: Some(baseline_width),
                            background_color: optional_bg_color,
                            ..TextFacetOptions::default()
                        },
                        StackMemberDataBuilder::new().top(paragraph_baseline_location).build(),
                    );
                    sample_paragraph_ref = Some(sample_paragraph);

                    let mut lines = Vec::new();

                    lines.push(stack_builder.h_line_with_data(
                        baseline_width,
                        LINE_THICKNESS,
                        self.line_color,
                        None,
                        StackMemberDataBuilder::new().top(baseline_location).build(),
                    ));

                    lines.push(
                        stack_builder.v_line_with_data(
                            ascent,
                            LINE_THICKNESS,
                            self.line_color,
                            None,
                            StackMemberDataBuilder::new()
                                .top(baseline_location - ascent)
                                .left(ascent_x)
                                .build(),
                        ),
                    );
                    lines.push(stack_builder.v_line_with_data(
                        -descent,
                        LINE_THICKNESS,
                        self.line_color,
                        None,
                        StackMemberDataBuilder::new().top(baseline_location).left(ascent_x).build(),
                    ));

                    let label_size = size.height.min(size.width) / 30.0;

                    stack_builder.text_with_data(
                        self.label_face.clone(),
                        "baseline",
                        label_size,
                        Point::zero(),
                        TextFacetOptions {
                            visual: true,
                            vertical_alignment: TextVerticalAlignment::Center,
                            ..TextFacetOptions::default()
                        },
                        StackMemberDataBuilder::new()
                            .top(baseline_location - label_size)
                            .left(baseline_left + baseline_width + padding)
                            .height(label_size * 2.0)
                            .build(),
                    );

                    stack_builder.text_with_data(
                        self.label_face.clone(),
                        "ascent",
                        label_size,
                        point2(ascent_x - padding, baseline_location - ascent / 2.0),
                        TextFacetOptions {
                            visual: true,
                            vertical_alignment: TextVerticalAlignment::Center,
                            ..TextFacetOptions::default()
                        },
                        StackMemberDataBuilder::new()
                            .top(baseline_location - ascent)
                            .height(ascent)
                            .right(label_right)
                            .build(),
                    );

                    stack_builder.text_with_data(
                        self.label_face.clone(),
                        "descent",
                        label_size,
                        point2(ascent_x - padding, baseline_location - descent / 2.0),
                        TextFacetOptions {
                            visual: true,
                            vertical_alignment: TextVerticalAlignment::Center,
                            ..TextFacetOptions::default()
                        },
                        StackMemberDataBuilder::new()
                            .top(baseline_location)
                            .height(-descent)
                            .right(label_right)
                            .build(),
                    );
                    lines_ref = Some(lines);
                },
            );

            let scene = root_builder.build();
            SceneDetails {
                scene,
                sample_paragraph: sample_paragraph_ref.unwrap(),
                sample_title: sample_title_ref.unwrap(),
                lines: lines_ref.unwrap(),
            }
        });
        self.scene_details = Some(scene_details);
        Some(&mut self.scene_details.as_mut().unwrap().scene)
    }

    fn handle_keyboard_event(
        &mut self,
        _context: &mut ViewAssistantContext,
        _event: &input::Event,
        keyboard_event: &input::keyboard::Event,
    ) -> Result<(), Error> {
        const B: u32 = 'b' as u32;
        const C: u32 = 'c' as u32;
        const F: u32 = 'f' as u32;
        const K: u32 = 'k' as u32;
        const T: u32 = 't' as u32;
        const O: u32 = 'o' as u32;
        const PLUS: u32 = '+' as u32;
        const EQUALS: u32 = '=' as u32;
        const MINUS: u32 = '-' as u32;
        if let Some(code_point) = keyboard_event.code_point {
            if keyboard_event.phase == input::keyboard::Phase::Pressed
                || keyboard_event.phase == input::keyboard::Phase::Repeat
            {
                match code_point {
                    B => self.new_blank_text(),
                    T => self.new_sample_text(),
                    F => self.show_next_face(),
                    PLUS | EQUALS => self.increase_sample_size(),
                    MINUS => self.decrease_sample_size(),
                    C => self.toggle_line_color(),
                    K => self.toggle_round_scene_corners(),
                    O => self.toggle_text_background_color(),
                    _ => println!("code_point = {}", code_point),
                }
            }
        }
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    App::run(make_app_assistant::<FontAppAssistant>())
}
