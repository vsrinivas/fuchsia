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

    fn parse_str(input: &str) -> Result<Self, MonikerError> {
        if input.chars().nth(0) != Some('/') {
            return Err(MonikerError::invalid_moniker(input));
        }
        if input == "/" {
            return Ok(Self::root());
        }
        let path =
            input[1..].split('/').map(Self::Part::parse).collect::<Result<_, MonikerError>>()?;
        Ok(Self::new(path))
    }

    // Creates an absolute moniker for a descendant of this component instance.
    fn descendant<T: RelativeMonikerBase<Part = Self::Part>>(&self, descendant: &T) -> Self {
        let mut path = self.path().clone();
        let mut relative_path = descendant.path().clone();
        path.append(&mut relative_path);
        Self::new(path)
    }

    fn path(&self) -> &Vec<Self::Part>;

    fn path_mut(&mut self) -> &mut Vec<Self::Part>;

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

    fn path_mut(&mut self) -> &mut Vec<Self::Part> {
        &mut self.path
    }
}

impl From<Vec<&str>> for AbsoluteMoniker {
    fn from(rep: Vec<&str>) -> Self {
        Self::parse(&rep).expect(&format!("absolute moniker failed to parse: {:?}", &rep))
    }
}

impl TryFrom<&str> for AbsoluteMoniker {
    type Error = MonikerError;

    fn try_from(input: &str) -> Result<Self, MonikerError> {
        Self::parse_str(input)
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
    use crate::relative_moniker::RelativeMoniker;

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

    #[test]
    fn absolute_moniker_descendant() {
        let scope_root: AbsoluteMoniker = vec!["a:test1", "b:test2"].into();

        let relative: RelativeMoniker = vec!["c:test3", "d:test4"].try_into().unwrap();
        let descendant = scope_root.descendant(&relative);
        assert_eq!("/a:test1/b:test2/c:test3/d:test4", format!("{}", descendant));

        let relative: RelativeMoniker = vec![].try_into().unwrap();
        let descendant = scope_root.descendant(&relative);
        assert_eq!("/a:test1/b:test2", format!("{}", descendant));
    }
}
