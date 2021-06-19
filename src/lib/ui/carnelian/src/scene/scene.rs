// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{
    facets::{FacetId, FacetPtr, RectangleFacet, SpacingFacet, TextFacet, TextFacetOptions},
    group::{GroupId, GroupMap, GroupMember},
    layout::{ArrangerPtr, Axis, Flex, FlexBuilder, StackBuilder},
    raster_for_corner_knockouts, BlendMode, FacetEntry, FacetMap, FillRule, IdGenerator,
    LayerGroup, Rendering,
};
use crate::{
    color::Color,
    drawing::{path_for_cursor, FontFace},
    render::{
        Composition, Context as RenderContext, Fill, Layer, PreClear, Raster, RenderExt, Style,
    },
    Coord, IntPoint, Point, Rect, Size, ViewAssistantContext,
};
use anyhow::{bail, Error};
use euclid::vec2;
use fuchsia_zircon::{AsHandleRef, Event, Signals};
use std::{
    any::Any,
    collections::{BTreeMap, HashMap},
    fmt::{self, Debug},
};

fn create_mouse_cursor_raster(render_context: &mut RenderContext) -> Raster {
    let path = path_for_cursor(Point::zero(), 20.0, render_context);
    let mut raster_builder = render_context.raster_builder().expect("raster_builder");
    raster_builder.add(&path, None);
    raster_builder.build()
}

fn cursor_layer(cursor_raster: &Raster, position: IntPoint, color: &Color) -> Layer {
    Layer {
        raster: cursor_raster.clone().translate(position.to_vector()),
        clip: None,
        style: Style {
            fill_rule: FillRule::NonZero,
            fill: Fill::Solid(*color),
            blend_mode: BlendMode::Over,
        },
    }
}

fn cursor_layer_pair(cursor_raster: &Raster, position: IntPoint) -> Vec<Layer> {
    let black_pos = position + vec2(-1, -1);
    vec![
        cursor_layer(cursor_raster, position, &Color::fuchsia()),
        cursor_layer(cursor_raster, black_pos, &Color::new()),
    ]
}

type LayerMap = BTreeMap<FacetId, LayerGroup>;

/// Options for creating a scene.
pub struct SceneOptions {
    /// Background color.
    pub background_color: Color,
    /// True if, when running without Scenic, if the scene
    /// should round the corners of the screen to match the
    /// presentation that sometimes occurs with Scenic.
    pub round_scene_corners: bool,
    /// True if, when running without Scenic, the mouse cursor should
    /// be drawn.
    pub enable_mouse_cursor: bool,
    /// Option arranger for the root group.
    pub root_arranger: Option<ArrangerPtr>,
}

impl SceneOptions {
    /// Create the default scene options with a specified background color.
    pub fn with_background_color(background_color: Color) -> Self {
        Self { background_color, ..Self::default() }
    }
}

impl Default for SceneOptions {
    fn default() -> Self {
        Self {
            background_color: Color::new(),
            round_scene_corners: false,
            enable_mouse_cursor: true,
            root_arranger: None,
        }
    }
}

/// A Carnelian scene is responsible for turning a collection of facets and groups
/// into rendered pixels.
pub struct Scene {
    renderings: HashMap<u64, Rendering>,
    mouse_cursor_raster: Option<Raster>,
    id_generator: IdGenerator,
    facets: FacetMap,
    facet_order: Vec<FacetId>,
    groups: GroupMap,
    layers: LayerMap,
    composition: Composition,
    options: SceneOptions,
}

impl Scene {
    fn new_from_builder(
        options: SceneOptions,
        facets: FacetMap,
        groups: GroupMap,
        id_generator: IdGenerator,
    ) -> Self {
        let facet_order: Vec<FacetId> = facets.iter().map(|(facet_id, _)| *facet_id).collect();
        Self {
            renderings: HashMap::new(),
            mouse_cursor_raster: None,
            id_generator,
            facets,
            facet_order,
            groups,
            layers: LayerMap::new(),
            composition: Composition::new(options.background_color),
            options,
        }
    }

