// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use euclid::Transform2D;

use crate::render::{
    mold::{Mold, MoldPath},
    RasterBuilder,
};

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MoldRaster {
    pub(crate) raster: mold::Raster,
}

#[derive(Debug)]
pub struct MoldRasterBuilder {
    paths_transforms: Vec<(MoldPath, [f32; 9])>,
}

impl MoldRasterBuilder {
    pub(crate) fn new() -> Self {
        Self { paths_transforms: vec![] }
    }
}

impl RasterBuilder<Mold> for MoldRasterBuilder {
    fn add_with_transform(&mut self, path: &MoldPath, transform: &Transform2D<f32>) -> &mut Self {
        let transform: [f32; 9] = [
            transform.m11,
            transform.m21,
            transform.m31,
            transform.m12,
            transform.m22,
            transform.m32,
            0.0,
            0.0,
            1.0,
        ];
        self.paths_transforms.push((path.clone(), transform));
        self
    }

    fn build(self) -> MoldRaster {
        MoldRaster {
            raster: mold::Raster::from_paths_and_transforms(
                self.paths_transforms.iter().map(|(path, transform)| (&*path.path, transform)),
            ),
        }
    }
}
