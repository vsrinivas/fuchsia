// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        child_moniker::{ChildMoniker, ChildMonikerBase},
        error::MonikerError,
        relative_moniker::RelativeMonikerBase,
    },
    core::cmp::{self, Ord, Ordering},
    std::{fmt, hash::Hash},
};

/// AbsoluteMonikerBase is the common trait for both InstancedAbsoluteMoniker
/// and AbsoluteMoniker concrete types.
///
/// AbsoluteMonikerBase describes the identity of a component instance in terms of its path
/// relative to the root of the component instance tree.
pub trait AbsoluteMonikerBase:
    Default + Eq + PartialEq + fmt::Debug + Clone + Hash + fmt::Display
{
    type Part: ChildMonikerBase;

    fn new(path: Vec<Self::Part>) -> Self;

    fn parse(path: &Vec<&str>) -> Result<Self, MonikerError> {
        let path: Result<Vec<Self::Part>, MonikerError> =
            path.iter().map(|x| Self::Part::parse(x)).collect();
        Ok(Self::new(path?))
    }

    fn parse_str(input: &str) -> Result<Self, MonikerError>;

    fn path(&self) -> &Vec<Self::Part>;

    /// Given an absolute moniker realm `start`, and a relative moniker from `start` to an `end`
    /// realm, returns the absolute moniker of the `end` realm.
    ///
    /// If an absolute moniker cannot be computed, then a MonikerError::InvalidMoniker error is
    /// returned.
    ///
    /// Example:
    ///
    ///          a
    ///        /   \
    ///      b      c
    ///    /
    ///  d
    ///
    ///  Given:
    ///    `start` = /a/c
    ///    `start_to_end` (c -> d) = .\c/b/d
    ///  Returns:
    ///    /a/b/d
    fn from_relative<
        S: AbsoluteMonikerBase<Part = Self::Part>,
        T: RelativeMonikerBase<Part = Self::Part>,
    >(
        start: &S,
        start_to_end: &T,
    ) -> Result<Self, MonikerError> {
        // Verify that `start.path`'s tail is of `start_to_end.up_path`.
        if start_to_end.up_path().len() > start.path().len()
            || !start_to_end.up_path().iter().eq(start
                .path()
                .iter()
                .rev()
                .take(start_to_end.up_path().len()))
        {
            return Err(MonikerError::invalid_moniker(start.to_string()));
        }

        Ok(Self::new(
            start
                .path()
                .iter()
                .take(start.path().len() - start_to_end.up_path().len()) // remove the first `start_to_end.up_path` elements from `from`
                .chain(start_to_end.down_path().iter()) // append the `start_to_end.down_path` elements
                .cloned()
                .collect(),
        ))
    }

    /// Indicates whether `other` is contained within the realm specified by
    /// this AbsoluteMonikerBase.
    fn contains_in_realm<S: AbsoluteMonikerBase<Part = Self::Part>>(&self, other: &S) -> bool {
        if other.path().len() < self.path().len() {
            return false;
        }

        self.path().iter().enumerate().all(|item| *item.1 == other.path()[item.0])
    }

    fn root() -> Self {
        Self::new(vec![])
    }

    fn leaf(&self) -> Option<&Self::Part> {
        self.path().last()
    }

    fn is_root(&self) -> bool {
        self.path().is_empty()
    }

    fn parent(&self) -> Option<Self> {
        if self.is_root() {
            None
        } else {
            let l = self.path().len() - 1;
            Some(Self::new(self.path()[..l].to_vec()))
        }
    }

    fn child(&self, child: Self::Part) -> Self {
        let mut path = self.path().clone();
        path.push(child);
        Self::new(path)
    }

    fn compare(&self, other: &Self) -> cmp::Ordering {
        let min_size = cmp::min(self.path().len(), other.path().len());
        for i in 0..min_size {
            if self.path()[i] < other.path()[i] {
                return cmp::Ordering::Less;
            } else if self.path()[i] > other.path()[i] {
                return cmp::Ordering::Greater;
            }
        }
        if self.path().len() > other.path().len() {
            return cmp::Ordering::Greater;
        } else if self.path().len() < other.path().len() {
            return cmp::Ordering::Less;
        }

        return cmp::Ordering::Equal;
    }

    fn format(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.path().is_empty() {
            write!(f, "/")?;
        } else {
            for segment in self.path().iter() {
                write!(f, "/{}", segment.as_str())?
            }
        }
        Ok(())
    }
}

