// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        error::MonikerError,
        partial_child_moniker::{validate_moniker_part, PartialChildMoniker},
    },
    core::{
        cmp::{Ord, Ordering},
        fmt::Display,
    },
    std::fmt,
};

#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};

/// A child moniker locally identifies a child component instance using the name assigned by
/// its parent and its collection (if present). It is a building block for more complex monikers.
///
/// Display notation: "[collection:]name:instance_id".
pub trait ChildMonikerBase: Eq + PartialOrd + Clone + Default + Display {
    fn parse<T: AsRef<str>>(rep: T) -> Result<Self, MonikerError>
    where
        Self: Sized;

    fn name(&self) -> &str;

    fn collection(&self) -> Option<&str>;

    fn as_str(&self) -> &str;

    fn to_partial(&self) -> PartialChildMoniker;
}

#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Eq, PartialEq, Debug, Clone, Hash, Default)]
pub struct ChildMoniker {
    name: String,
    collection: Option<String>,
    instance: InstanceId,
    rep: String,
}

pub type InstanceId = u32;

impl ChildMonikerBase for ChildMoniker {
    /// Parses an `ChildMoniker` from a string.
    ///
    /// Input strings should be of the format `<name>(:<collection>)?:<instance_id>`, e.g. `foo:42`
    /// or `biz:foo:42`.
    fn parse<T: AsRef<str>>(rep: T) -> Result<Self, MonikerError> {
        let rep = rep.as_ref();
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

        validate_moniker_part(Some(&name))?;
        validate_moniker_part(coll.as_deref())?;
        Ok(Self::new(name, coll, instance))
    }

    fn name(&self) -> &str {
        &self.name
    }

    fn collection(&self) -> Option<&str> {
        self.collection.as_ref().map(|s| &**s)
    }

    fn as_str(&self) -> &str {
        &self.rep
    }

    /// Converts this instanced moniker to a regular child moniker by stripping the instance id.
    fn to_partial(&self) -> PartialChildMoniker {
        PartialChildMoniker::new(self.name.clone(), self.collection.clone())
    }
}

impl ChildMoniker {
    // TODO(fxbug.dev/77563): This does not currently validate the String inputs.
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

    /// Converts this child moniker to an instanced moniker.
    pub fn from_partial(m: &PartialChildMoniker, instance: InstanceId) -> Self {
        Self::new(m.name.clone(), m.collection.clone(), instance)
    }

    pub fn instance(&self) -> InstanceId {
        self.instance
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

#[cfg(test)]
mod tests {
    use super::*;

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

        let max_length_part = "f".repeat(100);
        let m = ChildMoniker::parse(format!("{0}:{0}:42", max_length_part)).expect("valid moniker");
        assert_eq!(&max_length_part, m.name());
        assert_eq!(Some(max_length_part.as_str()), m.collection());
        assert_eq!(42, m.instance());

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
        assert!(ChildMoniker::parse("@:1").is_err(), "invalid character in name");
        assert!(ChildMoniker::parse("@:f:1").is_err(), "invalid character in collection");
        assert!(ChildMoniker::parse("f:@:1").is_err(), "invalid character in name with collection");
        assert!(ChildMoniker::parse("f".repeat(101) + ":1").is_err(), "name too long");
        assert!(ChildMoniker::parse("f".repeat(101) + ":f:1").is_err(), "collection too long");
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
}
