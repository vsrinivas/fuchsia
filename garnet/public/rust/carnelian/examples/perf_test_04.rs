// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use carnelian::{
    make_app_assistant, set_node_color, AnimationMode, App, AppAssistant, Color, ViewAssistant,
    ViewAssistantContext, ViewAssistantPtr, ViewKey,
};
use failure::Error;
use fuchsia_scenic::{Rectangle, SessionPtr, ShapeNode};

const COLOR_CODES: &[&str] = &[
    "#f80c12", "#ee1100", "#ff3311", "#ff4422", "#ff6644", "#ff9933", "#feae2d", "#ccbb33",
    "#d0c310", "#aacc22", "#69d025", "#22ccaa", "#12bdb9", "#11aabb", "#4444dd", "#3311bb",
    "#3b0cbd", "#442299",
];

const BAND_COUNT: usize = 64;
const INITIAL_Z: f32 = 0.0;

#[derive(Default)]
struct RainbowAppAssistant;

impl AppAssistant for RainbowAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(
        &mut self,
        _: ViewKey,
        _session: &SessionPtr,
    ) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(RainbowViewAssistant { colors: Vec::new(), index: 0 }))
    }
}

struct RainbowViewAssistant {
    colors: Vec<ShapeNode>,
    index: usize,
}

impl RainbowViewAssistant {}

impl ViewAssistant for RainbowViewAssistant {
    fn setup(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        for _ in 0..BAND_COUNT {
            let node = ShapeNode::new(context.session().clone());
            context.root_node().add_child(&node);
            self.colors.push(node);
        }

        Ok(())
    }

    fn update(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        let mut width = context.size.width;
        let mut height = context.size.height;
        let band_width = context.size.width / self.colors.len() as f32;
        let band_height = context.size.height / self.colors.len() as f32;
        let mut index = self.index;
        let mut z = INITIAL_Z;
        for band in &self.colors {
            let color_index = index % COLOR_CODES.len();
            let center_x = context.size.width * 0.5;
            let center_y = context.size.height * 0.5;
            band.set_shape(&Rectangle::new(context.session().clone(), width, height));
            band.set_translation(center_x, center_y, z);
            z -= 0.01;
            width -= band_width;
            height -= band_height;
            set_node_color(
                context.session(),
                &band,
                &Color::from_hash_code(COLOR_CODES[color_index])?,
            );
            index = index.wrapping_add(1);
        }
        self.index = self.index.wrapping_add(1);
        Ok(())
    }

    fn initial_animation_mode(&mut self) -> AnimationMode {
        return AnimationMode::EveryFrame;
    }
}

fn main() -> Result<(), Error> {
    App::run(make_app_assistant::<RainbowAppAssistant>())
}
