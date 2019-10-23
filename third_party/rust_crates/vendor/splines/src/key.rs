//! Spline control points.
//!
//! A control point associates to a “sampling value” (a.k.a. time) a carriede value that can be
//! interpolated along the curve made by the control points.
//!
//! Splines constructed with this crate have the property that it’s possible to change the
//! interpolation mode on a key-based way, allowing you to implement and encode complex curves.

#[cfg(feature = "serialization")] use serde_derive::{Deserialize, Serialize};

use crate::interpolation::Interpolation;

/// A spline control point.
///
/// This type associates a value at a given interpolation parameter value. It also contains an
/// interpolation mode used to determine how to interpolate values on the segment defined by this
/// key and the next one – if existing. Have a look at [`Interpolation`] for further details.
///
/// [`Interpolation`]: crate::interpolation::Interpolation
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
#[cfg_attr(feature = "serialization", derive(Deserialize, Serialize))]
#[cfg_attr(feature = "serialization", serde(rename_all = "snake_case"))]
pub struct Key<T, V> {
  /// Interpolation parameter at which the [`Key`] should be reached.
  pub t: T,
  /// Carried value.
  pub value: V,
  /// Interpolation mode.
  pub interpolation: Interpolation<T, V>
}

impl<T, V> Key<T, V> {
  /// Create a new key.
  pub fn new(t: T, value: V, interpolation: Interpolation<T, V>) -> Self {
    Key { t, value, interpolation }
  }
}
