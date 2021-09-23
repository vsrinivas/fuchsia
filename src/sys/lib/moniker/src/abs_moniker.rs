// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        child_moniker::{ChildMoniker, ChildMonikerBase},
        error::MonikerError,
        partial_child_moniker::PartialChildMoniker,
        relative_moniker::RelativeMonikerBase,
    },
    core::cmp::{self, Ord, Ordering},
    itertools,
    std::{fmt, hash::Hash},
};

#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};

/// AbsoluteMonikerBase is the common trait for both AbsoluteMoniker and PartialAbsoluteMoniker
/// concrete types.
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

    fn path(&self) -> &Vec<Self::Part>;

    /// Parse the given string as an absolute moniker. The string should be a '/' delimited series
    /// of child monikers without any instance identifiers, e.g. "/", or "/name1/name2" or
    /// "/name1:collection1".
    fn parse_string_without_instances(input: &str) -> Result<Self, MonikerError>;

    // Serializes absolute moniker into its string format, omitting instance ids.
    //
    // This method is the inverse of `parse_string_without_instances()`.
    fn to_string_without_instances(&self) -> String {
        format!(
            "/{}",
            itertools::join(
                (&self.path())
                    .into_iter()
                    .map(|segment: &Self::Part| segment.to_partial().as_str().to_string()),
                "/"
            )
        )
    }

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
            return Err(MonikerError::invalid_moniker(format!("{}", start)));
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

    fn to_partial(&self) -> PartialAbsoluteMoniker {
        let path: Vec<PartialChildMoniker> = self.path().iter().map(|p| p.to_partial()).collect();
        PartialAbsoluteMoniker::new(path)
    }
}

/// An absolute moniker describes the identity of a component instance in terms of its path
/// relative to the root of the component instance tree.
///
/// A root moniker is a moniker with an empty path.
///
/// Absolute monikers are only used internally within the component manager.  Externally,
/// components are referenced by encoded relative moniker so as to minimize the amount of
/// information which is disclosed about the overall structure of the component instance tree.
///
/// Display notation: "/", "/name1:1", "/name1:1/name2:2", ...
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
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

    // TODO(fxbug.dev/49968): Remove instance ID 0 assumption when removing instance IDs from
    // AbsoluteMonikerBase/ChildMoniker (and rename to parse_str + add From<&str> impl).
    fn parse_string_without_instances(input: &str) -> Result<AbsoluteMoniker, MonikerError> {
        if input.chars().nth(0) != Some('/') {
            return Err(MonikerError::invalid_moniker(input));
        }
        if input == "/" {
            return Ok(AbsoluteMoniker::root());
        }
        let path = input[1..]
            .split('/')
            .map(PartialChildMoniker::parse)
            .map(|p| p.map(|ok_p| ChildMoniker::from_partial(&ok_p, 0)))
            .collect::<Result<_, MonikerError>>()?;
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

/// Similar to AbsoluteMoniker, PartialAbsoluteMoniker describes the identity
/// of a component instance in terms of its path relative to the root of the
/// component instance tree. Unlike AbsoluteMoniker, the constituent parts of
/// a PartialAbsoluteMoniker do not need to include the instance ID of the child.
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Debug, Eq, PartialEq, Clone, Hash, Default)]
pub struct PartialAbsoluteMoniker {
    path: Vec<PartialChildMoniker>,
}

impl AbsoluteMonikerBase for PartialAbsoluteMoniker {
    type Part = PartialChildMoniker;

    fn new(path: Vec<Self::Part>) -> Self {
        Self { path }
    }

    fn path(&self) -> &Vec<Self::Part> {
        &self.path
    }

    fn parse_string_without_instances(input: &str) -> Result<PartialAbsoluteMoniker, MonikerError> {
        if input.chars().nth(0) != Some('/') {
            return Err(MonikerError::invalid_moniker(input));
        }
        if input == "/" {
            return Ok(PartialAbsoluteMoniker::root());
        }
        let path = input[1..]
            .split('/')
            .map(PartialChildMoniker::parse)
            .collect::<Result<_, MonikerError>>()?;
        Ok(PartialAbsoluteMoniker::new(path))
    }
}

impl From<Vec<&str>> for PartialAbsoluteMoniker {
    fn from(rep: Vec<&str>) -> Self {
        Self::parse(&rep).expect(&format!("absolute moniker failed to parse: {:?}", &rep))
    }
}

impl cmp::Ord for PartialAbsoluteMoniker {
    fn cmp(&self, other: &Self) -> cmp::Ordering {
        self.compare(other)
    }
}

impl PartialOrd for PartialAbsoluteMoniker {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl fmt::Display for PartialAbsoluteMoniker {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.format(f)
    }
}

#[cfg(test)]
mod tests {
    use {super::*, anyhow::Error};

