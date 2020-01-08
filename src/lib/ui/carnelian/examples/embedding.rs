// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use carnelian::{
    make_app_assistant, set_node_color, App, AppAssistant, Color, Coord, Size, ViewAssistant,
    ViewAssistantContext, ViewAssistantPtr, ViewKey,
};
use fidl_fuchsia_math::RectF;
use fidl_fuchsia_ui_app::ViewProviderMarker;
use fidl_fuchsia_ui_gfx::{BoundingBox, Vec3, ViewProperties};
use fuchsia_component::client::{launch, launcher, App as LaunchedApp};
use fuchsia_scenic::{EntityNode, Rectangle, SessionPtr, ShapeNode, ViewHolder, ViewTokenPair};
use itertools::Itertools;
use std::collections::BTreeMap;

const BACKGROUND_Z: f32 = 0.0;
const EMBED_Z: f32 = BACKGROUND_Z - 0.1;

fn inset(rect: &mut RectF, border: f32) {
    let inset = border.min(rect.width / 0.3).min(rect.height / 0.3);
    rect.x += inset;
    rect.y += inset;
    let inset_width = inset * 2.0;
    rect.width = rect.width - inset_width;
    rect.height = rect.height - inset_width;
}

#[derive(Default)]
struct EmbeddingAppAssistant;

impl AppAssistant for EmbeddingAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(
        &mut self,
        _: ViewKey,
        session: &SessionPtr,
    ) -> Result<ViewAssistantPtr, Error> {
        let app = launch(
            &launcher()?,
            "fuchsia-pkg://fuchsia.com/spinning_square_rs#meta/spinning_square_rs.cmx".to_string(),
            None,
        )?;
        Ok(Box::new(EmbeddingViewAssistant {
            background_node: ShapeNode::new(session.clone()),
            size: Size::zero(),
            app,
            views: BTreeMap::new(),
        }))
    }
}

struct ViewData {
    bounds: Option<RectF>,
    host_node: EntityNode,
    host_view_holder: ViewHolder,
}

impl ViewData {
    pub fn new(host_node: EntityNode, host_view_holder: ViewHolder) -> ViewData {
        ViewData { bounds: None, host_node: host_node, host_view_holder: host_view_holder }
    }
}

struct EmbeddingViewAssistant {
    background_node: ShapeNode,
    size: Size,
    #[allow(unused)]
    app: LaunchedApp,
    views: BTreeMap<u32, ViewData>,
}

impl EmbeddingViewAssistant {
    fn create_and_setup_view(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        let token_pair = ViewTokenPair::new()?;

        let view_provider = self.app.connect_to_service::<ViewProviderMarker>()?;
        view_provider.create_view(token_pair.view_token.value, None, None)?;

        let holder_node = EntityNode::new(context.session().clone());
        let view_holder = ViewHolder::new(
            context.session().clone(),
            token_pair.view_holder_token,
            Some(String::from("Carnelian Embedded View")),
        );
        holder_node.attach(&view_holder);
        context.root_node().add_child(&holder_node);

        let view_data = ViewData::new(holder_node, view_holder);
        self.views.insert(view_data.host_view_holder.id(), view_data);

        Ok(())
    }

    pub fn layout(&mut self) {
        if !self.views.is_empty() {
            let num_tiles = self.views.len();

            let columns = (num_tiles as f32).sqrt().ceil() as usize;
            let rows = (columns + num_tiles - 1) / columns;
            let tile_height = (self.size.height / rows as Coord).floor();

            for (row_index, view_chunk) in
                self.views.iter_mut().chunks(columns).into_iter().enumerate()
            {
                let tiles_in_row = if row_index == rows - 1 && (num_tiles % columns) != 0 {
                    num_tiles % columns
                } else {
                    columns
                };
                let tile_width = (self.size.width / tiles_in_row as Coord).floor();
                for (column_index, (_key, view)) in view_chunk.enumerate() {
                    let mut tile_bounds = RectF {
                        height: tile_height,
                        width: tile_width,
                        x: column_index as f32 * tile_width,
                        y: row_index as f32 * tile_height,
                    };
                    inset(&mut tile_bounds, 5.0);
                    let view_properties = ViewProperties {
                        bounding_box: BoundingBox {
                            min: Vec3 { x: 0.0, y: 0.0, z: 0.0 },
                            max: Vec3 { x: tile_bounds.width, y: tile_bounds.height, z: 0.0 },
                        },
                        inset_from_min: Vec3 { x: 0.0, y: 0.0, z: 0.0 },
                        inset_from_max: Vec3 { x: 0.0, y: 0.0, z: 0.0 },
                        focus_change: true,
                        downward_input: false,
                    };
                    view.host_view_holder.set_view_properties(view_properties);
                    view.host_node.set_translation(tile_bounds.x, tile_bounds.y, EMBED_Z);
                    view.bounds = Some(tile_bounds);
                }
            }
        }
    }
}

impl ViewAssistant for EmbeddingViewAssistant {
    fn setup(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        set_node_color(
            context.session(),
            &self.background_node,
            &Color { r: 0x00, g: 0xc0, b: 0x00, a: 0xff },
        );
        context.root_node().add_child(&self.background_node);

        for _ in 1..5 {
            self.create_and_setup_view(&context)?;
        }

        Ok(())
    }

    fn update(&mut self, context: &ViewAssistantContext<'_>) -> Result<(), Error> {
        self.size = context.size;

        let center_x = self.size.width * 0.5;
        let center_y = self.size.height * 0.5;
        self.background_node.set_shape(&Rectangle::new(
            context.session().clone(),
            self.size.width,
            self.size.height,
        ));
        self.background_node.set_translation(center_x, center_y, BACKGROUND_Z);
        self.layout();
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    App::run(make_app_assistant::<EmbeddingAppAssistant>())
}
