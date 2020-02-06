// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::render::Backend;

use euclid::Size2D;
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
    ) -> MoldContext {
        MoldContext::new(token, size)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl::endpoints::create_endpoints;

    use crate::render;

    #[test]
    fn mold_init() {
        render::tests::run(|| {
            let (token, _) =
                create_endpoints::<BufferCollectionTokenMarker>().expect("create_endpoint");
            Mold::new_context(token, Size2D::new(100, 100));
        });
    }
}
