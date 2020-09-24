// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    core::cmp::{self, Ord, Ordering},
    std::fmt,
    thiserror::Error,
};

/// A child moniker locally identifies a child component instance using the name assigned by
/// its parent and its collection (if present). It is a building block for more complex monikers.
///
/// Display notation: "[collection:]name:instance_id".
#[derive(Eq, PartialEq, Debug, Clone, Hash)]
pub struct ChildMoniker {
    name: String,
    collection: Option<String>,
    instance: InstanceId,
    rep: String,
}

pub type InstanceId = u32;

impl ChildMoniker {
    pub fn new(name: String, collection: Option<String>, instance: InstanceId) -> Self {
        assert!(!name.is_empty());
        let rep = if let Some(c) = collection.as_ref() {
            assert!(!c.is_empty());
            format!("{}:{}:{}", c, name, instance)
        } else {
            format!("{}:{}", name, instance)
        };
        Self { name, collection, instance, rep }
    }

    /// Parses an `ChildMoniker` from a string.
    ///
    /// Input strings should be of the format `<name>(:<collection>)?:<instance_id>`, e.g. `foo:42`
    /// or `biz:foo:42`.
    fn parse(rep: &str) -> Result<Self, MonikerError> {
        let parts: Vec<_> = rep.split(":").collect();
        let invalid = || MonikerError::invalid_moniker(rep);
        // An instanced moniker is either just a name (static instance), or
        // collection:name:instance_id.
        if parts.len() != 2 && parts.len() != 3 {
            return Err(invalid());
        }
        for p in parts.iter() {
            if p.is_empty() {
                return Err(invalid());
            }
        }
        let (name, coll, instance) = match parts.len() == 3 {
            true => {
                let name = parts[1].to_string();
                let coll = parts[0].to_string();
                let instance: InstanceId = match parts[2].parse() {
                    Ok(i) => i,
                    _ => {
                        return Err(invalid());
                    }
                };
                (name, Some(coll), instance)
            }
            false => {
                let instance: InstanceId = match parts[1].parse() {
                    Ok(i) => i,
                    _ => {
                        return Err(invalid());
                    }
                };
                (parts[0].to_string(), None, instance)
            }
        };
        Ok(Self::new(name, coll, instance))
    }

    /// Converts this instanced moniker to a regular child moniker by stripping the instance id.
    pub fn to_partial(&self) -> PartialMoniker {
        PartialMoniker::new(self.name.clone(), self.collection.clone())
    }

    /// Converts this child moniker to an instanced moniker.
    pub fn from_partial(m: &PartialMoniker, instance: InstanceId) -> Self {
        Self::new(m.name.clone(), m.collection.clone(), instance)
    }

    pub fn name(&self) -> &str {
        &self.name
    }

    pub fn collection(&self) -> Option<&str> {
        self.collection.as_ref().map(|s| &**s)
    }

    pub fn instance(&self) -> InstanceId {
        self.instance
    }

    pub fn as_str(&self) -> &str {
        &self.rep
    }
}

impl From<&str> for ChildMoniker {
    fn from(rep: &str) -> Self {
        ChildMoniker::parse(rep).expect(&format!("instanced moniker failed to parse: {}", rep))
    }
}

impl Ord for ChildMoniker {
    fn cmp(&self, other: &Self) -> Ordering {
        (&self.collection, &self.name, &self.instance).cmp(&(
            &other.collection,
            &other.name,
            &other.instance,
        ))
    }
}

impl PartialOrd for ChildMoniker {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl fmt::Display for ChildMoniker {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.as_str())
    }
}

/// A variant of child moniker that does not distinguish between instances
///
/// Display notation: "name[:collection]".
#[derive(Eq, PartialEq, Debug, Clone, Hash)]
pub struct PartialMoniker {
    name: String,
    collection: Option<String>,
    rep: String,
}

impl PartialMoniker {
    pub fn new(name: String, collection: Option<String>) -> Self {
        assert!(!name.is_empty());
        let rep = if let Some(c) = collection.as_ref() {
            assert!(!c.is_empty());
            format!("{}:{}", c, name)
        } else {
            name.clone()
        };
        PartialMoniker { name, collection, rep }
    }

