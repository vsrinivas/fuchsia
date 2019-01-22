// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use carnelian::{App, AppAssistant, ViewAssistant, ViewAssistantContext, ViewAssistantPtr};
use failure::Error;
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_ui_gfx::{self as gfx, ColorRgba};
use fidl_fuchsia_ui_viewsv1::ViewProviderMarker;
use fidl_fuchsia_ui_viewsv1token::ViewOwnerMarker;
use fuchsia_app::client::{App as LaunchedApp, Launcher};
use fuchsia_scenic::{Circle, EntityNode, ImportNode, Material, Rectangle, SessionPtr, ShapeNode};
use parking_lot::Mutex;
use std::collections::BTreeMap;
use std::{any::Any, cell::RefCell};

mod layout;

use crate::layout::{layout, ChildViewData};

struct VoilaAppAssistant {}

impl AppAssistant for VoilaAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, session: &SessionPtr) -> Result<ViewAssistantPtr, Error> {
        Ok(Mutex::new(RefCell::new(Box::new(VoilaViewAssistant {
            background_node: ShapeNode::new(session.clone()),
            circle_node: ShapeNode::new(session.clone()),
            width: 0.0,
            height: 0.0,
            child_sessions: BTreeMap::new(),
        }))))
    }
}

struct VoilaViewAssistant {
    background_node: ShapeNode,
    circle_node: ShapeNode,
    width: f32,
    height: f32,
    child_sessions: BTreeMap<u32, ChildSessionData>,
}

/// Represents an emulated session and holds its internal state.
struct ChildSessionData {
    #[allow(unused)]
    app: LaunchedApp,
    view: ChildViewData,
}

impl VoilaViewAssistant {
    fn create_and_setup_view(
        &mut self, key: u32, url: &str, session: &SessionPtr,
        view_container: &fidl_fuchsia_ui_viewsv1::ViewContainerProxy, import_node: &ImportNode,
    ) -> Result<(), Error> {
        let app = Launcher::new()?.launch(url.to_string(), None)?;
        let view_provider = app.connect_to_service(ViewProviderMarker)?;
        let (view_owner_client, view_owner_server) = create_endpoints::<ViewOwnerMarker>()?;
        view_provider.create_view(view_owner_server, None)?;
        let host_node = EntityNode::new(session.clone());
        let host_import_token = host_node.export_as_request();
        import_node.add_child(&host_node);
        let view_data = ChildViewData::new(key, host_node);
        let session_data = ChildSessionData {
            app: app,
            view: view_data,
        };
        self.child_sessions.insert(key, session_data);
        view_container.add_child(key, view_owner_client, host_import_token)?;
        Ok(())
    }
}

impl ViewAssistant for VoilaViewAssistant {
    fn setup(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        context
            .import_node
            .resource()
            .set_event_mask(gfx::METRICS_EVENT_MASK);
        context.import_node.add_child(&self.background_node);
        let material = Material::new(context.session.clone());
        material.set_color(ColorRgba {
            red: 0x00,
            green: 0x00,
            blue: 0xff,
            alpha: 0xff,
        });
        self.background_node.set_material(&material);

        context.import_node.add_child(&self.circle_node);
        let material = Material::new(context.session.clone());
        material.set_color(ColorRgba {
            red: 0xff,
            green: 0x00,
            blue: 0xff,
            alpha: 0xff,
        });
        self.circle_node.set_material(&material);

        self.create_and_setup_view(
            1,
            "fuchsia-pkg://fuchsia.com/spinning_square_rs#meta/spinning_square_rs.cmx",
            context.session,
            context.view_container,
            context.import_node,
        )?;
        self.create_and_setup_view(
            2,
            "fuchsia-pkg://fuchsia.com/noodles#meta/noodles.cmx",
            context.session,
            context.view_container,
            context.import_node,
        )?;
        Ok(())
    }

    fn update(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        self.width = context.width;
        self.height = context.height;

        let center_x = self.width * 0.5;
        let center_y = self.height * 0.5;
        self.background_node.set_shape(&Rectangle::new(
            context.session.clone(),
            self.width,
            self.height,
        ));
        self.background_node
            .set_translation(center_x, center_y, 0.0);

        let circle_radius = self.width.min(self.height) * 0.25;
        self.circle_node
            .set_shape(&Circle::new(context.session.clone(), circle_radius));
        self.circle_node.set_translation(center_x, center_y, 8.0);

        let mut views: Vec<&mut ChildViewData> = self
            .child_sessions
            .iter_mut()
            .map(|(_key, child_session)| &mut child_session.view)
            .collect();
        layout(&mut views, context.view_container, self.width, self.height);
        Ok(())
    }

    fn handle_message(&mut self, _message: &Any) {}
}

fn main() -> Result<(), Error> {
    let assistant = VoilaAppAssistant {};
    App::run(Box::new(assistant))
}