    #[test]
    fn absolute_monikers() {
        let root = AbsoluteMoniker::root();
        assert_eq!(true, root.is_root());
        assert_eq!("/", format!("{}", root));
        assert_eq!(root, AbsoluteMoniker::from(vec![]));

        let m = AbsoluteMoniker::new(vec![
            ChildMoniker::new("a".to_string(), None, 1),
            ChildMoniker::new("b".to_string(), Some("coll".to_string()), 2),
        ]);
        assert_eq!(false, m.is_root());
        assert_eq!("/a:1/coll:b:2", format!("{}", m));
        assert_eq!(m, AbsoluteMoniker::from(vec!["a:1", "coll:b:2"]));
        assert_eq!(m.leaf().map(|m| m.collection()).flatten(), Some("coll"));
        assert_eq!(m.leaf().map(|m| m.name()), Some("b"));
        assert_eq!(m.leaf().map(|m| m.instance()), Some(2));
        assert_eq!(m.leaf(), Some(&ChildMoniker::from("coll:b:2")));
    }

    #[test]
    fn partial_absolute_monikers() {
        let root = PartialAbsoluteMoniker::root();
        assert_eq!(true, root.is_root());
        assert_eq!("/", format!("{}", root));
        assert_eq!(root, PartialAbsoluteMoniker::from(vec![]));

        let m = PartialAbsoluteMoniker::new(vec![
            PartialChildMoniker::new("a".to_string(), None),
            PartialChildMoniker::new("b".to_string(), Some("coll".to_string())),
        ]);
        assert_eq!(false, m.is_root());
        assert_eq!("/a/coll:b", format!("{}", m));
        assert_eq!(m, PartialAbsoluteMoniker::from(vec!["a", "coll:b"]));
        assert_eq!(m.leaf().map(|m| m.collection()).flatten(), Some("coll"));
        assert_eq!(m.leaf().map(|m| m.name()), Some("b"));
        assert_eq!(m.leaf(), Some(&PartialChildMoniker::from("coll:b")));
    }

    #[test]
    fn absolute_moniker_parent() {
        let root = AbsoluteMoniker::root();
        assert_eq!(true, root.is_root());
        assert_eq!(None, root.parent());

        let m = AbsoluteMoniker::new(vec![
            ChildMoniker::new("a".to_string(), None, 1),
            ChildMoniker::new("b".to_string(), None, 2),
        ]);
        assert_eq!("/a:1/b:2", format!("{}", m));
        assert_eq!("/a:1", format!("{}", m.parent().unwrap()));
        assert_eq!("/", format!("{}", m.parent().unwrap().parent().unwrap()));
        assert_eq!(None, m.parent().unwrap().parent().unwrap().parent());
        assert_eq!(m.leaf(), Some(&ChildMoniker::from("b:2")));
    }

    #[test]
    fn partial_absolute_moniker_parent() {
        let root = PartialAbsoluteMoniker::root();
        assert_eq!(true, root.is_root());
        assert_eq!(None, root.parent());

        let m = PartialAbsoluteMoniker::new(vec![
            PartialChildMoniker::new("a".to_string(), None),
            PartialChildMoniker::new("b".to_string(), None),
        ]);
        assert_eq!("/a/b", format!("{}", m));
        assert_eq!("/a", format!("{}", m.parent().unwrap()));
        assert_eq!("/", format!("{}", m.parent().unwrap().parent().unwrap()));
        assert_eq!(None, m.parent().unwrap().parent().unwrap().parent());
        assert_eq!(m.leaf(), Some(&PartialChildMoniker::from("b")));
    }

    #[test]
    fn absolute_moniker_compare() {
        let a = AbsoluteMoniker::new(vec![
            ChildMoniker::new("a".to_string(), None, 1),
            ChildMoniker::new("b".to_string(), None, 2),
            ChildMoniker::new("c".to_string(), None, 3),
        ]);
        let a2 = AbsoluteMoniker::new(vec![
            ChildMoniker::new("a".to_string(), None, 1),
            ChildMoniker::new("b".to_string(), None, 3),
            ChildMoniker::new("c".to_string(), None, 3),
        ]);
        let b = AbsoluteMoniker::new(vec![
            ChildMoniker::new("a".to_string(), None, 1),
            ChildMoniker::new("b".to_string(), None, 2),
            ChildMoniker::new("b".to_string(), None, 3),
        ]);
        let c = AbsoluteMoniker::new(vec![
            ChildMoniker::new("a".to_string(), None, 1),
            ChildMoniker::new("b".to_string(), None, 2),
            ChildMoniker::new("c".to_string(), None, 3),
            ChildMoniker::new("d".to_string(), None, 4),
        ]);
        let d = AbsoluteMoniker::new(vec![
            ChildMoniker::new("a".to_string(), None, 1),
            ChildMoniker::new("b".to_string(), None, 2),
            ChildMoniker::new("c".to_string(), None, 3),
        ]);

        assert_eq!(Ordering::Less, a.cmp(&a2));
        assert_eq!(Ordering::Greater, a2.cmp(&a));
        assert_eq!(Ordering::Greater, a.cmp(&b));
        assert_eq!(Ordering::Less, b.cmp(&a));
        assert_eq!(Ordering::Less, a.cmp(&c));
        assert_eq!(Ordering::Greater, c.cmp(&a));
        assert_eq!(Ordering::Equal, a.cmp(&d));
        assert_eq!(Ordering::Equal, d.cmp(&a));
        assert_eq!(Ordering::Less, b.cmp(&c));
        assert_eq!(Ordering::Greater, c.cmp(&b));
        assert_eq!(Ordering::Less, b.cmp(&d));
        assert_eq!(Ordering::Greater, d.cmp(&b));
        assert_eq!(Ordering::Greater, c.cmp(&d));
        assert_eq!(Ordering::Less, d.cmp(&c));
    }

