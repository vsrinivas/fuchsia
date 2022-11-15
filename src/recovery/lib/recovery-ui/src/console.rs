// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use carnelian::{
    color::Color,
    drawing::{linebreak_text, FontFace},
    render::Context as RenderContext,
    scene::{
        facets::{TextFacetOptions, TextHorizontalAlignment, TextVerticalAlignment},
        layout::{CrossAxisAlignment, Flex, FlexOptions, MainAxisAlignment, MainAxisSize},
        scene::{Scene, SceneBuilder},
    },
    Message, Point, Size, ViewAssistant, ViewAssistantContext,
};
use fuchsia_zircon::Event;

const TEXT_FONT_SIZE: f32 = 24.0;
const MAX_CONSOLE_LINE_COUNT: usize = 20;
const CONSOLE_WELCOME_MESSAGE: &str = "Welcome. Tap top-left to exit debug console.";

#[derive(Debug)]
pub enum ConsoleMessages {
    // Appends provided text to the Console output.
    AddText(String),
}

// Caches the composited Scene built in console_scene().
pub struct SceneDetails {
    pub(crate) scene: Scene,
}

pub struct ConsoleViewAssistant {
    font_face: FontFace,
    // SceneDetails created and cached by console_scene().
    scene_details: Option<SceneDetails>,
    // Console output lines.
    lines: Vec<String>,
    // Width of the area where the view will be rendered.
    layout_width: Option<f32>,
}

impl ConsoleViewAssistant {
    pub fn new(font_face: FontFace) -> Result<ConsoleViewAssistant, Error> {
        // Fill with blanks to "maximize" the area consumed for display.
        let mut lines: Vec<String> = Vec::new();
        for _ in 1..MAX_CONSOLE_LINE_COUNT {
            lines.push("".to_string());
        }
        lines.push(CONSOLE_WELCOME_MESSAGE.to_string());

        Ok(ConsoleViewAssistant { font_face, scene_details: None, lines, layout_width: None })
    }

    // Returns cached SceneDetails if available, otherwise builds one from scratch and caches result.
    pub fn console_scene(&mut self, context: &ViewAssistantContext) -> SceneDetails {
        let scene_details = self.scene_details.take().unwrap_or_else(|| {
            let target_size = context.size;
            self.layout_width = Some(target_size.width.into());

            let mut builder = SceneBuilder::new().background_color(Color::new());
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
                    builder.text(
                        self.font_face.clone(),
                        &self.lines.join("\n"),
                        TEXT_FONT_SIZE,
                        Point::zero(),
                        TextFacetOptions {
                            horizontal_alignment: TextHorizontalAlignment::Left,
                            vertical_alignment: TextVerticalAlignment::Top,
                            color: Color::green(),
                            ..TextFacetOptions::default()
                        },
                    );
                    builder.end_group();
                });

            // Create a scene from the builder constructed above.
            let mut scene = builder.build();

            scene.layout(target_size);
            SceneDetails { scene }
        });

        scene_details
    }
}

impl ViewAssistant for ConsoleViewAssistant {
    fn resize(&mut self, _new_size: &Size) -> Result<(), Error> {
        self.scene_details = None;

        Ok(())
    }

    // Called repeatedly from ProxyViewAssistant's render() when Console is active.
    fn render(
        &mut self,
        render_context: &mut RenderContext,
        ready_event: Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        let mut scene_details = self.console_scene(context);
        scene_details.scene.render(render_context, ready_event, context)?;
        self.scene_details = Some(scene_details);

        context.request_render();

        Ok(())
    }

