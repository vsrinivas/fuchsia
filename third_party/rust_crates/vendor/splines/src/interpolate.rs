//! The [`Interpolate`] trait and associated symbols.
//!
//! The [`Interpolate`] trait is the central concept of the crate. It enables a spline to be
//! sampled at by interpolating in between control points.
//!
//! In order for a type to be used in [`Spline<K, V>`], some properties must be met about the `K`
//! type must implementing several traits:
//!
//!   - [`One`], giving a neutral element for the multiplication monoid.
//!   - [`Additive`], making the type additive (i.e. one can add or subtract with it).
//!   - [`Linear`], unlocking linear combinations, required for interpolating.
//!   - [`Trigo`], a trait giving *π* and *cosine*, required for e.g. cosine interpolation.
//!
//! Feel free to have a look at current implementors for further help.
//!
//! > *Why doesn’t this crate use [num-traits] instead of
//! > defining its own traits?*
//!
//! The reason for this is quite simple: this crate provides a `no_std` support, which is not
//! currently available easily with [num-traits]. Also, if something changes in [num-traits] with
//! those traits, it would make this whole crate unstable.
//!
//! [`Interpolate`]: crate::interpolate::Interpolate
//! [`Spline<K, V>`]: crate::spline::Spline
//! [`One`]: crate::interpolate::One
//! [`Additive`]: crate::interpolate::Additive
//! [`Linear`]: crate::interpolate::Linear
//! [`Trigo`]: crate::interpolate::Trigo
//! [num-traits]: https://crates.io/crates/num-traits

#[cfg(feature = "std")] use std::f32;
#[cfg(not(feature = "std"))] use core::f32;
#[cfg(not(feature = "std"))] use core::intrinsics::cosf32;
#[cfg(feature = "std")] use std::f64;
#[cfg(not(feature = "std"))] use core::f64;
#[cfg(not(feature = "std"))] use core::intrinsics::cosf64;
#[cfg(feature = "std")] use std::ops::{Add, Mul, Sub};
#[cfg(not(feature = "std"))] use core::ops::{Add, Mul, Sub};

/// Keys that can be interpolated in between. Implementing this trait is required to perform
/// sampling on splines.
///
/// `T` is the variable used to sample with. Typical implementations use [`f32`] or [`f64`], but
/// you’re free to use the ones you like. Feel free to have a look at [`Spline::sample`] for
/// instance to know which trait your type must implement to be usable.
///
/// [`Spline::sample`]: crate::spline::Spline::sample
pub trait Interpolate<T>: Sized + Copy {
  /// Linear interpolation.
  fn lerp(a: Self, b: Self, t: T) -> Self;

  /// Cubic hermite interpolation.
  ///
  /// Default to [`lerp`].
  ///
  /// [`lerp`]: Interpolate::lerp
  fn cubic_hermite(_: (Self, T), a: (Self, T), b: (Self, T), _: (Self, T), t: T) -> Self {
    Self::lerp(a.0, b.0, t)
  }

  /// Quadratic Bézier interpolation.
  fn quadratic_bezier(a: Self, u: Self, b: Self, t: T) -> Self;

  /// Cubic Bézier interpolation.
  fn cubic_bezier(a: Self, u: Self, v: Self, b: Self, t: T) -> Self;
}

/// Set of types that support additions and subtraction.
///
/// The [`Copy`] trait is also a supertrait as it’s likely to be used everywhere.
pub trait Additive:
  Copy +
  Add<Self, Output = Self> +
  Sub<Self, Output = Self> {
}

impl<T> Additive for T
where T: Copy +
         Add<Self, Output = Self> +
         Sub<Self, Output = Self> {
}

/// Set of additive types that support outer multiplication and division, making them linear.
pub trait Linear<T>: Additive {
  /// Apply an outer multiplication law.
  fn outer_mul(self, t: T) -> Self;

  /// Apply an outer division law.
  fn outer_div(self, t: T) -> Self;
}

macro_rules! impl_linear_simple {
  ($t:ty) => {
    impl Linear<$t> for $t {
      fn outer_mul(self, t: $t) -> Self {
        self * t
      }

      /// Apply an outer division law.
      fn outer_div(self, t: $t) -> Self {
        self / t
      }
    }
  }
}

impl_linear_simple!(f32);
impl_linear_simple!(f64);

macro_rules! impl_linear_cast {
  ($t:ty, $q:ty) => {
    impl Linear<$t> for $q {
      fn outer_mul(self, t: $t) -> Self {
        self * t as $q
      }

      /// Apply an outer division law.
      fn outer_div(self, t: $t) -> Self {
        self / t as $q
      }
    }
  }
}

impl_linear_cast!(f32, f64);
impl_linear_cast!(f64, f32);

/// Types with a neutral element for multiplication.
pub trait One {
  /// The neutral element for the multiplicative monoid — typically called `1`.
  fn one() -> Self;
}

macro_rules! impl_one_float {
  ($t:ty) => {
    impl One for $t {
      #[inline(always)]
      fn one() -> Self {
        1.
      }
    }
  }
}

impl_one_float!(f32);
impl_one_float!(f64);

/// Types with a sane definition of π and cosine.
pub trait Trigo {
  /// π.
  fn pi() -> Self;

