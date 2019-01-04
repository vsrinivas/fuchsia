// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fidl::encoding::OutOfLine;
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_math::{InsetF, RectF, SizeF};
use fidl_fuchsia_ui_gfx::{self as gfx, ColorRgba};
use fidl_fuchsia_ui_viewsv1::ViewProviderMarker;
use fidl_fuchsia_ui_viewsv1::{CustomFocusBehavior, ViewLayout, ViewProperties};
use fidl_fuchsia_ui_viewsv1token::ViewOwnerMarker;
use fuchsia_app::client::{App as LaunchedApp, Launcher};
use fuchsia_scenic::{EntityNode, ImportNode, Material, Rectangle, SessionPtr, ShapeNode};
use fuchsia_ui::{App, AppAssistant, ViewAssistant, ViewAssistantContext, ViewAssistantPtr};
use itertools::Itertools;
use parking_lot::Mutex;
use std::collections::BTreeMap;
use std::{any::Any, cell::RefCell};

fn inset(rect: &mut RectF, border: f32) {
    let inset = border.min(rect.width / 0.3).min(rect.height / 0.3);
    rect.x += inset;
    rect.y += inset;
    let inset_width = inset * 2.0;
    rect.width = rect.width - inset_width;
    rect.height = rect.height - inset_width;
}

struct EmbeddingAppAssistant {}

impl AppAssistant for EmbeddingAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, session: &SessionPtr) -> Result<ViewAssistantPtr, Error> {
        let app = Launcher::new()?.launch(
            "fuchsia-pkg://fuchsia.com/spinning_square_rs#meta/spinning_square_rs.cmx".to_string(),
            None,
        )?;
        Ok(Mutex::new(RefCell::new(Box::new(EmbeddingViewAssistant {
            background_node: ShapeNode::new(session.clone()),
            width: 0.0,
            height: 0.0,
            app,
            views: BTreeMap::new(),
        }))))
    }
}

#[allow(unused)]
struct ViewData {
    key: u32,
    bounds: Option<RectF>,
    host_node: EntityNode,
}

impl ViewData {
    pub fn new(key: u32, host_node: EntityNode) -> ViewData {
        ViewData {
            key: key,
            bounds: None,
            host_node: host_node,
        }
    }
}

struct EmbeddingViewAssistant {
    background_node: ShapeNode,
    width: f32,
    height: f32,
    #[allow(unused)]
    app: LaunchedApp,
    views: BTreeMap<u32, ViewData>,
}

impl EmbeddingViewAssistant {
    fn create_and_setup_view(
        &mut self, key: u32, session: &SessionPtr,
        view_container: &fidl_fuchsia_ui_viewsv1::ViewContainerProxy, import_node: &ImportNode,
    ) -> Result<(), Error> {
        let view_provider = self.app.connect_to_service(ViewProviderMarker)?;
        let (view_owner_client, view_owner_server) = create_endpoints::<ViewOwnerMarker>()?;
        view_provider.create_view(view_owner_server, None)?;
        let host_node = EntityNode::new(session.clone());
        let host_import_token = host_node.export_as_request();
        import_node.add_child(&host_node);
        let view_data = ViewData::new(key, host_node);
        self.views.insert(key, view_data);
        view_container.add_child(key, view_owner_client, host_import_token)?;
        Ok(())
    }

    pub fn layout(&mut self, view_container: &fidl_fuchsia_ui_viewsv1::ViewContainerProxy) {
        if !self.views.is_empty() {
            let num_tiles = self.views.len();

            let columns = (num_tiles as f32).sqrt().ceil() as usize;
            let rows = (columns + num_tiles - 1) / columns;
            let tile_height = (self.height / rows as f32).floor();

            for (row_index, view_chunk) in self
                .views
                .iter_mut()
                .chunks(columns)
                .into_iter()
                .enumerate()
            {
                let tiles_in_row = if row_index == rows - 1 && (num_tiles % columns) != 0 {
                    num_tiles % columns
                } else {
                    columns
                };
                let tile_width = (self.width / tiles_in_row as f32).floor();
                for (column_index, (_key, view)) in view_chunk.enumerate() {
                    let mut tile_bounds = RectF {
                        height: tile_height,
                        width: tile_width,
                        x: column_index as f32 * tile_width,
                        y: row_index as f32 * tile_height,
                    };
                    inset(&mut tile_bounds, 5.0);
                    let mut view_properties = ViewProperties {
                        custom_focus_behavior: Some(Box::new(CustomFocusBehavior {
                            allow_focus: true,
                        })),
                        view_layout: Some(Box::new(ViewLayout {
                            inset: InsetF {
                                bottom: 0.0,
                                left: 0.0,
                                right: 0.0,
                                top: 0.0,
                            },
                            size: SizeF {
                                width: tile_bounds.width,
                                height: tile_bounds.height,
                            },
                        })),
                    };
                    view_container
                        .set_child_properties(view.key, Some(OutOfLine(&mut view_properties)))
                        .unwrap();
                    view.host_node
                        .set_translation(tile_bounds.x, tile_bounds.y, 0.0);
                    view.bounds = Some(tile_bounds);
                }
            }
        }
    }
}

impl ViewAssistant for EmbeddingViewAssistant {
    fn setup(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        context
            .import_node
            .resource()
            .set_event_mask(gfx::METRICS_EVENT_MASK);
        context.import_node.add_child(&self.background_node);
        let material = Material::new(context.session.clone());
        material.set_color(ColorRgba {
            red: 0x00,
            green: 0xc0,
            blue: 0x00,
            alpha: 0xff,
        });
        self.background_node.set_material(&material);

        for n in 1..5 {
            self.create_and_setup_view(
                n,
                context.session,
                context.view_container,
                context.import_node,
            )?;
        }

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
        self.layout(context.view_container);
        Ok(())
    }

    fn handle_message(&mut self, _message: &Any) {
        // If spinning square had any custom messages they
        // would be handled here.
    }
}

fn main() -> Result<(), Error> {
    let assistant = EmbeddingAppAssistant {};
    App::run(Box::new(assistant))
}
