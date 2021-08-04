// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{
    facets::{
        FacetEntry, FacetId, FacetMap, FacetPtr, RectangleFacet, SpacingFacet, TextFacet,
        TextFacetOptions,
    },
    group::{GroupId, GroupMap, GroupMember, GroupMemberData},
    layout::{ArrangerPtr, Axis, Flex, FlexBuilder, StackBuilder},
    raster_for_corner_knockouts, BlendMode, FillRule, IdGenerator, LayerGroup, Rendering,
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
use euclid::{size2, vec2};
use fuchsia_trace::duration;
use fuchsia_zircon::{AsHandleRef, Event, Signals};
use std::{
    any::Any,
    collections::{BTreeMap, HashMap},
    convert::TryFrom,
    fmt::{self, Debug},
};

struct DirectLayerGroup<'a>(&'a mut Composition);

impl LayerGroup for DirectLayerGroup<'_> {
    fn clear(&mut self) {
        self.0.clear();
    }
    fn insert(&mut self, order: u16, layer: Layer) {
        self.0.insert(order, layer);
    }
    fn remove(&mut self, order: u16) {
        self.0.remove(order);
    }
}

struct SimpleLayerGroup<'a>(&'a mut BTreeMap<u16, Layer>);

impl LayerGroup for SimpleLayerGroup<'_> {
    fn clear(&mut self) {
        self.0.clear();
    }
    fn insert(&mut self, order: u16, layer: Layer) {
        self.0.insert(order, layer);
    }
    fn remove(&mut self, order: u16) {
        self.0.remove(&order);
    }
}

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

fn cursor_layer_pair(cursor_raster: &Raster, position: IntPoint) -> (Layer, Layer) {
    let black_pos = position + vec2(-1, -1);
    (
        cursor_layer(cursor_raster, position, &Color::fuchsia()),
        cursor_layer(cursor_raster, black_pos, &Color::new()),
    )
}

