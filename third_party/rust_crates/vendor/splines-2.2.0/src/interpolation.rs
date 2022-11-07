//! Available interpolation modes.

#[cfg(feature = "serialization")] use serde_derive::{Deserialize, Serialize};

/// Available kind of interpolations.
///
/// Feel free to visit each variant for more documentation.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
#[cfg_attr(feature = "serialization", derive(Deserialize, Serialize))]
#[cfg_attr(feature = "serialization", serde(rename_all = "snake_case"))]
pub enum Interpolation<T, V> {
  /// Hold a [`Key`] until the sampling value passes the normalized step threshold, in which
  /// case the next key is used.
  ///
  /// > Note: if you set the threshold to `0.5`, the first key will be used until half the time
  /// > between the two keys; the second key will be in used afterwards. If you set it to `1.0`, the
  /// > first key will be kept until the next key. Set it to `0.` and the first key will never be
  /// > used.
  ///
  /// [`Key`]: crate::key::Key
  Step(T),
  /// Linear interpolation between a key and the next one.
  Linear,
  /// Cosine interpolation between a key and the next one.
  Cosine,
  /// Catmull-Rom interpolation, performing a cubic Hermite interpolation using four keys.
  CatmullRom,
  /// Bézier interpolation.
  ///
  /// A control point that uses such an interpolation is associated with an extra point. The segmant
  /// connecting both is called the _tangent_ of this point. The part of the spline defined between
  /// this control point and the next one will be interpolated across with Bézier interpolation. Two
  /// cases are possible:
  ///
  /// - The next control point also has a Bézier interpolation mode. In this case, its tangent is
  ///   used for the interpolation process. This is called _cubic Bézier interpolation_ and it
  ///   kicks ass.
  /// - The next control point doesn’t have a Bézier interpolation mode set. In this case, the
  ///   tangent used for the next control point is defined as the segment connecting that control
  ///   point and the current control point’s associated point. This is called _quadratic Bézer
  ///   interpolation_ and it kicks ass too, but a bit less than cubic.
  Bezier(V),
  /// A special Bézier interpolation using an _input tangent_ and an _output tangent_.
  ///
  /// With this kind of interpolation, a control point has an input tangent, which has the same role
  /// as the one defined by [`Interpolation::Bezier`], and an output tangent, which has the same
  /// role defined by the next key’s [`Interpolation::Bezier`] if present, normally.
  ///
  /// What it means is that instead of setting the output tangent as the next key’s Bézier tangent,
  /// this interpolation mode allows you to manually set the output tangent. That will yield more
  /// control on the tangents but might generate discontinuities. Use with care.
  ///
  /// Stroke Bézier interpolation is always a cubic Bézier interpolation by default.
  StrokeBezier(V, V),
  #[doc(hidden)]
  __NonExhaustive
}

impl<T, V> Default for Interpolation<T, V> {
  /// [`Interpolation::Linear`] is the default.
  fn default() -> Self {
    Interpolation::Linear
  }
}
