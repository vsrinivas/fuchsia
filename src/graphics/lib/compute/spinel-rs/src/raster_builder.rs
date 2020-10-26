// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::RefCell, ptr, rc::Rc};

use crate::{
    context::{Context, ContextInner},
    path::Path,
    raster::Raster,
    spinel_sys::*,
    Clip, Transform,
};

/// Spinel `Raster` builder.
///
/// Is actually a thin wrapper over the [spn_raster_builder_t] stored in [`Context`].
///
/// [spn_raster_builder_t]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/graphics/lib/compute/spinel/spinel.h#123
///
/// # Examples
///
/// ```no_run
/// # use spinel_rs::{Context, Path, Point, RasterBuilder, Transform, Clip};
/// #
/// # fn catch() -> Option<()> {
/// #     let context: Context = unimplemented!();
/// #     let circle1: Path = unimplemented!();
/// #     let circle2: Path = unimplemented!();
/// #
/// let tl = Point { x: 1.0, y: 1.0 };
/// let tr = Point { x: 5.0, y: 1.0 };
/// let br = Point { x: 5.0, y: 5.0 };
/// let bl = Point { x: 1.0, y: 5.0 };
///
/// let shapes = RasterBuilder::new(&context)
///     .fill(circle1, Transform::default(), Clip::default())
///     .fill(circle2, Transform::default(), Clip::default())
///     .build()?;
/// #     None
/// # }
/// ```
#[derive(Clone, Debug)]
pub struct RasterBuilder {
    context: Rc<RefCell<ContextInner>>,
    paths: Vec<SpnPath>,
    transforms: Vec<[f32; 8]>,
    clips: Vec<[f32; 4]>,
}

impl RasterBuilder {
    /// Allocates the `spn_raster_builder` which contains all the internal (host) raster-related
    /// data. [spn_raster_builder_create]
    ///
    /// [spn_raster_builder_create]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/graphics/lib/compute/spinel/spinel.h#128
    pub fn new(context: &Context) -> Self {
        Self {
            context: Rc::clone(&context.inner),
            paths: vec![],
            transforms: vec![],
            clips: vec![],
        }
    }

    /// Fills current `Raster` with `path` by applying the 2D `transform` and rectangular `clip`.
    /// [spn_raster_fill]
    ///
    /// [spn_raster_fill]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/graphics/lib/compute/spinel/spinel.h#175
    pub fn fill(mut self, path: Path, transform: Transform, clip: Clip) -> Self {
        if !transform.is_finite() {
            panic!("{:?} does not have finite f32 values", transform);
        }

        if !clip.is_finite() {
            panic!("{:?} does not have finite f32 values", clip);
        }

        self.paths.push(path.inner.spn_path);
        self.transforms.push(transform.as_array());
        self.clips.push([
            clip.bottom_left.x,
            clip.bottom_left.y,
            clip.top_right.x,
            clip.top_right.y,
        ]);
        self
    }

    /// Builds `Raster` and resets builder. Calls [spn_raster_begin] and [spn_raster_end] to
    /// allocate the path.
    ///
    /// [spn_raster_begin]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/graphics/lib/compute/spinel/spinel.h#144
    /// [spn_raster_end]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/graphics/lib/compute/spinel/spinel.h#147
    pub fn build(self) -> Option<Raster> {
        macro_rules! success {
            ( $result:expr, $raster_builder:expr $( , )? ) => {{
                if let Err(SpnError::SpnErrorRasterBuilderLost) = $result.res() {
                    $raster_builder.context.borrow_mut().reset_raster_builder();
                    return None;
                }
                $result.success();
            }};
        }

        unsafe {
            let spn_raster_builder = self.context.borrow().spn_raster_builder;

            success!(spn_raster_begin(spn_raster_builder), self);

            let transforms: Vec<_> =
                self.transforms.iter().map(|transform| transform.as_ptr()).collect();
            let clips: Vec<_> = self.clips.iter().map(|clip| clip.as_ptr()).collect();

            success!(
                spn_raster_fill(
                    spn_raster_builder,
                    self.paths.as_ptr(),
                    ptr::null(),
                    transforms.as_ptr(),
                    ptr::null(),
                    clips.as_ptr(),
                    self.paths.len() as u32,
                ),
                self,
            );

            let mut spn_raster = Default::default();
            success!(spn_raster_end(spn_raster_builder, &mut spn_raster as *mut _), self);

            Some(Raster::new(&self.context, spn_raster))
        }
    }
}

#[cfg(test)]
mod tests {
    use std::convert::TryInto;

    use crate::{path::Path, path_builder::PathBuilder, Point};

    use super::*;

    const FINITE: Clip =
        Clip { bottom_left: Point { x: 0.0, y: 0.0 }, top_right: Point { x: 0.0, y: 0.0 } };
    const BL_NAN: Clip = Clip {
        bottom_left: Point { x: std::f32::NAN, y: 0.0 },
        top_right: Point { x: 0.0, y: 0.0 },
    };
    const TR_NAN: Clip = Clip {
        bottom_left: Point { x: 0.0, y: 0.0 },
        top_right: Point { x: std::f32::NAN, y: 0.0 },
    };

    fn new_path_and_raster_builder() -> (Path, RasterBuilder) {
        let context = Context::new();
        let path_builder = PathBuilder::new(&context, Point::default());
        let path = path_builder.build().unwrap();

        let raster_builder = RasterBuilder::new(&context);

        (path, raster_builder)
    }

    #[test]
    fn transform_is_finite() {
        let mut values = [1.0; 17];
        values[8] = std::f32::NAN;

        for i in 0..9 {
            let transform = Transform::from_matrix(values[i..i + 9].try_into().unwrap());
            assert!(!transform.is_finite());
        }

        let transform = Transform::from_matrix([1.0; 9]);
        assert!(transform.is_finite());
    }

    #[test]
    fn raster_builder_finite_transform_and_clip() {
        let (path, raster_builder) = new_path_and_raster_builder();
        raster_builder.fill(path, Transform::identity(), Clip::default());
    }

    #[test]
    #[should_panic]
    fn raster_builder_non_finite_transform() {
        let (path, raster_builder) = new_path_and_raster_builder();
        let mut transform = Transform::identity();

        transform.scale_x = std::f32::NAN;

        raster_builder.fill(path, transform, Clip::default());
    }

    #[test]
    fn clip_is_finite() {
        assert!(FINITE.is_finite());
        assert!(!BL_NAN.is_finite());
        assert!(!TR_NAN.is_finite());
    }

    #[test]
    #[should_panic]
    fn raster_builder_non_finite_clip() {
        let (path, raster_builder) = new_path_and_raster_builder();
        raster_builder.fill(path, Transform::identity(), BL_NAN);
    }
}