/// AbsoluteMoniker describes the identity of a component instance
/// in terms of its path relative to the root of the component instance
/// tree. The constituent parts of a AbsoluteMoniker do not include the
/// instance ID of the child.
///
/// Display notation: "/", "/name1", "/name1/name2", ...
#[derive(Debug, Eq, PartialEq, Clone, Hash, Default)]
pub struct AbsoluteMoniker {
    path: Vec<ChildMoniker>,
}

impl AbsoluteMonikerBase for AbsoluteMoniker {
    type Part = ChildMoniker;

    fn new(path: Vec<Self::Part>) -> Self {
        Self { path }
    }

    fn path(&self) -> &Vec<Self::Part> {
        &self.path
    }

    /// Parse the given string as an absolute moniker. The string should be a '/' delimited series
    /// of child monikers without any instance identifiers, e.g. "/", or "/name1/name2" or
    /// "/name1:collection1".
    fn parse_str(input: &str) -> Result<AbsoluteMoniker, MonikerError> {
        if input.chars().nth(0) != Some('/') {
            return Err(MonikerError::invalid_moniker(input));
        }
        if input == "/" {
            return Ok(AbsoluteMoniker::root());
        }
        let path =
            input[1..].split('/').map(ChildMoniker::parse).collect::<Result<_, MonikerError>>()?;
        Ok(AbsoluteMoniker::new(path))
    }
}

impl From<Vec<&str>> for AbsoluteMoniker {
    fn from(rep: Vec<&str>) -> Self {
        Self::parse(&rep).expect(&format!("absolute moniker failed to parse: {:?}", &rep))
    }
}

impl cmp::Ord for AbsoluteMoniker {
    fn cmp(&self, other: &Self) -> cmp::Ordering {
        self.compare(other)
    }
}

impl PartialOrd for AbsoluteMoniker {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl fmt::Display for AbsoluteMoniker {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.format(f)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn absolute_monikers() {
        let root = AbsoluteMoniker::root();
        assert_eq!(true, root.is_root());
        assert_eq!("/", format!("{}", root));
        assert_eq!(root, AbsoluteMoniker::from(vec![]));

        let m = AbsoluteMoniker::new(vec![
            ChildMoniker::try_new("a", None).unwrap(),
            ChildMoniker::try_new("b", Some("coll")).unwrap(),
        ]);
        assert_eq!(false, m.is_root());
        assert_eq!("/a/coll:b", format!("{}", m));
        assert_eq!(m, AbsoluteMoniker::from(vec!["a", "coll:b"]));
        assert_eq!(m.leaf().map(|m| m.collection()).flatten(), Some("coll"));
        assert_eq!(m.leaf().map(|m| m.name()), Some("b"));
        assert_eq!(m.leaf(), Some(&ChildMoniker::from("coll:b")));
    }

    #[test]
    fn absolute_moniker_parent() {
        let root = AbsoluteMoniker::root();
        assert_eq!(true, root.is_root());
        assert_eq!(None, root.parent());

        let m = AbsoluteMoniker::new(vec![
            ChildMoniker::try_new("a", None).unwrap(),
            ChildMoniker::try_new("b", None).unwrap(),
        ]);
        assert_eq!("/a/b", format!("{}", m));
        assert_eq!("/a", format!("{}", m.parent().unwrap()));
        assert_eq!("/", format!("{}", m.parent().unwrap().parent().unwrap()));
        assert_eq!(None, m.parent().unwrap().parent().unwrap().parent());
        assert_eq!(m.leaf(), Some(&ChildMoniker::from("b")));
    }
}
