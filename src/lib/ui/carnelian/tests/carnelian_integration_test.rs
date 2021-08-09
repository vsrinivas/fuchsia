// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use carnelian::{
    color::Color,
    make_app_assistant,
    render::{Composition, Context as RenderContext, PreClear, RenderExt},
    App, AppAssistant, ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey,
};
use fuchsia_zircon::{AsHandleRef, Event, Signals};

#[derive(Default)]
struct IntegrationTestAppAssistant;

impl AppAssistant for IntegrationTestAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        let bg_color = Color::from_hash_code("#EBD5B3")?;
        let composition = Composition::new(bg_color);
        Ok(Box::new(IntegrationTestViewAssistant { bg_color, composition }))
    }
}

struct IntegrationTestViewAssistant {
    bg_color: Color,
    composition: Composition,
}

impl ViewAssistant for IntegrationTestViewAssistant {
    fn setup(&mut self, _: &ViewAssistantContext) -> Result<(), Error> {
        Ok(())
    }

    fn render(
        &mut self,
        render_context: &mut RenderContext,
        ready_event: Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        let image = render_context.get_current_image(context);
        let ext =
            RenderExt { pre_clear: Some(PreClear { color: self.bg_color }), ..Default::default() };
        render_context.render(&mut self.composition, None, image, &ext);
        ready_event.as_handle_ref().signal(Signals::NONE, Signals::EVENT_SIGNALED)?;
        context.request_render();
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    println!("carnelian_integration_test");
    App::run(make_app_assistant::<IntegrationTestAppAssistant>())
}

#[cfg(test)]
mod test {

    use crate::IntegrationTestAppAssistant;
    use carnelian::{make_app_assistant, App};

    #[test]
    fn carnelian_integration_test() -> std::result::Result<(), anyhow::Error> {
        println!("carnelian_integration_test");
        App::test(make_app_assistant::<IntegrationTestAppAssistant>())
    }
}
