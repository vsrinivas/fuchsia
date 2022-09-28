// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::instanced_child_moniker::InstancedChildMoniker,
    core::cmp::{self, Ord, Ordering},
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, ChildMoniker, ChildMonikerBase, MonikerError},
    std::{fmt, hash::Hash},
};

#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};

/// An instanced absolute moniker describes the identity of a component instance in terms of its path
/// relative to the root of the component instance tree.
///
/// A root moniker is a moniker with an empty path.
///
/// Instanced absolute monikers are only used internally within the component manager.  Externally,
/// components are referenced by encoded relative moniker so as to minimize the amount of
/// information which is disclosed about the overall structure of the component instance tree.
///
/// Display notation: "/", "/name1:1", "/name1:1/name2:2", ...
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Debug, Eq, PartialEq, Clone, Hash, Default)]
pub struct InstancedAbsoluteMoniker {
    path: Vec<InstancedChildMoniker>,
}

impl InstancedAbsoluteMoniker {
    /// Convert an InstancedAbsoluteMoniker into an allocated AbsoluteMoniker without InstanceIds
    pub fn without_instance_ids(&self) -> AbsoluteMoniker {
        let path: Vec<ChildMoniker> = self.path().iter().map(|p| p.without_instance_id()).collect();
        AbsoluteMoniker::new(path)
    }
}

impl AbsoluteMonikerBase for InstancedAbsoluteMoniker {
    type Part = InstancedChildMoniker;

    fn new(path: Vec<Self::Part>) -> Self {
        Self { path }
    }

    fn path(&self) -> &Vec<Self::Part> {
        &self.path
    }

    /// Parse the given string as an instanced absolute moniker. The string should b a '/'
    /// delimited series of child monikers *with* instance identifiers, e.g. "/", or
    /// "/name1:0/name2:0" or "/name1:collection1:0".
    fn parse_str(input: &str) -> Result<InstancedAbsoluteMoniker, MonikerError> {
        if input.chars().nth(0) != Some('/') {
            return Err(MonikerError::invalid_moniker(input));
        }
        if input == "/" {
            return Ok(InstancedAbsoluteMoniker::root());
        }
        let path = input[1..]
            .split('/')
            .map(InstancedChildMoniker::parse)
            .collect::<Result<_, MonikerError>>()?;
        Ok(InstancedAbsoluteMoniker::new(path))
    }
}

impl From<Vec<&str>> for InstancedAbsoluteMoniker {
    fn from(rep: Vec<&str>) -> Self {
        Self::parse(&rep).expect(&format!("instanced absolute moniker failed to parse: {:?}", &rep))
    }
}

impl cmp::Ord for InstancedAbsoluteMoniker {
    fn cmp(&self, other: &Self) -> cmp::Ordering {
        self.compare(other)
    }
}

impl PartialOrd for InstancedAbsoluteMoniker {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl fmt::Display for InstancedAbsoluteMoniker {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.format(f)
    }
}
#[cfg(test)]
mod tests {
    use {
        super::*,
        moniker::{AbsoluteMonikerBase, ChildMonikerBase, MonikerError},
    };

    #[test]
    fn instanced_absolute_monikers() {
        let root = InstancedAbsoluteMoniker::root();
        assert_eq!(true, root.is_root());
        assert_eq!("/", format!("{}", root));
        assert_eq!(root, InstancedAbsoluteMoniker::from(vec![]));

        let m = InstancedAbsoluteMoniker::new(vec![
            InstancedChildMoniker::try_new("a", None, 1).unwrap(),
            InstancedChildMoniker::try_new("b", Some("coll"), 2).unwrap(),
        ]);
        assert_eq!(false, m.is_root());
        assert_eq!("/a:1/coll:b:2", format!("{}", m));
        assert_eq!(m, InstancedAbsoluteMoniker::from(vec!["a:1", "coll:b:2"]));
        assert_eq!(m.leaf().map(|m| m.collection()).flatten(), Some("coll"));
        assert_eq!(m.leaf().map(|m| m.name()), Some("b"));
        assert_eq!(m.leaf().map(|m| m.instance()), Some(2));
        assert_eq!(m.leaf(), Some(&InstancedChildMoniker::from("coll:b:2")));
    }

