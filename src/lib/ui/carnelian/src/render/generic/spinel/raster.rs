// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ops::Add;

use euclid::default::{Transform2D, Vector2D};
use smallvec::{smallvec, SmallVec};

use crate::render::generic::{
    spinel::{Spinel, SpinelPath},
    Raster, RasterBuilder,
};

#[derive(Clone, Debug)]
pub struct SpinelRaster {
    pub(crate) rasters: SmallVec<[(Vec<(SpinelPath, Transform2D<f32>)>, Vector2D<f32>); 1]>,
}

impl Raster for SpinelRaster {
    fn translate(mut self, translation: Vector2D<i32>) -> Self {
        for (_, txty) in &mut self.rasters {
            *txty += translation.to_f32();
        }
        self
    }
}

impl Add for SpinelRaster {
    type Output = Self;

    fn add(mut self, other: Self) -> Self::Output {
        self.rasters.extend(other.rasters);
        self
    }
}

impl Eq for SpinelRaster {}

impl PartialEq for SpinelRaster {
    fn eq(&self, other: &Self) -> bool {
        self.rasters.len() == other.rasters.len()
            && self.rasters.iter().zip(other.rasters.iter()).all(|(left, right)| left == right)
    }
}

#[derive(Debug)]
pub struct SpinelRasterBuilder {
    pub(crate) paths: Vec<(SpinelPath, Transform2D<f32>)>,
}

impl RasterBuilder<Spinel> for SpinelRasterBuilder {
    fn add_with_transform(&mut self, path: &SpinelPath, transform: &Transform2D<f32>) -> &mut Self {
        self.paths.push((path.clone(), *transform));
        self
    }

    fn build(self) -> SpinelRaster {
        SpinelRaster { rasters: smallvec![(self.paths, Vector2D::zero())] }
    }
}
