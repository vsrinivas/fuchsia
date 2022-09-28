// Copyright 2022 The Fuchsia Authors. All rights reserved.
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
    crate::instanced_child_moniker::InstancedChildMoniker,
    moniker::{ChildMoniker, ChildMonikerBase, MonikerError, RelativeMoniker, RelativeMonikerBase},
    std::{convert::TryFrom, fmt},
};

#[derive(Debug, Eq, PartialEq, Clone, Hash, Default)]
pub struct InstancedRelativeMoniker {
    up_path: Vec<InstancedChildMoniker>,
    down_path: Vec<InstancedChildMoniker>,
}

impl InstancedRelativeMoniker {
    /// Create and allocate a `RelativeMoniker`, without instance ids
    /// from this instanced moniker
    pub fn without_instance_ids(&self) -> RelativeMoniker {
        let up_path: Vec<ChildMoniker> =
            self.up_path().iter().map(|c| c.without_instance_id()).collect();
        let down_path: Vec<ChildMoniker> =
            self.down_path().iter().map(|c| c.without_instance_id()).collect();
        RelativeMoniker::new(up_path, down_path)
    }

    /// Transforms an `InstancedRelativeMoniker` into a representation where all dynamic children
    /// have `0` value instance ids.
    pub fn with_zero_value_instance_ids(&self) -> InstancedRelativeMoniker {
        let up_path = self
            .up_path()
            .iter()
            .map(|c| InstancedChildMoniker::new(c.name(), c.collection(), 0))
            .collect();
        let down_path = self
            .down_path()
            .iter()
            .map(|c| InstancedChildMoniker::new(c.name(), c.collection(), 0))
            .collect();
        InstancedRelativeMoniker::new(up_path, down_path)
    }
}

impl RelativeMonikerBase for InstancedRelativeMoniker {
    type Part = InstancedChildMoniker;

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

impl fmt::Display for InstancedRelativeMoniker {
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

impl TryFrom<&str> for InstancedRelativeMoniker {
    type Error = MonikerError;

    fn try_from(input: &str) -> Result<Self, MonikerError> {
        Self::parse(input)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::instanced_abs_moniker::InstancedAbsoluteMoniker,
        moniker::{AbsoluteMonikerBase, ChildMonikerBase, MonikerError},
        std::convert::TryInto,
    };

    #[test]
    fn instanced_relative_monikers() {
        let me = InstancedRelativeMoniker::new(vec![], vec![]);
        assert_eq!(true, me.is_self());
        assert_eq!(".", format!("{}", me));

        let ancestor = InstancedRelativeMoniker::new(
            vec![
                InstancedChildMoniker::new("a", None, 1),
                InstancedChildMoniker::new("b", None, 2),
            ],
            vec![],
        );
        assert_eq!(false, ancestor.is_self());
        assert_eq!(".\\a:1\\b:2", format!("{}", ancestor));

        let descendant = InstancedRelativeMoniker::new(
            vec![],
            vec![
                InstancedChildMoniker::new("a", None, 1),
                InstancedChildMoniker::new("b", None, 2),
            ],
        );
        assert_eq!(false, descendant.is_self());
        assert_eq!("./a:1/b:2", format!("{}", descendant));

        let sibling = InstancedRelativeMoniker::new(
            vec![InstancedChildMoniker::new("a", None, 1)],
            vec![InstancedChildMoniker::new("b", None, 2)],
        );
        assert_eq!(false, sibling.is_self());
        assert_eq!(".\\a:1/b:2", format!("{}", sibling));

        let cousin = InstancedRelativeMoniker::new(
            vec![
                InstancedChildMoniker::new("a", None, 1),
                InstancedChildMoniker::new("a0", None, 1),
            ],
            vec![
                InstancedChildMoniker::new("b0", None, 2),
                InstancedChildMoniker::new("b", None, 2),
            ],
        );
        assert_eq!(false, cousin.is_self());
        assert_eq!(".\\a:1\\a0:1/b0:2/b:2", format!("{}", cousin));
    }

