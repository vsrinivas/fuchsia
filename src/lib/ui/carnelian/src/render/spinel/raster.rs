// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::RefCell, ops::Add, ptr, rc::Rc};

use euclid::Transform2D;
use smallvec::{smallvec, SmallVec};
use spinel_rs_sys::*;

use crate::render::{
    spinel::{init, InnerContext, Spinel, SpinelPath},
    RasterBuilder,
};

#[derive(Clone, Debug)]
pub struct SpinelRaster {
    context: Rc<RefCell<InnerContext>>,
    rasters: Option<SmallVec<[Rc<SpnRaster>; 1]>>,
}

impl SpinelRaster {
    pub(crate) fn rasters(&self) -> &[Rc<SpnRaster>] {
        self.rasters.as_ref().map(|vec| vec.as_slice()).unwrap_or(&[])
    }
}

impl Add for SpinelRaster {
    type Output = Self;

    fn add(mut self, mut other: Self) -> Self::Output {
        assert!(Rc::ptr_eq(&self.context, &other.context));

        self.rasters = match (self.rasters.take(), other.rasters.take()) {
            (Some(mut rasters0), Some(rasters1)) => {
                rasters0.extend(rasters1);
                Some(rasters0)
            }
            (Some(rasters), None) | (None, Some(rasters)) => Some(rasters),
            _ => None,
        };
        self
    }
}

impl Drop for SpinelRaster {
    fn drop(&mut self) {
        if let (Some(context), Some(raster)) =
            (self.context.borrow().get_checked(), self.rasters.as_ref())
        {
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

impl Eq for SpinelRaster {}

impl PartialEq for SpinelRaster {
    fn eq(&self, other: &Self) -> bool {
        Rc::ptr_eq(&self.context, &other.context)
            && self.rasters().len() == other.rasters().len()
            && self
                .rasters()
                .iter()
                .zip(other.rasters().iter())
                .all(|(left, right)| Rc::ptr_eq(left, right))
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
            rasters: Some(smallvec![Rc::new(unsafe {
                init(|ptr| spn!(spn_raster_builder_end(*self.raster_builder, ptr)))
            })]),
        }
    }
}
