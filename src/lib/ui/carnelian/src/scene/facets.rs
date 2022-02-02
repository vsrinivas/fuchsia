// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{IdGenerator, LayerGroup};
use crate::{
    color::Color,
    drawing::{
        linebreak_text, measure_text_size, path_for_rectangle, path_for_rounded_rectangle,
        FontFace, GlyphMap, Text,
    },
    render::{
        rive::{load_rive, RenderCache as RiveRenderCache},
        BlendMode, Context as RenderContext, Fill, FillRule, Layer, Raster, Shed, Style,
    },
    scene::scene::SceneOrder,
    Coord, Point, Rect, Size, ViewAssistantContext,
};
use anyhow::Error;
use euclid::{default::Transform2D, size2, vec2};
use fuchsia_trace::duration;
use rive_rs::{self as rive};
use std::{any::Any, collections::BTreeMap, convert::TryFrom, path::PathBuf};

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
/// Identifier for a Facet
pub struct FacetId(usize);

pub(crate) struct FacetEntry {
    pub facet: FacetPtr,
    pub location: Point,
    pub size: Size,
}

pub(crate) type FacetMap = BTreeMap<FacetId, FacetEntry>;

impl FacetId {
    pub(crate) fn new(id_generator: &mut IdGenerator) -> Self {
        let facet_id = id_generator.next().expect("facet ID");
        FacetId(facet_id)
    }
}

#[derive(Debug)]
/// Message used to set the color on a facet
pub struct SetColorMessage {
    /// color value to use.
    pub color: Color,
}

#[derive(Debug)]
/// Message used to set the background color on a facet
pub struct SetBackgroundColorMessage {
    /// color value to use.
    pub color: Option<Color>,
}

/// Message used to set the text on a facet
pub struct SetTextMessage {
    /// text value to use.
    pub text: String,
}

/// Message used to set the size of a facet
pub struct SetSizeMessage {
    /// size to use
    pub size: Size,
}

/// Message used to set the corner radius of a facet
pub struct SetCornerRadiusMessage {
    /// corner radius to use, None for sharp-cornered
    pub corner_radius: Option<Coord>,
}

/// The Facet trait is used to create composable units of rendering, sizing and
/// message handling.
pub trait Facet {
    /// Called by the scene on facets when it is time for them to update their contents.
    /// Facets can add, remove or change layers in the layer group. Those layers will be
    /// combined with all the other facet layers in the scene and added to a render
    /// composition for display.
    fn update_layers(
        &mut self,
        size: Size,
        layer_group: &mut dyn LayerGroup,
        render_context: &mut RenderContext,
        view_context: &ViewAssistantContext,
    ) -> Result<(), Error>;

    /// Method for receiving arbitrary message, like `SetColorMessage` or `SetTextMessage`.
    fn handle_message(&mut self, msg: Box<dyn Any>) {
        println!("Unhandled message {:#?}", msg);
    }

    /// Should return the current size needed by this facet.
    fn calculate_size(&self, available: Size) -> Size;
}

/// A reference to a struct implementing Facet.
pub type FacetPtr = Box<dyn Facet>;

/// A facet that renders a colored rectangle.
pub struct RectangleFacet {
    size: Size,
    color: Color,
    corner_radius: Option<Coord>,
    raster: Option<Raster>,
}

impl RectangleFacet {
    /// Create a rectangle facet of size and color.
    pub fn new(size: Size, color: Color) -> FacetPtr {
        Box::new(Self { size, color, corner_radius: None, raster: None })
    }

    /// Create a rounded rectangle facet of size, corner radius and color.
    pub fn new_rounded(size: Size, corner: Coord, color: Color) -> FacetPtr {
        Box::new(Self { size, color, corner_radius: Some(corner), raster: None })
    }

    /// Create a rectangle describing a horizontal line of width, thickness and color.
    pub fn h_line(width: Coord, thickness: Coord, color: Color) -> FacetPtr {
        Self::new(size2(width, thickness), color)
    }

    /// Create a rectangle describing a vertical line of height, thickness and color.
    pub fn v_line(height: Coord, thickness: Coord, color: Color) -> FacetPtr {
        Self::new(size2(thickness, height), color)
    }
}

