// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    fmt::Debug,
    hash::Hash,
    io::Read,
    ops::{Add, RangeBounds},
    u32,
};

use anyhow::Error;
use euclid::default::{Point2D, Rect, Size2D, Transform2D, Vector2D};
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_sysmem::BufferCollectionTokenMarker;
use fuchsia_framebuffer::PixelFormat;

use crate::{color::Color, Point, ViewAssistantContext};

pub mod mold;
pub mod spinel;

pub use self::mold::Mold;
pub use spinel::Spinel;

/// Either Spinel or Mold. Zero-sized.
pub trait Backend: Copy + Debug + Default + Eq + Hash + Ord + Sized + 'static {
    /// Buffer-backed image that can be used for rendering or storing pixel data.
    type Image: Copy + Debug + Eq + Hash + Ord;
    /// Backend's rendering context.
    type Context: Context<Self>;
    /// Vector path.
    type Path: Clone + Eq;
    /// Stateful path builder.
    type PathBuilder: PathBuilder<Self>;
    /// Compact rasterized form of any number of paths.
    type Raster: Raster;
    /// Stateful raster builder.
    type RasterBuilder: RasterBuilder<Self>;
    /// Composition of stylized rasters.
    type Composition: Composition<Self>;

    /// Creates a new rendering context
    fn new_context(
        token: ClientEnd<BufferCollectionTokenMarker>,
        size: Size2D<u32>,
    ) -> Self::Context;
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
pub struct RenderExt<B: Backend> {
    /// Clears render image before rendering.
    pub pre_clear: Option<PreClear>,
    /// Copies from source image to render image before rendering.
    pub pre_copy: Option<PreCopy<B>>,
    /// Copies from render image to destination image after rendering.
    pub post_copy: Option<PostCopy<B>>,
}

/// Pre-render image clear.
#[derive(Clone, Debug, PartialEq)]
pub struct PreClear {
    /// Clear color.
    pub color: Color,
}

/// Pre-render image copy.
#[derive(Clone, Debug, PartialEq)]
pub struct PreCopy<B: Backend> {
    /// Source image to copy from.
    pub image: B::Image,
    /// Copy region properties.
    pub copy_region: CopyRegion,
}

/// Post-render image copy.
#[derive(Clone, Debug, PartialEq)]
pub struct PostCopy<B: Backend> {
    /// Destination image to copy to. Must be different from render image.
    pub image: B::Image,
    /// Border color.
    pub color: Color,
    /// Motion exposure distance.
    pub exposure_distance: Vector2D<i32>,
    /// Copy region properties.
    pub copy_region: CopyRegion,
}

/// Rendering context and API start point.
pub trait Context<B: Backend> {
    /// Returns the context's pixel format.
    fn pixel_format(&self) -> PixelFormat;
    /// Optionally returns a `PathBuilder`. May return `None` of old builder is still alive.
    fn path_builder(&self) -> Option<B::PathBuilder>;
    /// Optionally returns a `RasterBuilder`. May return `None` of old builder is still alive.
    fn raster_builder(&self) -> Option<B::RasterBuilder>;
    /// Creates a new image with `size`.
    fn new_image(&mut self, size: Size2D<u32>) -> B::Image;
    /// Creates a new image frmo PNG `reader`.
    fn new_image_from_png<R: Read>(
        &mut self,
        reader: &mut png::Reader<R>,
    ) -> Result<B::Image, Error>;
    /// Returns the image at `image_index`.
    fn get_image(&mut self, image_index: u32) -> B::Image;
    /// Returns the `context`'s current image.
    fn get_current_image(&mut self, context: &ViewAssistantContext) -> B::Image;
    /// Renders the composition with an optional clip to the image.
    fn render(
        &mut self,
        composition: &B::Composition,
        clip: Option<Rect<u32>>,
        image: B::Image,
        ext: &RenderExt<B>,
    ) {
        self.render_with_clip(
            composition,
            clip.unwrap_or_else(|| {
                Rect::new(Point2D::new(u32::MIN, u32::MIN), Size2D::new(u32::MAX, u32::MAX))
            }),
            image,
            ext,
        );
    }
    /// Renders the composition with a clip to the image.
    fn render_with_clip(
        &mut self,
        composition: &B::Composition,
        clip: Rect<u32>,
        image: B::Image,
        ext: &RenderExt<B>,
    );
}

