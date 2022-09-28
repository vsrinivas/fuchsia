// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::MonikerError,
    cm_types::{LongName, Name},
    core::cmp::{Ord, Ordering},
    std::fmt,
};

pub trait ChildMonikerBase: Eq + PartialOrd + Clone + fmt::Display {
    fn parse<T: AsRef<str>>(rep: T) -> Result<Self, MonikerError>
    where
        Self: Sized;

    fn name(&self) -> &str;

    fn collection(&self) -> Option<&str>;

    fn as_str(&self) -> &str;
}

/// An child moniker locally identifies a child component instance using the name assigned by
/// its parent and its collection (if present). It is a building block for more complex monikers.
///
/// The child moniker does not distinguish between instances.
///
/// Display notation: "name[:collection]".
#[derive(Eq, PartialEq, Debug, Clone, Hash)]
pub struct ChildMoniker {
    pub name: LongName,
    pub collection: Option<Name>,
    rep: String,
}

impl ChildMonikerBase for ChildMoniker {
    /// Parses a `ChildMoniker` from a string.
    ///
    /// Input strings should be of the format `<name>(:<collection>)?`, e.g. `foo` or `biz:foo`.
    fn parse<T: AsRef<str>>(rep: T) -> Result<Self, MonikerError> {
        let rep = rep.as_ref();
        let parts: Vec<&str> = rep.split(":").collect();
        let (coll, name) = match parts.len() {
            1 => (None, parts[0]),
            2 => (Some(parts[0]), parts[1]),
            _ => return Err(MonikerError::invalid_moniker(rep)),
        };
        ChildMoniker::try_new(name, coll)
    }

    fn name(&self) -> &str {
        self.name.as_str()
    }

    fn collection(&self) -> Option<&str> {
        self.collection.as_ref().map(|c| c.as_str())
    }

    fn as_str(&self) -> &str {
        &self.rep
    }
}

impl ChildMoniker {
    pub fn try_new<S>(name: S, collection: Option<S>) -> Result<Self, MonikerError>
    where
        S: Into<String>,
    {
        let name = LongName::try_new(name)?;
        let (collection, rep) = match collection {
            Some(coll) => {
                let coll_name = Name::try_new(coll)?;
                let rep = format!("{}:{}", coll_name, name);
                (Some(coll_name), rep)
            }
            None => (None, name.to_string()),
        };
        Ok(Self { name, collection, rep })
    }
}

impl From<&str> for ChildMoniker {
    fn from(rep: &str) -> Self {
        ChildMoniker::parse(rep).expect(&format!("child moniker failed to parse: {}", rep))
    }
}

impl Ord for ChildMoniker {
    fn cmp(&self, other: &Self) -> Ordering {
        (&self.collection, &self.name).cmp(&(&other.collection, &other.name))
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
    use {
        super::*,
        cm_types::{MAX_LONG_NAME_LENGTH, MAX_NAME_LENGTH},
    };

    #[test]
    fn child_monikers() {
        let m = ChildMoniker::try_new("test", None).unwrap();
        assert_eq!("test", m.name());
        assert_eq!(None, m.collection());
        assert_eq!("test", m.as_str());
        assert_eq!("test", format!("{}", m));
        assert_eq!(m, ChildMoniker::from("test"));

        let m = ChildMoniker::try_new("test", Some("coll")).unwrap();
        assert_eq!("test", m.name());
        assert_eq!(Some("coll"), m.collection());
        assert_eq!("coll:test", m.as_str());
        assert_eq!("coll:test", format!("{}", m));
        assert_eq!(m, ChildMoniker::from("coll:test"));

        let max_coll_length_part = "f".repeat(MAX_NAME_LENGTH);
        let max_name_length_part = "f".repeat(MAX_LONG_NAME_LENGTH);
        let max_moniker_length = format!("{}:{}", max_coll_length_part, max_name_length_part);
        let m = ChildMoniker::parse(max_moniker_length).expect("valid moniker");
        assert_eq!(&max_name_length_part, m.name());
        assert_eq!(Some(max_coll_length_part.as_str()), m.collection());

        assert!(ChildMoniker::parse("").is_err(), "cannot be empty");
        assert!(ChildMoniker::parse(":").is_err(), "cannot be empty with colon");
        assert!(ChildMoniker::parse("f:").is_err(), "second part cannot be empty with colon");
        assert!(ChildMoniker::parse(":f").is_err(), "first part cannot be empty with colon");
        assert!(ChildMoniker::parse("f:f:f").is_err(), "multiple colons not allowed");
        assert!(ChildMoniker::parse("@").is_err(), "invalid character in name");
        assert!(ChildMoniker::parse("@:f").is_err(), "invalid character in collection");
        assert!(ChildMoniker::parse("f:@").is_err(), "invalid character in name with collection");
        assert!(
            ChildMoniker::parse(&format!("f:{}", "x".repeat(MAX_LONG_NAME_LENGTH + 1))).is_err(),
            "name too long"
        );
        assert!(
            ChildMoniker::parse(&format!("{}:x", "f".repeat(MAX_NAME_LENGTH + 1))).is_err(),
            "collection too long"
        );
    }

    #[test]
    fn child_moniker_compare() {
        let a = ChildMoniker::try_new("a", None).unwrap();
        let aa = ChildMoniker::try_new("a", Some("a")).unwrap();
        let ab = ChildMoniker::try_new("a", Some("b")).unwrap();
        let ba = ChildMoniker::try_new("b", Some("a")).unwrap();
        let bb = ChildMoniker::try_new("b", Some("b")).unwrap();
        let aa_same = ChildMoniker::try_new("a", Some("a")).unwrap();

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
}