    /// Parses a `PartialMoniker` from a string.
    ///
    /// Input strings should be of the format `<name>(:<collection>)?`, e.g. `foo` or `biz:foo`.
    fn parse(rep: &str) -> Result<Self, MonikerError> {
        let mut parts = rep.split(":").fuse();
        let invalid = || MonikerError::invalid_moniker(rep);
        let first = parts.next().ok_or_else(invalid)?;
        let second = parts.next();
        if parts.next().is_some() || first.len() == 0 || second.map_or(false, |s| s.len() == 0) {
            return Err(invalid());
        }
        let (name, coll) = match second {
            Some(s) => (s, Some(first.to_string())),
            None => (first, None),
        };
        Ok(PartialMoniker::new(name.to_string(), coll))
    }

    pub fn name(&self) -> &str {
        &self.name
    }

    pub fn collection(&self) -> Option<&str> {
        self.collection.as_ref().map(|s| &**s)
    }

    pub fn as_str(&self) -> &str {
        &self.rep
    }
}

impl From<&str> for PartialMoniker {
    fn from(rep: &str) -> Self {
        PartialMoniker::parse(rep).expect(&format!("child moniker failed to parse: {}", rep))
    }
}

impl Ord for PartialMoniker {
    fn cmp(&self, other: &Self) -> Ordering {
        (&self.collection, &self.name).cmp(&(&other.collection, &other.name))
    }
}

impl PartialOrd for PartialMoniker {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl fmt::Display for PartialMoniker {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.as_str())
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
#[derive(Eq, PartialEq, Debug, Clone, Hash)]
pub struct AbsoluteMoniker {
    path: Vec<ChildMoniker>,
}

impl AbsoluteMoniker {
    pub fn new(path: Vec<ChildMoniker>) -> AbsoluteMoniker {
        AbsoluteMoniker { path }
    }

    fn parse(path: &Vec<&str>) -> Result<Self, MonikerError> {
        let path: Result<Vec<ChildMoniker>, MonikerError> =
            path.iter().map(|x| ChildMoniker::parse(x)).collect();
        Ok(AbsoluteMoniker::new(path?))
    }

    /// Parse the given string as an absolute moniker. The string should be a '/' delimited series
    /// of child monikers without any instance identifiers, e.g. "/", or "/name1/name2" or
    /// "/name1:collection1".
    // TODO(fxbug.dev/49968): Remove instance ID 0 assumption when removing instance IDs from
    // AbsoluteMoniker/ChildMoniker (and rename to parse_str + add From<&str> impl).
    pub fn parse_string_without_instances(input: &str) -> Result<Self, MonikerError> {
        if input.chars().nth(0) != Some('/') {
            return Err(MonikerError::invalid_moniker(input));
        }
        if input == "/" {
            return Ok(Self::root());
        }
        let path = input[1..]
            .split('/')
            .map(PartialMoniker::parse)
            .map(|p| p.map(|ok_p| ChildMoniker::from_partial(&ok_p, 0)))
            .collect::<Result<_, MonikerError>>()?;
        Ok(Self::new(path))
    }

    pub fn path(&self) -> &Vec<ChildMoniker> {
        &self.path
    }

    /// Indicates whether `other` is contained within the realm specified by
    /// this AbsoluteMoniker.
    pub fn contains_in_realm(&self, other: &AbsoluteMoniker) -> bool {
        if other.path.len() < self.path.len() {
            return false;
        }

        self.path.iter().enumerate().all(|item| *item.1 == other.path[item.0])
    }

    pub fn root() -> AbsoluteMoniker {
        AbsoluteMoniker { path: vec![] }
    }

    pub fn leaf(&self) -> Option<&ChildMoniker> {
        self.path.last()
    }

    pub fn is_root(&self) -> bool {
        self.path.is_empty()
    }

    pub fn parent(&self) -> Option<AbsoluteMoniker> {
        if self.is_root() {
            None
        } else {
            let l = self.path.len() - 1;
            Some(AbsoluteMoniker { path: self.path[..l].to_vec() })
        }
    }

