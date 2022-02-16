// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
use {
    crate::{
        abs_moniker::AbsoluteMonikerBase,
        child_moniker::{ChildMoniker, ChildMonikerBase},
        error::MonikerError,
    },
    std::{convert::TryFrom, fmt},
};

pub trait RelativeMonikerBase: Sized {
    type Part: ChildMonikerBase;

    fn new(up_path: Vec<Self::Part>, down_path: Vec<Self::Part>) -> Self;

    fn up_path(&self) -> &Vec<Self::Part>;

    fn up_path_mut(&mut self) -> &mut Vec<Self::Part>;

    fn down_path(&self) -> &Vec<Self::Part>;

    fn down_path_mut(&mut self) -> &mut Vec<Self::Part>;

    fn from_absolute<S: AbsoluteMonikerBase<Part = Self::Part>>(from: &S, to: &S) -> Self {
        let mut from_path = from.path().iter().peekable();
        let mut to_path = to.path().iter().peekable();

        while from_path.peek().is_some() && from_path.peek() == to_path.peek() {
            from_path.next();
            to_path.next();
        }

        let mut res = Self::new(from_path.cloned().collect(), to_path.cloned().collect());
        res.up_path_mut().reverse();
        res
    }

    fn is_self(&self) -> bool {
        self.up_path().is_empty() && self.down_path().is_empty()
    }

    fn parse_up_down_paths(rep: &str) -> Result<(Vec<&str>, Vec<&str>), MonikerError> {
        if rep.chars().nth(0) != Some('.') {
            return Err(MonikerError::invalid_moniker(rep));
        }
        let stripped_input = rep.strip_prefix(".").unwrap();

        let mut up_vs_down = stripped_input.splitn(2, '/');
        let set_one = up_vs_down.next().unwrap();
        let set_two = up_vs_down.next();

        let up_string = set_one.strip_prefix("\\").unwrap_or(set_one);
        let down_string = set_two.unwrap_or("");

        if down_string.contains("\\") {
            return Err(MonikerError::invalid_moniker(rep));
        }

        let up_path: Vec<&str> = match up_string {
            "" => vec![],
            _ => up_string.split("\\").collect(),
        };

        let down_path: Vec<&str>;
        if down_string == "" {
            down_path = vec![];
        } else {
            down_path = down_string.split("/").collect()
        }

        Ok((up_path, down_path))
    }

    /// Parses n `RelativeMoniker` from a string.
    ///
    /// Input strings should be of the format
    /// `.(\<name>(:<collection>)?)*(/<name>(:<collection>)?)*`, such
    /// as `.\foo/bar/baz` or `./biz:foo`.
    fn parse(rep: &str) -> Result<Self, MonikerError> {
        let (up_path, down_path) = Self::parse_up_down_paths(rep)?;

        let up_path = up_path
            .iter()
            .map(Self::Part::parse)
            .collect::<Result<Vec<Self::Part>, MonikerError>>()?;
        let down_path = down_path
            .iter()
            .map(Self::Part::parse)
            .collect::<Result<Vec<Self::Part>, MonikerError>>()?;

        Ok(Self::new(up_path, down_path))
    }
}
#[derive(Debug, Eq, PartialEq, Clone, Hash, Default)]
pub struct RelativeMoniker {
    up_path: Vec<ChildMoniker>,
    down_path: Vec<ChildMoniker>,
}

impl RelativeMonikerBase for RelativeMoniker {
    type Part = ChildMoniker;

    fn new(up_path: Vec<Self::Part>, down_path: Vec<Self::Part>) -> Self {
        Self { up_path, down_path }
    }

    fn up_path(&self) -> &Vec<Self::Part> {
        &self.up_path
    }

    fn up_path_mut(&mut self) -> &mut Vec<Self::Part> {
        &mut self.up_path
    }

    fn down_path(&self) -> &Vec<Self::Part> {
        &self.down_path
    }