    #[test]
    fn instanced_relative_monikers_from_absolute() {
        let me = InstancedRelativeMoniker::from_absolute::<InstancedAbsoluteMoniker>(
            &vec![].into(),
            &vec![].into(),
        );
        assert_eq!(true, me.is_self());
        assert_eq!(".", format!("{}", me));

        let me = InstancedRelativeMoniker::from_absolute::<InstancedAbsoluteMoniker>(
            &vec!["a:1", "b:2", "c:3"].into(),
            &vec!["a:1", "b:2", "c:3"].into(),
        );
        assert_eq!(true, me.is_self());
        assert_eq!(".", format!("{}", me));

        let ancestor = InstancedRelativeMoniker::from_absolute::<InstancedAbsoluteMoniker>(
            &vec!["a:1", "b:2"].into(),
            &vec![].into(),
        );
        assert_eq!(false, ancestor.is_self());
        assert_eq!(".\\b:2\\a:1", format!("{}", ancestor));

        let ancestor = InstancedRelativeMoniker::from_absolute::<InstancedAbsoluteMoniker>(
            &vec!["a:1", "b:2", "c:3", "d:4"].into(),
            &vec!["a:1", "b:2"].into(),
        );
        assert_eq!(false, ancestor.is_self());
        assert_eq!(".\\d:4\\c:3", format!("{}", ancestor));

        let descendant = InstancedRelativeMoniker::from_absolute::<InstancedAbsoluteMoniker>(
            &vec![].into(),
            &vec!["a:1", "b:2"].into(),
        );
        assert_eq!(false, descendant.is_self());
        assert_eq!("./a:1/b:2", format!("{}", descendant));

        let descendant = InstancedRelativeMoniker::from_absolute::<InstancedAbsoluteMoniker>(
            &vec!["a:1", "b:2"].into(),
            &vec!["a:1", "b:2", "c:3", "d:4"].into(),
        );
        assert_eq!(false, descendant.is_self());
        assert_eq!("./c:3/d:4", format!("{}", descendant));

        let sibling = InstancedRelativeMoniker::from_absolute::<InstancedAbsoluteMoniker>(
            &vec!["a:1"].into(),
            &vec!["b:2"].into(),
        );
        assert_eq!(false, sibling.is_self());
        assert_eq!(".\\a:1/b:2", format!("{}", sibling));

        let sibling = InstancedRelativeMoniker::from_absolute::<InstancedAbsoluteMoniker>(
            &vec!["c:3", "a:1"].into(),
            &vec!["c:3", "b:2"].into(),
        );
        assert_eq!(false, sibling.is_self());
        assert_eq!(".\\a:1/b:2", format!("{}", sibling));

        let cousin = InstancedRelativeMoniker::from_absolute::<InstancedAbsoluteMoniker>(
            &vec!["a0:1", "a:1"].into(),
            &vec!["b0:2", "b:2"].into(),
        );
        assert_eq!(false, cousin.is_self());
        assert_eq!(".\\a:1\\a0:1/b0:2/b:2", format!("{}", cousin));

        let cousin = InstancedRelativeMoniker::from_absolute::<InstancedAbsoluteMoniker>(
            &vec!["c:3", "d:4", "a0:1", "a:1"].into(),
            &vec!["c:3", "d:4", "b0:2", "b:2"].into(),
        );
        assert_eq!(false, cousin.is_self());
        assert_eq!(".\\a:1\\a0:1/b0:2/b:2", format!("{}", cousin));
    }
    #[test]
    fn instanced_absolute_moniker_from_relative_success() {
        // This test assumes the following topology:
        //        a
        //     b     c
        // d

        // ====
        // Test cases where relative moniker has up path *and* down path
        let ac = InstancedAbsoluteMoniker::parse_str("/a:0/c:0").unwrap();
        let abd = InstancedAbsoluteMoniker::parse_str("/a:0/b:0/d:0").unwrap();
        let c_to_d = InstancedRelativeMoniker::from_absolute(&ac, &abd);
        assert_eq!(abd, InstancedAbsoluteMoniker::from_relative(&ac, &c_to_d).unwrap());
        // Test the opposite direction
        let d_to_c = InstancedRelativeMoniker::from_absolute(&abd, &ac);
        assert_eq!(ac, InstancedAbsoluteMoniker::from_relative(&abd, &d_to_c).unwrap());

        // ===
        // Test case where relative moniker has only up path
        let ab = InstancedAbsoluteMoniker::parse_str("/a:0/b:0").unwrap();
        let d_to_b = InstancedRelativeMoniker::from_absolute(&abd, &ab);
        assert_eq!(ab, InstancedAbsoluteMoniker::from_relative(&abd, &d_to_b).unwrap());

        // ===
        // Test case where relative moniker has only down path
        let b_to_d = InstancedRelativeMoniker::from_absolute(&ab, &abd);
        assert_eq!(abd, InstancedAbsoluteMoniker::from_relative(&ab, &b_to_d).unwrap());
    }

