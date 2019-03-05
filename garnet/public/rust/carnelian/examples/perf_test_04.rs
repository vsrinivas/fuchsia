// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use carnelian::{
    set_node_color, App, AppAssistant, Color, ViewAssistant, ViewAssistantContext,
    ViewAssistantPtr, ViewKey, ViewMessages,
};
use failure::Error;
use fuchsia_async::{self as fasync, Interval};
use fuchsia_scenic::{Rectangle, SessionPtr, ShapeNode};
use fuchsia_zircon::Duration;
use futures::StreamExt;

const COLOR_CODES: &[&str] = &[
    "#f80c12", "#ee1100", "#ff3311", "#ff4422", "#ff6644", "#ff9933", "#feae2d", "#ccbb33",
    "#d0c310", "#aacc22", "#69d025", "#22ccaa", "#12bdb9", "#11aabb", "#4444dd", "#3311bb",
    "#3b0cbd", "#442299",
];

const BAND_COUNT: usize = 64;

struct RainbowAppAssistant;

impl AppAssistant for RainbowAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(
        &mut self,
        key: ViewKey,
        _session: &SessionPtr,
    ) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(RainbowViewAssistant { key: key, colors: Vec::new(), index: 0 }))
    }
}

struct RainbowViewAssistant {
    key: ViewKey,
    colors: Vec<ShapeNode>,
    index: usize,
}

impl RainbowViewAssistant {
    fn setup_timer(key: ViewKey) {
        let timer = Interval::new(Duration::from_millis(100));
        let f = timer
            .map(move |_| {
                App::with(|app| {
                    app.send_message(key, &ViewMessages::Update);
                })
            })
            .collect::<()>();
        fasync::spawn(f);
    }
}

impl ViewAssistant for RainbowViewAssistant {
    fn setup(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        for _ in 0..BAND_COUNT {
            let node = ShapeNode::new(context.session.clone());
            context.root_node.add_child(&node);
            self.colors.push(node);
        }

        Self::setup_timer(self.key);
        Ok(())
    }

    fn update(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        let mut width = context.size.width;
        let mut height = context.size.height;
        let band_width = context.size.width / self.colors.len() as f32;
        let band_height = context.size.height / self.colors.len() as f32;
        let mut index = self.index;
        for band in &self.colors {
            let color_index = index % COLOR_CODES.len();
            let center_x = context.size.width * 0.5;
            let center_y = context.size.height * 0.5;
            band.set_shape(&Rectangle::new(context.session.clone(), width, height));
            band.set_translation(center_x, center_y, 0.0);
            width -= band_width;
            height -= band_height;
            set_node_color(
                context.session,
                &band,
                &Color::from_hash_code(COLOR_CODES[color_index])?,
            );
            index = index.wrapping_add(1);
        }
        self.index = self.index.wrapping_add(1);
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    let assistant = RainbowAppAssistant {};
    App::run(Box::new(assistant))
}
