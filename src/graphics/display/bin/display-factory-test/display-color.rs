// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh::FromArgs,
    carnelian::{
        color::Color,
        make_app_assistant,
        render::{Composition, Context, PreClear, RenderExt},
        App, AppAssistant, RenderOptions, ViewAssistant, ViewAssistantContext, ViewAssistantPtr,
        ViewKey,
    },
    euclid::default::Rect,
    fuchsia_async as fasync,
    fuchsia_zircon::{AsHandleRef, Duration, Event, Signals},
    std::process,
};

const WHITE_COLOR: Color = Color { r: 255, g: 255, b: 255, a: 255 };

/// Display Color.
#[derive(Debug, FromArgs)]
#[argh(name = "display_color")]
struct Args {
    /// color (default is white)
    #[argh(option, from_str_fn(parse_color))]
    color: Option<Color>,

    /// seconds of delay before application exits (default is 1 second)
    #[argh(option, default = "1")]
    timeout: i64,
}

fn parse_color(value: &str) -> Result<Color, String> {
    Color::from_hash_code(value).map_err(|err| err.to_string())
}

#[derive(Default)]
struct DisplayColorAppAssistant {
    color: Option<Color>,
}

impl AppAssistant for DisplayColorAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        let args: Args = argh::from_env();
        self.color = args.color;
        let timer = fasync::Timer::new(fasync::Time::after(Duration::from_seconds(args.timeout)));
        fasync::Task::local(async move {
            timer.await;
            process::exit(1);
        })
        .detach();
        Ok(())
    }

    fn create_view_assistant(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(DisplayColorViewAssistant::new(self.color.take().unwrap_or(WHITE_COLOR))))
    }

    fn get_render_options(&self) -> RenderOptions {
        RenderOptions { ..RenderOptions::default() }
    }
}

struct DisplayColorViewAssistant {
    color: Color,
    composition: Composition,
}

impl DisplayColorViewAssistant {
    pub fn new(color: Color) -> Self {
        let color = Color { r: color.r, g: color.g, b: color.b, a: 255 };
        let composition = Composition::new(color);

        Self { color, composition }
    }
}

impl ViewAssistant for DisplayColorViewAssistant {
    fn setup(&mut self, _context: &ViewAssistantContext) -> Result<(), Error> {
        Ok(())
    }

    fn render(
        &mut self,
        render_context: &mut Context,
        ready_event: Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        // Clear |image| to color.
        let ext =
            RenderExt { pre_clear: Some(PreClear { color: self.color }), ..Default::default() };
        let image = render_context.get_current_image(context);
        render_context.render(&self.composition, Some(Rect::zero()), image, &ext);

        ready_event.as_handle_ref().signal(Signals::NONE, Signals::EVENT_SIGNALED)?;
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    App::run(make_app_assistant::<DisplayColorAppAssistant>())
}
