// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    color::Color,
    drawing::{
        path_for_corner_knockouts, path_for_cursor, path_for_rectangle, FontFace, GlyphMap, Text,
    },
    geometry::IntPoint,
    render::{
        BlendMode, Composition, Context as RenderContext, Fill, FillRule, Layer, PreClear, Raster,
        RenderExt, Shed, Style,
    },
    Coord, Point, Rect, Size, ViewAssistantContext,
};
use anyhow::{bail, Error};
use euclid::{default::Transform2D, point2, size2, vec2};
use fuchsia_zircon::{AsHandleRef, Event, Signals};
use std::{
    any::Any,
    collections::{BTreeMap, HashMap},
    path::PathBuf,
    sync::atomic::{AtomicUsize, Ordering},
};

fn pixel_size(face: &FontFace, font_size: f32, value: i16) -> Option<f32> {
    face.face
        .units_per_em()
        .and_then(|units_per_em| Some((value as f32 / units_per_em as f32) * font_size))
}

pub fn measure_text_width(face: &FontFace, font_size: f32, text: &str) -> f32 {
    text.chars()
        .filter_map(|c| {
            let glyph_index = face.face.glyph_index(c);
            glyph_index.and_then(|glyph_index| {
                let hor_advance = face
                    .face
                    .glyph_hor_advance(glyph_index)
                    .and_then(|hor_advance| pixel_size(face, font_size, hor_advance as i16))
                    .expect("hor_advance");
                Some(hor_advance)
            })
        })
        .sum()
}

pub fn linebreak_text(face: &FontFace, font_size: f32, text: &str, max_width: f32) -> Vec<String> {
    let chunks: Vec<&str> = text.split_whitespace().collect();
    let space_width = measure_text_width(face, font_size, " ");
    let breaks: Vec<usize> = chunks
        .iter()
        .enumerate()
        .scan(0.0, |width, (index, word)| {
            let word_width = measure_text_width(face, font_size, word);
            let resulting_line_len = *width + word_width;
            if resulting_line_len > max_width {
                *width = 0.0;
                Some(Some(index))
            } else {
                *width += word_width;
                *width += space_width;
                Some(None)
            }
        })
        .flatten()
        .chain(std::iter::once(chunks.len()))
        .collect();
    let lines: Vec<String> = breaks
        .iter()
        .scan(0, |first_word_index, last_word_index| {
            let first = *first_word_index;
            *first_word_index = *last_word_index;
            let line = &chunks[first..*last_word_index];
            let line_str = String::from(line.join(" "));
            Some(line_str)
        })
        .collect();

    lines
}

#[derive(Debug)]
pub struct SetColorMessage {
    pub color: Color,
}

pub struct SetTextMessage {
    pub text: String,
}

pub struct SetLocationMessage {
    pub location: Point,
}

pub trait Facet {
    fn update_layers(
        &mut self,
        size: Size,
        layer_group: &mut LayerGroup,
        render_context: &mut RenderContext,
    ) -> Result<(), Error>;

    fn handle_message(&mut self, msg: Box<dyn Any>) {
        println!("Unhandled message {:#?}", msg);
    }
}

pub type FacetPtr = Box<dyn Facet>;
pub type LayerIterator = Box<dyn Iterator<Item = Layer>>;

pub struct RectangleFacet {
    bounds: Rect,
    color: Color,
    raster: Option<Raster>,
}

impl RectangleFacet {
    pub fn new(bounds: Rect, color: Color) -> FacetPtr {
        Box::new(Self { bounds, color, raster: None })
    }

    pub fn h_line(
        y: Coord,
        x_start: Coord,
        x_end: Coord,
        thickness: Coord,
        color: Color,
    ) -> FacetPtr {
        let x = x_start.min(x_end);
        let half_thickness = thickness / 2.0;
        let top_left = point2(x, y - half_thickness);
        let line_bounds = Rect::new(top_left, size2((x_end - x_start).abs(), thickness));
        Self::new(line_bounds, color)
    }