    /// Set the option to round scene corners.
    pub fn round_scene_corners(&mut self, round_scene_corners: bool) {
        self.options.round_scene_corners = round_scene_corners;
    }

    /// Add a facet to the scene, returning its ID.
    pub fn add_facet(&mut self, facet: FacetPtr) -> FacetId {
        let facet_id = FacetId::new(&mut self.id_generator);
        self.facets.insert(facet_id, FacetEntry { facet, location: Point::zero() });
        self.facet_order.push(facet_id);
        facet_id
    }

    /// Remove a particular facet from the scene.
    pub fn remove_facet(&mut self, facet_id: FacetId) -> Result<(), Error> {
        if let Some(_) = self.facets.remove(&facet_id).as_mut() {
            self.layers.remove(&facet_id);
            self.facet_order.retain(|fid| facet_id != *fid);
            Ok(())
        } else {
            bail!("Tried to remove non-existant facet")
        }
    }

    /// Move a facet forward in the facet order list.
    pub fn move_facet_forward(&mut self, facet_id: FacetId) -> Result<(), Error> {
        if let Some(index) = self.facet_order.iter().position(|fid| *fid == facet_id) {
            if index > 0 {
                let new_index = index - 1;
                self.facet_order.swap(new_index, index)
            }
            Ok(())
        } else {
            bail!("Tried to move_facet_forward non-existant facet")
        }
    }

    /// Move a facet backwards in the facet order list.
    pub fn move_facet_backward(&mut self, facet_id: FacetId) -> Result<(), Error> {
        if let Some(index) = self.facet_order.iter().position(|fid| *fid == facet_id) {
            if index < self.facet_order.len() - 1 {
                let new_index = index + 1;
                self.facet_order.swap(new_index, index)
            }
            Ok(())
        } else {
            bail!("Tried to move_facet_backward non-existant facet")
        }
    }

    /// Create a new group.
    pub fn new_group(&mut self) -> GroupId {
        GroupId::new(&mut self.id_generator)
    }

    /// Add a facet to a group, removing it from any group it might already belong to.
    pub fn add_facet_to_group(&mut self, facet_id: FacetId, group_id: GroupId) {
        self.groups.add_facet_to_group(facet_id, group_id);
    }

    /// Remove a facet from a group.
    pub fn remove_facet_from_group(&mut self, facet_id: FacetId, group_id: GroupId) {
        self.groups.remove_facet_from_group(facet_id, group_id);
    }

    /// Set the arranger for a group. No change in facet position will occur until layout
    /// is called.
    pub fn set_group_arranger(&mut self, group_id: GroupId, arranger: ArrangerPtr) {
        self.groups.set_group_arranger(group_id, arranger);
    }

    pub(crate) fn layers(
        &mut self,
        size: Size,
        render_context: &mut RenderContext,
        view_context: &ViewAssistantContext,
    ) -> Vec<Layer> {
        let mut layers = Vec::new();

        for facet_id in &self.facet_order {
            let facet_entry = self.facets.get_mut(facet_id).expect("facet");
            let facet_layers = if let Some(facet_layers) = self.layers.get(facet_id) {
                facet_layers.0.clone()
            } else {
                Vec::new()
            };
            let mut layer_group = LayerGroup(facet_layers);
            facet_entry
                .facet
                .update_layers(size, &mut layer_group, render_context, view_context)
                .expect("update_layers");
            for layer in &mut layer_group.0 {
                layer.raster =
                    layer.raster.clone().translate(facet_entry.location.to_vector().to_i32());
            }
            layers.append(&mut layer_group.0.clone());

            self.layers.insert(*facet_id, layer_group);
        }
        layers
    }