type LayerMap = BTreeMap<FacetId, BTreeMap<u16, Layer>>;

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
    corner_knockouts_raster: Option<Raster>,
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
            corner_knockouts_raster: None,
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
        self.facets
            .insert(facet_id, FacetEntry { facet, location: Point::zero(), size: Size::zero() });
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
    pub fn add_facet_to_group(
        &mut self,
        facet_id: FacetId,
        group_id: GroupId,
        member_data: Option<GroupMemberData>,
    ) {
        self.groups.add_facet_to_group(facet_id, group_id, member_data);
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

    pub(crate) fn update_scene_layers(
        &mut self,
        size: Size,
        render_context: &mut RenderContext,
        view_context: &ViewAssistantContext,
    ) {
        duration!("gfx", "Scene::update_scene_layers");

        for (facet_id, facet_entry) in &mut self.facets {
            let facet_layers = self.layers.entry(*facet_id).or_insert_with(|| BTreeMap::new());
            let mut layer_group = SimpleLayerGroup(facet_layers);
            facet_entry
                .facet
                .update_layers(size, &mut layer_group, render_context, view_context)
                .expect("update_layers");
        }
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
        layers: impl IntoIterator<Item = (u16, Layer)>,
        mouse_position: &Option<IntPoint>,
        mouse_cursor_raster: &Option<Raster>,
        corner_knockouts: &Option<Raster>,
        composition: &mut Composition,
    ) {
        duration!("gfx", "Scene::update_composition");

        for (order, layer) in layers.into_iter() {
            composition.insert(order, layer);
        }

        // Mold backend currently supports up to u16::MAX / 2 orders.
        const MAX_ORDER: u16 = u16::MAX / 2;

        const CORNER_KOCKOUTS_ORDER: u16 = MAX_ORDER;
        const MOUSE_CURSOR_LAYER_0_ORDER: u16 = MAX_ORDER - 1;
        const MOUSE_CURSOR_LAYER_1_ORDER: u16 = MAX_ORDER - 2;

        if let Some(position) = mouse_position {
            if let Some(raster) = mouse_cursor_raster {
                let (layer0, layer1) = cursor_layer_pair(raster, *position);
                composition.insert(MOUSE_CURSOR_LAYER_0_ORDER, layer0);
                composition.insert(MOUSE_CURSOR_LAYER_1_ORDER, layer1);
            }
        }

        if let Some(raster) = corner_knockouts {
            composition.insert(
                CORNER_KOCKOUTS_ORDER,
                Layer {
                    raster: raster.clone(),
                    clip: None,
                    style: Style {
                        fill_rule: FillRule::NonZero,
                        fill: Fill::Solid(Color::new()),
                        blend_mode: BlendMode::Over,
                    },
                },
            );
        }
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

        const CORNER_KNOCKOUTS_SIZE: f32 = 10.0;

        if self.options.round_scene_corners && self.corner_knockouts_raster.is_none() {
            self.corner_knockouts_raster = Some(raster_for_corner_knockouts(
                &Rect::from_size(size),
                CORNER_KNOCKOUTS_SIZE,
                render_context,
            ));
        }

        let mouse_cursor_position = if self.options.enable_mouse_cursor {
            if context.mouse_cursor_position.is_some() && self.mouse_cursor_raster.is_none() {
                self.mouse_cursor_raster = Some(create_mouse_cursor_raster(render_context));
            }
            &context.mouse_cursor_position
        } else {
            &None
        };

        // Allow single scene facets to mutate the composition directly for optimal
        // performance.
        if self.facets.len() == 1 {
            let (_, facet_entry) = self.facets.iter_mut().next().expect("first facet");
            if facet_entry.location != Point::zero() {
                panic!("single facet scene with non-zero ({:?}) location", facet_entry.location);
            }
            let composition = &mut self.composition;
            let mut layer_group = DirectLayerGroup(composition);
            facet_entry
                .facet
                .update_layers(size, &mut layer_group, render_context, context)
                .expect("update_layers");

            Self::update_composition(
                std::iter::empty(),
                mouse_cursor_position,
                &self.mouse_cursor_raster,
                &self.corner_knockouts_raster,
                composition,
            );
        } else {
            self.update_scene_layers(size, render_context, context);

            let composition = &mut self.composition;
            let facet_order = &self.facet_order;
            let facets = &self.facets;
            let layers = &self.layers;

            // Clear composition and generate a completely new set of layers.
            composition.clear();

            let mut next_origin: u32 = 0;
            let layers = facet_order.iter().rev().flat_map(|facet_id| {
                let facet_entry = facets.get(facet_id).expect("facets");
                let facet_translation = facet_entry.location.to_vector().to_i32();
                let facet_layers = layers.get(facet_id).expect("layers");
                let facet_origin = next_origin;

                // Advance origin for next facet.
                next_origin += facet_layers.keys().next_back().map(|o| *o as u32 + 1).unwrap_or(0);

                facet_layers.iter().map(move |(order, layer)| {
                    (
                        u16::try_from(facet_origin + *order as u32).expect("too many layers"),
                        Layer {
                            raster: layer.raster.clone().translate(facet_translation),
                            clip: layer.clip.clone().map(|c| c.translate(facet_translation)),
                            style: layer.style.clone(),
                        },
                    )
                })
            });

            Self::update_composition(
                layers,
                mouse_cursor_position,
                &self.mouse_cursor_raster,
                &self.corner_knockouts_raster,
                composition,
            );
        }

        render_context.render(&mut self.composition, None, image, &ext);
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

    /// Set the size of a facet.
    pub fn set_facet_size(&mut self, target: &FacetId, size: Size) {
        if let Some(facet_entry) = self.facets.get_mut(target) {
            facet_entry.size = size;
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
            .and_then(|facet_entry| Some(facet_entry.size))
            .expect("get_facet_size")
    }

    pub(crate) fn calculate_facet_size(&self, target: &FacetId, available: Size) -> Size {
        self.facets
            .get(target)
            .and_then(|facet_entry| Some(facet_entry.facet.calculate_size(available)))
            .expect("calculate_facet_size")
    }

    /// Get a rectangle created from the absolute position and size of a facet.
    pub fn get_facet_bounds(&self, target: &FacetId) -> Rect {
        Rect::new(self.get_facet_location(target), self.get_facet_size(target))
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
                        let size =
                            size_map.get(member_id).map_or_else(|| size2(10.0, 10.0), |size| *size);
                        self.set_facet_size(facet_id, size);
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
                self.facets
                    .get(facet_id)
                    .and_then(|facet_entry| Some(Rect::new(facet_entry.location, facet_entry.size)))
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

    /// Create a row-oriented flex builder with member data.
    pub fn row_with_member_data(self, member_data: Option<GroupMemberData>) -> FlexBuilder<'a> {
        FlexBuilder::new(self, Axis::Horizontal, member_data)
    }

    /// Create a row-oriented flex builder.
    pub fn row(self) -> FlexBuilder<'a> {
        self.row_with_member_data(None)
    }

    /// Create a column-oriented flex builder with member data.
    pub fn column_with_member_data(self, member_data: Option<GroupMemberData>) -> FlexBuilder<'a> {
        FlexBuilder::new(self, Axis::Vertical, member_data)
    }

    /// Create a column-oriented flex builder.
    pub fn column(self) -> FlexBuilder<'a> {
        self.column_with_member_data(None)
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

    /// Create the stack group, with contents provided by
    /// `f` and member data for the group
    pub fn contents_with_member_data<F>(
        self,
        member_data: Option<GroupMemberData>,
        mut f: F,
    ) -> GroupId
    where
        F: FnMut(&mut SceneBuilder),
    {
        self.builder.start_group_with_member_data(
            &self.label,
            self.arranger.unwrap_or(Flex::new_ptr()),
            member_data,
        );
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

    fn push_facet(
        &mut self,
        facet: FacetPtr,
        location: Point,
        member_data: Option<GroupMemberData>,
    ) -> FacetId {
        let facet_id = self.allocate_facet_id();
        self.facets.insert(facet_id.clone(), FacetEntry { facet, location, size: Size::zero() });
        if let Some(group_id) = self.group_stack.last() {
            self.groups.add_facet_to_group(facet_id, *group_id, member_data);
        }
        facet_id
    }

    /// Add a rectangle facet of size and color to the scene.
    pub fn rectangle(&mut self, size: Size, color: Color) -> FacetId {
        self.push_facet(RectangleFacet::new(size, color), Point::zero(), None)
    }

    /// Add a spacing facet of size.
    pub fn space(&mut self, size: Size) -> FacetId {
        self.push_facet(Box::new(SpacingFacet::new(size)), Point::zero(), None)
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
            None,
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
            None,
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
        self.push_facet(TextFacet::with_options(face, text, size, options), location, None)
    }

    /// Add an object that implements the Facet trait to the scene, along with
    /// some data for the group arranger.
    pub fn facet_with_data(
        &mut self,
        facet: FacetPtr,
        member_data: Option<GroupMemberData>,
    ) -> FacetId {
        self.push_facet(facet, Point::zero(), member_data)
    }

    /// Add an object that implements the Facet trait to the scene.
    pub fn facet(&mut self, facet: FacetPtr) -> FacetId {
        self.push_facet(facet, Point::zero(), None)
    }

    /// Add an object that implements the Facet trait to the scene and set
    /// its location.
    pub fn facet_at_location(&mut self, facet: FacetPtr, location: Point) -> FacetId {
        self.push_facet(facet, location, None)
    }

    /// Start a facet group with member data. Any facets added to the scene or
    // groups started will become members of this group.
    pub fn start_group_with_member_data(
        &mut self,
        label: &str,
        arranger: ArrangerPtr,
        member_data: Option<GroupMemberData>,
    ) {
        let group_id = GroupId::new(&mut self.id_generator);
        self.groups.start_group(group_id, label, arranger, self.group_stack.last(), member_data);
        self.group_stack.push(group_id);
    }

    /// Start a facet group. Any facets added to the scene or groups started will become
    // members of this group.
    pub fn start_group(&mut self, label: &str, arranger: ArrangerPtr) {
        self.start_group_with_member_data(label, arranger, None);
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