    #[test]
    fn instanced_absolute_moniker_from_relative_failure() {
        // This test assumes the following topology:
        //        a
        //     b     c
        //  d

        // InstancedAbsolute moniker does not point to the right path
        let a = InstancedAbsoluteMoniker::parse_str("/a:0").unwrap();
        let ab = InstancedAbsoluteMoniker::parse_str("/a:0/b:0").unwrap();
        let ac = InstancedAbsoluteMoniker::parse_str("/a:0/c:0").unwrap();
        let abd = InstancedAbsoluteMoniker::parse_str("/a:0/b:0/d:0").unwrap();

        let d_to_c = InstancedRelativeMoniker::from_absolute(&abd, &ac);
        // error: `d_to_c`'s up_path is longer than `a`'s path
        assert!(d_to_c.up_path().len() > a.path().len());
        assert!(matches!(
            InstancedAbsoluteMoniker::from_relative(&a, &d_to_c),
            Err(MonikerError::InvalidMoniker { rep: _ })
        ));

        let b_to_a = InstancedRelativeMoniker::from_absolute(&ab, &a);
        // error: `b_to_a`'s up_path is the same length as `a`'s path, but up_path doesn't overlap with `a`'s path
        assert!(b_to_a.up_path().len() == a.path().len());
        assert!(matches!(
            InstancedAbsoluteMoniker::from_relative(&a, &b_to_a),
            Err(MonikerError::InvalidMoniker { rep: _ })
        ));
    }

    #[test]
    fn instanced_relative_monikers_parse() {
        for (up_path, down_path, string_to_parse) in vec![
            (vec![], vec![], "."),
            (vec!["a:0"], vec![], ".\\a:0"),
            (vec!["a:0", "b:1"], vec![], ".\\a:0\\b:1"),
            (vec!["a:0"], vec!["b:1"], ".\\a:0/b:1"),
            (vec!["a:0", "b:1"], vec!["c:2"], ".\\a:0\\b:1/c:2"),
            (vec!["a:0", "b:1"], vec!["c:2", "d:3"], ".\\a:0\\b:1/c:2/d:3"),
            (vec!["a:0"], vec!["b:1", "c:2"], ".\\a:0/b:1/c:2"),
            (vec![], vec!["a:0", "b:1"], "./a:0/b:1"),
            (vec![], vec!["a:0"], "./a:0"),
        ] {
            let up_path = up_path
                .into_iter()
                .map(|s| InstancedChildMoniker::parse(s).unwrap())
                .collect::<Vec<InstancedChildMoniker>>();
            let down_path = down_path
                .into_iter()
                .map(|s| InstancedChildMoniker::parse(s).unwrap())
                .collect::<Vec<InstancedChildMoniker>>();
            assert_eq!(
                InstancedRelativeMoniker::new(up_path, down_path),
                string_to_parse.try_into().unwrap()
            );
        }

        for invalid_string_to_parse in vec![
            ".\\missing/instance/ids",
            ".\\only:0/one:1/is:2/missing/an:4/id:5",
            ".\\up:0/then-down:1\\then-up-again:2",
            ".\\\\double-leading-slash-up:0",
            ".//double-leading-slash-down:0",
            "doesnt:0\\start:1\\with:2/a:3/dot:4",
            "..//double:0/dot:0/oh:0/my:0",
            ".\\internal:1/../double:2/dot:3",
            ".\\internal:1/./single:2/dot:3",
            "./negative-instance-id:-1",
        ] {
            let res: Result<InstancedRelativeMoniker, MonikerError> =
                invalid_string_to_parse.try_into();
            assert!(
                res.is_err(),
                "didn't expect to correctly parse this: {:?}",
                invalid_string_to_parse
            );
        }
    }
}