    fn create_or_update_rendering(
        renderings: &mut HashMap<u64, Rendering>,
        background_color: Color,
        context: &ViewAssistantContext,
    ) -> Option<PreClear> {
        let image_id = context.image_id;
        let size_rendering = renderings.entry(image_id).or_insert_with(|| Rendering::new());
        let size = context.size;
        if size != size_rendering.size {
            size_rendering.size = context.size;
            Some(PreClear { color: background_color })
        } else {
            None
        }
    }

    fn update_composition(
        layers: Vec<Layer>,
        mouse_position: &Option<IntPoint>,
        mouse_cursor_raster: &Option<Raster>,
        corner_knockouts: &Option<Raster>,
        composition: &mut Composition,
    ) {
        let corner_knockouts_layer = corner_knockouts.as_ref().and_then(|raster| {
            Some(Layer {
                raster: raster.clone(),
                clip: None,
                style: Style {
                    fill_rule: FillRule::NonZero,
                    fill: Fill::Solid(Color::new()),
                    blend_mode: BlendMode::Over,
                },
            })
        });

        let cursor_layers: Vec<Layer> = mouse_position
            .and_then(|position| {
                let mouse_cursor_raster =
                    mouse_cursor_raster.as_ref().expect("mouse_cursor_raster");
                Some(cursor_layer_pair(mouse_cursor_raster, position))
            })
            .into_iter()
            .flatten()
            .collect();

        composition.replace(
            ..,
            cursor_layers
                .clone()
                .into_iter()
                .chain(corner_knockouts_layer.into_iter())
                .chain(layers.into_iter()),
        );
    }

    /// Render the scene. Expected to be called from the view assistant's render method.
    pub fn render(
        &mut self,
        render_context: &mut RenderContext,
        ready_event: Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        let image = render_context.get_current_image(context);
        let background_color = self.options.background_color;
        let pre_clear =
            Self::create_or_update_rendering(&mut self.renderings, background_color, context);
        let size = context.size;

        let ext = RenderExt { pre_clear, ..Default::default() };

        let corner_knockouts = if self.options.round_scene_corners {
            Some(raster_for_corner_knockouts(&Rect::from_size(size), 10.0, render_context))
        } else {
            None
        };

        let mouse_cursor_position = if self.options.enable_mouse_cursor {
            if context.mouse_cursor_position.is_some() && self.mouse_cursor_raster.is_none() {
                self.mouse_cursor_raster = Some(create_mouse_cursor_raster(render_context));
            }
            &context.mouse_cursor_position
        } else {
            &None
        };

        Self::update_composition(
            self.layers(size, render_context, context),
            mouse_cursor_position,
            &self.mouse_cursor_raster,
            &corner_knockouts,
            &mut self.composition,
        );
        render_context.render(&self.composition, None, image, &ext);
        ready_event.as_handle_ref().signal(Signals::NONE, Signals::EVENT_SIGNALED)?;

        Ok(())
    }

    /// Send a message to a facet.
    pub fn send_message(&mut self, target: &FacetId, msg: Box<dyn Any>) {
        if let Some(facet_entry) = self.facets.get_mut(target) {
            facet_entry.facet.handle_message(msg);
        }
    }

    /// Set the absolute position of a facet.
    pub fn set_facet_location(&mut self, target: &FacetId, location: Point) {
        if let Some(facet_entry) = self.facets.get_mut(target) {
            facet_entry.location = location;
        }
    }

    /// Get the absolute position of a facet.
    pub fn get_facet_location(&self, target: &FacetId) -> Point {
        self.facets
            .get(target)
            .and_then(|facet_entry| Some(facet_entry.location))
            .unwrap_or(Point::zero())
    }

    /// Get the size of a facet.
    pub fn get_facet_size(&self, target: &FacetId) -> Size {
        self.facets
            .get(target)
            .and_then(|facet_entry| Some(facet_entry.facet.get_size()))
            .expect("get_facet_size")
    }

    /// Get a rectangle created from the absolute position and size of a facet.
    pub fn get_facet_bounds(&self, target: &FacetId) -> Rect {
        Rect::new(self.get_facet_location(target), self.get_facet_size(target))
    }