    pub fn v_line(
        x: Coord,
        y_start: Coord,
        y_end: Coord,
        thickness: Coord,
        color: Color,
    ) -> FacetPtr {
        let y = y_start.min(y_end);
        let height = (y_end - y_start).abs();
        let half_thickness = thickness / 2.0;
        let top_left = point2(x - half_thickness, y);
        let line_bounds = Rect::new(top_left, size2(thickness, height));
        Self::new(line_bounds, color)
    }
}

impl Facet for RectangleFacet {
    fn update_layers(
        &mut self,
        _size: Size,
        layer_group: &mut LayerGroup,
        render_context: &mut RenderContext,
    ) -> Result<(), Error> {
        let line_raster = self.raster.take().unwrap_or_else(|| {
            let line_path = path_for_rectangle(&self.bounds, render_context);
            let mut raster_builder = render_context.raster_builder().expect("raster_builder");
            raster_builder.add(&line_path, None);
            raster_builder.build()
        });
        let raster = line_raster.clone();
        self.raster = Some(line_raster);
        layer_group.replace_all(std::iter::once(Layer {
            raster: raster,
            style: Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(self.color),
                blend_mode: BlendMode::Over,
            },
        }));
        Ok(())
    }

    fn handle_message(&mut self, msg: Box<dyn Any>) {
        if let Some(set_color) = msg.downcast_ref::<SetColorMessage>() {
            self.color = set_color.color;
        }
    }
}

pub enum TextHorizontalAlignment {
    Left,
    Right,
    Center,
}

impl Default for TextHorizontalAlignment {
    fn default() -> Self {
        Self::Left
    }
}

pub enum TextVerticalAlignment {
    Baseline,
    Top,
    Bottom,
    Center,
}

impl Default for TextVerticalAlignment {
    fn default() -> Self {
        Self::Baseline
    }
}

#[derive(Default)]
pub struct TextFacetOptions {
    pub horizontal_alignment: TextHorizontalAlignment,
    pub vertical_alignment: TextVerticalAlignment,
    pub color: Color,
    pub max_width: Option<f32>,
}

pub struct TextFacet {
    face: FontFace,
    lines: Vec<String>,
    size: f32,
    location: Point,
    options: TextFacetOptions,
    rendered_text: Option<Text>,
    glyphs: GlyphMap,
}

impl TextFacet {
    fn wrap_lines(face: &FontFace, size: f32, text: &str, max_width: &Option<f32>) -> Vec<String> {
        let lines: Vec<String> = text.lines().map(|line| String::from(line)).collect();
        if let Some(max_width) = max_width {
            let wrapped_lines = lines
                .iter()
                .map(|line| linebreak_text(face, size, line, *max_width))
                .flatten()
                .collect();
            wrapped_lines
        } else {
            lines
        }
    }

    pub fn new(face: FontFace, text: &str, size: f32, location: Point) -> FacetPtr {
        Self::with_options(face, text, size, location, TextFacetOptions::default())
    }

    pub fn with_options(
        face: FontFace,
        text: &str,
        size: f32,
        location: Point,
        options: TextFacetOptions,
    ) -> FacetPtr {
        let lines = Self::wrap_lines(&face, size, text, &options.max_width);

        Box::new(Self {
            face,
            lines,
            size,
            location,
            options,
            rendered_text: None,
            glyphs: GlyphMap::new(),
        })
    }
}

impl Facet for TextFacet {
    fn update_layers(
        &mut self,
        _size: Size,
        layer_group: &mut LayerGroup,
        render_context: &mut RenderContext,
    ) -> Result<(), Error> {
        let rendered_text = self.rendered_text.take().unwrap_or_else(|| {
            Text::new_with_lines(
                render_context,
                &self.lines,
                self.size,
                &self.face,
                &mut self.glyphs,
            )
        });
        let scale = rusttype::Scale::uniform(self.size);
        let v_metrics = self.face.font.v_metrics(scale);
        let x = match self.options.horizontal_alignment {
            TextHorizontalAlignment::Left => self.location.x,
            TextHorizontalAlignment::Center => {
                self.location.x - rendered_text.bounding_box.size.width / 2.0
            }
            TextHorizontalAlignment::Right => {
                self.location.x - rendered_text.bounding_box.size.width
            }
        };
        let y = match self.options.vertical_alignment {
            TextVerticalAlignment::Baseline => self.location.y - v_metrics.ascent,
            TextVerticalAlignment::Top => self.location.y,
            TextVerticalAlignment::Bottom => self.location.y - v_metrics.ascent + v_metrics.descent,
            TextVerticalAlignment::Center => {
                let capital_height = self.face.capital_height(self.size).unwrap_or(self.size);
                self.location.y + capital_height / 2.0 - v_metrics.ascent
            }
        };
        let translation = vec2(x, y);
        let raster = rendered_text.raster.clone().translate(translation.to_i32());
        self.rendered_text = Some(rendered_text);

        layer_group.replace_all(std::iter::once(Layer {
            raster,
            style: Style {
                fill_rule: FillRule::NonZero,
                fill: Fill::Solid(self.options.color),
                blend_mode: BlendMode::Over,
            },
        }));
        Ok(())
    }