impl Facet for RectangleFacet {
    fn update_layers(
        &mut self,
        _size: Size,
        layer_group: &mut dyn LayerGroup,
        render_context: &mut RenderContext,
        _view_context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        let rectangle_raster = self.raster.take().unwrap_or_else(|| {
            let rectangle_path = if let Some(corner) = self.corner_radius.as_ref() {
                path_for_rounded_rectangle(&Rect::from_size(self.size), *corner, render_context)
            } else {
                path_for_rectangle(&Rect::from_size(self.size), render_context)
            };
            let mut raster_builder = render_context.raster_builder().expect("raster_builder");
            raster_builder.add(&rectangle_path, None);
            raster_builder.build()
        });
        let raster = rectangle_raster.clone();
        self.raster = Some(rectangle_raster);
        layer_group.insert(
            SceneOrder::default(),
            Layer {
                raster,
                clip: None,
                style: Style {
                    fill_rule: FillRule::NonZero,
                    fill: Fill::Solid(self.color),
                    blend_mode: BlendMode::Over,
                },
            },
        );
        Ok(())
    }

    fn handle_message(&mut self, msg: Box<dyn Any>) {
        if let Some(set_color) = msg.downcast_ref::<SetColorMessage>() {
            self.color = set_color.color;
        } else if let Some(set_size) = msg.downcast_ref::<SetSizeMessage>() {
            self.size = set_size.size;
            self.raster = None;
        } else if let Some(set_corner) = msg.downcast_ref::<SetCornerRadiusMessage>() {
            self.corner_radius = set_corner.corner_radius;
            self.raster = None;
        }
    }

    fn calculate_size(&self, _available: Size) -> Size {
        self.size
    }
}

/// Enum for specifying text horizontal alignment.
#[derive(Clone, Copy)]
pub enum TextHorizontalAlignment {
    /// Align the left edge of the text to the left
    /// edge of the facet.
    Left,
    /// Align the right edge of the text to the right
    /// edge of the facet.
    Right,
    /// Align the horizontal center of the text to the horizontal
    /// center of the facet.
    Center,
}

impl Default for TextHorizontalAlignment {
    fn default() -> Self {
        Self::Left
    }
}

/// Enum for specifying text horizontal alignment.
#[derive(Clone, Copy)]
pub enum TextVerticalAlignment {
    /// Align the top edge of the text to the top
    /// edge of the facet.
    Top,
    /// Align the bottom edge of the text to the bottom
    /// edge of the facet.
    Bottom,
    /// Align the vertical center of the text to the vertical
    /// center of the facet.
    Center,
}

impl Default for TextVerticalAlignment {
    fn default() -> Self {
        Self::Top
    }
}

#[derive(Default, Clone, Copy)]
/// Options for a text facet.
pub struct TextFacetOptions {
    /// Possibly obsolete horizontal alignment.
    pub horizontal_alignment: TextHorizontalAlignment,
    /// Possibly obsolete vertical alignment.
    pub vertical_alignment: TextVerticalAlignment,
    /// Use visual alignment, vs purely font metrics
    pub visual: bool,
    /// Foreground color for the text.
    pub color: Color,
    /// Background color for the text.
    pub background_color: Option<Color>,
    /// Optional maximum width. If present, text is word wrapped
    /// to attempt to be no wider than maximum width.
    pub max_width: Option<f32>,
}

/// A facet that renders text.
pub struct TextFacet {
    face: FontFace,
    lines: Vec<String>,
    font_size: f32,
    size: Size,
    options: TextFacetOptions,
    rendered_text: Option<Text>,
    rendered_background: Option<Raster>,
    rendered_background_size: Option<Size>,
    glyphs: GlyphMap,
}

impl TextFacet {
    fn set_text(&mut self, text: &str) {
        let lines = Self::wrap_lines(&self.face, self.font_size, text, &self.options.max_width);
        let size = Self::calculate_size(
            &self.face,
            &lines,
            self.font_size,
            self.options.max_width.unwrap_or(0.0),
            self.options.visual,
        );
        self.lines = lines;
        self.size = size;
        self.rendered_background_size = None;
        self.rendered_background = None;
        self.rendered_text = None;
    }

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

