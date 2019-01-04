// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fidl_fuchsia_ui_gfx::{self as gfx, ColorRgba};
use fuchsia_scenic::{ImportNode, Material, Rectangle, SessionPtr, ShapeNode};
use fuchsia_ui::{App, AppAssistant, ViewAssistant, ViewAssistantPtr, ViewKey};
use parking_lot::Mutex;
use std::{any::Any, cell::RefCell};

struct VoilaAppAssistant {}

impl AppAssistant for VoilaAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, session: &SessionPtr) -> Result<ViewAssistantPtr, Error> {
        Ok(Mutex::new(RefCell::new(Box::new(
            VoilaViewAssistant {
                background_node: ShapeNode::new(session.clone()),
                square_node: ShapeNode::new(session.clone()),
                width: 0.0,
                height: 0.0,
            },
        ))))
    }
}

struct VoilaViewAssistant {
    background_node: ShapeNode,
    square_node: ShapeNode,
    width: f32,
    height: f32,
}

impl ViewAssistant for VoilaViewAssistant {
    fn setup(
        &mut self, import_node: &ImportNode, session: &SessionPtr, _key: ViewKey,
    ) -> Result<(), Error> {
        import_node
            .resource()
            .set_event_mask(gfx::METRICS_EVENT_MASK);
        import_node.add_child(&self.background_node);
        let material = Material::new(session.clone());
        material.set_color(ColorRgba {
            red: 0x00,
            green: 0x00,
            blue: 0xff,
            alpha: 0xff,
        });
        self.background_node.set_material(&material);

        import_node.add_child(&self.square_node);
        let material = Material::new(session.clone());
        material.set_color(ColorRgba {
            red: 0xff,
            green: 0x00,
            blue: 0xff,
            alpha: 0xff,
        });
        self.square_node.set_material(&material);
        Ok(())
    }

    fn update(
        &mut self, _import_node: &ImportNode, session: &SessionPtr, width: f32, height: f32,
    ) -> Result<(), Error> {
        self.width = width;
        self.height = height;

        let center_x = self.width * 0.5;
        let center_y = self.height * 0.5;
        self.background_node
            .set_shape(&Rectangle::new(session.clone(), self.width, self.height));
        self.background_node
            .set_translation(center_x, center_y, 0.0);

        let square_size = self.width.min(self.height) * 0.5;
        self.square_node.set_shape(&Rectangle::new(
            session.clone(),
            square_size,
            square_size
        ));
        self.square_node
            .set_translation(center_x, center_y, 8.0);
        Ok(())
    }

    fn handle_message(&mut self, _message: &Any) {
    }
}

fn main() -> Result<(), Error> {
    let assistant = VoilaAppAssistant {};
    App::run(Box::new(assistant))
}