/// Builds one closed path.
pub trait PathBuilder<B: Backend> {
    /// Move end-point to.
    fn move_to(&mut self, point: Point) -> &mut Self;
    /// Create line from end-point to point and update end-point.
    fn line_to(&mut self, point: Point) -> &mut Self;
    /// Create quadratic Bézier from end-point to `p2` with `p1` as control point.
    fn quad_to(&mut self, p1: Point, p2: Point) -> &mut Self;
    /// Create cubic Bézier from end-point to `p3` with `p1` and `p2` as control points.
    fn cubic_to(&mut self, p1: Point, p2: Point, p3: Point) -> &mut Self;
    /// Create rational quadratic Bézier from end-point to `p2` with `p1` as control point
    /// and `w` as its weight.
    fn rat_quad_to(&mut self, p1: Point, p2: Point, w: f32) -> &mut Self;
    /// Create rational cubic Bézier from end-point to `p3` with `p1` and `p2` as control
    /// points, and `w1` and `w2` their weights.
    fn rat_cubic_to(&mut self, p1: Point, p2: Point, p3: Point, w1: f32, w2: f32) -> &mut Self;
    /// Closes the path with a line if not yet closed and builds the path.
    ///
    /// Consumes the builder; another one can be requested from the `Context`.
    fn build(self) -> B::Path;
}

/// Raster.
pub trait Raster: Add<Output = Self> + Clone + Eq {
    /// Translate raster.
    fn translate(self, translation: Vector2D<i32>) -> Self;
}

/// Builds one Raster.
pub trait RasterBuilder<B: Backend> {
    /// Add a path to the raster with optional transform.
    fn add(&mut self, path: &B::Path, transform: Option<&Transform2D<f32>>) -> &mut Self {
        self.add_with_transform(path, transform.unwrap_or(&Transform2D::identity()))
    }
    /// Add a path to the raster with transform.
    fn add_with_transform(&mut self, path: &B::Path, transform: &Transform2D<f32>) -> &mut Self;
    /// Builds the raster.
    ///
    /// Consumes the builder; another one can be requested from the `Context`.
    fn build(self) -> B::Raster;
}

/// Raster fill rule that determines how self-intersecting parts of the path are filled. It uses the
/// winding number--the count of how many full counter-clockwise revolutions ('windings') the curve
/// makes around a point--to determine whether this point should be filled or not.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum FillRule {
    /// Points with non-zero winding number are filled.
    NonZero,
    /// Points with odd non-zero numbers are filled.
    EvenOdd,
    /// Points are always filled. Useful for clearing.
    WholeTile,
}

/// Raster fill type.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Fill {
    /// Fills the raster with a uniform color.
    Solid(Color),
}

/// Raster blend mode.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum BlendMode {
    /// Blends raster over previous rasters.
    Over,
}

/// Raster style.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Style {
    /// Raster fill rule.
    pub fill_rule: FillRule,
    /// Raster fill type.
    pub fill: Fill,
    /// Raster blend mode.
    pub blend_mode: BlendMode,
}

/// 2D layer containing a raster with a style.
#[derive(Clone, Debug)]
pub struct Layer<B: Backend> {
    /// Layer raster.
    pub raster: B::Raster,
    /// Layer style.
    pub style: Style, // Will also contain txty when available.
}