    #[test]
    fn instanced_absolute_moniker_parent() {
        let root = InstancedAbsoluteMoniker::root();
        assert_eq!(true, root.is_root());
        assert_eq!(None, root.parent());

        let m = InstancedAbsoluteMoniker::new(vec![
            InstancedChildMoniker::try_new("a", None, 1).unwrap(),
            InstancedChildMoniker::try_new("b", None, 2).unwrap(),
        ]);
        assert_eq!("/a:1/b:2", format!("{}", m));
        assert_eq!("/a:1", format!("{}", m.parent().unwrap()));
        assert_eq!("/", format!("{}", m.parent().unwrap().parent().unwrap()));
        assert_eq!(None, m.parent().unwrap().parent().unwrap().parent());
        assert_eq!(m.leaf(), Some(&InstancedChildMoniker::from("b:2")));
    }

    #[test]
    fn instanced_absolute_moniker_compare() {
        let a = InstancedAbsoluteMoniker::new(vec![
            InstancedChildMoniker::try_new("a", None, 1).unwrap(),
            InstancedChildMoniker::try_new("b", None, 2).unwrap(),
            InstancedChildMoniker::try_new("c", None, 3).unwrap(),
        ]);
        let a2 = InstancedAbsoluteMoniker::new(vec![
            InstancedChildMoniker::try_new("a", None, 1).unwrap(),
            InstancedChildMoniker::try_new("b", None, 3).unwrap(),
            InstancedChildMoniker::try_new("c", None, 3).unwrap(),
        ]);
        let b = InstancedAbsoluteMoniker::new(vec![
            InstancedChildMoniker::try_new("a", None, 1).unwrap(),
            InstancedChildMoniker::try_new("b", None, 2).unwrap(),
            InstancedChildMoniker::try_new("b", None, 3).unwrap(),
        ]);
        let c = InstancedAbsoluteMoniker::new(vec![
            InstancedChildMoniker::try_new("a", None, 1).unwrap(),
            InstancedChildMoniker::try_new("b", None, 2).unwrap(),
            InstancedChildMoniker::try_new("c", None, 3).unwrap(),
            InstancedChildMoniker::try_new("d", None, 4).unwrap(),
        ]);
        let d = InstancedAbsoluteMoniker::new(vec![
            InstancedChildMoniker::try_new("a", None, 1).unwrap(),
            InstancedChildMoniker::try_new("b", None, 2).unwrap(),
            InstancedChildMoniker::try_new("c", None, 3).unwrap(),
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
    fn instanced_absolute_monikers_contains_in_realm() {
        let root = InstancedAbsoluteMoniker::root();
        let a = InstancedAbsoluteMoniker::new(vec![
            InstancedChildMoniker::try_new("a", None, 1).unwrap()
        ]);
        let ab = InstancedAbsoluteMoniker::new(vec![
            InstancedChildMoniker::try_new("a", None, 1).unwrap(),
            InstancedChildMoniker::try_new("b", None, 2).unwrap(),
        ]);
        let abc = InstancedAbsoluteMoniker::new(vec![
            InstancedChildMoniker::try_new("a", None, 1).unwrap(),
            InstancedChildMoniker::try_new("b", None, 2).unwrap(),
            InstancedChildMoniker::try_new("c", None, 3).unwrap(),
        ]);
        let abd = InstancedAbsoluteMoniker::new(vec![
            InstancedChildMoniker::try_new("a", None, 1).unwrap(),
            InstancedChildMoniker::try_new("b", None, 2).unwrap(),
            InstancedChildMoniker::try_new("d", None, 3).unwrap(),
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
    fn instanced_absolute_moniker_parse_str() -> Result<(), MonikerError> {
        let under_test = |s| InstancedAbsoluteMoniker::parse_str(s);

        assert_eq!(under_test("/")?, InstancedAbsoluteMoniker::root());

        let a = InstancedChildMoniker::try_new("a", None, 0).unwrap();
        let bb = InstancedChildMoniker::try_new("b", Some("b"), 0).unwrap();

        assert_eq!(under_test("/a:0")?, InstancedAbsoluteMoniker::new(vec![a.clone()]));
        assert_eq!(
            under_test("/a:0/b:b:0")?,
            InstancedAbsoluteMoniker::new(vec![a.clone(), bb.clone()])
        );
        assert_eq!(
            under_test("/a:0/b:b:0/a:0/b:b:0")?,
            InstancedAbsoluteMoniker::new(vec![a.clone(), bb.clone(), a.clone(), bb.clone()])
        );

        assert!(under_test("").is_err(), "cannot be empty");
        assert!(under_test("a:0").is_err(), "must start with root");
        assert!(under_test("a:0/b:0").is_err(), "must start with root");
        assert!(under_test("//").is_err(), "path segments cannot be empty");
        assert!(under_test("/a:0/").is_err(), "path segments cannot be empty");
        assert!(under_test("/a:0//b:0").is_err(), "path segments cannot be empty");
        assert!(under_test("/a:a").is_err(), "must contain instance id");

        Ok(())
    }
}
