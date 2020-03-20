// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::RefCell, ops::Add, rc::Rc};

use euclid::default::{Transform2D, Vector2D};
use smallvec::{smallvec, SmallVec};

use crate::render::generic::{
    mold::{Mold, MoldPath},
    Raster, RasterBuilder,
};

#[derive(Clone, Debug)]
pub(crate) struct Print {
    pub(crate) path: Rc<mold_next::Path>,
    pub(crate) transform: Transform2D<f32>,
}

#[derive(Debug)]
pub struct MoldRaster {
    pub(crate) prints: SmallVec<[Print; 1]>,
    pub(crate) layer_id: Rc<RefCell<Option<mold_next::LayerId>>>,
    pub(crate) translation: Vector2D<f32>,
}

impl Raster for MoldRaster {
    fn translate(mut self, translation: Vector2D<i32>) -> Self {
        self.translation += translation.to_f32();
        self
    }
}

impl Add for MoldRaster {
    type Output = Self;

    fn add(mut self, mut other: Self) -> Self::Output {
        if self.translation != Vector2D::zero() {
            for print in &mut self.prints {
                print.transform.m31 += self.translation.x;
                print.transform.m32 += self.translation.y;
            }

            self.translation = Vector2D::zero();
        }

        if other.translation != Vector2D::zero() {
            for print in &mut other.prints {
                print.transform.m31 += other.translation.x;
                print.transform.m32 += other.translation.y;
            }
        }

        self.prints.extend(other.prints);
        self.layer_id = Rc::new(RefCell::new(None));
        self
    }
}

impl Clone for MoldRaster {
    fn clone(&self) -> Self {
        Self {
            prints: self.prints.clone(),
            layer_id: Rc::new(RefCell::new(None)),
            translation: self.translation.clone(),
        }
    }
}

impl Eq for MoldRaster {}

impl PartialEq for MoldRaster {
    fn eq(&self, _other: &Self) -> bool {
        todo!()
    }
}

#[derive(Debug)]
pub struct MoldRasterBuilder {
    prints: SmallVec<[Print; 1]>,
}

impl MoldRasterBuilder {
    pub(crate) fn new() -> Self {
        Self { prints: smallvec![] }
    }
}

impl RasterBuilder<Mold> for MoldRasterBuilder {
    fn add_with_transform(&mut self, path: &MoldPath, transform: &Transform2D<f32>) -> &mut Self {
        self.prints.push(Print { path: Rc::clone(&path.path), transform: *transform });
        self
    }

    fn build(self) -> MoldRaster {
        MoldRaster {
            prints: self.prints,
            layer_id: Rc::new(RefCell::new(None)),
            translation: Vector2D::zero(),
        }
    }
}
