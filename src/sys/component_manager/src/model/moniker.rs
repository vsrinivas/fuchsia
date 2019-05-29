// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;

/// A child moniker locally identifies a child component instance using the name assigned by
/// its parent.  It is a building block for more complex monikers.
///
/// Display notation: "#name".
///
/// TODO: Add a mechanism for representing children grouped into collections by index.
#[derive(Eq, PartialEq, Debug, Clone, Hash)]
pub struct ChildMoniker {
    name: String,
}

impl ChildMoniker {
    pub fn new(name: String) -> ChildMoniker {
        assert!(!name.is_empty());
        ChildMoniker { name }
    }

    pub fn name(&self) -> &str {
        &self.name
    }
}

impl From<&str> for ChildMoniker {
    fn from(name: &str) -> Self {
        ChildMoniker::new(name.to_string())
    }
}

impl From<String> for ChildMoniker {
    fn from(name: String) -> Self {
        ChildMoniker::new(name)
    }
}

impl fmt::Display for ChildMoniker {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "#{}", self.name)
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
/// Display notation: "/", "/name1", "/name1/name2", ...
#[derive(Eq, PartialEq, Debug, Clone, Hash)]
pub struct AbsoluteMoniker {
    path: Vec<ChildMoniker>,
}

impl AbsoluteMoniker {
    pub fn new<F>(path: Vec<F>) -> AbsoluteMoniker
    where
        F: Into<ChildMoniker>,
    {
        AbsoluteMoniker { path: path.into_iter().map(|x| x.into()).collect() }
    }

    pub fn path(&self) -> &Vec<ChildMoniker> {
        &self.path
    }

    pub fn root() -> AbsoluteMoniker {
        AbsoluteMoniker { path: vec![] }
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

impl fmt::Display for AbsoluteMoniker {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        if self.path.is_empty() {
            write!(f, "/")?;
        } else {
            for segment in &self.path {
                write!(f, "/{}", segment.name())?
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
/// strong guarantee that relative monikers are only meaningful when interpreted within isomorphic
/// component instance subtrees.  (Compare with relative filesystem path notations which use
/// ".." to perform upwards traversal and offer correspondingly weaker guarantees.)
///
/// For example, if two sibling component instances named "A" and "B" both possess relative
/// monikers for another component instance named "C", then A's moniker for C and B's moniker
/// for C will be distinct.
///
/// Display notation: ".", "./down1", ".\up1/down1", ".\up1\up2/down1", ...
#[derive(Eq, PartialEq, Debug)]
pub struct RelativeMoniker {
    up_path: Vec<ChildMoniker>,
    down_path: Vec<ChildMoniker>,
}

impl RelativeMoniker {
    pub fn new(up_path: Vec<ChildMoniker>, down_path: Vec<ChildMoniker>) -> RelativeMoniker {
        RelativeMoniker { up_path, down_path }
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
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, ".")?;
        for segment in &self.up_path {
            write!(f, "\\{}", segment.name())?
        }
        for segment in &self.down_path {
            write!(f, "/{}", segment.name())?
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn child_monikers() {
        let m = ChildMoniker::new("test".to_string());
        assert_eq!("test", m.name());
        assert_eq!("#test", format!("{}", m));
    }

    #[test]
    fn absolute_monikers() {
        let root = AbsoluteMoniker::root();
        assert_eq!(true, root.is_root());
        assert_eq!("/", format!("{}", root));

        let leaf = AbsoluteMoniker::new(vec![
            ChildMoniker::new("a".to_string()),
            ChildMoniker::new("b".to_string()),
        ]);
        assert_eq!(false, leaf.is_root());
        assert_eq!("/a/b", format!("{}", leaf));
    }

    #[test]
    fn absolute_moniker_parent() {
        let root = AbsoluteMoniker::root();
        assert_eq!(true, root.is_root());
        assert_eq!(None, root.parent());

        let leaf = AbsoluteMoniker::new(vec![
            ChildMoniker::new("a".to_string()),
            ChildMoniker::new("b".to_string()),
        ]);
        assert_eq!("/a/b", format!("{}", leaf));
        assert_eq!("/a", format!("{}", leaf.parent().unwrap()));
        assert_eq!("/", format!("{}", leaf.parent().unwrap().parent().unwrap()));
        assert_eq!(None, leaf.parent().unwrap().parent().unwrap().parent());
    }

    #[test]
    fn relative_monikers() {
        let me = RelativeMoniker::new(vec![], vec![]);
        assert_eq!(true, me.is_self());
        assert_eq!(".", format!("{}", me));

        let ancestor = RelativeMoniker::new(
            vec![ChildMoniker::new("a".to_string()), ChildMoniker::new("b".to_string())],
            vec![],
        );
        assert_eq!(false, ancestor.is_self());
        assert_eq!(".\\a\\b", format!("{}", ancestor));

        let descendant = RelativeMoniker::new(
            vec![],
            vec![ChildMoniker::new("a".to_string()), ChildMoniker::new("b".to_string())],
        );
        assert_eq!(false, descendant.is_self());
        assert_eq!("./a/b", format!("{}", descendant));

        let sibling = RelativeMoniker::new(
            vec![ChildMoniker::new("a".to_string())],
            vec![ChildMoniker::new("b".to_string())],
        );
        assert_eq!(false, sibling.is_self());
        assert_eq!(".\\a/b", format!("{}", sibling));

        let cousin = RelativeMoniker::new(
            vec![ChildMoniker::new("a".to_string()), ChildMoniker::new("a0".to_string())],
            vec![ChildMoniker::new("b0".to_string()), ChildMoniker::new("b".to_string())],
        );
        assert_eq!(false, cousin.is_self());
        assert_eq!(".\\a\\a0/b0/b", format!("{}", cousin));
    }
}
