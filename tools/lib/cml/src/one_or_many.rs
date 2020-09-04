// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    fmt::{self, Display, Formatter},
    slice, vec,
};

/// Represents either a single value, or multiple values of T.
/// Useful for differentiating between an array of length 1 and a single value.
#[derive(Debug, Clone)]
pub enum OneOrMany<T> {
    /// A single instance of T.
    One(T),
    /// One or more instances of T.
    Many(Vec<T>),
}

impl<T> OneOrMany<T> {
    /// Returns `true` if this `OneOrMany<T>` is a `Many` value.
    pub fn is_many(&self) -> bool {
        match self {
            Self::One(_) => false,
            Self::Many(_) => true,
        }
    }

    /// Returns an iterator over the values of `OneOrMany<T>`.
    pub fn iter(&self) -> Iter<'_, T> {
        match self {
            OneOrMany::One(item) => Iter { inner_one: Some(item), inner_many: None },
            OneOrMany::Many(items) => Iter { inner_one: None, inner_many: Some(items.iter()) },
        }
    }
}

impl<'a, T> IntoIterator for &'a OneOrMany<T> {
    type Item = &'a T;
    type IntoIter = Iter<'a, T>;

    fn into_iter(self) -> Iter<'a, T> {
        self.iter()
    }
}

impl<T> IntoIterator for OneOrMany<T> {
    type Item = T;
    type IntoIter = IntoIter<T>;

    fn into_iter(self) -> IntoIter<T> {
        match self {
            OneOrMany::One(item) => IntoIter { inner_one: Some(item), inner_many: None },
            OneOrMany::Many(items) => {
                IntoIter { inner_one: None, inner_many: Some(items.into_iter()) }
            }
        }
    }
}

impl<T> OneOrMany<T> {
    pub fn to_vec(&self) -> Vec<&T> {
        match self {
            OneOrMany::One(x) => vec![&x],
            OneOrMany::Many(v) => v.iter().collect(),
        }
    }
}

impl<'a, T: Display> Display for OneOrMany<T> {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        match self {
            OneOrMany::One(item) => Display::fmt(item, f),
            OneOrMany::Many(items) => {
                let mut iter = items.iter();
                if let Some(first_item) = iter.next() {
                    Display::fmt(first_item, f)?;
                }
                for item in iter {
                    f.write_str(", ")?;
                    Display::fmt(item, f)?;
                }
                Ok(())
            }
        }
    }
}

/// Immutable iterator over a `OneOrMany`.
/// This `struct` is created by [`OneOrMany::iter`].
///
/// [`OneOrMany::iter`]: struct.OneOrMany.html#method.iter
pub struct Iter<'a, T> {
    inner_one: Option<&'a T>,
    inner_many: Option<slice::Iter<'a, T>>,
}

impl<'a, T> Iterator for Iter<'a, T> {
    type Item = &'a T;

    fn next(&mut self) -> Option<Self::Item> {
        if let Some(item) = self.inner_one.take() {
            Some(item)
        } else if let Some(ref mut iter) = &mut self.inner_many {
            iter.next()
        } else {
            None
        }
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        if let Some(_) = self.inner_one {
            (1, Some(1))
        } else if let Some(iter) = &self.inner_many {
            iter.size_hint()
        } else {
            (0, Some(0))
        }
    }
}

impl<'a, T> ExactSizeIterator for Iter<'a, T> {}

/// An iterator that moves out of a `OneOrMany`.
/// This `struct` is created by the `into_iter` method on [`OneOrMany`] (provided by the [`IntoIterator`] trait).
///
/// [`OneOrMany`]: struct.OneOrMany.html
/// [`IntoIterator`]: https://doc.rust-lang.org/std/iter/trait.IntoIterator.html
pub struct IntoIter<T> {
    inner_one: Option<T>,
    inner_many: Option<vec::IntoIter<T>>,
}

impl<T> Iterator for IntoIter<T> {
    type Item = T;

    fn next(&mut self) -> Option<Self::Item> {
        if let Some(item) = self.inner_one.take() {
            Some(item)
        } else if let Some(ref mut iter) = &mut self.inner_many {
            iter.next()
        } else {
            None
        }
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        if let Some(_) = self.inner_one {
            (1, Some(1))
        } else if let Some(iter) = &self.inner_many {
            iter.size_hint()
        } else {
            (0, Some(0))
        }
    }
}

impl<T> ExactSizeIterator for IntoIter<T> {}

#[cfg(test)]
mod tests {
    use {super::*, matches::assert_matches};

    #[test]
    fn test_iter_one() {
        let v = OneOrMany::One(34);
        let mut iter = v.iter();
        assert_matches!(iter.next(), Some(&34));
        assert_matches!(iter.next(), None);
    }

    #[test]
    fn test_iter_many() {
        let v = OneOrMany::Many(vec![1, 2, 3]);
        let mut iter = v.iter();
        assert_matches!(iter.next(), Some(&1));
        assert_matches!(iter.next(), Some(&2));
        assert_matches!(iter.next(), Some(&3));
        assert_matches!(iter.next(), None);
    }

    #[test]
    fn test_is_many() {
        let v = OneOrMany::One(34);
        assert_eq!(v.is_many(), false);

        let v = OneOrMany::Many(vec![1, 2, 3]);
        assert_eq!(v.is_many(), true);
    }

    #[test]
    fn test_to_vec() {
        let v = OneOrMany::One(34);
        assert_eq!(&[&34], &v.to_vec()[..]);

        let v = OneOrMany::Many(vec![1, 2, 3]);
        assert_eq!(&[&1, &2, &3], &v.to_vec()[..]);
    }

    #[test]
    fn test_display() {
        let val = 34;
        let v = OneOrMany::One(val);
        assert_eq!(v.to_string(), "34");

        let val = vec![1, 2, 3];
        let v = OneOrMany::Many(val);
        assert_eq!(v.to_string(), "1, 2, 3");
    }
}
