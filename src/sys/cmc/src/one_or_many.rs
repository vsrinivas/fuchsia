// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    serde_derive::Deserialize,
    std::{
        fmt::{self, Display, Formatter},
        slice, vec,
    },
};

/// Represents either a single value, or multiple values of T.
/// Useful for differentiating between an array of length 1 and a single value.
#[derive(Deserialize, Debug, Clone)]
#[serde(untagged)]
pub enum OneOrMany<T> {
    /// A single instance of T.
    One(T),
    /// One or more instances of T.
    Many(Vec<T>),
}

impl<T> OneOrMany<T> {
    /// Returns an unowned view of `OneOrMany<T>`.
    pub fn as_ref(&self) -> OneOrManyBorrow<T> {
        match self {
            OneOrMany::One(item) => OneOrManyBorrow::One(item),
            OneOrMany::Many(items) => OneOrManyBorrow::Many(items),
        }
    }

    /// Transforms the `OneOrMany<T>` into an `Option<&T>`, where `One(t)` maps to
    /// `Some(&t)` and `Many(_)` maps to None.
    pub fn one(&self) -> Option<&T> {
        match self {
            OneOrMany::One(item) => Some(item),
            _ => None,
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

impl<T: Clone> OneOrMany<T> {
    pub fn to_vec(&self) -> Vec<T> {
        match self {
            OneOrMany::One(x) => return vec![x.clone()],
            OneOrMany::Many(xs) => return xs.to_vec(),
        }
    }
}

/// An unowned version of [`OneOrMany`].
///
/// [`OneOrMany`]: struct.OneOrMany.html
#[derive(Debug, Clone)]
pub enum OneOrManyBorrow<'a, T> {
    /// A single instance of T.
    One(&'a T),
    /// One or more instances of T.
    Many(&'a [T]),
}

impl<'a, T> OneOrManyBorrow<'a, T> {
    /// Returns `true` if this `OneOrManyBorrow<T>` is a `Many` value.
    pub fn is_many(&self) -> bool {
        match self {
            OneOrManyBorrow::One(_) => false,
            OneOrManyBorrow::Many(_) => true,
        }
    }

    /// Transforms the `OneOrManyBorrow<T>` into an `Option<&T>`, where `One(t)` maps to
    /// `Some(&t)` and `Many(_)` maps to None.
    pub fn one(&self) -> Option<&T> {
        match self {
            OneOrManyBorrow::One(item) => Some(*item),
            _ => None,
        }
    }

    /// Returns an iterator over the values of `OneOrManyBorrow<T>`.
    pub fn iter(&self) -> Iter<'a, T> {
        match self {
            OneOrManyBorrow::One(item) => Iter { inner_one: Some(*item), inner_many: None },
            OneOrManyBorrow::Many(items) => {
                Iter { inner_one: None, inner_many: Some(items.iter()) }
            }
        }
    }
}

impl<'a, T> IntoIterator for OneOrManyBorrow<'a, T> {
    type Item = &'a T;
    type IntoIter = Iter<'a, T>;

    fn into_iter(self) -> Iter<'a, T> {
        self.iter()
    }
}

impl<'a, T> IntoIterator for &'a OneOrManyBorrow<'a, T> {
    type Item = &'a T;
    type IntoIter = Iter<'a, T>;

    fn into_iter(self) -> Iter<'a, T> {
        self.iter()
    }
}

impl<'a, T: Display> Display for OneOrManyBorrow<'a, T> {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        match self {
            OneOrManyBorrow::One(item) => Display::fmt(item, f),
            OneOrManyBorrow::Many(items) => {
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

/// Immutable iterator over a `OneOrMany` or `OneOrManyBorrow`.
/// This `struct` is created by the [`OneOrMany::iter`] and [`OneOrManyBorrow::iter`] methods.
///
/// [`OneOrMany::iter`]: struct.OneOrMany.html#method.iter
/// [`OneOrManyBorrow::iter`]: struct.OneOrManyBorrow.html#method.iter
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
    use {
        super::*,
        serde_json::{self, json},
        test_util::assert_matches,
    };

    #[test]
    fn test_one() {
        let v = OneOrMany::One(34);
        assert_matches!(v.one(), Some(&34));

        let v = OneOrMany::Many(vec![1, 2, 3]);
        assert_matches!(v.one(), None);

        let v = OneOrManyBorrow::One(&34);
        assert_matches!(v.one(), Some(&34));

        let v = OneOrManyBorrow::Many(&[1, 2, 3]);
        assert_matches!(v.one(), None);
    }

    #[test]
    fn test_iter_one() {
        let v = OneOrMany::One(34);
        let mut iter = v.iter();
        assert_matches!(iter.next(), Some(&34));
        assert_matches!(iter.next(), None);

        let v = OneOrManyBorrow::One(&34);
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

        let v = OneOrManyBorrow::Many(&[1, 2, 3]);
        let mut iter = v.iter();
        assert_matches!(iter.next(), Some(&1));
        assert_matches!(iter.next(), Some(&2));
        assert_matches!(iter.next(), Some(&3));
        assert_matches!(iter.next(), None);
    }

    #[test]
    fn test_is_many() {
        let v = OneOrManyBorrow::One(&34);
        assert_eq!(v.is_many(), false);

        let v = OneOrManyBorrow::Many(&[1, 2, 3]);
        assert_eq!(v.is_many(), true);
    }

    #[test]
    fn test_to_vec() {
        let v = OneOrMany::One(34);
        assert_eq!(&[34], &v.to_vec()[..]);

        let v = OneOrMany::Many(vec![1, 2, 3]);
        assert_eq!(&[1, 2, 3], &v.to_vec()[..]);
    }

    #[test]
    fn test_display() {
        let val = 34;
        let v = OneOrManyBorrow::One(&val);
        assert_eq!(v.to_string(), "34");

        let val = vec![1, 2, 3];
        let v = OneOrManyBorrow::Many(&val);
        assert_eq!(v.to_string(), "1, 2, 3");
    }

    #[derive(Deserialize)]
    struct Wrapper {
        key: OneOrMany<String>,
    }

    #[test]
    fn parses_one() {
        let j = json!({
            "key": "foo",
        });
        let v: Wrapper = serde_json::from_value(j).expect("json parsed");
        assert_matches!(v.key, OneOrMany::One(val) if val == "foo");
    }

    #[test]
    fn parses_many() {
        let j = json!({
            "key": [ "foo" ],
        });
        let v: Wrapper = serde_json::from_value(j).expect("json parsed");
        assert_matches!(v.key.as_ref(), OneOrManyBorrow::Many(&[ ref val ]) if val == "foo");
    }
}