    // Adds text provided by ConsoleMessage::AddText message as a Console line.
    fn handle_message(&mut self, message: Message) {
        if let Some(console_message) = message.downcast_ref::<ConsoleMessages>() {
            match console_message {
                ConsoleMessages::AddText(text) => {
                    for line in text.split("\n") {
                        self.lines.push(line.to_string());
                    }

                    if let Some(max_width) = self.layout_width {
                        let mut wrapped_lines = Vec::new();

                        for line in &self.lines {
                            wrapped_lines.extend(linebreak_text(
                                &self.font_face,
                                TEXT_FONT_SIZE,
                                &line,
                                max_width,
                            ));
                        }

                        self.lines = wrapped_lines;
                    }

                    if self.lines.len() > MAX_CONSOLE_LINE_COUNT {
                        self.lines.drain(0..self.lines.len() - MAX_CONSOLE_LINE_COUNT);
                    }

                    // Force scene to be rebuilt with new lines on next render().
                    self.scene_details = None;
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::font;
    use carnelian::make_message;

    const TEST_MESSAGE: &str = "Test message";
    const SMALL_MULTILINE_TEST_MESSAGE: &str = "1\n2\n3\n4\n5";
    const GIANT_MULTILINE_TEST_MESSAGE: &str =
        "1\n2\n3\n4\n5\n6\n7\n8\n9\nA\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK\nL\nM\nN\nO\nP\nQ\nR\nS\nT";
    const LONG_TEST_MESSAGE: &str =
        "this is a very long message that will get split into multiple lines because the screen is \
        not wide enough to hold it";

    #[test]
    fn test_add_text_message_modifies_lines() -> std::result::Result<(), anyhow::Error> {
        let font_face = font::load_default_font_face()?;
        let mut console_view_assistant = ConsoleViewAssistant::new(font_face).unwrap();

        // Verify line buffer is "initialized full" after console_view_assistant is constructed.
        assert_eq!(console_view_assistant.lines.len(), MAX_CONSOLE_LINE_COUNT);

        // Add text to Console by requesting it handle an "AddText" message.
        console_view_assistant
            .handle_message(make_message(ConsoleMessages::AddText(TEST_MESSAGE.to_string())));

        // Verify old data has been culled.
        assert_eq!(console_view_assistant.lines.len(), MAX_CONSOLE_LINE_COUNT);

        // Verify our newest Console message contains expected value.
        assert_eq!(
            console_view_assistant.lines.last().unwrap().to_string(),
            TEST_MESSAGE.to_string()
        );

        Ok(())
    }

    #[test]
    fn test_small_multiline_messages_get_split() -> std::result::Result<(), anyhow::Error> {
        let font_face = font::load_default_font_face()?;
        let mut console_view_assistant = ConsoleViewAssistant::new(font_face).unwrap();

        // Add a multiline message to Console.
        console_view_assistant.handle_message(make_message(ConsoleMessages::AddText(
            SMALL_MULTILINE_TEST_MESSAGE.to_string(),
        )));

        // Verify the tail of our Console messages are the individual lines.
        let expected_lines = SMALL_MULTILINE_TEST_MESSAGE.split("\n").collect::<Vec<&str>>();
        for (i, expected) in expected_lines.iter().enumerate() {
            let actual = &console_view_assistant.lines
                [console_view_assistant.lines.len() - expected_lines.len() + i];
            assert_eq!(expected, actual, "Multiline message not split as expected");
        }

        // Verify our overall line count still "fills the screen."
        assert_eq!(console_view_assistant.lines.len(), MAX_CONSOLE_LINE_COUNT);

        Ok(())
    }

    #[test]
    fn test_giant_multiline_messages_get_split() -> std::result::Result<(), anyhow::Error> {
        let font_face = font::load_default_font_face()?;
        let mut console_view_assistant = ConsoleViewAssistant::new(font_face).unwrap();

        // Add a giant (more rows than the screen can display) multiline message to Console.
        console_view_assistant.handle_message(make_message(ConsoleMessages::AddText(
            GIANT_MULTILINE_TEST_MESSAGE.to_string(),
        )));

        // Verify the lines in our Console are just the "underflow" lines from the giant message.
        let expected_lines = &GIANT_MULTILINE_TEST_MESSAGE.rsplit("\n").collect::<Vec<&str>>()
            [..MAX_CONSOLE_LINE_COUNT];
        for (i, expected) in expected_lines.iter().rev().enumerate() {
            let actual = &console_view_assistant.lines
                [console_view_assistant.lines.len() - expected_lines.len() + i];
            assert_eq!(expected, actual, "Multiline message not split as expected");
        }

        // Verify our overall line count still "fills the screen."
        assert_eq!(console_view_assistant.lines.len(), MAX_CONSOLE_LINE_COUNT);

        Ok(())
    }

    #[test]
    fn test_long_messages_get_split() -> std::result::Result<(), anyhow::Error> {
        let font_face = font::load_default_font_face()?;
        let max_width = 100f32;
        let expected_lines =
            linebreak_text(&font_face, TEXT_FONT_SIZE, LONG_TEST_MESSAGE, max_width);
        assert!(expected_lines.len() > 1);

        let mut console_view_assistant = ConsoleViewAssistant::new(font_face).unwrap();
        console_view_assistant.layout_width = Some(max_width);
        console_view_assistant
            .handle_message(make_message(ConsoleMessages::AddText(LONG_TEST_MESSAGE.to_string())));

        // Verify the lines in our Console are split into multiple lines.
        for (i, expected) in expected_lines.iter().enumerate() {
            let actual = &console_view_assistant.lines
                [console_view_assistant.lines.len() - expected_lines.len() + i];
            assert_eq!(expected, actual, "Message not split as expected");
        }

        // Verify our overall line count still "fills the screen."
        assert_eq!(console_view_assistant.lines.len(), MAX_CONSOLE_LINE_COUNT);

        Ok(())
    }

    #[test]
    fn test_long_messages_get_split_after_multiple_messages(
    ) -> std::result::Result<(), anyhow::Error> {
        let font_face = font::load_default_font_face()?;
        let max_width = 300f32;
        let mut expected_lines =
            linebreak_text(&font_face, TEXT_FONT_SIZE, LONG_TEST_MESSAGE, max_width);

        // Need two sets of lines for the two messages.
        expected_lines.extend(expected_lines.clone());
        assert!(expected_lines.len() > 1);

        let mut console_view_assistant = ConsoleViewAssistant::new(font_face).unwrap();
        console_view_assistant.layout_width = Some(max_width);
        console_view_assistant
            .handle_message(make_message(ConsoleMessages::AddText(LONG_TEST_MESSAGE.to_string())));

        // A second message should still be split like the first.
        console_view_assistant
            .handle_message(make_message(ConsoleMessages::AddText(LONG_TEST_MESSAGE.to_string())));

        // Verify the lines in our Console are split into multiple lines.
        for (i, expected) in expected_lines.iter().enumerate() {
            let actual = &console_view_assistant.lines
                [console_view_assistant.lines.len() - expected_lines.len() + i];
            assert_eq!(expected, actual, "Message not split as expected");
        }

        // Verify our overall line count still "fills the screen."
        assert_eq!(console_view_assistant.lines.len(), MAX_CONSOLE_LINE_COUNT);

        Ok(())
    }

    #[test]
    fn test_long_paragraphs_get_split_preserving_paragraph_breaks(
    ) -> std::result::Result<(), anyhow::Error> {
        let font_face = font::load_default_font_face()?;
        let paragraphs = format!("{msg}\n{msg}\n{msg}", msg = LONG_TEST_MESSAGE);

        let max_width = 300f32;
        let lines = linebreak_text(&font_face, TEXT_FONT_SIZE, LONG_TEST_MESSAGE, max_width);
        let mut expected_lines = Vec::new();

        // Create three paragraphs to match `paragraphs`.
        expected_lines.extend(lines.clone());
        expected_lines.extend(lines.clone());
        expected_lines.extend(lines);
        assert!(expected_lines.len() > 1);

        let mut console_view_assistant = ConsoleViewAssistant::new(font_face).unwrap();
        console_view_assistant.layout_width = Some(max_width);
        console_view_assistant.handle_message(make_message(ConsoleMessages::AddText(paragraphs)));

        // Verify the lines in our Console are split into multiple lines.
        for (i, expected) in expected_lines.iter().enumerate() {
            let actual = &console_view_assistant.lines
                [console_view_assistant.lines.len() - expected_lines.len() + i];
            assert_eq!(expected, actual, "Message not split as expected");
        }

        // Verify our overall line count still "fills the screen."
        assert_eq!(console_view_assistant.lines.len(), MAX_CONSOLE_LINE_COUNT);

        Ok(())
    }
}