    pub fn child(&self, child: ChildMoniker) -> AbsoluteMoniker {
        let mut path = self.path.clone();
        path.push(child);
        AbsoluteMoniker { path }
    }
}

impl From<Vec<&str>> for AbsoluteMoniker {
    fn from(rep: Vec<&str>) -> Self {
        AbsoluteMoniker::parse(&rep)
            .expect(&format!("absolute moniker failed to parse: {:?}", &rep))
    }
}

impl cmp::Ord for AbsoluteMoniker {
    fn cmp(&self, other: &Self) -> cmp::Ordering {
        let min_size = cmp::min(self.path.len(), other.path.len());
        for i in 0..min_size {
            if self.path[i] < other.path[i] {
                return cmp::Ordering::Less;
            } else if self.path[i] > other.path[i] {
                return cmp::Ordering::Greater;
            }
        }
        if self.path.len() > other.path.len() {
            return cmp::Ordering::Greater;
        } else if self.path.len() < other.path.len() {
            return cmp::Ordering::Less;
        }

        return cmp::Ordering::Equal;
    }
}

impl PartialOrd for AbsoluteMoniker {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl fmt::Display for AbsoluteMoniker {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.path.is_empty() {
            write!(f, "/")?;
        } else {
            for segment in &self.path {
                write!(f, "/{}", segment.as_str())?
            }
        }
        Ok(())
    }
}

/// One of:
/// - An absolute moniker
/// - A marker representing component manager's realm
#[derive(Eq, PartialEq, Debug, Clone, Hash)]
pub enum ExtendedMoniker {
    ComponentInstance(AbsoluteMoniker),
    ComponentManager,
}

impl fmt::Display for ExtendedMoniker {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::ComponentInstance(m) => {
                write!(f, "{}", m)?;
            }
            Self::ComponentManager => {
                write!(f, "<component_manager>")?;
            }
        }
        Ok(())
    }
}

/// A relative moniker describes the identity of a component instance in terms of its path
/// relative to another (unspecified) component in the component instance tree.
///
/// A self-reference moniker is a moniker with both empty "up" and "down" paths.
///
/// Relative monikers consist of two paths called "up" and "down".
/// - The "up" path describes a sequence of child-to-parent traversals heading towards the root of
///   the component instance tree.
/// - The "down" path describes a sequence of parent-to-child traversals heading towards a
///   different component instance in the tree.
///
/// These paths are minimal: no suffix segments of the "up" path can be a prefix segments of the
/// "down" path.  All such common segments must be elided as part of canonicalizing the relative
/// moniker prior to construction.
///
/// Naming child monikers along both the "upwards" and "downwards" paths provides a strong
/// guarantee that relative monikers are only meaningful when interpreted within isomorphic
/// component instance subtrees.  (Compare with relative filesystem path notations which use ".."
/// to perform upwards traversal and offer correspondingly weaker guarantees.)
///
/// For example, if two sibling component instances named "A" and "B" both possess relative
/// monikers for another component instance named "C", then A's moniker for C and B's moniker
/// for C will be distinct.
///
/// Display notation: ".", "./down1", ".\up1/down1", ".\up1\up2/down1", ...
#[derive(Eq, PartialEq, Debug, Clone)]
pub struct RelativeMoniker {
    up_path: Vec<ChildMoniker>,
    down_path: Vec<ChildMoniker>,
}

impl RelativeMoniker {
    pub fn new(up_path: Vec<ChildMoniker>, down_path: Vec<ChildMoniker>) -> RelativeMoniker {
        RelativeMoniker { up_path, down_path }
    }

    pub fn from_absolute(from: &AbsoluteMoniker, to: &AbsoluteMoniker) -> RelativeMoniker {
        let mut from_path = from.path().iter().peekable();
        let mut to_path = to.path().iter().peekable();

        while from_path.peek().is_some() && from_path.peek() == to_path.peek() {
            from_path.next();
            to_path.next();
        }

        let mut res = RelativeMoniker {
            up_path: from_path.cloned().collect(),
            down_path: to_path.cloned().collect(),
        };
        res.up_path.reverse();
        res
    }

    pub fn up_path(&self) -> &Vec<ChildMoniker> {
        &self.up_path
    }

    pub fn down_path(&self) -> &Vec<ChildMoniker> {
        &self.down_path
    }

    pub fn is_self(&self) -> bool {
        self.up_path.is_empty() && self.down_path.is_empty()
    }
}

impl fmt::Display for RelativeMoniker {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, ".")?;
        for segment in &self.up_path {
            write!(f, "\\{}", segment)?
        }
        for segment in &self.down_path {
            write!(f, "/{}", segment)?
        }
        Ok(())
    }
}

