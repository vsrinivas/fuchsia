// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use carnelian::{
    color::Color,
    drawing::{FontFace, GlyphMap, Text},
    make_message,
    render::{
        BlendMode, Composition, Context as RenderContext, Fill, FillRule, Layer, PreClear,
        RenderExt, Style,
    },
    App, AppAssistant, AppAssistantPtr, AppContext, AssistantCreatorFunc, LocalBoxFuture, Message,
    Point, ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey,
};
use fuchsia_async as fasync;
use fuchsia_zircon::{AsHandleRef, Event, Signals};
use futures::StreamExt;

mod setup;

// TODO(33662): Remove this when storage reinitialization is used.
#[allow(dead_code)]
mod storage;

static FONT_DATA: &'static [u8] =
    include_bytes!("../../../../prebuilt/third_party/fonts/robotoslab/RobotoSlab-Regular.ttf");

enum RecoveryMessages {
    EventReceived,
}

struct RecoveryAppAssistant {
    app_context: AppContext,
}

impl RecoveryAppAssistant {
    pub fn new(app_context: &AppContext) -> Self {
        Self { app_context: app_context.clone() }
    }
}

impl AppAssistant for RecoveryAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, view_key: ViewKey) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(RecoveryViewAssistant::new(
            &self.app_context,
            view_key,
            "Fuchsia System Recovery",
            "Waiting...",
        )?))
    }
}

struct RecoveryViewAssistant<'a> {
    face: FontFace<'a>,
    bg_color: Color,
    composition: Composition,
    glyphs: GlyphMap,
    heading: String,
    heading_label: Option<Text>,
    body: String,
    body_label: Option<Text>,
}

impl<'a> RecoveryViewAssistant<'a> {
    fn new(
        app_context: &AppContext,
        view_key: ViewKey,
        heading: &str,
        body: &str,
    ) -> Result<RecoveryViewAssistant<'a>, Error> {
        let mut receiver = setup::start_server()?;
        let local_app_context = app_context.clone();
        let f = async move {
            while let Some(_event) = receiver.next().await {
                println!("recovery: received request");
                local_app_context
                    .queue_message(view_key, make_message(RecoveryMessages::EventReceived));
            }
        };

        fasync::spawn_local(f);

        let bg_color = Color { r: 255, g: 0, b: 255, a: 255 };
        let composition = Composition::new(bg_color);
        let face = FontFace::new(FONT_DATA)?;

        Ok(RecoveryViewAssistant {
            face,
            bg_color,
            composition,
            glyphs: GlyphMap::new(),
            heading: heading.to_string(),
            heading_label: None,
            body: body.to_string(),
            body_label: None,
        })
    }
}

impl ViewAssistant for RecoveryViewAssistant<'_> {
    fn setup(&mut self, _context: &ViewAssistantContext) -> Result<(), Error> {
        Ok(())
    }

    fn render(
        &mut self,
        render_context: &mut RenderContext,
        ready_event: Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        let text_size = context.size.height / 12.0;

        let fg_color = Color { r: 255, g: 255, b: 255, a: 255 };

        self.heading_label = Some(Text::new(
            render_context,
            &self.heading,
            text_size,
            100,
            &self.face,
            &mut self.glyphs,
        ));

        let heading_label = self.heading_label.as_ref().expect("label");
        let heading_label_size = heading_label.bounding_box.size;
        let heading_label_offet = Point::new(
            (context.size.width / 2.0) - (heading_label_size.width / 2.0),
            (context.size.height / 4.0) - (heading_label_size.height / 2.0),
        );

        let heading_label_offet = heading_label_offet.to_i32().to_vector();

        let heading_label_layer = Layer {
            raster: heading_label.raster.clone().translate(heading_label_offet),
            style: Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(fg_color),
                blend_mode: BlendMode::Over,
            },
        };

        self.body_label = Some(Text::new(
            render_context,
            &self.body,
            text_size,
            100,
            &self.face,
            &mut self.glyphs,
        ));

        let body_label = self.body_label.as_ref().expect("body_label");
        let body_label_size = body_label.bounding_box.size;
        let body_label_offet = Point::new(
            (context.size.width / 2.0) - (body_label_size.width / 2.0),
            (context.size.height * 0.75) - (body_label_size.height / 2.0),
        );

        let body_label_offet = body_label_offet.to_i32().to_vector();

        let body_label_layer = Layer {
            raster: body_label.raster.clone().translate(body_label_offet),
            style: Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(fg_color),
                blend_mode: BlendMode::Over,
            },
        };

        self.composition.replace(
            ..,
            std::iter::once(heading_label_layer).chain(std::iter::once(body_label_layer)),
        );

        let image = render_context.get_current_image(context);
        let ext =
            RenderExt { pre_clear: Some(PreClear { color: self.bg_color }), ..Default::default() };
        render_context.render(&self.composition, None, image, &ext);
        ready_event.as_handle_ref().signal(Signals::NONE, Signals::EVENT_SIGNALED)?;
        Ok(())
    }

    fn handle_message(&mut self, message: Message) {
        if let Some(message) = message.downcast_ref::<RecoveryMessages>() {
            match message {
                RecoveryMessages::EventReceived => {
                    self.body = "Got event".to_string();
                }
            }
        }
    }
}

fn make_app_assistant_fut(
    app_context: &AppContext,
) -> LocalBoxFuture<'_, Result<AppAssistantPtr, Error>> {
    let f = async move {
        let assistant = Box::new(RecoveryAppAssistant::new(app_context));
        Ok::<AppAssistantPtr, Error>(assistant)
    };
    Box::pin(f)
}

pub fn make_app_assistant() -> AssistantCreatorFunc {
    Box::new(make_app_assistant_fut)
}

fn main() -> Result<(), Error> {
    println!("recovery: started");
    App::run(make_app_assistant())
}

#[cfg(test)]
mod tests {
    use super::make_app_assistant;
    use carnelian::App;

    #[test]
    fn test_ui() -> std::result::Result<(), anyhow::Error> {
        let assistant = make_app_assistant();
        App::test(assistant)
    }
}
