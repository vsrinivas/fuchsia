// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, ChildMoniker, ChildMonikerBase},
    std::{fmt, fmt::Display},
};

/// A representation of a component's position in the component topology. The last segment of
/// a component's `NodePath` is its `ChildMoniker` as designated by its parent component,
/// and the prefix is the parent component's `NodePath`.
#[derive(Clone, Debug, Default, Eq, Hash, PartialEq)]
pub struct NodePath(Vec<ChildMoniker>);

impl NodePath {
    pub fn new(monikers: Vec<ChildMoniker>) -> Self {
        let mut node_path = NodePath::default();
        node_path.0 = monikers;
        node_path
    }

    /// Construct NodePath from string references that correspond to parsable
    /// `ChildMoniker` instances.
    pub fn absolute_from_vec(vec: Vec<&str>) -> Self {
        let abs_moniker: AbsoluteMoniker = vec.into();
        Self::new(abs_moniker.path().clone())
    }

    /// Returns a new `NodePath` which extends `self` by appending `moniker` at the end of the path.
    pub fn extended(&self, moniker: ChildMoniker) -> Self {
        let mut node_path = NodePath::new(self.0.clone());
        node_path.0.push(moniker);
        node_path
    }

    /// Construct string references that correspond to underlying
    /// `ChildMoniker` instances.
    pub fn as_vec(&self) -> Vec<&str> {
        self.0.iter().map(|moniker| moniker.as_str()).collect()
    }
}

impl Display for NodePath {
    // Displays a `NodePath` as a slash-separated path.
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.0.is_empty() {
            return write!(f, "/");
        }
        let mut path_string = "".to_owned();
        for moniker in self.0.iter() {
            path_string.push('/');
            path_string.push_str(moniker.as_str());
        }
        write!(f, "{}", path_string)
    }
}

impl From<AbsoluteMoniker> for NodePath {
    fn from(moniker: AbsoluteMoniker) -> Self {
        Self::new(moniker.path().clone())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // Tests the `extended()` and `to_string()` methods of `NodePath`.
    #[test]
    fn node_path_operations() {
        let empty_node_path = NodePath::default();
        assert_eq!(empty_node_path.to_string(), "/");

        let foo_moniker = ChildMoniker::new("foo", None);
        let foo_node_path = empty_node_path.extended(foo_moniker);
        assert_eq!(foo_node_path.to_string(), "/foo");

        let bar_moniker = ChildMoniker::new("bar", None);
        let bar_node_path = foo_node_path.extended(bar_moniker);
        assert_eq!(bar_node_path.to_string(), "/foo/bar");
    }
}
