use cgmath::{
  BaseFloat, BaseNum, InnerSpace, Quaternion, Vector1, Vector2, Vector3, Vector4, VectorSpace
};

use crate::interpolate::{
  Additive, Interpolate, Linear, One, cubic_bezier_def, cubic_hermite_def, quadratic_bezier_def
};

macro_rules! impl_interpolate_vec {
  ($($t:tt)*) => {
    impl<T> Linear<T> for $($t)*<T> where T: BaseNum {
      #[inline(always)]
      fn outer_mul(self, t: T) -> Self {
        self * t
      }

      #[inline(always)]
      fn outer_div(self, t: T) -> Self {
        self / t
      }
    }

    impl<T> Interpolate<T> for $($t)*<T>
    where Self: InnerSpace<Scalar = T>, T: Additive + BaseFloat + One {
      #[inline(always)]
      fn lerp(a: Self, b: Self, t: T) -> Self {
        a.lerp(b, t)
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

impl_interpolate_vec!(Vector1);
impl_interpolate_vec!(Vector2);
impl_interpolate_vec!(Vector3);
impl_interpolate_vec!(Vector4);

impl<T> Linear<T> for Quaternion<T> where T: BaseFloat {
  #[inline(always)]
  fn outer_mul(self, t: T) -> Self {
    self * t
  }

  #[inline(always)]
  fn outer_div(self, t: T) -> Self {
    self / t
  }
}

impl<T> Interpolate<T> for Quaternion<T>
where Self: InnerSpace<Scalar = T>, T: Additive + BaseFloat + One {
  #[inline(always)]
  fn lerp(a: Self, b: Self, t: T) -> Self {
    a.nlerp(b, t)
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
