// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{IdGenerator, LayerGroup};
use crate::{
    color::Color,
    drawing::{linebreak_text, measure_text_width, path_for_rectangle, FontFace, GlyphMap, Text},
    render::{
        rive::RenderCache as RiveRenderCache, BlendMode, Context as RenderContext, Fill, FillRule,
        Layer, Raster, Shed, Style,
    },
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
    raster: Option<Raster>,
}

impl RectangleFacet {
    /// Create a rectangle facet of size and color.
    pub fn new(size: Size, color: Color) -> FacetPtr {
        Box::new(Self { size, color, raster: None })
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
        let line_raster = self.raster.take().unwrap_or_else(|| {
            let line_path = path_for_rectangle(&Rect::from_size(self.size), render_context);
            let mut raster_builder = render_context.raster_builder().expect("raster_builder");
            raster_builder.add(&line_path, None);
            raster_builder.build()
        });
        let raster = line_raster.clone();
        self.raster = Some(line_raster);
        layer_group.insert(
            0,
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
        }
    }

    fn calculate_size(&self, _available: Size) -> Size {
        self.size
    }
}

/// Possibly obsolete enum for specifying text horizontal alignment.
#[allow(missing_docs)]
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

/// Possibly obsolete enum for specifying text horizontal alignment.
#[allow(missing_docs)]
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
/// Options for a text facet.
pub struct TextFacetOptions {
    /// Possibly obsolete horizontal alignment.
    pub horizontal_alignment: TextHorizontalAlignment,
    /// Possibly obsolete vertical alignment.
    pub vertical_alignment: TextVerticalAlignment,
    /// Foreground color for the text.
    pub color: Color,
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

        let size = if lines.len() > 1 {
            size2(options.max_width.expect("max_width"), lines.len() as f32 * font_size)
        } else {
            let ascent = face.ascent(font_size);
            let descent = face.descent(font_size);
            size2(measure_text_width(&face, font_size, text), ascent - descent)
        };

        Box::new(Self {
            face,
            lines,
            font_size,
            size,
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
        layer_group: &mut dyn LayerGroup,
        render_context: &mut RenderContext,
        _view_context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        let rendered_text = self.rendered_text.take().unwrap_or_else(|| {
            Text::new_with_lines(
                render_context,
                &self.lines,
                self.font_size,
                &self.face,
                &mut self.glyphs,
            )
        });
        let ascent = self.face.ascent(self.font_size);
        let descent = self.face.descent(self.font_size);
        let x = match self.options.horizontal_alignment {
            TextHorizontalAlignment::Left => 0.0,
            TextHorizontalAlignment::Center => -rendered_text.bounding_box.size.width / 2.0,
            TextHorizontalAlignment::Right => -rendered_text.bounding_box.size.width,
        };
        let y = match self.options.vertical_alignment {
            TextVerticalAlignment::Baseline => 0.0 - ascent,
            TextVerticalAlignment::Top => 0.0,
            TextVerticalAlignment::Bottom => 0.0 - ascent + descent,
            TextVerticalAlignment::Center => {
                let capital_height =
                    self.face.capital_height(self.font_size).unwrap_or(self.font_size);
                0.0 + capital_height / 2.0 - ascent
            }
        };
        let translation = vec2(x, y);
        let raster = rendered_text.raster.clone().translate(translation.to_i32());
        self.rendered_text = Some(rendered_text);

        layer_group.insert(
            0,
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
        Ok(())
    }

    fn handle_message(&mut self, msg: Box<dyn Any>) {
        if let Some(set_text) = msg.downcast_ref::<SetTextMessage>() {
            self.lines = Self::wrap_lines(
                &self.face,
                self.font_size,
                &set_text.text,
                &self.options.max_width,
            );
            self.rendered_text = None;
        } else if let Some(set_color) = msg.downcast_ref::<SetColorMessage>() {
            self.options.color = set_color.color;
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
            0,
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
                u16::try_from(i).expect("too many layers"),
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
            layer_group.insert(u16::try_from(i).expect("too many layers"), layer);
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
