// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    color::{srgb_to_linear, Color},
    drawing::DisplayRotation,
    render::generic::Backend,
};

use euclid::default::Size2D;
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_sysmem::BufferCollectionTokenMarker;

mod composition;
mod context;
mod image;
mod path;
mod raster;

pub use composition::MoldComposition;
pub use context::MoldContext;
pub use image::MoldImage;
pub use path::{MoldPath, MoldPathBuilder};
pub use raster::{MoldRaster, MoldRasterBuilder};

#[derive(Clone, Copy, Debug, Default, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct Mold;

impl Mold {
    // Available for tests.
    pub fn new_context_without_token(
        size: Size2D<u32>,
        display_rotation: DisplayRotation,
    ) -> MoldContext {
        MoldContext::without_token(size, display_rotation)
    }
}

impl Backend for Mold {
    type Image = MoldImage;
    type Context = MoldContext;
    type Path = MoldPath;
    type PathBuilder = MoldPathBuilder;
    type Raster = MoldRaster;
    type RasterBuilder = MoldRasterBuilder;
    type Composition = MoldComposition;

    fn new_context(
        token: ClientEnd<BufferCollectionTokenMarker>,
        size: Size2D<u32>,
        display_rotation: DisplayRotation,
    ) -> MoldContext {
        MoldContext::new(token, size, display_rotation)
    }
}

impl From<&Color> for mold::Color {
    fn from(color: &Color) -> Self {
        let Color { r, g, b, a } = color;
        mold::Color {
            r: srgb_to_linear(*r),
            g: srgb_to_linear(*g),
            b: srgb_to_linear(*b),
            a: *a as f32 * 255.0f32.recip(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl::endpoints::create_endpoints;

    use euclid::size2;

    use crate::{drawing::DisplayRotation, render::generic};

    #[test]
    fn mold_init() {
        generic::tests::run(|| {
            let (token, _) =
                create_endpoints::<BufferCollectionTokenMarker>().expect("create_endpoint");
            Mold::new_context(token, size2(100, 100), DisplayRotation::Deg0);
        });
    }
}