    /// Calculate the size of a group.
    pub fn caculate_group_size(
        &self,
        target: &GroupId,
        target_size: &Size,
        member_sizes: &[Size],
    ) -> Size {
        self.groups
            .group_arranger(*target)
            .and_then(|arranger| Some(arranger.calculate_size(*target_size, member_sizes)))
            .unwrap_or_else(|| {
                println!("no arranger");
                Size::zero()
            })
    }

    fn position_group(
        &mut self,
        origin: Point,
        group_id: GroupId,
        size_map: &HashMap<GroupMember, Size>,
    ) {
        let members = self.groups.group_members(group_id);
        let group_size = size_map.get(&GroupMember::Group(group_id)).expect("group size");
        let sizes: Vec<_> =
            members.iter().map(|member| *size_map.get(member).expect("members size")).collect();
        if let Some(arranger) = self.groups.group_arranger(group_id) {
            let positions = arranger.arrange(*group_size, &sizes);
            let member_ids = self.groups.group_members(group_id);
            for (pos, member_id) in positions.iter().zip(member_ids.iter()) {
                match member_id {
                    GroupMember::Facet(facet_id) => {
                        self.set_facet_location(facet_id, *pos + origin.to_vector())
                    }
                    GroupMember::Group(member_group_id) => {
                        self.position_group(*pos + origin.to_vector(), *member_group_id, size_map)
                    }
                }
            }
        } else {
            println!("no arranger");
        }
    }

    /// Run the layout process, positioning groups and facets.
    pub fn layout(&mut self, target_size: Size) {
        let size_map = self.groups.calculate_size_map(target_size, self);
        self.position_group(Point::zero(), self.groups.get_root_group_id(), &size_map);
    }

    /// Return the bounds of all facets.
    pub fn all_facet_bounds(&self) -> Vec<Rect> {
        self.facet_order
            .iter()
            .filter_map(|facet_id| {
                self.facets.get(facet_id).and_then(|facet_entry| {
                    Some(Rect::new(facet_entry.location, facet_entry.facet.get_size()))
                })
            })
            .collect()
    }
}

impl Debug for Scene {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Scene").field("groups", &self.groups).finish()
    }
}

/// Fluent builder for facet groups
pub struct GroupBuilder<'a> {
    pub(crate) builder: &'a mut SceneBuilder,
    label: String,
    pub(crate) arranger: Option<ArrangerPtr>,
}

impl<'a> GroupBuilder<'a> {
    pub(crate) fn new(builder: &'a mut SceneBuilder) -> Self {
        Self { builder, label: String::from(""), arranger: None }
    }

    /// Set the debugging label for a group.
    pub fn label(mut self, label: &str) -> Self {
        self.label = String::from(label);
        self
    }

    /// Create a row-oriented flex builder.
    pub fn row(self) -> FlexBuilder<'a> {
        FlexBuilder::new(self, Axis::Horizontal)
    }

    /// Create a column-oriented flex builder.
    pub fn column(self) -> FlexBuilder<'a> {
        FlexBuilder::new(self, Axis::Vertical)
    }

    /// Create a stack builder.
    pub fn stack(self) -> StackBuilder<'a> {
        StackBuilder::new(self)
    }

    /// Create the stack group, with contents provided by
    /// `f`.
    pub fn contents<F>(self, mut f: F) -> GroupId
    where
        F: FnMut(&mut SceneBuilder),
    {
        self.builder.start_group(&self.label, self.arranger.unwrap_or(Flex::new_ptr()));
        f(self.builder);
        self.builder.end_group()
    }
}

/// Fluent builder for scenes.
pub struct SceneBuilder {
    options: SceneOptions,
    id_generator: IdGenerator,
    facets: FacetMap,
    groups: GroupMap,
    group_stack: Vec<GroupId>,
}

impl SceneBuilder {
    /// Create a new fluent builder for building Scenes.
    pub fn new() -> Self {
        let id_generator = IdGenerator::default();
        Self {
            options: SceneOptions::default(),
            id_generator,
            facets: FacetMap::new(),
            groups: GroupMap::new(),
            group_stack: vec![],
        }
    }

