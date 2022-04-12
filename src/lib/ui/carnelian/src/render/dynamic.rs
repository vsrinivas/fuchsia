// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use crate::render::generic::{BlendMode, Fill, FillRule, Gradient, GradientType, Order, Style};
use crate::{
    color::Color,
    render::generic::{
        self, forma::*, Composition as _, Context as _, PathBuilder as _, Raster as _,
        RasterBuilder as _,
    },
    Point, ViewAssistantContext,
};
use anyhow::Error;
use euclid::{
    default::{Point2D, Rect, Size2D, Transform2D, Vector2D},
    point2, size2,
};
use fuchsia_framebuffer::PixelFormat;
use std::{io::Read, mem, ops::Add, u32};
/// Rendering context and API start point.
#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct Image {
    inner: ImageInner,
}
#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
enum ImageInner {
    Forma(FormaImage),
}
/// Rectangular copy region.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct CopyRegion {
    /// Top-left origin of source rectangle.
    pub src_offset: Point2D<u32>,
    /// Top-left origin of destination rectangle.
    pub dst_offset: Point2D<u32>,
    /// Size of both source and destination rectangles.
    pub extent: Size2D<u32>,
}
/// Rendering extensions.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct RenderExt {
    /// Clears render image before rendering.
    pub pre_clear: Option<PreClear>,
    /// Copies from source image to render image before rendering.
    pub pre_copy: Option<PreCopy>,
    /// Copies from render image to destination image after rendering.
    pub post_copy: Option<PostCopy>,
}
/// Pre-render image clear.
#[derive(Clone, Debug, PartialEq)]
pub struct PreClear {
    /// Clear color.
    pub color: Color,
}
/// Pre-render image copy.
#[derive(Clone, Debug, PartialEq)]
pub struct PreCopy {
    /// Source image to copy from.
    pub image: Image,
    /// Copy region properties.
    pub copy_region: CopyRegion,
}
/// Post-render image copy.
#[derive(Clone, Debug, PartialEq)]
pub struct PostCopy {
    /// Destination image to copy to. Must be different from render image.
    pub image: Image,
    /// Copy region properties.
    pub copy_region: CopyRegion,
}
/// Rendering context and API start point.
#[derive(Debug)]
pub struct Context {
    pub inner: ContextInner,
}
#[derive(Debug)]
pub enum ContextInner {
    Forma(FormaContext),
}
impl Context {
    /// Returns the context's pixel format.
    pub fn pixel_format(&self) -> PixelFormat {
        match &self.inner {
            ContextInner::Forma(context) => context.pixel_format(),
        }
    }
    /// Optionally returns a `PathBuilder`. May return `None` of old builder is still alive.
    pub fn path_builder(&self) -> Option<PathBuilder> {
        match &self.inner {
            ContextInner::Forma(context) => context
                .path_builder()
                .map(|path_builder| PathBuilder { inner: PathBuilderInner::Forma(path_builder) }),
        }
    }
    /// Optionally returns a `RasterBuilder`. May return `None` of old builder is still alive.
    pub fn raster_builder(&self) -> Option<RasterBuilder> {
        match &self.inner {
            ContextInner::Forma(context) => context.raster_builder().map(|raster_builder| {
                RasterBuilder { inner: RasterBuilderInner::Forma(raster_builder) }
            }),
        }
    }
    /// Creates a new image with `size`.
    pub fn new_image(&mut self, size: Size2D<u32>) -> Image {
        match &mut self.inner {
            ContextInner::Forma(ref mut context) => {
                Image { inner: ImageInner::Forma(context.new_image(size)) }
            }
        }
    }
    /// Creates a new image from PNG `reader`.
    pub fn new_image_from_png<R: Read>(
        &mut self,
        reader: &mut png::Reader<R>,
    ) -> Result<Image, Error> {
        Ok(Image {
            inner: match &mut self.inner {
                ContextInner::Forma(ref mut context) => {
                    ImageInner::Forma(context.new_image_from_png(reader)?)
                }
            },
        })
    }
    /// Returns the image at `image_index`.
    pub fn get_image(&mut self, image_index: u32) -> Image {
        match &mut self.inner {
            ContextInner::Forma(ref mut render_context) => {
                Image { inner: ImageInner::Forma(render_context.get_image(image_index)) }
            }
        }
    }
    /// Returns the `context`'s current image.
    pub fn get_current_image(&mut self, context: &ViewAssistantContext) -> Image {
        match &mut self.inner {
            ContextInner::Forma(ref mut render_context) => {
                Image { inner: ImageInner::Forma(render_context.get_current_image(context)) }
            }
        }
    }