/// Errors produced by `MonikerEnvironment`.
#[derive(Debug, Error)]
pub enum MonikerError {
    #[error("invalid moniker: {}", rep)]
    InvalidMoniker { rep: String },
}

impl MonikerError {
    pub fn invalid_moniker(rep: impl Into<String>) -> MonikerError {
        MonikerError::InvalidMoniker { rep: rep.into() }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, anyhow::Error};

    #[test]
    fn child_monikers() {
        let m = ChildMoniker::new("test".to_string(), None, 42);
        assert_eq!("test", m.name());
        assert_eq!(None, m.collection());
        assert_eq!(42, m.instance());
        assert_eq!("test:42", m.as_str());
        assert_eq!("test:42", format!("{}", m));
        assert_eq!(m, ChildMoniker::from("test:42"));
        assert_eq!("test", m.to_partial().as_str());
        assert_eq!(m, ChildMoniker::from_partial(&"test".into(), 42));

        let m = ChildMoniker::new("test".to_string(), Some("coll".to_string()), 42);
        assert_eq!("test", m.name());
        assert_eq!(Some("coll"), m.collection());
        assert_eq!(42, m.instance());
        assert_eq!("coll:test:42", m.as_str());
        assert_eq!("coll:test:42", format!("{}", m));
        assert_eq!(m, ChildMoniker::from("coll:test:42"));
        assert_eq!("coll:test", m.to_partial().as_str());
        assert_eq!(m, ChildMoniker::from_partial(&"coll:test".into(), 42));

        assert!(ChildMoniker::parse("").is_err(), "cannot be empty");
        assert!(ChildMoniker::parse(":").is_err(), "cannot be empty with colon");
        assert!(ChildMoniker::parse("::").is_err(), "cannot be empty with double colon");
        assert!(ChildMoniker::parse("f:").is_err(), "second part cannot be empty with colon");
        assert!(ChildMoniker::parse(":1").is_err(), "first part cannot be empty with colon");
        assert!(ChildMoniker::parse("f:f:").is_err(), "third part cannot be empty with colon");
        assert!(ChildMoniker::parse("f::1").is_err(), "second part cannot be empty with colon");
        assert!(ChildMoniker::parse(":f:1").is_err(), "first part cannot be empty with colon");
        assert!(ChildMoniker::parse("f:f:1:1").is_err(), "more than three colons not allowed");
        assert!(ChildMoniker::parse("f:f").is_err(), "second part must be int");
        assert!(ChildMoniker::parse("f:f:f").is_err(), "third part must be int");
    }

    #[test]
    fn child_moniker_compare() {
        let a = ChildMoniker::new("a".to_string(), None, 1);
        let a2 = ChildMoniker::new("a".to_string(), None, 2);
        let aa = ChildMoniker::new("a".to_string(), Some("a".to_string()), 1);
        let aa2 = ChildMoniker::new("a".to_string(), Some("a".to_string()), 2);
        let ab = ChildMoniker::new("a".to_string(), Some("b".to_string()), 1);
        let ba = ChildMoniker::new("b".to_string(), Some("a".to_string()), 1);
        let bb = ChildMoniker::new("b".to_string(), Some("b".to_string()), 1);
        let aa_same = ChildMoniker::new("a".to_string(), Some("a".to_string()), 1);

        assert_eq!(Ordering::Less, a.cmp(&a2));
        assert_eq!(Ordering::Greater, a2.cmp(&a));
        assert_eq!(Ordering::Less, a2.cmp(&aa));
        assert_eq!(Ordering::Greater, aa.cmp(&a2));
        assert_eq!(Ordering::Less, a.cmp(&ab));
        assert_eq!(Ordering::Greater, ab.cmp(&a));
        assert_eq!(Ordering::Less, a.cmp(&ba));
        assert_eq!(Ordering::Greater, ba.cmp(&a));
        assert_eq!(Ordering::Less, a.cmp(&bb));
        assert_eq!(Ordering::Greater, bb.cmp(&a));

        assert_eq!(Ordering::Less, aa.cmp(&aa2));
        assert_eq!(Ordering::Greater, aa2.cmp(&aa));
        assert_eq!(Ordering::Less, aa.cmp(&ab));
        assert_eq!(Ordering::Greater, ab.cmp(&aa));
        assert_eq!(Ordering::Less, aa.cmp(&ba));
        assert_eq!(Ordering::Greater, ba.cmp(&aa));
        assert_eq!(Ordering::Less, aa.cmp(&bb));
        assert_eq!(Ordering::Greater, bb.cmp(&aa));
        assert_eq!(Ordering::Equal, aa.cmp(&aa_same));
        assert_eq!(Ordering::Equal, aa_same.cmp(&aa));

        assert_eq!(Ordering::Greater, ab.cmp(&ba));
        assert_eq!(Ordering::Less, ba.cmp(&ab));
        assert_eq!(Ordering::Less, ab.cmp(&bb));
        assert_eq!(Ordering::Greater, bb.cmp(&ab));

        assert_eq!(Ordering::Less, ba.cmp(&bb));
        assert_eq!(Ordering::Greater, bb.cmp(&ba));
    }

