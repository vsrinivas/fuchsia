// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{ops::Add, rc::Rc};

use euclid::{Transform2D, Vector2D};
use smallvec::{smallvec, SmallVec};

use crate::render::{
    mold::{Mold, MoldPath},
    Raster, RasterBuilder,
};

#[derive(Clone, Debug)]
pub struct MoldRaster {
    pub(crate) rasters: SmallVec<[(Vec<(Rc<mold::Path>, Transform2D<f32>)>, Vector2D<f32>); 1]>,
}

impl Raster for MoldRaster {
    fn translate(mut self, translation: Vector2D<i32>) -> Self {
        for (_, txty) in &mut self.rasters {
            *txty += translation.to_f32();
        }
        self
    }
}

impl Add for MoldRaster {
    type Output = Self;

    fn add(mut self, other: Self) -> Self::Output {
        self.rasters.extend(other.rasters);
        self
    }
}

impl Eq for MoldRaster {}

impl PartialEq for MoldRaster {
    fn eq(&self, other: &Self) -> bool {
        self.rasters.len() == other.rasters.len()
            && self.rasters.iter().zip(other.rasters.iter()).all(
                |((paths_transforms, txty), (other_paths_transforms, other_txty))| {
                    txty == other_txty
                        && paths_transforms.len() == other_paths_transforms.len()
                        && paths_transforms.iter().zip(other_paths_transforms.iter()).all(
                            |((path, transform), (other_path, other_transform))| {
                                Rc::ptr_eq(&path, &other_path) && transform == other_transform
                            },
                        )
                },
            )
    }
}

#[derive(Debug)]
pub struct MoldRasterBuilder {
    paths_transforms: Vec<(Rc<mold::Path>, Transform2D<f32>)>,
}

impl MoldRasterBuilder {
    pub(crate) fn new() -> Self {
        Self { paths_transforms: vec![] }
    }
}

impl RasterBuilder<Mold> for MoldRasterBuilder {
    fn add_with_transform(&mut self, path: &MoldPath, transform: &Transform2D<f32>) -> &mut Self {
        self.paths_transforms.push((Rc::clone(&path.path), *transform));
        self
    }

    fn build(self) -> MoldRaster {
        MoldRaster { rasters: smallvec![(self.paths_transforms, Vector2D::zero())] }
    }
}
