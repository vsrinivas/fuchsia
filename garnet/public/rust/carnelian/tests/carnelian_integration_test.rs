// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use carnelian::{
    set_node_color, App, AppAssistant, Color, ViewAssistant, ViewAssistantContext,
    ViewAssistantPtr, ViewKey,
};
use failure::Error;
use fuchsia_scenic::{Rectangle, SessionPtr, ShapeNode};

const BACKGROUND_Z: f32 = 0.0;
const SQUARE_Z: f32 = BACKGROUND_Z - 8.0;

struct IntegrationTestAppAssistant;

impl AppAssistant for IntegrationTestAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(
        &mut self,
        _: ViewKey,
        session: &SessionPtr,
    ) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(IntegrationTestViewAssistant {
            background_node: ShapeNode::new(session.clone()),
            square_node: ShapeNode::new(session.clone()),
        }))
    }
}

struct IntegrationTestViewAssistant {
    background_node: ShapeNode,
    square_node: ShapeNode,
}

impl ViewAssistant for IntegrationTestViewAssistant {
    fn setup(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        set_node_color(
            context.session(),
            &self.background_node,
            &Color { r: 0xb7, g: 0x41, b: 0x0e, a: 0xff },
        );
        set_node_color(
            context.session(),
            &self.square_node,
            &Color { r: 0xff, g: 0x00, b: 0xff, a: 0xff },
        );
        context.root_node().add_child(&self.background_node);
        context.root_node().add_child(&self.square_node);
        Ok(())
    }

    fn update(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        let center_x = context.size.width * 0.5;
        let center_y = context.size.height * 0.5;
        self.background_node.set_shape(&Rectangle::new(
            context.session().clone(),
            context.size.width,
            context.size.height,
        ));
        self.background_node.set_translation(center_x, center_y, BACKGROUND_Z);
        let square_size = context.size.width.min(context.size.height) * 0.6;
        self.square_node.set_shape(&Rectangle::new(
            context.session().clone(),
            square_size,
            square_size,
        ));
        self.square_node.set_translation(center_x, center_y, SQUARE_Z);
        Ok(())
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