    /// Renders the composition with an optional clip to the image.
    pub fn render(
        &mut self,
        composition: &mut Composition,
        clip: Option<Rect<u32>>,
        image: Image,
        ext: &RenderExt,
    ) {
        self.render_with_clip(
            composition,
            clip.unwrap_or_else(|| {
                Rect::new(point2(u32::MIN, u32::MIN), size2(u32::MAX, u32::MAX))
            }),
            image,
            ext,
        );
    }
    /// Renders the composition with a clip to the image.
    pub fn render_with_clip(
        &mut self,
        composition: &mut Composition,
        clip: Rect<u32>,
        image: Image,
        ext: &RenderExt,
    ) {
        let background_color = composition.background_color;

        match &mut self.inner {
            ContextInner::Forma(ref mut context) => {
                let ImageInner::Forma(image) = image.inner;
                let ext = generic::RenderExt {
                    pre_clear: ext
                        .pre_clear
                        .clone()
                        .map(|pre_clear| generic::PreClear { color: pre_clear.color }),
                    pre_copy: ext.pre_copy.clone().map(|pre_copy| generic::PreCopy {
                        image: image,
                        copy_region: generic::CopyRegion {
                            src_offset: pre_copy.copy_region.src_offset,
                            dst_offset: pre_copy.copy_region.dst_offset,
                            extent: pre_copy.copy_region.extent,
                        },
                    }),
                    post_copy: ext.post_copy.clone().map(|post_copy| generic::PostCopy {
                        image: image,
                        copy_region: generic::CopyRegion {
                            src_offset: post_copy.copy_region.src_offset,
                            dst_offset: post_copy.copy_region.dst_offset,
                            extent: post_copy.copy_region.extent,
                        },
                    }),
                };
                composition.with_inner_composition(|inner| {
                    let mut composition = match inner {
                        CompositionInner::Forma(composition) => composition,
                        CompositionInner::Empty => FormaComposition::new(background_color),
                    };
                    context.render_with_clip(&mut composition, clip, image, &ext);
                    CompositionInner::Forma(composition)
                });
            }
        }
    }
}
/// Closed path built by a `PathBuilder`.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Path {
    inner: PathInner,
}
#[derive(Clone, Debug, Eq, PartialEq)]
enum PathInner {
    Forma(FormaPath),
}
/// Builds one closed path.
#[derive(Debug)]
pub struct PathBuilder {
    inner: PathBuilderInner,
}
#[derive(Debug)]
enum PathBuilderInner {
    Forma(FormaPathBuilder),
}
impl PathBuilder {
    /// Move end-point to.
    pub fn move_to(&mut self, point: Point) -> &mut Self {
        match &mut self.inner {
            PathBuilderInner::Forma(ref mut path_builder) => {
                path_builder.move_to(point);
            }
        }
        self
    }
    /// Create line from end-point to point and update end-point.
    pub fn line_to(&mut self, point: Point) -> &mut Self {
        match &mut self.inner {
            PathBuilderInner::Forma(ref mut path_builder) => {
                path_builder.line_to(point);
            }
        }
        self
    }
    /// Create quadratic Bézier from end-point to `p2` with `p1` as control point.
    pub fn quad_to(&mut self, p1: Point, p2: Point) -> &mut Self {
        match &mut self.inner {
            PathBuilderInner::Forma(ref mut path_builder) => {
                path_builder.quad_to(p1, p2);
            }
        }
        self
    }
    /// Create cubic Bézier from end-point to `p3` with `p1` and `p2` as control points.
    pub fn cubic_to(&mut self, p1: Point, p2: Point, p3: Point) -> &mut Self {
        match &mut self.inner {
            PathBuilderInner::Forma(ref mut path_builder) => {
                path_builder.cubic_to(p1, p2, p3);
            }
        }
        self
    }
    /// Create rational quadratic Bézier from end-point to `p2` with `p1` as control point
    /// and `w` as its weight.
    pub fn rat_quad_to(&mut self, p1: Point, p2: Point, w: f32) -> &mut Self {
        match &mut self.inner {
            PathBuilderInner::Forma(ref mut path_builder) => {
                path_builder.rat_quad_to(p1, p2, w);
            }
        }
        self
    }
    /// Create rational cubic Bézier from end-point to `p3` with `p1` and `p2` as control
    /// points, and `w1` and `w2` their weights.
    pub fn rat_cubic_to(&mut self, p1: Point, p2: Point, p3: Point, w1: f32, w2: f32) -> &mut Self {
        match &mut self.inner {
            PathBuilderInner::Forma(ref mut path_builder) => {
                path_builder.rat_cubic_to(p1, p2, p3, w1, w2);
            }
        }
        self
    }
    /// Closes the path with a line if not yet closed and builds the path.
    ///
    /// Consumes the builder; another one can be requested from the `Context`.
    pub fn build(self) -> Path {
        match self.inner {
            PathBuilderInner::Forma(path_builder) => {
                Path { inner: PathInner::Forma(path_builder.build()) }
            }
        }
    }
}
/// Raster built by a `RasterBuilder`.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Raster {
    inner: RasterInner,
}
#[derive(Clone, Debug, Eq, PartialEq)]
enum RasterInner {
    Forma(FormaRaster),
}
impl Raster {
    /// Translate raster.
    pub fn translate(self, translation: Vector2D<i32>) -> Self {
        match self.inner {
            RasterInner::Forma(raster) => {
                Raster { inner: RasterInner::Forma(raster.translate(translation)) }
            }
        }
    }
}
impl Add for Raster {
    type Output = Self;
    fn add(self, other: Self) -> Self::Output {
        let RasterInner::Forma(other_raster) = other.inner;
        match self.inner {
            RasterInner::Forma(raster) => {
                Raster { inner: RasterInner::Forma(raster + other_raster) }
            }
        }
    }
}
/// Builds one Raster.
#[derive(Debug)]
pub struct RasterBuilder {
    inner: RasterBuilderInner,
}
#[derive(Debug)]
enum RasterBuilderInner {
    Forma(FormaRasterBuilder),
}
impl RasterBuilder {
    /// Add a path to the raster with optional transform.
    pub fn add(&mut self, path: &Path, transform: Option<&Transform2D<f32>>) -> &mut Self {
        self.add_with_transform(path, transform.unwrap_or(&Transform2D::identity()))
    }
    /// Add a path to the raster with transform.
    pub fn add_with_transform(&mut self, path: &Path, transform: &Transform2D<f32>) -> &mut Self {
        match &mut self.inner {
            RasterBuilderInner::Forma(ref mut raster_builder) => {
                let PathInner::Forma(path) = &path.inner;
                raster_builder.add_with_transform(path, transform);
            }
        }
        self
    }
    /// Builds the raster.
    ///
    /// Consumes the builder; another one can be requested from the `Context`.
    pub fn build(self) -> Raster {
        match self.inner {
            RasterBuilderInner::Forma(raster_builder) => {
                Raster { inner: RasterInner::Forma(raster_builder.build()) }
            }
        }
    }
}