    fn down_path_mut(&mut self) -> &mut Vec<Self::Part> {
        &mut self.down_path
    }
}

impl fmt::Display for RelativeMoniker {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, ".")?;
        for segment in self.up_path() {
            write!(f, "\\{}", segment)?
        }
        for segment in self.down_path() {
            write!(f, "/{}", segment)?
        }
        Ok(())
    }
}

impl TryFrom<&str> for RelativeMoniker {
    type Error = MonikerError;

    fn try_from(input: &str) -> Result<Self, MonikerError> {
        Self::parse(input)
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::abs_moniker::AbsoluteMoniker};
    #[test]
    fn relative_monikers_from_absolute() {
        let me = RelativeMoniker::from_absolute::<AbsoluteMoniker>(&vec![].into(), &vec![].into());
        assert_eq!(true, me.is_self());
        assert_eq!(".", format!("{}", me));

        let me = RelativeMoniker::from_absolute::<AbsoluteMoniker>(
            &vec!["a:1", "b:2", "c:3"].into(),
            &vec!["a:1", "b:2", "c:3"].into(),
        );

        assert_eq!(true, me.is_self());
        assert_eq!(".", format!("{}", me));

        let ancestor = RelativeMoniker::from_absolute::<AbsoluteMoniker>(
            &vec!["a:1", "b:2"].into(),
            &vec![].into(),
        );
        assert_eq!(false, ancestor.is_self());
        assert_eq!(".\\b:2\\a:1", format!("{}", ancestor));

        let ancestor = RelativeMoniker::from_absolute::<AbsoluteMoniker>(
            &vec!["a:1", "b:2", "c:3", "d:4"].into(),
            &vec!["a:1", "b:2"].into(),
        );
        assert_eq!(false, ancestor.is_self());
        assert_eq!(".\\d:4\\c:3", format!("{}", ancestor));

        let descendant = RelativeMoniker::from_absolute::<AbsoluteMoniker>(
            &vec![].into(),
            &vec!["a:1", "b:2"].into(),
        );
        assert_eq!(false, descendant.is_self());
        assert_eq!("./a:1/b:2", format!("{}", descendant));

        let descendant = RelativeMoniker::from_absolute::<AbsoluteMoniker>(
            &vec!["a:1", "b:2"].into(),
            &vec!["a:1", "b:2", "c:3", "d:4"].into(),
        );
        assert_eq!(false, descendant.is_self());
        assert_eq!("./c:3/d:4", format!("{}", descendant));

        let sibling = RelativeMoniker::from_absolute::<AbsoluteMoniker>(
            &vec!["a:1"].into(),
            &vec!["b:2"].into(),
        );
        assert_eq!(false, sibling.is_self());
        assert_eq!(".\\a:1/b:2", format!("{}", sibling));

        let sibling = RelativeMoniker::from_absolute::<AbsoluteMoniker>(
            &vec!["c:3", "a:1"].into(),
            &vec!["c:3", "b:2"].into(),
        );
        assert_eq!(false, sibling.is_self());
        assert_eq!(".\\a:1/b:2", format!("{}", sibling));

        let cousin = RelativeMoniker::from_absolute::<AbsoluteMoniker>(
            &vec!["a0:1", "a:1"].into(),
            &vec!["b0:2", "b:2"].into(),
        );
        assert_eq!(false, cousin.is_self());
        assert_eq!(".\\a:1\\a0:1/b0:2/b:2", format!("{}", cousin));

        let cousin = RelativeMoniker::from_absolute::<AbsoluteMoniker>(
            &vec!["c:3", "d:4", "a0:1", "a:1"].into(),
            &vec!["c:3", "d:4", "b0:2", "b:2"].into(),
        );
        assert_eq!(false, cousin.is_self());
        assert_eq!(".\\a:1\\a0:1/b0:2/b:2", format!("{}", cousin));
    }
}
