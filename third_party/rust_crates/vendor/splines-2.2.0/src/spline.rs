//! Spline curves and operations.

#[cfg(feature = "serialization")] use serde_derive::{Deserialize, Serialize};
#[cfg(not(feature = "std"))] use alloc::vec::Vec;
#[cfg(feature = "std")] use std::cmp::Ordering;
#[cfg(feature = "std")] use std::ops::{Div, Mul};
#[cfg(not(feature = "std"))] use core::ops::{Div, Mul};
#[cfg(not(feature = "std"))] use core::cmp::Ordering;

use crate::interpolate::{Interpolate, Additive, One, Trigo};
use crate::interpolation::Interpolation;
use crate::key::Key;

/// Spline curve used to provide interpolation between control points (keys).
///
/// Splines are made out of control points ([`Key`]). When creating a [`Spline`] with
/// [`Spline::from_vec`] or [`Spline::from_iter`], the keys don’t have to be sorted (they are sorted
/// automatically by the sampling value).
///
/// You can sample from a spline with several functions:
///
///   - [`Spline::sample`]: allows you to sample from a spline. If not enough keys are available
///     for the required interpolation mode, you get `None`.
///   - [`Spline::clamped_sample`]: behaves like [`Spline::sample`] but will return either the first
///     or last key if out of bound; it will return `None` if not enough key.
#[derive(Debug, Clone)]
#[cfg_attr(feature = "serialization", derive(Deserialize, Serialize))]
pub struct Spline<T, V>(pub(crate) Vec<Key<T, V>>);

impl<T, V> Spline<T, V> {
  /// Internal sort to ensure invariant of sorting keys is valid.
  fn internal_sort(&mut self) where T: PartialOrd {
    self.0.sort_by(|k0, k1| k0.t.partial_cmp(&k1.t).unwrap_or(Ordering::Less));
  }

  /// Create a new spline out of keys. The keys don’t have to be sorted even though it’s recommended
  /// to provide ascending sorted ones (for performance purposes).
  pub fn from_vec(keys: Vec<Key<T, V>>) -> Self where T: PartialOrd {
    let mut spline = Spline(keys);
    spline.internal_sort();
    spline
  }

  /// Create a new spline by consuming an `Iterater<Item = Key<T>>`. They keys don’t have to be
  /// sorted.
  ///
  /// # Note on iterators
  ///
  /// It’s valid to use any iterator that implements `Iterator<Item = Key<T>>`. However, you should
  /// use [`Spline::from_vec`] if you are passing a [`Vec`].
  pub fn from_iter<I>(iter: I) -> Self where I: Iterator<Item = Key<T, V>>, T: PartialOrd {
    Self::from_vec(iter.collect())
  }

  /// Retrieve the keys of a spline.
  pub fn keys(&self) -> &[Key<T, V>] {
    &self.0
  }

  /// Number of keys.
  #[inline(always)]
  pub fn len(&self) -> usize {
    self.0.len()
  }

  /// Check whether the spline has no key.
  #[inline(always)]
  pub fn is_empty(&self) -> bool {
    self.0.is_empty()
  }

  /// Sample a spline at a given time, returning the interpolated value along with its associated
  /// key.
  ///
  /// The current implementation, based on immutability, cannot perform in constant time. This means
  /// that sampling’s processing complexity is currently *O(log n)*. It’s possible to achieve *O(1)*
  /// performance by using a slightly different spline type. If you are interested by this feature,
  /// an implementation for a dedicated type is foreseen yet not started yet.
  ///
  /// # Return
  ///
  /// `None` if you try to sample a value at a time that has no key associated with. That can also
  /// happen if you try to sample between two keys with a specific interpolation mode that makes the
  /// sampling impossible. For instance, [`Interpolation::CatmullRom`] requires *four* keys. If
  /// you’re near the beginning of the spline or its end, ensure you have enough keys around to make
  /// the sampling.
  pub fn sample_with_key(&self, t: T) -> Option<(V, &Key<T, V>, Option<&Key<T, V>>)>
  where T: Additive + One + Trigo + Mul<T, Output = T> + Div<T, Output = T> + PartialOrd,
        V: Interpolate<T> {
    let keys = &self.0;
    let i = search_lower_cp(keys, t)?;
    let cp0 = &keys[i];

    match cp0.interpolation {
      Interpolation::Step(threshold) => {
        let cp1 = &keys[i + 1];
        let nt = normalize_time(t, cp0, cp1);
        let value = if nt < threshold { cp0.value } else { cp1.value };

        Some((value, cp0, Some(cp1)))
      }

      Interpolation::Linear => {
        let cp1 = &keys[i + 1];
        let nt = normalize_time(t, cp0, cp1);
        let value = Interpolate::lerp(cp0.value, cp1.value, nt);

        Some((value, cp0, Some(cp1)))
      }

      Interpolation::Cosine => {
        let two_t = T::one() + T::one();
        let cp1 = &keys[i + 1];
        let nt = normalize_time(t, cp0, cp1);
        let cos_nt = (T::one() - (nt * T::pi()).cos()) / two_t;
        let value = Interpolate::lerp(cp0.value, cp1.value, cos_nt);

        Some((value, cp0, Some(cp1)))
      }

      Interpolation::CatmullRom => {
        // We need at least four points for Catmull Rom; ensure we have them, otherwise, return
        // None.
        if i == 0 || i >= keys.len() - 2 {
          None
        } else {
          let cp1 = &keys[i + 1];
          let cpm0 = &keys[i - 1];
          let cpm1 = &keys[i + 2];
          let nt = normalize_time(t, cp0, cp1);
          let value = Interpolate::cubic_hermite((cpm0.value, cpm0.t), (cp0.value, cp0.t), (cp1.value, cp1.t), (cpm1.value, cpm1.t), nt);

          Some((value, cp0, Some(cp1)))
        }
      }

      Interpolation::Bezier(u) => {
        // We need to check the next control point to see whether we want quadratic or cubic Bezier.
        let cp1 = &keys[i + 1];
        let nt = normalize_time(t, cp0, cp1);

        let value =
          if let Interpolation::Bezier(v) = cp1.interpolation {
            Interpolate::cubic_bezier(cp0.value, u, v, cp1.value, nt)
          } else {
            Interpolate::quadratic_bezier(cp0.value, u, cp1.value, nt)
          };

        Some((value, cp0, Some(cp1)))
      }

      Interpolation::StrokeBezier(input, output) => {
        let cp1 = &keys[i + 1];
        let nt = normalize_time(t, cp0, cp1);
        let value = Interpolate::cubic_bezier(cp0.value, input, output, cp1.value, nt);

        Some((value, cp0, Some(cp1)))
      }

      Interpolation::__NonExhaustive => unreachable!(),
    }
  }