#[derive(Clone, Debug)]
pub struct Layer {
    /// Layer raster.
    pub raster: Raster,
    /// Layer clip.
    pub clip: Option<Raster>,
    /// Layer style.
    pub style: Style, // Will also contain txty when available.
}
#[derive(Debug)]
/// A group of ordered layers.
pub struct Composition {
    inner: CompositionInner,
    background_color: Color,
}
#[derive(Debug)]
enum CompositionInner {
    Forma(FormaComposition),
    Empty,
}
impl Composition {
    /// Creates a composition of ordered layers where the layers with lower index appear on top.
    pub fn new(background_color: Color) -> Self {
        Self { inner: CompositionInner::Empty, background_color }
    }
    fn with_inner_composition(&mut self, mut f: impl FnMut(CompositionInner) -> CompositionInner) {
        let inner = f(mem::replace(&mut self.inner, CompositionInner::Empty));
        self.inner = inner;
    }
    /// Resets composition by removing all layers.
    pub fn clear(&mut self) {
        match &mut self.inner {
            CompositionInner::Forma(composition) => composition.clear(),
            CompositionInner::Empty => (),
        }
    }
    /// Insert layer into composition.
    pub fn insert(&mut self, order: Order, layer: Layer) {
        if let CompositionInner::Empty = self.inner {
            match layer {
                Layer { raster: Raster { inner: RasterInner::Forma(_) }, .. } => {
                    self.inner =
                        CompositionInner::Forma(FormaComposition::new(self.background_color));
                }
            }
        }
        match &mut self.inner {
            CompositionInner::Forma(composition) => composition.insert(
                order,
                generic::Layer {
                    raster: {
                        let RasterInner::Forma(raster) = layer.raster.inner;
                        raster
                    },
                    clip: layer.clip.map(|clip| {
                        let RasterInner::Forma(clip) = clip.inner;
                        clip
                    }),
                    style: layer.style,
                },
            ),
            _ => unreachable!(),
        }
    }
    /// Remove layer from composition.
    pub fn remove(&mut self, order: Order) {
        match &mut self.inner {
            CompositionInner::Forma(composition) => composition.remove(order),
            _ => (),
        }
    }
}
