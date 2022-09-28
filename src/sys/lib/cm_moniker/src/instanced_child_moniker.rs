// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cm_types::{LongName, Name},
    core::cmp::{Ord, Ordering},
    moniker::{ChildMoniker, ChildMonikerBase, MonikerError},
    std::fmt,
};

#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};

/// An instanced child moniker locally identifies a child component instance using the name assigned by
/// its parent and its collection (if present). It is a building block for more complex monikers.
///
/// Display notation: "[collection:]name:instance_id".
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Eq, PartialEq, Debug, Clone, Hash, Default)]
pub struct InstancedChildMoniker {
    name: String,
    collection: Option<String>,
    instance: IncarnationId,
    rep: String,
}

pub type IncarnationId = u32;

impl ChildMonikerBase for InstancedChildMoniker {
    /// Parses an `ChildMoniker` from a string.
    ///
    /// Input strings should be of the format `(<collection>:)?<name>:<instance_id>`, e.g. `foo:42`
    /// or `coll:foo:42`.
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
                let instance: IncarnationId = match parts[2].parse() {
                    Ok(i) => i,
                    _ => {
                        return Err(invalid());
                    }
                };
                (name, Some(coll), instance)
            }
            false => {
                let instance: IncarnationId = match parts[1].parse() {
                    Ok(i) => i,
                    _ => {
                        return Err(invalid());
                    }
                };
                (parts[0].to_string(), None, instance)
            }
        };

        LongName::validate(&name).map_err(|_| MonikerError::invalid_moniker_part(name.clone()))?;
        if let Some(ref c) = coll {
            Name::validate(c).map_err(|_| MonikerError::invalid_moniker_part(c))?;
        }
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
}

impl InstancedChildMoniker {
    // TODO(fxbug.dev/77563): This does not currently validate the String inputs.
    pub fn new(name: String, collection: Option<String>, instance: IncarnationId) -> Self {
        assert!(!name.is_empty());
        let rep = if let Some(c) = collection.as_ref() {
            assert!(!c.is_empty());
            format!("{}:{}:{}", c, name, instance)
        } else {
            format!("{}:{}", name, instance)
        };
        Self { name, collection, instance, rep }
    }

    /// Returns a moniker for a static child.
    ///
    /// The returned value will have no `collection`, and will have an `instance_id` of 0.
    ///
    /// TODO(fxbug.dev/77563): This does not currently validate the String inputs.
    pub fn static_child(name: String) -> Self {
        Self::new(name, None, 0)
    }

    /// Converts this child moniker into an instanced moniker.
    pub fn from_child_moniker(m: &ChildMoniker, instance: IncarnationId) -> Self {
        Self::new(m.name.clone(), m.collection.clone(), instance)
    }

    /// Convert an InstancedChildMoniker to an allocated ChildMoniker
    /// without an InstanceId
    pub fn without_instance_id(&self) -> ChildMoniker {
        ChildMoniker::new(self.name.clone(), self.collection.clone())
    }

    pub fn instance(&self) -> IncarnationId {
        self.instance
    }
}

impl From<&str> for InstancedChildMoniker {
    fn from(rep: &str) -> Self {
        InstancedChildMoniker::parse(rep)
            .expect(&format!("instanced moniker failed to parse: {}", rep))
    }
}

impl Ord for InstancedChildMoniker {
    fn cmp(&self, other: &Self) -> Ordering {
        (&self.collection, &self.name, &self.instance).cmp(&(
            &other.collection,
            &other.name,
            &other.instance,
        ))
    }
}