  /// Sample a spline at a given time.
  ///
  pub fn sample(&self, t: T) -> Option<V>
  where T: Additive + One + Trigo + Mul<T, Output = T> + Div<T, Output = T> + PartialOrd,
        V: Interpolate<T> {
    self.sample_with_key(t).map(|(v, _, _)| v)
  }

  /// Sample a spline at a given time with clamping, returning the interpolated value along with its
  /// associated key.
  ///
  /// # Return
  ///
  /// If you sample before the first key or after the last one, return the first key or the last
  /// one, respectively. Otherwise, behave the same way as [`Spline::sample`].
  ///
  /// # Error
  ///
  /// This function returns [`None`] if you have no key.
  pub fn clamped_sample_with_key(&self, t: T) -> Option<(V, &Key<T, V>, Option<&Key<T, V>>)>
  where T: Additive + One + Trigo + Mul<T, Output = T> + Div<T, Output = T> + PartialOrd,
        V: Interpolate<T> {
    if self.0.is_empty() {
      return None;
    }

    self.sample_with_key(t).or_else(move || {
      let first = self.0.first().unwrap();
      if t <= first.t {
        let second = if self.0.len() >= 2 { Some(&self.0[1]) } else { None };
        Some((first.value, &first, second))
      } else {
        let last = self.0.last().unwrap();

        if t >= last.t {
          Some((last.value, &last, None))
        } else {
          None
        }
      }
    })
  }

  /// Sample a spline at a given time with clamping.
  pub fn clamped_sample(&self, t: T) -> Option<V>
  where T: Additive + One + Trigo + Mul<T, Output = T> + Div<T, Output = T> + PartialOrd,
        V: Interpolate<T> {
    self.clamped_sample_with_key(t).map(|(v, _, _)| v)
  }

  /// Add a key into the spline.
  pub fn add(&mut self, key: Key<T, V>) where T: PartialOrd {
    self.0.push(key);
    self.internal_sort();
  }

  /// Remove a key from the spline.
  pub fn remove(&mut self, index: usize) -> Option<Key<T, V>> {
    if index >= self.0.len() {
      None
    } else {
      Some(self.0.remove(index))
    }
  }

  /// Update a key and return the key already present.
  ///
  /// The key is updated — if present — with the provided function.
  ///
  /// # Notes
  ///
  /// That function makes sense only if you want to change the interpolator (i.e. [`Key::t`]) of
  /// your key. If you just want to change the interpolation mode or the carried value, consider
  /// using the [`Spline::get_mut`] method instead as it will be way faster.
  pub fn replace<F>(
    &mut self,
    index: usize,
    f: F
  ) -> Option<Key<T, V>>
  where
    F: FnOnce(&Key<T, V>) -> Key<T, V>,
    T: PartialOrd
  {
    let key = self.remove(index)?;
    self.add(f(&key));
    Some(key)
  }

  /// Get a key at a given index.
  pub fn get(&self, index: usize) -> Option<&Key<T, V>> {
    self.0.get(index)
  }

  /// Mutably get a key at a given index.
  pub fn get_mut(&mut self, index: usize) -> Option<KeyMut<T, V>> {
    self.0.get_mut(index).map(|key| KeyMut {
      value: &mut key.value,
      interpolation: &mut key.interpolation
    })
  }
}

/// A mutable [`Key`].
///
/// Mutable keys allow to edit the carried values and the interpolation mode but not the actual
/// interpolator value as it would invalidate the internal structure of the [`Spline`]. If you
/// want to achieve this, you’re advised to use [`Spline::replace`].
pub struct KeyMut<'a, T, V> {
  /// Carried value.
  pub value: &'a mut V,
  /// Interpolation mode to use for that key.
  pub interpolation: &'a mut Interpolation<T, V>,
}

// Normalize a time ([0;1]) given two control points.
#[inline(always)]
pub(crate) fn normalize_time<T, V>(
  t: T,
  cp: &Key<T, V>,
  cp1: &Key<T, V>
) -> T where T: Additive + Div<T, Output = T> + PartialEq {
  assert!(cp1.t != cp.t, "overlapping keys");
  (t - cp.t) / (cp1.t - cp.t)
}

// Find the lower control point corresponding to a given time.
fn search_lower_cp<T, V>(cps: &[Key<T, V>], t: T) -> Option<usize> where T: PartialOrd {
  let mut i = 0;
  let len = cps.len();

  if len < 2 {
    return None;
  }

  loop {
    let cp = &cps[i];
    let cp1 = &cps[i+1];

    if t >= cp1.t {
      if i >= len - 2 {
        return None;
      }

      i += 1;
    } else if t < cp.t {
      if i == 0 {
        return None;
      }

      i -= 1;
    } else {
      break; // found
    }
  }

  Some(i)
}
