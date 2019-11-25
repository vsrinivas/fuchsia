// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use carnelian::{
    AnimationMode, App, AppAssistant, Color, Point, Rect, ViewAssistant, ViewAssistantContext,
    ViewAssistantPtr, ViewKey, ViewMode,
};
use failure::Error;
use fuchsia_zircon::Duration;

struct IntegrationTestAppAssistant;

impl AppAssistant for IntegrationTestAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant_canvas(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        let bg_color = Color::from_hash_code("#EBD5B3")?;
        Ok(Box::new(IntegrationTestViewAssistant { bg_color }))
    }

    fn get_mode(&self) -> ViewMode {
        ViewMode::Canvas
    }
}

struct IntegrationTestViewAssistant {
    bg_color: Color,
}

impl ViewAssistant for IntegrationTestViewAssistant {
    fn setup(&mut self, _: &ViewAssistantContext<'_>) -> Result<(), Error> {
        Ok(())
    }

    fn update(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        let canvas = &mut context.canvas.as_ref().unwrap().borrow_mut();

        let bounds = Rect::new(Point::zero(), context.size);
        canvas.fill_rect(&bounds, self.bg_color);
        Ok(())
    }

    fn initial_animation_mode(&mut self) -> AnimationMode {
        return AnimationMode::RefreshRate(Duration::from_millis(1));
    }
}

fn main() -> Result<(), Error> {
    println!("carnelian_integration_test");
    let assistant = IntegrationTestAppAssistant {};
    App::test(Box::new(assistant))
}

#[cfg(test)]
mod test {

    use crate::IntegrationTestAppAssistant;
    use carnelian::App;

    #[test]
    fn carnelain_integration_test() -> std::result::Result<(), failure::Error> {
        println!("carnelian_integration_test");
        let assistant = IntegrationTestAppAssistant {};
        App::test(Box::new(assistant))
    }
}
