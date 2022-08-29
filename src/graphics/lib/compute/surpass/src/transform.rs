// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{convert::TryFrom, error::Error, fmt};

use crate::{path::MAX_ERROR, AffineTransform, Point, MAX_HEIGHT, MAX_WIDTH};

const MAX_SCALING_FACTOR_X: f32 = 1.0 + MAX_ERROR as f32 / MAX_WIDTH as f32;
const MAX_SCALING_FACTOR_Y: f32 = 1.0 + MAX_ERROR as f32 / MAX_HEIGHT as f32;

#[derive(Debug, Eq, PartialEq)]
pub enum GeomPresTransformError {
    ExceededScalingFactor { x: bool, y: bool },
}

impl fmt::Display for GeomPresTransformError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            GeomPresTransformError::ExceededScalingFactor { x: true, y: false } => {
                write!(f, "exceeded scaling factor on the X axis (-1.0 to 1.0)")
            }
            GeomPresTransformError::ExceededScalingFactor { x: false, y: true } => {
                write!(f, "exceeded scaling factor on the Y axis (-1.0 to 1.0)")
            }
            GeomPresTransformError::ExceededScalingFactor { x: true, y: true } => {
                write!(f, "exceeded scaling factor on both axis (-1.0 to 1.0)")
            }
            _ => panic!("cannot display invalid GeomPresTransformError"),
        }
    }
}

impl Error for GeomPresTransformError {}

#[derive(Default, Clone, Copy, Debug, Eq, PartialEq)]
pub struct GeomPresTransform(pub(crate) AffineTransform);

impl GeomPresTransform {
    /// ```text
    /// [ x' ]   [ t.0 t.1 t.4 ] [ x ]
    /// [ y' ] = [ t.2 t.3 t.5 ] [ y ]
    /// [ 1  ]   [   0   0   1 ] [ 1 ]
    /// ```
    #[inline]
    pub fn new(mut transform: [f32; 9]) -> Option<Self> {
        (transform[6].abs() <= f32::EPSILON && transform[7].abs() <= f32::EPSILON)
            .then(|| {
                if (transform[8] - 1.0).abs() > f32::EPSILON {
                    let recip = transform[8].recip();
                    for val in &mut transform[..6] {
                        *val *= recip;
                    }
                }

                Self::try_from(AffineTransform {
                    ux: transform[0],
                    vx: transform[1],
                    uy: transform[3],
                    vy: transform[4],
                    tx: transform[2],
                    ty: transform[5],
                })
                .ok()
            })
            .flatten()
    }

    pub fn is_identity(&self) -> bool {
        self.0.is_identity()
    }

    pub(crate) fn transform(&self, point: Point) -> Point {
        self.0.transform(point)
    }

    #[inline]
    pub fn as_slice(&self) -> [f32; 6] {
        [self.0.ux, self.0.vx, self.0.uy, self.0.vy, self.0.tx, self.0.ty]
    }
}

impl TryFrom<[f32; 6]> for GeomPresTransform {
    type Error = GeomPresTransformError;
    fn try_from(transform: [f32; 6]) -> Result<Self, Self::Error> {
        GeomPresTransform::try_from(AffineTransform::from(transform))
    }
}

impl TryFrom<AffineTransform> for GeomPresTransform {
    type Error = GeomPresTransformError;

    fn try_from(t: AffineTransform) -> Result<Self, Self::Error> {
        let scales_up_x = t.ux * t.ux + t.uy * t.uy > MAX_SCALING_FACTOR_X;
        let scales_up_y = t.vx * t.vx + t.vy * t.vy > MAX_SCALING_FACTOR_Y;

        (!scales_up_x && !scales_up_y)
            .then(|| Self(t))
            .ok_or(GeomPresTransformError::ExceededScalingFactor { x: scales_up_x, y: scales_up_y })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_identity() {
        let transform = GeomPresTransform::default();

        assert_eq!(transform.transform(Point::new(2.0, 3.0)), Point::new(2.0, 3.0));
    }

    #[test]
    fn as_slice() {
        let slice = [0.1, 0.5, 0.4, 0.3, 0.7, 0.9];

        assert_eq!(slice, GeomPresTransform::try_from(slice).unwrap().as_slice());
    }

    #[test]
    fn scale_translate() {
        let transform = GeomPresTransform::try_from([0.1, 0.5, 0.4, 0.3, 0.5, 0.6]).unwrap();

        assert_eq!(transform.transform(Point::new(2.0, 3.0)), Point::new(2.2, 2.3));
    }

    #[test]
    fn wrong_scaling_factor() {
        let transform =
            [0.1, MAX_SCALING_FACTOR_Y.sqrt(), MAX_SCALING_FACTOR_X.sqrt(), 0.1, 0.5, 0.0];

        assert_eq!(
            GeomPresTransform::try_from(transform),
            Err(GeomPresTransformError::ExceededScalingFactor { x: true, y: true })
        );
    }

    #[test]
    fn wrong_scaling_factor_x() {
        let transform = [0.1, 0.0, MAX_SCALING_FACTOR_X.sqrt(), 0.0, 0.5, 0.0];

        assert_eq!(
            GeomPresTransform::try_from(transform),
            Err(GeomPresTransformError::ExceededScalingFactor { x: true, y: false })
        );
    }

    #[test]
    fn wrong_scaling_factor_y() {
        let transform = [0.0, MAX_SCALING_FACTOR_Y.sqrt(), 0.0, 0.1, 0.5, 0.0];

        assert_eq!(
            GeomPresTransform::try_from(transform),
            Err(GeomPresTransformError::ExceededScalingFactor { x: false, y: true })
        );
    }

    #[test]
    fn correct_scaling_factor() {
        let transform = [1.0, MAX_SCALING_FACTOR_Y.sqrt(), 0.0, 0.0, 0.5, 0.0];

        assert_eq!(transform, GeomPresTransform::try_from(transform).unwrap().as_slice());
    }
}