    fn calculate_size(
        face: &FontFace,
        lines: &[String],
        font_size: f32,
        max_width: f32,
        visual: bool,
    ) -> Size {
        let ascent = face.ascent(font_size);
        let descent = face.descent(font_size);
        if lines.len() > 1 {
            size2(max_width, lines.len() as f32 * (ascent - descent))
        } else {
            if lines.len() == 0 {
                Size::zero()
            } else {
                if visual {
                    measure_text_size(&face, font_size, &lines[0], true)
                } else {
                    let measured_size = measure_text_size(&face, font_size, &lines[0], false);
                    let new_size = size2(measured_size.width, ascent - descent);
                    new_size
                }
            }
        }
    }

    /// create a new text facet with default options.
    pub fn new(face: FontFace, text: &str, size: f32) -> FacetPtr {
        Self::with_options(face, text, size, TextFacetOptions::default())
    }

    /// create a new text facet with these options.
    pub fn with_options(
        face: FontFace,
        text: &str,
        font_size: f32,
        options: TextFacetOptions,
    ) -> FacetPtr {
        let lines = Self::wrap_lines(&face, font_size, text, &options.max_width);
        let size = Self::calculate_size(
            &face,
            &lines,
            font_size,
            options.max_width.unwrap_or(0.0),
            options.visual,
        );

        Box::new(Self {
            face,
            lines,
            font_size,
            size,
            options,
            rendered_text: None,
            rendered_background: None,
            rendered_background_size: None,
            glyphs: GlyphMap::new(),
        })
    }
}

impl Facet for TextFacet {
    fn update_layers(
        &mut self,
        size: Size,
        layer_group: &mut dyn LayerGroup,
        render_context: &mut RenderContext,
        _view_context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        const BACKGROUND_LAYER_ORDER: SceneOrder = SceneOrder::from_u16(0);
        const TEXT_LAYER_ORDER: SceneOrder = SceneOrder::from_u16(1);

        if self.rendered_background_size != Some(size) {
            self.rendered_background = None;
        }

        let rendered_text = self.rendered_text.take().unwrap_or_else(|| {
            Text::new_with_lines(
                render_context,
                &self.lines,
                self.font_size,
                &self.face,
                &mut self.glyphs,
            )
        });
        let mut x = match self.options.horizontal_alignment {
            TextHorizontalAlignment::Left => 0.0,
            TextHorizontalAlignment::Center => (size.width - self.size.width) / 2.0,
            TextHorizontalAlignment::Right => size.width - self.size.width,
        };
        if self.options.visual {
            x -= rendered_text.bounding_box.origin.x;
        }
        let mut y = match self.options.vertical_alignment {
            TextVerticalAlignment::Top => 0.0,
            TextVerticalAlignment::Bottom => size.height - self.size.height,
            TextVerticalAlignment::Center => (size.height - self.size.height) / 2.0,
        };
        if self.options.visual {
            y -= rendered_text.bounding_box.origin.y;
        }
        let translation = vec2(x, y);
        let raster = rendered_text.raster.clone().translate(translation.to_i32());
        self.rendered_text = Some(rendered_text);

        layer_group.insert(
            TEXT_LAYER_ORDER,
            Layer {
                raster,
                clip: None,
                style: Style {
                    fill_rule: FillRule::NonZero,
                    fill: Fill::Solid(self.options.color),
                    blend_mode: BlendMode::Over,
                },
            },
        );

        if let Some(background_color) = self.options.background_color.as_ref() {
            let rendered_background = self.rendered_background.take().unwrap_or_else(|| {
                let bg_bounds = Rect::from_size(size);
                let rect_path = path_for_rectangle(&bg_bounds, render_context);
                let mut raster_builder = render_context.raster_builder().expect("raster_builder");
                raster_builder.add(&rect_path, None);
                raster_builder.build()
            });
            let raster = rendered_background.clone();
            self.rendered_background = Some(rendered_background);
            self.rendered_background_size = Some(size);
            layer_group.insert(
                BACKGROUND_LAYER_ORDER,
                Layer {
                    raster,
                    clip: None,
                    style: Style {
                        fill_rule: FillRule::NonZero,
                        fill: Fill::Solid(*background_color),
                        blend_mode: BlendMode::Over,
                    },
                },
            );
        } else {
            layer_group.remove(BACKGROUND_LAYER_ORDER)
        }
        Ok(())
    }