impl PartialOrd for InstancedChildMoniker {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl fmt::Display for InstancedChildMoniker {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.as_str())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        cm_types::{MAX_LONG_NAME_LENGTH, MAX_NAME_LENGTH},
    };

    #[test]
    fn instanced_child_monikers() {
        let m = InstancedChildMoniker::new("test".to_string(), None, 42);
        assert_eq!("test", m.name());
        assert_eq!(None, m.collection());
        assert_eq!(42, m.instance());
        assert_eq!("test:42", m.as_str());
        assert_eq!("test:42", format!("{}", m));
        assert_eq!(m, InstancedChildMoniker::from("test:42"));
        assert_eq!("test", m.without_instance_id().as_str());
        assert_eq!(m, InstancedChildMoniker::from_child_moniker(&"test".into(), 42));

        let m = InstancedChildMoniker::new("test".to_string(), Some("coll".to_string()), 42);
        assert_eq!("test", m.name());
        assert_eq!(Some("coll"), m.collection());
        assert_eq!(42, m.instance());
        assert_eq!("coll:test:42", m.as_str());
        assert_eq!("coll:test:42", format!("{}", m));
        assert_eq!(m, InstancedChildMoniker::from("coll:test:42"));
        assert_eq!("coll:test", m.without_instance_id().as_str());
        assert_eq!(m, InstancedChildMoniker::from_child_moniker(&"coll:test".into(), 42));

        let max_coll_length_part = "f".repeat(MAX_NAME_LENGTH);
        let max_name_length_part = "f".repeat(MAX_LONG_NAME_LENGTH);
        let m = InstancedChildMoniker::parse(format!(
            "{}:{}:42",
            max_coll_length_part, max_name_length_part
        ))
        .expect("valid moniker");
        assert_eq!(max_name_length_part, m.name());
        assert_eq!(Some(max_coll_length_part.as_str()), m.collection());
        assert_eq!(42, m.instance());

        assert!(InstancedChildMoniker::parse("").is_err(), "cannot be empty");
        assert!(InstancedChildMoniker::parse(":").is_err(), "cannot be empty with colon");
        assert!(InstancedChildMoniker::parse("::").is_err(), "cannot be empty with double colon");
        assert!(
            InstancedChildMoniker::parse("f:").is_err(),
            "second part cannot be empty with colon"
        );
        assert!(
            InstancedChildMoniker::parse(":1").is_err(),
            "first part cannot be empty with colon"
        );
        assert!(
            InstancedChildMoniker::parse("f:f:").is_err(),
            "third part cannot be empty with colon"
        );
        assert!(
            InstancedChildMoniker::parse("f::1").is_err(),
            "second part cannot be empty with colon"
        );
        assert!(
            InstancedChildMoniker::parse(":f:1").is_err(),
            "first part cannot be empty with colon"
        );
        assert!(
            InstancedChildMoniker::parse("f:f:1:1").is_err(),
            "more than three colons not allowed"
        );
        assert!(InstancedChildMoniker::parse("f:f").is_err(), "second part must be int");
        assert!(InstancedChildMoniker::parse("f:f:f").is_err(), "third part must be int");
        assert!(InstancedChildMoniker::parse("@:1").is_err(), "invalid character in name");
        assert!(InstancedChildMoniker::parse("@:f:1").is_err(), "invalid character in collection");
        assert!(
            InstancedChildMoniker::parse("f:@:1").is_err(),
            "invalid character in name with collection"
        );
        assert!(
            InstancedChildMoniker::parse(&format!("f:{}", "x".repeat(MAX_LONG_NAME_LENGTH + 1)))
                .is_err(),
            "name too long"
        );
        assert!(
            InstancedChildMoniker::parse(&format!("{}:x", "f".repeat(MAX_NAME_LENGTH + 1)))
                .is_err(),
            "collection too long"
        );
    }

    #[test]
    fn instanced_child_moniker_compare() {
        let a = InstancedChildMoniker::new("a".to_string(), None, 1);
        let a2 = InstancedChildMoniker::new("a".to_string(), None, 2);
        let aa = InstancedChildMoniker::new("a".to_string(), Some("a".to_string()), 1);
        let aa2 = InstancedChildMoniker::new("a".to_string(), Some("a".to_string()), 2);
        let ab = InstancedChildMoniker::new("a".to_string(), Some("b".to_string()), 1);
        let ba = InstancedChildMoniker::new("b".to_string(), Some("a".to_string()), 1);
        let bb = InstancedChildMoniker::new("b".to_string(), Some("b".to_string()), 1);
        let aa_same = InstancedChildMoniker::new("a".to_string(), Some("a".to_string()), 1);

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