    #[test]
    fn partial_monikers() {
        let m = PartialMoniker::new("test".to_string(), None);
        assert_eq!("test", m.name());
        assert_eq!(None, m.collection());
        assert_eq!("test", m.as_str());
        assert_eq!("test", format!("{}", m));
        assert_eq!(m, PartialMoniker::from("test"));

        let m = PartialMoniker::new("test".to_string(), Some("coll".to_string()));
        assert_eq!("test", m.name());
        assert_eq!(Some("coll"), m.collection());
        assert_eq!("coll:test", m.as_str());
        assert_eq!("coll:test", format!("{}", m));
        assert_eq!(m, PartialMoniker::from("coll:test"));

        assert!(PartialMoniker::parse("").is_err(), "cannot be empty");
        assert!(PartialMoniker::parse(":").is_err(), "cannot be empty with colon");
        assert!(PartialMoniker::parse("f:").is_err(), "second part cannot be empty with colon");
        assert!(PartialMoniker::parse(":f").is_err(), "first part cannot be empty with colon");
        assert!(PartialMoniker::parse("f:f:f").is_err(), "multiple colons not allowed");
    }

    #[test]
    fn partial_moniker_compare() {
        let a = PartialMoniker::new("a".to_string(), None);
        let aa = PartialMoniker::new("a".to_string(), Some("a".to_string()));
        let ab = PartialMoniker::new("a".to_string(), Some("b".to_string()));
        let ba = PartialMoniker::new("b".to_string(), Some("a".to_string()));
        let bb = PartialMoniker::new("b".to_string(), Some("b".to_string()));
        let aa_same = PartialMoniker::new("a".to_string(), Some("a".to_string()));

        assert_eq!(Ordering::Less, a.cmp(&aa));
        assert_eq!(Ordering::Greater, aa.cmp(&a));
        assert_eq!(Ordering::Less, a.cmp(&ab));
        assert_eq!(Ordering::Greater, ab.cmp(&a));
        assert_eq!(Ordering::Less, a.cmp(&ba));
        assert_eq!(Ordering::Greater, ba.cmp(&a));
        assert_eq!(Ordering::Less, a.cmp(&bb));
        assert_eq!(Ordering::Greater, bb.cmp(&a));

        assert_eq!(Ordering::Less, aa.cmp(&ab));
        assert_eq!(Ordering::Greater, ab.cmp(&aa));
        assert_eq!(Ordering::Less, aa.cmp(&ba));
        assert_eq!(Ordering::Greater, ba.cmp(&aa));
        assert_eq!(Ordering::Less, aa.cmp(&bb));
        assert_eq!(Ordering::Greater, bb.cmp(&aa));
        assert_eq!(Ordering::Equal, aa.cmp(&aa_same));
        assert_eq!(Ordering::Equal, aa_same.cmp(&aa));

        assert_eq!(Ordering::Greater, ab.cmp(&ba));
        assert_eq!(Ordering::Less, ba.cmp(&ab));
        assert_eq!(Ordering::Less, ab.cmp(&bb));
        assert_eq!(Ordering::Greater, bb.cmp(&ab));

        assert_eq!(Ordering::Less, ba.cmp(&bb));
        assert_eq!(Ordering::Greater, bb.cmp(&ba));
    }

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
        assert_eq!(m.leaf(), Some(&ChildMoniker::from("coll:b:2")));
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
    fn relative_monikers() {
        let me = RelativeMoniker::new(vec![], vec![]);
        assert_eq!(true, me.is_self());
        assert_eq!(".", format!("{}", me));

        let ancestor = RelativeMoniker::new(
            vec![
                ChildMoniker::new("a".to_string(), None, 1),
                ChildMoniker::new("b".to_string(), None, 2),
            ],
            vec![],
        );
        assert_eq!(false, ancestor.is_self());
        assert_eq!(".\\a:1\\b:2", format!("{}", ancestor));

        let descendant = RelativeMoniker::new(
            vec![],
            vec![
                ChildMoniker::new("a".to_string(), None, 1),
                ChildMoniker::new("b".to_string(), None, 2),
            ],
        );
        assert_eq!(false, descendant.is_self());
        assert_eq!("./a:1/b:2", format!("{}", descendant));

        let sibling = RelativeMoniker::new(
            vec![ChildMoniker::new("a".to_string(), None, 1)],
            vec![ChildMoniker::new("b".to_string(), None, 2)],
        );
        assert_eq!(false, sibling.is_self());
        assert_eq!(".\\a:1/b:2", format!("{}", sibling));

        let cousin = RelativeMoniker::new(
            vec![
                ChildMoniker::new("a".to_string(), None, 1),
                ChildMoniker::new("a0".to_string(), None, 1),
            ],
            vec![
                ChildMoniker::new("b0".to_string(), None, 2),
                ChildMoniker::new("b".to_string(), None, 2),
            ],
        );
        assert_eq!(false, cousin.is_self());
        assert_eq!(".\\a:1\\a0:1/b0:2/b:2", format!("{}", cousin));
    }