    fn handle_message(&mut self, msg: Box<dyn Any>) {
        if let Some(set_text) = msg.downcast_ref::<SetTextMessage>() {
            self.set_text(&set_text.text);
        } else if let Some(set_color) = msg.downcast_ref::<SetColorMessage>() {
            self.options.color = set_color.color;
        } else if let Some(set_background_color) = msg.downcast_ref::<SetBackgroundColorMessage>() {
            self.options.background_color = set_background_color.color;
            self.rendered_background = None;
        }
    }

    fn calculate_size(&self, _available: Size) -> Size {
        self.size
    }
}

/// A facet constructed with a raster, style and size.
pub struct RasterFacet {
    raster: Raster,
    style: Style,
    size: Size,
}

impl RasterFacet {
    /// Construct a facet with a raster, style and size.
    pub fn new(raster: Raster, style: Style, size: Size) -> Self {
        Self { raster, style, size }
    }
}

impl Facet for RasterFacet {
    fn update_layers(
        &mut self,
        _size: Size,
        layer_group: &mut dyn LayerGroup,
        _render_context: &mut RenderContext,
        _view_context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        layer_group.insert(
            SceneOrder::default(),
            Layer { raster: self.raster.clone(), clip: None, style: self.style.clone() },
        );
        Ok(())
    }

    fn calculate_size(&self, _available: Size) -> Size {
        self.size
    }
}

/// A facet constructed with the contents of a Shed vector image file.
pub struct ShedFacet {
    path: PathBuf,
    size: Size,
    rasters: Option<Vec<(Raster, Style)>>,
}

impl ShedFacet {
    /// Create a shed facet with the contents of a Shed vector image file.
    pub fn new(path: PathBuf, size: Size) -> Self {
        Self { path, size, rasters: None }
    }
}

impl Facet for ShedFacet {
    fn update_layers(
        &mut self,
        _size: Size,
        layer_group: &mut dyn LayerGroup,
        render_context: &mut RenderContext,
        _view_context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        let rasters = self.rasters.take().unwrap_or_else(|| {
            if let Some(shed) = Shed::open(&self.path).ok() {
                let shed_size = shed.size();
                let scale_factor: Size =
                    size2(self.size.width / shed_size.width, self.size.height / shed_size.height);
                let transform =
                    Transform2D::translation(-shed_size.width / 2.0, -shed_size.height / 2.0)
                        .then_scale(scale_factor.width, scale_factor.height);

                shed.rasters(render_context, Some(&transform))
            } else {
                let placeholder_rect =
                    Rect::from_size(self.size).translate(self.size.to_vector() / -2.0);
                let rect_path = path_for_rectangle(&placeholder_rect, render_context);
                let mut raster_builder = render_context.raster_builder().expect("raster_builder");
                raster_builder.add(&rect_path, None);
                let raster = raster_builder.build();
                vec![(
                    raster,
                    Style {
                        fill_rule: FillRule::NonZero,
                        fill: Fill::Solid(Color::red()),
                        blend_mode: BlendMode::Over,
                    },
                )]
            }
        });
        for (i, (raster, style)) in rasters.iter().rev().enumerate() {
            layer_group.insert(
                SceneOrder::try_from(i).unwrap_or_else(|e| panic!("{}", e)),
                Layer { raster: raster.clone(), clip: None, style: style.clone() },
            );
        }
        self.rasters = Some(rasters);
        Ok(())
    }

    fn calculate_size(&self, _available: Size) -> Size {
        self.size
    }
}

/// A facet that occupies space without rendering.
pub struct SpacingFacet {
    size: Size,
}

impl SpacingFacet {
    /// Create a new spacing facet of size.
    pub fn new(size: Size) -> Self {
        Self { size }
    }
}

impl Facet for SpacingFacet {
    fn update_layers(
        &mut self,
        _size: Size,
        _layer_group: &mut dyn LayerGroup,
        _render_context: &mut RenderContext,
        _view_context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        Ok(())
    }

    fn calculate_size(&self, _available: Size) -> Size {
        self.size
    }
}

/// A facet constructed with the contents of a Rive animation file.
pub struct RiveFacet {
    size: Size,
    artboard: rive::Object<rive::Artboard>,
    render_cache: RiveRenderCache,
}

impl RiveFacet {
    /// Create a Rive facet with the contents of a Rive file.
    pub fn new(size: Size, artboard: rive::Object<rive::Artboard>) -> Self {
        Self { size, artboard, render_cache: RiveRenderCache::new() }
    }