/// A group of ordered layers.
pub trait Composition<B: Backend> {
    /// Creates a composition of ordered layers where the layers with lower index appear on top.
    fn new(background_color: Color) -> Self;
    /// Creates a composition using an initial set of layers.
    fn with_layers(layers: impl IntoIterator<Item = Layer<B>>, background_color: Color) -> Self;
    /// Resets composition by removing all layers.
    fn clear(&mut self);
    /// Replaces the specified range in the composition with the given `with` iterator.
    /// `replace_with` does not need to be the same length as `range`.
    fn replace<R, I>(&mut self, range: R, with: I)
    where
        R: RangeBounds<usize>,
        I: IntoIterator<Item = Layer<B>>;
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    use std::ptr;

    use fuchsia_async as fasync;
    use vk_sys as vk;

    #[test]
    fn generic_compile_test() {
        fn _generic<B: Backend>(
            token: ClientEnd<BufferCollectionTokenMarker>,
            size: Size2D<u32>,
            view_context: &ViewAssistantContext,
        ) {
            let mut context = B::new_context(token, size);

            let mut path_builder = context.path_builder().unwrap();
            path_builder.move_to(Point::new(0.0, 0.0)).line_to(Point::new(1.0, 1.0));
            let path = path_builder.build();

            let mut raster_builder = context.raster_builder().unwrap();
            raster_builder.add(&path, None);
            let raster = raster_builder.build();

            let src_image = context.new_image(Size2D::new(100, 100));
            let dst_image = context.get_current_image(view_context);

            context.render(
                &Composition::with_layers(
                    vec![Layer {
                        raster: raster.clone() + raster,
                        style: Style {
                            fill_rule: FillRule::NonZero,
                            fill: Fill::Solid(Color::white()),
                            blend_mode: BlendMode::Over,
                        },
                    }],
                    Color::new(),
                ),
                None,
                dst_image,
                &RenderExt {
                    pre_copy: Some(PreCopy {
                        image: src_image,
                        copy_region: CopyRegion {
                            src_offset: Point2D::zero(),
                            dst_offset: Point2D::zero(),
                            extent: Size2D::new(100, 100),
                        },
                    }),
                    ..Default::default()
                },
            );
        }
    }

    // Currently, render API tests are only run if a compatible Vulkan driver is found. This means
    // that these tests will be skipped on CI, but be able to run manually through the
    // `carnelian_tests` component.
    fn has_vk_instance() -> bool {
        let entry_points = super::spinel::entry_points();

        macro_rules! cstr {
            ( $bytes:expr ) => {
                ::std::ffi::CStr::from_bytes_with_nul($bytes).expect("CStr must end with '\\0'")
            };
        }

        macro_rules! vulkan_version {
            ( $major:expr, $minor:expr, $patch:expr ) => {
                ($major as u32) << 22 | ($minor as u32) << 12 | ($patch as u32)
            };
        }

        let layers = vec![];

        let mut result = false;
        unsafe {
            super::spinel::init(|ptr| {
                result = entry_points.CreateInstance(
                    &vk::InstanceCreateInfo {
                        sType: vk::STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                        pNext: ptr::null(),
                        flags: 0,
                        pApplicationInfo: &vk::ApplicationInfo {
                            sType: vk::STRUCTURE_TYPE_APPLICATION_INFO,
                            pNext: ptr::null(),
                            pApplicationName: cstr!(b"Carnelian unit test\0").as_ptr(),
                            applicationVersion: 0,
                            pEngineName: ptr::null(),
                            engineVersion: 0,
                            apiVersion: vulkan_version!(1, 1, 0),
                        },
                        enabledLayerCount: layers.len() as u32,
                        ppEnabledLayerNames: layers.as_ptr(),
                        enabledExtensionCount: 0,
                        ppEnabledExtensionNames: ptr::null(),
                    },
                    ptr::null(),
                    ptr,
                ) == vk::SUCCESS;
            });
        }

        result
    }

    pub(crate) fn run(f: impl Fn()) {
        if !has_vk_instance() {
            return;
        }

        let mut executor = fasync::Executor::new().expect("Error creating executor");
        let f = async { f() };
        executor.run_singlethreaded(f);
    }
}