    #[test]
    fn relative_monikers_from_absolute() {
        let me = RelativeMoniker::from_absolute(&vec![].into(), &vec![].into());
        assert_eq!(true, me.is_self());
        assert_eq!(".", format!("{}", me));

        let me = RelativeMoniker::from_absolute(
            &vec!["a:1", "b:2", "c:3"].into(),
            &vec!["a:1", "b:2", "c:3"].into(),
        );
        assert_eq!(true, me.is_self());
        assert_eq!(".", format!("{}", me));

        let ancestor = RelativeMoniker::from_absolute(&vec!["a:1", "b:2"].into(), &vec![].into());
        assert_eq!(false, ancestor.is_self());
        assert_eq!(".\\b:2\\a:1", format!("{}", ancestor));

        let ancestor = RelativeMoniker::from_absolute(
            &vec!["a:1", "b:2", "c:3", "d:4"].into(),
            &vec!["a:1", "b:2"].into(),
        );
        assert_eq!(false, ancestor.is_self());
        assert_eq!(".\\d:4\\c:3", format!("{}", ancestor));

        let descendant = RelativeMoniker::from_absolute(&vec![].into(), &vec!["a:1", "b:2"].into());
        assert_eq!(false, descendant.is_self());
        assert_eq!("./a:1/b:2", format!("{}", descendant));

        let descendant = RelativeMoniker::from_absolute(
            &vec!["a:1", "b:2"].into(),
            &vec!["a:1", "b:2", "c:3", "d:4"].into(),
        );
        assert_eq!(false, descendant.is_self());
        assert_eq!("./c:3/d:4", format!("{}", descendant));

        let sibling = RelativeMoniker::from_absolute(&vec!["a:1"].into(), &vec!["b:2"].into());
        assert_eq!(false, sibling.is_self());
        assert_eq!(".\\a:1/b:2", format!("{}", sibling));

        let sibling =
            RelativeMoniker::from_absolute(&vec!["c:3", "a:1"].into(), &vec!["c:3", "b:2"].into());
        assert_eq!(false, sibling.is_self());
        assert_eq!(".\\a:1/b:2", format!("{}", sibling));

        let cousin = RelativeMoniker::from_absolute(
            &vec!["a0:1", "a:1"].into(),
            &vec!["b0:2", "b:2"].into(),
        );
        assert_eq!(false, cousin.is_self());
        assert_eq!(".\\a:1\\a0:1/b0:2/b:2", format!("{}", cousin));

        let cousin = RelativeMoniker::from_absolute(
            &vec!["c:3", "d:4", "a0:1", "a:1"].into(),
            &vec!["c:3", "d:4", "b0:2", "b:2"].into(),
        );
        assert_eq!(false, cousin.is_self());
        assert_eq!(".\\a:1\\a0:1/b0:2/b:2", format!("{}", cousin));
    }
}