    /// Given an already loaded Rive file, create a new Rive facet with the given named
    /// artboard, or the first if artboard_name is None.
    pub fn new_from_file(
        size: Size,
        file: &rive::File,
        artboard_name: Option<&str>,
    ) -> Result<Self, Error> {
        let artboard = if let Some(artboard_name) = artboard_name {
            file.get_artboard(artboard_name).ok_or_else(|| {
                anyhow::anyhow!("artboard {} not found in rive file {:?}", artboard_name, file)
            })?
        } else {
            file.artboard().ok_or_else(|| anyhow::anyhow!("no artboard in rive file {:?}", file))?
        };
        let facet = RiveFacet::new(size, artboard.clone());
        let artboard_ref = artboard.as_ref();
        artboard_ref.advance(0.0);
        Ok(facet)
    }

    /// Given a path to a file, load the file and create a new Rive facet with the given named
    /// artboard, or the first if artboard_name is None.
    pub fn new_from_path<P: AsRef<std::path::Path> + std::fmt::Debug>(
        size: Size,
        path: P,
        artboard_name: Option<&str>,
    ) -> Result<Self, Error> {
        let file = load_rive(&path)?;
        Self::new_from_file(size, &file, artboard_name)
    }
}

impl Facet for RiveFacet {
    fn update_layers(
        &mut self,
        _size: Size,
        layer_group: &mut dyn LayerGroup,
        render_context: &mut RenderContext,
        _view_context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        duration!("gfx", "render::RiveFacet::update_layers");

        let artboard_ref = self.artboard.as_ref();
        let width = self.size.width as f32;
        let height = self.size.height as f32;
        self.render_cache.with_renderer(render_context, |renderer| {
            artboard_ref.draw(
                renderer,
                rive::layout::align(
                    rive::layout::Fit::Contain,
                    rive::layout::Alignment::center(),
                    rive::math::Aabb::new(0.0, 0.0, width, height),
                    artboard_ref.bounds(),
                ),
            );
        });

        let layers = self.render_cache.layers.drain(..).filter(|Layer { style, .. }|
                    // Skip transparent fills. This optimization is especially useful for
                    // artboards with transparent backgrounds.
                    match &style.fill {
                        Fill::Solid(color) => color.a != 0 || style.blend_mode != BlendMode::Over,
                        _ => true
                    });

        layer_group.clear();
        for (i, layer) in layers.enumerate() {
            layer_group.insert(SceneOrder::try_from(i).unwrap_or_else(|e| panic!("{}", e)), layer);
        }

        Ok(())
    }

    fn calculate_size(&self, _available: Size) -> Size {
        self.size
    }

    fn handle_message(&mut self, msg: Box<dyn Any>) {
        if let Some(set_size) = msg.downcast_ref::<SetSizeMessage>() {
            self.size = set_size.size;
        }
    }
}

#[cfg(test)]
mod tests {
    use crate::{
        drawing::FontFace,
        scene::facets::{TextFacet, TextFacetOptions},
    };
    use once_cell::sync::Lazy;

    static FONT_DATA: &'static [u8] = include_bytes!(
        "../../../../../../prebuilt/third_party/fonts/robotoslab/RobotoSlab-Regular.ttf"
    );
    static FONT_FACE: Lazy<FontFace> =
        Lazy::new(|| FontFace::new(&FONT_DATA).expect("Failed to create font"));

    const SAMPLE_TEXT: &'static str = "Lorem ipsum dolor sit amet, consectetur \
    adipisicing elit, sed do eiusmod tempor incididunt ut labore et dolore magna \
    aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris \
    nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit \
    in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint \
    occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim \
    id est laborum.";

    #[test]
    fn test_text_facet() {
        let _ = TextFacet::with_options(FONT_FACE.clone(), "", 32.0, TextFacetOptions::default());
        let _ = TextFacet::with_options(
            FONT_FACE.clone(),
            SAMPLE_TEXT,
            32.0,
            TextFacetOptions::default(),
        );
        let max_width_options =
            TextFacetOptions { max_width: Some(200.0), ..TextFacetOptions::default() };
        let _ = TextFacet::with_options(FONT_FACE.clone(), "", 32.0, max_width_options);
        let _ = TextFacet::with_options(FONT_FACE.clone(), SAMPLE_TEXT, 32.0, max_width_options);
    }
}