    fn handle_message(&mut self, msg: Box<dyn Any>) {
        if let Some(set_text) = msg.downcast_ref::<SetTextMessage>() {
            self.lines =
                Self::wrap_lines(&self.face, self.size, &set_text.text, &self.options.max_width);
            self.rendered_text = None;
        } else if let Some(set_color) = msg.downcast_ref::<SetColorMessage>() {
            self.options.color = set_color.color;
        }
    }
}

pub struct RasterFacet {
    raster: Raster,
    style: Style,
    location: Point,
}

impl RasterFacet {
    pub fn new(raster: Raster, style: Style, location: Point) -> Self {
        Self { raster, style, location }
    }
}

impl Facet for RasterFacet {
    fn update_layers(
        &mut self,
        _size: Size,
        layer_group: &mut LayerGroup,
        _render_context: &mut RenderContext,
    ) -> Result<(), Error> {
        layer_group.replace_all(std::iter::once(Layer {
            raster: self.raster.clone().translate(self.location.to_vector().to_i32()),
            style: self.style.clone(),
        }));
        Ok(())
    }

    fn handle_message(&mut self, msg: Box<dyn Any>) {
        if let Some(set_location) = msg.downcast_ref::<SetLocationMessage>() {
            self.location = set_location.location;
        }
    }
}

pub struct ShedFacet {
    path: PathBuf,
    location: Point,
    rasters: Option<Vec<(Raster, Style)>>,
}

impl ShedFacet {
    pub fn new(path: PathBuf, location: Point) -> Self {
        Self { path, location, rasters: None }
    }
}

impl Facet for ShedFacet {
    fn update_layers(
        &mut self,
        size: Size,
        layer_group: &mut LayerGroup,
        render_context: &mut RenderContext,
    ) -> Result<(), Error> {
        let rasters = self.rasters.take().unwrap_or_else(|| {
            let shed = Shed::open(&self.path).unwrap();
            let shed_size = shed.size();
            let min_side = shed_size.width.min(shed_size.height);
            let min_view_side = size.width.min(size.height);
            let scale_factor = min_view_side / min_side * 0.75;
            let transform =
                Transform2D::create_translation(-shed_size.width / 2.0, -shed_size.height / 2.0)
                    .post_scale(scale_factor, scale_factor);

            shed.rasters(render_context, Some(&transform))
        });
        let location = self.location;
        layer_group.replace_all(rasters.iter().map(|(raster, style)| Layer {
            raster: raster.clone().translate(location.to_vector().to_i32()),
            style: *style,
        }));
        self.rasters = Some(rasters);
        Ok(())
    }

    fn handle_message(&mut self, msg: Box<dyn Any>) {
        if let Some(set_location) = msg.downcast_ref::<SetLocationMessage>() {
            self.location = set_location.location;
        }
    }
}

struct Rendering {
    size: Size,
    previous_rasters: Vec<Raster>,
}

impl Rendering {
    fn new() -> Rendering {
        Rendering { previous_rasters: Vec::new(), size: Size::zero() }
    }
}

fn raster_for_corner_knockouts(
    bounds: &Rect,
    corner_radius: Coord,
    render_context: &mut RenderContext,
) -> Raster {
    let path = path_for_corner_knockouts(bounds, corner_radius, render_context);
    let mut raster_builder = render_context.raster_builder().expect("raster_builder");
    raster_builder.add(&path, None);
    raster_builder.build()
}