    /// True if, when running without Scenic, if the scene
    /// should round the corners of the screen to match the
    /// presentation that sometimes occurs with Scenic.
    pub fn round_scene_corners(mut self, round: bool) -> Self {
        self.options.round_scene_corners = round;
        self
    }

    /// If true, when running without Scenic, the mouse cursor should
    /// be drawn.
    pub fn enable_mouse_cursor(mut self, enable: bool) -> Self {
        self.options.enable_mouse_cursor = enable;
        self
    }

    /// Set the color to use for the background of a scene.
    pub fn background_color(mut self, background_color: Color) -> Self {
        self.options.background_color = background_color;
        self
    }

    fn allocate_facet_id(&mut self) -> FacetId {
        FacetId::new(&mut self.id_generator)
    }

    fn push_facet(&mut self, facet: FacetPtr, location: Point) -> FacetId {
        let facet_id = self.allocate_facet_id();
        self.facets.insert(facet_id.clone(), FacetEntry { facet, location });
        if let Some(group_id) = self.group_stack.last() {
            self.groups.add_facet_to_group(facet_id, *group_id);
        }
        facet_id
    }

    /// Add a rectangle facet of size and color to the scene.
    pub fn rectangle(&mut self, size: Size, color: Color) -> FacetId {
        self.push_facet(RectangleFacet::new(size, color), Point::zero())
    }

    /// Add a spacing facet of size.
    pub fn space(&mut self, size: Size) -> FacetId {
        self.push_facet(Box::new(SpacingFacet::new(size)), Point::zero())
    }

    /// Add a horizontal line to the scene.
    pub fn h_line(
        &mut self,
        width: Coord,
        thickness: Coord,
        color: Color,
        location: Option<Point>,
    ) -> FacetId {
        self.push_facet(
            RectangleFacet::h_line(width, thickness, color),
            location.unwrap_or(Point::zero()),
        )
    }

    /// Add a vertical line to the scene.
    pub fn v_line(
        &mut self,
        height: Coord,
        thickness: Coord,
        color: Color,
        location: Option<Point>,
    ) -> FacetId {
        self.push_facet(
            RectangleFacet::v_line(height, thickness, color),
            location.unwrap_or(Point::zero()),
        )
    }

    /// Add a text facet to the scene.
    pub fn text(
        &mut self,
        face: FontFace,
        text: &str,
        size: f32,
        location: Point,
        options: TextFacetOptions,
    ) -> FacetId {
        self.push_facet(TextFacet::with_options(face, text, size, options), location)
    }

    /// Add an object that implements the Facet trait to the scene.
    pub fn facet(&mut self, facet: FacetPtr) -> FacetId {
        self.push_facet(facet, Point::zero())
    }

    /// Add an object that implements the Facet trait to the scene and set
    /// its location.
    pub fn facet_at_location(&mut self, facet: FacetPtr, location: Point) -> FacetId {
        self.push_facet(facet, location)
    }

    /// Start a facet group. Any facets added to the scene or groups started will become
    // members of this group.
    pub fn start_group(&mut self, label: &str, arranger: ArrangerPtr) {
        let group_id = GroupId::new(&mut self.id_generator);
        self.groups.start_group(group_id, label, arranger, self.group_stack.last());
        self.group_stack.push(group_id);
    }

    /// End the current group, returning its group ID.
    pub fn end_group(&mut self) -> GroupId {
        self.group_stack.pop().expect("group stack to always have a group ID")
    }

    /// Create a group builder.
    pub fn group(&mut self) -> GroupBuilder<'_> {
        GroupBuilder::new(self)
    }

    /// Consume this builder and build the scene.
    pub fn build(mut self) -> Scene {
        while self.group_stack.len() > 0 {
            self.end_group();
        }
        Scene::new_from_builder(self.options, self.facets, self.groups, self.id_generator)
    }
}
