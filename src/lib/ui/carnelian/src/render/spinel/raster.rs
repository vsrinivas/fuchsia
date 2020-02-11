// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::RefCell, ops::Add, ptr, rc::Rc};

use euclid::Transform2D;
use spinel_rs_sys::*;

use crate::render::{
    ops::Op,
    spinel::{init, InnerContext, Spinel, SpinelPath},
    RasterBuilder,
};

#[derive(Clone, Debug)]
pub struct SpinelRaster {
    context: Rc<RefCell<InnerContext>>,
    raster: Option<Op<Rc<SpnRaster>>>,
}

impl SpinelRaster {
    pub(crate) fn raster(&self) -> &Op<Rc<SpnRaster>> {
        self.raster.as_ref().unwrap()
    }
}

impl Add for SpinelRaster {
    type Output = Self;

    fn add(mut self, mut other: Self) -> Self::Output {
        assert!(Rc::ptr_eq(&self.context, &other.context));

        Self {
            context: Rc::clone(&self.context),
            raster: self.raster.take().and_then(|raster| other.raster.take().map(|other| raster.add(other)))
        }
    }
}

impl Drop for SpinelRaster {
    fn drop(&mut self) {
        if let Some(context) = self.context.borrow().get_checked() {
            if let Some(raster) = self.raster.as_ref() {
                for raster in raster.iter() {
                    if Rc::strong_count(raster) == 1 {
                        unsafe {
                            spn!(spn_raster_release(context, &**raster as *const _, 1));
                        }
                    }
                }
            }
        }
    }
}

impl Eq for SpinelRaster {}

impl PartialEq for SpinelRaster {
    fn eq(&self, other: &Self) -> bool {
        Rc::ptr_eq(&self.context, &other.context) && self.raster().len() == other.raster().len() &&
            self.raster().iter().zip(other.raster().iter()).all(|(left, right)| Rc::ptr_eq(left, right))
    }
}

#[derive(Debug)]
pub struct SpinelRasterBuilder {
    pub(crate) context: Rc<RefCell<InnerContext>>,
    pub(crate) raster_builder: Rc<SpnRasterBuilder>,
}

impl RasterBuilder<Spinel> for SpinelRasterBuilder {
    fn add_with_transform(&mut self, path: &SpinelPath, transform: &Transform2D<f32>) -> &mut Self {
        const SPINEL_TRANSFORM_MULTIPLIER: f32 = 32.0;

        let transform = SpnTransform {
            sx: transform.m11 * SPINEL_TRANSFORM_MULTIPLIER,
            shx: transform.m21 * SPINEL_TRANSFORM_MULTIPLIER,
            tx: transform.m31 * SPINEL_TRANSFORM_MULTIPLIER,
            shy: transform.m12 * SPINEL_TRANSFORM_MULTIPLIER,
            sy: transform.m22 * SPINEL_TRANSFORM_MULTIPLIER,
            ty: transform.m32 * SPINEL_TRANSFORM_MULTIPLIER,
            w0: 0.0,
            w1: 0.0,
        };
        let clip =
            SpnClip { x0: std::f32::MIN, y0: std::f32::MIN, x1: std::f32::MAX, y1: std::f32::MAX };

        unsafe {
            spn!(spn_raster_builder_add(
                *self.raster_builder,
                &*path.path,
                ptr::null_mut(),
                &transform,
                ptr::null_mut(),
                &clip,
                1,
            ));
        }

        self
    }

    fn build(self) -> SpinelRaster {
        SpinelRaster {
            context: Rc::clone(&self.context),
            raster: Some(Op::Raster(Rc::new(unsafe {
                init(|ptr| spn!(spn_raster_builder_end(*self.raster_builder, ptr)))
            }))),
        }
    }
}