  /// Cosine of the argument.
  fn cos(self) -> Self;
}

impl Trigo for f32 {
  #[inline(always)]
  fn pi() -> Self {
    f32::consts::PI
  }

  #[inline(always)]
  fn cos(self) -> Self {
    #[cfg(feature = "std")]
    {
      self.cos()
    }

    #[cfg(not(feature = "std"))]
    {
      unsafe { cosf32(self) }
    }
  }
}

impl Trigo for f64 {
  #[inline(always)]
  fn pi() -> Self {
    f64::consts::PI
  }

  #[inline(always)]
  fn cos(self) -> Self {
    #[cfg(feature = "std")]
    {
      self.cos()
    }

    #[cfg(not(feature = "std"))]
    {
      unsafe { cosf64(self) }
    }
  }
}

/// Default implementation of [`Interpolate::cubic_hermite`].
///
/// `V` is the value being interpolated. `T` is the sampling value (also sometimes called time).
pub fn cubic_hermite_def<V, T>(x: (V, T), a: (V, T), b: (V, T), y: (V, T), t: T) -> V
where V: Linear<T>,
      T: Additive + Mul<T, Output = T> + One {
  // some stupid generic constants, because Rust doesn’t have polymorphic literals…
  let one_t = T::one();
  let two_t = one_t + one_t; // lolololol
  let three_t = two_t + one_t; // megalol

  // sampler stuff
  let t2 = t * t;
  let t3 = t2 * t;
  let two_t3 = t3 * two_t;
  let three_t2 = t2 * three_t;

  // tangents
  let m0 = (b.0 - x.0).outer_div(b.1 - x.1);
  let m1 = (y.0 - a.0).outer_div(y.1 - a.1);

  a.0.outer_mul(two_t3 - three_t2 + one_t) + m0.outer_mul(t3 - t2 * two_t + t) + b.0.outer_mul(three_t2 - two_t3) + m1.outer_mul(t3 - t2)
}

/// Default implementation of [`Interpolate::quadratic_bezier`].
///
/// `V` is the value being interpolated. `T` is the sampling value (also sometimes called time).
pub fn quadratic_bezier_def<V, T>(a: V, u: V, b: V, t: T) -> V
where V: Linear<T>,
      T: Additive + Mul<T, Output = T> + One {
  let one_t = T::one() - t;
  let one_t_2 = one_t * one_t;
  u + (a - u).outer_mul(one_t_2) + (b - u).outer_mul(t * t)
}

/// Default implementation of [`Interpolate::cubic_bezier`].
///
/// `V` is the value being interpolated. `T` is the sampling value (also sometimes called time).
pub fn cubic_bezier_def<V, T>(a: V, u: V, v: V, b: V, t: T) -> V
where V: Linear<T>,
      T: Additive + Mul<T, Output = T> + One {
  let one_t = T::one() - t;
  let one_t_2 = one_t * one_t;
  let one_t_3 = one_t_2 * one_t;
  let three = T::one() + T::one() + T::one();

  // mirror the “output” tangent based on the next key “input” tangent
  let v_ = b + b - v;

  a.outer_mul(one_t_3) + u.outer_mul(three * one_t_2 * t) + v_.outer_mul(three * one_t * t * t) + b.outer_mul(t * t * t)
}

macro_rules! impl_interpolate_simple {
  ($t:ty) => {
    impl Interpolate<$t> for $t {
      fn lerp(a: Self, b: Self, t: $t) -> Self {
        a * (1. - t) + b * t
      }

      fn cubic_hermite(x: (Self, $t), a: (Self, $t), b: (Self, $t), y: (Self, $t), t: $t) -> Self {
        cubic_hermite_def(x, a, b, y, t)
      }

      fn quadratic_bezier(a: Self, u: Self, b: Self, t: $t) -> Self {
        quadratic_bezier_def(a, u, b, t)
      }

      fn cubic_bezier(a: Self, u: Self, v: Self, b: Self, t: $t) -> Self {
        cubic_bezier_def(a, u, v, b, t)
      }
    }
  }
}

impl_interpolate_simple!(f32);
impl_interpolate_simple!(f64);

macro_rules! impl_interpolate_via {
  ($t:ty, $v:ty) => {
    impl Interpolate<$t> for $v {
      fn lerp(a: Self, b: Self, t: $t) -> Self {
        a * (1. - t as $v) + b * t as $v
      }

      fn cubic_hermite((x, xt): (Self, $t), (a, at): (Self, $t), (b, bt): (Self, $t), (y, yt): (Self, $t), t: $t) -> Self {
        cubic_hermite_def((x, xt as $v), (a, at as $v), (b, bt as $v), (y, yt as $v), t as $v)
      }

      fn quadratic_bezier(a: Self, u: Self, b: Self, t: $t) -> Self {
        quadratic_bezier_def(a, u, b, t as $v)
      }

      fn cubic_bezier(a: Self, u: Self, v: Self, b: Self, t: $t) -> Self {
        cubic_bezier_def(a, u, v, b, t as $v)
      }
    }
  }
}

impl_interpolate_via!(f32, f64);
impl_interpolate_via!(f64, f32);
