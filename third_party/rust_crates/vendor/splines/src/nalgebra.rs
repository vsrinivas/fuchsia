use alga::general::{ClosedAdd, ClosedDiv, ClosedMul, ClosedSub};
use nalgebra::{Scalar, Vector, Vector1, Vector2, Vector3, Vector4, Vector5, Vector6};
use num_traits as nt;
use std::ops::Mul;

use crate::interpolate::{
  Interpolate, Linear, Additive, One, cubic_bezier_def, cubic_hermite_def, quadratic_bezier_def
};

macro_rules! impl_interpolate_vector {
  ($($t:tt)*) => {
    // implement Linear
    impl<T> Linear<T> for $($t)*<T> where T: Scalar + ClosedAdd + ClosedSub + ClosedMul + ClosedDiv {
      #[inline(always)]
      fn outer_mul(self, t: T) -> Self {
        self * t
      }

      #[inline(always)]
      fn outer_div(self, t: T) -> Self {
        self / t
      }
    }

    impl<T, V> Interpolate<T> for $($t)*<V>
    where Self: Linear<T>,
          T: Additive + One + Mul<T, Output = T>,
          V: nt::One +
             nt::Zero +
             Additive +
             Scalar +
             ClosedAdd +
             ClosedMul +
             ClosedSub +
             Interpolate<T> {
      #[inline(always)]
      fn lerp(a: Self, b: Self, t: T) -> Self {
        Vector::zip_map(&a, &b, |c1, c2| Interpolate::lerp(c1, c2, t))
      }

      #[inline(always)]
      fn cubic_hermite(x: (Self, T), a: (Self, T), b: (Self, T), y: (Self, T), t: T) -> Self {
        cubic_hermite_def(x, a, b, y, t)
      }

      #[inline(always)]
      fn quadratic_bezier(a: Self, u: Self, b: Self, t: T) -> Self {
        quadratic_bezier_def(a, u, b, t)
      }

      #[inline(always)]
      fn cubic_bezier(a: Self, u: Self, v: Self, b: Self, t: T) -> Self {
        cubic_bezier_def(a, u, v, b, t)
      }
    }
  }
}

impl_interpolate_vector!(Vector1);
impl_interpolate_vector!(Vector2);
impl_interpolate_vector!(Vector3);
impl_interpolate_vector!(Vector4);
impl_interpolate_vector!(Vector5);
impl_interpolate_vector!(Vector6);