    #[test]
    fn absolute_monikers_contains_in_realm() {
        let root = AbsoluteMoniker::root();
        let a = AbsoluteMoniker::new(vec![ChildMoniker::new("a".to_string(), None, 1)]);
        let ab = AbsoluteMoniker::new(vec![
            ChildMoniker::new("a".to_string(), None, 1),
            ChildMoniker::new("b".to_string(), None, 2),
        ]);
        let abc = AbsoluteMoniker::new(vec![
            ChildMoniker::new("a".to_string(), None, 1),
            ChildMoniker::new("b".to_string(), None, 2),
            ChildMoniker::new("c".to_string(), None, 3),
        ]);
        let abd = AbsoluteMoniker::new(vec![
            ChildMoniker::new("a".to_string(), None, 1),
            ChildMoniker::new("b".to_string(), None, 2),
            ChildMoniker::new("d".to_string(), None, 3),
        ]);

        assert!(root.contains_in_realm(&root));
        assert!(root.contains_in_realm(&a));
        assert!(root.contains_in_realm(&ab));
        assert!(root.contains_in_realm(&abc));
        assert!(root.contains_in_realm(&abd));

        assert!(!a.contains_in_realm(&root));
        assert!(a.contains_in_realm(&a));
        assert!(a.contains_in_realm(&ab));
        assert!(a.contains_in_realm(&abc));
        assert!(a.contains_in_realm(&abd));

        assert!(!ab.contains_in_realm(&root));
        assert!(!ab.contains_in_realm(&a));
        assert!(ab.contains_in_realm(&ab));
        assert!(ab.contains_in_realm(&abc));
        assert!(ab.contains_in_realm(&abd));

        assert!(!abc.contains_in_realm(&root));
        assert!(abc.contains_in_realm(&abc));
        assert!(!abc.contains_in_realm(&a));
        assert!(!abc.contains_in_realm(&ab));
        assert!(!abc.contains_in_realm(&abd));

        assert!(!abc.contains_in_realm(&abd));
        assert!(abd.contains_in_realm(&abd));
        assert!(!abd.contains_in_realm(&a));
        assert!(!abd.contains_in_realm(&ab));
        assert!(!abd.contains_in_realm(&abc));
    }

    #[test]
    fn absolute_moniker_from_string_without_instance_id() -> Result<(), Error> {
        let under_test = |s| AbsoluteMoniker::parse_string_without_instances(s);

        assert_eq!(under_test("/")?, AbsoluteMoniker::root());

        let a = ChildMoniker::new("a".to_string(), None, 0);
        let bb = ChildMoniker::new("b".to_string(), Some("b".to_string()), 0);

        assert_eq!(under_test("/a")?, AbsoluteMoniker::new(vec![a.clone()]));
        assert_eq!(under_test("/a/b:b")?, AbsoluteMoniker::new(vec![a.clone(), bb.clone()]));
        assert_eq!(
            under_test("/a/b:b/a/b:b")?,
            AbsoluteMoniker::new(vec![a.clone(), bb.clone(), a.clone(), bb.clone()])
        );

        assert!(under_test("").is_err(), "cannot be empty");
        assert!(under_test("a").is_err(), "must start with root");
        assert!(under_test("a/b").is_err(), "must start with root");
        assert!(under_test("//").is_err(), "path segments cannot be empty");
        assert!(under_test("/a/").is_err(), "path segments cannot be empty");
        assert!(under_test("/a//b").is_err(), "path segments cannot be empty");
        assert!(under_test("/a:a:0").is_err(), "cannot contain instance id");

        Ok(())
    }

    #[test]
    fn absolute_moniker_to_string_without_instance_id() {
        assert_eq!("/", AbsoluteMoniker::root().to_string_without_instances());

        let a = ChildMoniker::new("a".to_string(), None, 0);
        let bb = ChildMoniker::new("b".to_string(), Some("b".to_string()), 0);

        assert_eq!("/a", AbsoluteMoniker::new(vec![a.clone()]).to_string_without_instances());
        assert_eq!(
            "/a/b:b",
            AbsoluteMoniker::new(vec![a.clone(), bb.clone()]).to_string_without_instances()
        );
        assert_eq!(
            "/a/b:b/a/b:b",
            AbsoluteMoniker::new(vec![a.clone(), bb.clone(), a.clone(), bb.clone()])
                .to_string_without_instances()
        );
    }
}