pub struct SceneOptions {
    pub background_color: Color,
    pub round_scene_corners: bool,
}

impl Default for SceneOptions {
    fn default() -> Self {
        Self { background_color: Color::new(), round_scene_corners: true }
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

pub type FacetId = usize;
pub type FacetMap = BTreeMap<FacetId, FacetPtr>;

pub struct LayerGroup(Vec<Layer>);

impl LayerGroup {
    pub fn replace_all(&mut self, new_layers: impl IntoIterator<Item = Layer>) {
        self.0 = new_layers.into_iter().collect();
    }
}

#[derive(Default)]
struct IdGenerator {}

impl Iterator for IdGenerator {
    type Item = usize;

    fn next(&mut self) -> Option<usize> {
        static NEXT_ID: AtomicUsize = AtomicUsize::new(100);
        let id = NEXT_ID.fetch_add(1, Ordering::SeqCst);
        // fetch_add wraps on overflow, which we'll use as a signal
        // that this generator is out of ids.
        if id == 0 {
            None
        } else {
            Some(id)
        }
    }
}

type FacetIdGenerator = IdGenerator;
pub type LayerMap = BTreeMap<FacetId, LayerGroup>;

pub struct Scene {
    renderings: HashMap<u64, Rendering>,
    mouse_cursor_raster: Option<Raster>,
    facet_id_generator: FacetIdGenerator,
    facets: FacetMap,
    facet_order: Vec<FacetId>,
    layers: LayerMap,
    composition: Composition,
    size: Option<Size>,
    options: SceneOptions,
}

impl Scene {
    fn new_from_builder(
        options: SceneOptions,
        facets: FacetMap,
        facet_id_generator: FacetIdGenerator,
    ) -> Self {
        let facet_order: Vec<FacetId> = facets.iter().map(|(facet_id, _)| *facet_id).collect();
        Self {
            renderings: HashMap::new(),
            mouse_cursor_raster: None,
            facet_id_generator,
            facets,
            facet_order,
            layers: LayerMap::new(),
            composition: Composition::new(options.background_color),
            size: None,
            options,
        }
    }

    pub fn round_scene_corners(&mut self, round_scene_corners: bool) {
        self.options.round_scene_corners = round_scene_corners;
    }

    pub fn add_facet(&mut self, facet: FacetPtr) -> FacetId {
        let facet_id = self.facet_id_generator.next().expect("facet ID");
        self.facets.insert(facet_id, facet);
        self.facet_order.push(facet_id);
        facet_id
    }

    pub fn remove_facet(&mut self, facet_id: FacetId) -> Result<(), Error> {
        if let Some(_) = self.facets.remove(&facet_id).as_mut() {
            self.layers.remove(&facet_id);
            self.facet_order.retain(|fid| facet_id != *fid);
            Ok(())
        } else {
            bail!("Tried to remove non-existant facet")
        }
    }

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

    pub fn layers(&mut self, size: Size, render_context: &mut RenderContext) -> Vec<Layer> {
        let mut layers = Vec::new();

        for facet_id in &self.facet_order {
            let facet = self.facets.get_mut(facet_id).expect("facet");
            let facet_layers = if let Some(facet_layers) = self.layers.get(facet_id) {
                facet_layers.0.clone()
            } else {
                Vec::new()
            };
            let mut layer_group = LayerGroup(facet_layers);
            facet.update_layers(size, &mut layer_group, render_context).expect("update_layers");
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
            size_rendering.previous_rasters.clear();
            Some(PreClear { color: background_color })
        } else {
            None
        }
    }

    fn update_composition(
        image_id: u64,
        layers: Vec<Layer>,
        mouse_position: &Option<IntPoint>,
        mouse_cursor_raster: &Option<Raster>,
        corner_knockouts: &Option<Raster>,
        renderings: &mut HashMap<u64, Rendering>,
        background_color: Color,
        composition: &mut Composition,
    ) -> Vec<Layer> {
        let corner_knockouts_layer = corner_knockouts.as_ref().and_then(|raster| {
            Some(Layer {
                raster: raster.clone(),
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

        let clear_rendering = renderings.get_mut(&image_id).expect("rendering");

        composition.replace(
            ..,
            cursor_layers
                .clone()
                .into_iter()
                .chain(corner_knockouts_layer.into_iter())
                .chain(layers.into_iter())
                .chain(clear_rendering.previous_rasters.drain(..).map(|raster| Layer {
                    raster,
                    style: Style {
                        fill_rule: FillRule::WholeTile,
                        fill: Fill::Solid(background_color),
                        blend_mode: BlendMode::Over,
                    },
                })),
        );

        cursor_layers
    }

    pub fn render(
        &mut self,
        render_context: &mut RenderContext,
        ready_event: Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        let image = render_context.get_current_image(context);
        let image_id = context.image_id;
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

        if context.mouse_cursor_position.is_some() && self.mouse_cursor_raster.is_none() {
            self.mouse_cursor_raster = Some(create_mouse_cursor_raster(render_context));
        }

        let layers: Vec<Layer> = self.layers(size, render_context);
        let cursor_layer = Self::update_composition(
            image_id,
            layers.clone(),
            &context.mouse_cursor_position,
            &self.mouse_cursor_raster,
            &corner_knockouts,
            &mut self.renderings,
            background_color,
            &mut self.composition,
        );
        self.size = Some(size);
        render_context.render(&self.composition, None, image, &ext);
        ready_event.as_handle_ref().signal(Signals::NONE, Signals::EVENT_SIGNALED)?;

        let update_rendering = self.renderings.entry(image_id).or_insert_with(|| Rendering::new());

        let previous_rasters: Vec<Raster> = layers
            .iter()
            .chain(cursor_layer.iter())
            .map(|layer| layer.raster.clone())
            .chain(corner_knockouts.into_iter())
            .collect();
        update_rendering.previous_rasters = previous_rasters;
        Ok(())
    }

    pub fn send_message(&mut self, target: &FacetId, msg: Box<dyn Any>) {
        if let Some(facet) = self.facets.get_mut(target) {
            facet.handle_message(msg);
        }
    }
}

pub struct SceneBuilder {
    background_color: Color,
    round_scene_corners: bool,
    facet_id_generator: FacetIdGenerator,
    facets: FacetMap,
}

impl SceneBuilder {
    pub fn new(background_color: Color) -> Self {
        Self {
            background_color,
            round_scene_corners: false,
            facet_id_generator: FacetIdGenerator::default(),
            facets: FacetMap::new(),
        }
    }

    pub fn round_scene_corners(&mut self, round: bool) {
        self.round_scene_corners = round;
    }

    fn allocate_facet_id(&mut self) -> FacetId {
        self.facet_id_generator.next().expect("facet_id")
    }

    fn push_facet(&mut self, facet: FacetPtr) -> FacetId {
        let facet_id = self.allocate_facet_id();
        self.facets.insert(facet_id.clone(), facet);
        facet_id
    }

    pub fn rectangle(&mut self, bounds: Rect, color: Color) -> FacetId {
        self.push_facet(RectangleFacet::new(bounds, color))
    }

    pub fn h_line(
        &mut self,
        y: Coord,
        x_start: Coord,
        x_end: Coord,
        thickness: Coord,
        color: Color,
    ) -> FacetId {
        self.push_facet(RectangleFacet::h_line(y, x_start, x_end, thickness, color))
    }

    pub fn v_line(
        &mut self,
        x: Coord,
        y_start: Coord,
        y_end: Coord,
        thickness: Coord,
        color: Color,
    ) -> FacetId {
        self.push_facet(RectangleFacet::v_line(x, y_start, y_end, thickness, color))
    }

    pub fn text(
        &mut self,
        face: FontFace,
        text: &str,
        size: f32,
        location: Point,
        options: TextFacetOptions,
    ) -> FacetId {
        self.push_facet(TextFacet::with_options(face, text, size, location, options))
    }

    pub fn facet(&mut self, facet: FacetPtr) -> FacetId {
        self.push_facet(facet)
    }

    pub fn scene_options(&self) -> SceneOptions {
        SceneOptions {
            background_color: self.background_color,
            round_scene_corners: self.round_scene_corners,
        }
    }

    pub fn build(self) -> Scene {
        Scene::new_from_builder(self.scene_options(), self.facets, self.facet_id_generator)
    }
}
