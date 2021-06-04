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
    crate::{abs_moniker::AbsoluteMoniker, child_moniker::ChildMoniker, error::MonikerError},
    std::{convert::TryFrom, fmt, iter},
};

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

    pub fn to_string_without_instances(&self) -> String {
        let mut res = ".".to_string();
        for (segment, leading_char) in self
            .up_path
            .iter()
            .zip(iter::repeat("\\"))
            .chain(self.down_path.iter().zip(iter::repeat("/")))
        {
            res.push_str(leading_char);
            res.push_str(segment.name());
            if let Some(collection) = segment.collection() {
                res.push_str(":");
                res.push_str(collection);
            }
        }
        res
    }

    /// Parses n `RelativeMoniker` from a string.
    ///
    /// Input strings should be of the format
    /// `.(\<name>(:<collection>)?:<instance_id>)*(/<name>(:<collection>)?:<instance_id>)*`, such
    /// as `.\foo:42/bar:12/baz:54` or `./biz:foo:42`.
    fn parse(rep: &str) -> Result<Self, MonikerError> {
        if rep.chars().nth(0) != Some('.') {
            return Err(MonikerError::invalid_moniker(rep));
        }
        let stripped_input = rep.strip_prefix(".").unwrap();

        let mut up_vs_down = stripped_input.splitn(2, '/');
        let set_one = up_vs_down.next().unwrap();
        let set_two = up_vs_down.next();

        let up_string = set_one.strip_prefix("\\").unwrap_or(set_one);
        let down_string = set_two.unwrap_or("");

        if up_string == "" && down_string == "" {
            return Ok(Self::new(vec![], vec![]));
        }

        if down_string.contains("\\") {
            return Err(MonikerError::invalid_moniker(rep));
        }

        let up_path;
        if up_string == "" {
            up_path = vec![];
        } else {
            up_path = up_string
                .split("\\")
                .map(ChildMoniker::parse)
                .collect::<Result<Vec<ChildMoniker>, MonikerError>>()?;
        }

        let down_path;
        if down_string == "" {
            down_path = vec![];
        } else {
            down_path = down_string
                .split("/")
                .map(ChildMoniker::parse)
                .collect::<Result<Vec<ChildMoniker>, MonikerError>>()?;
        }
        Ok(RelativeMoniker { up_path, down_path })
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

impl TryFrom<&str> for RelativeMoniker {
    type Error = MonikerError;

    fn try_from(input: &str) -> Result<Self, MonikerError> {
        RelativeMoniker::parse(input)
    }
}

#[cfg(test)]
mod tests {
    use {super::*, std::convert::TryInto};

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

    #[test]
    fn absolute_moniker_from_relative_success() {
        // This test assumes the following topology:
        //        a
        //     b     c
        // d

        // ====
        // Test cases where relative moniker has up path *and* down path
        let ac = AbsoluteMoniker::parse_string_without_instances("/a/c").unwrap();
        let abd = AbsoluteMoniker::parse_string_without_instances("/a/b/d").unwrap();
        let c_to_d = RelativeMoniker::from_absolute(&ac, &abd);
        assert_eq!(abd, AbsoluteMoniker::from_relative(&ac, &c_to_d).unwrap());
        // Test the opposite direction
        let d_to_c = RelativeMoniker::from_absolute(&abd, &ac);
        assert_eq!(ac, AbsoluteMoniker::from_relative(&abd, &d_to_c).unwrap());

        // ===
        // Test case where relative moniker has only up path
        let ab = AbsoluteMoniker::parse_string_without_instances("/a/b").unwrap();
        let d_to_b = RelativeMoniker::from_absolute(&abd, &ab);
        assert_eq!(ab, AbsoluteMoniker::from_relative(&abd, &d_to_b).unwrap());

        // ===
        // Test case where relative moniker has only down path
        let b_to_d = RelativeMoniker::from_absolute(&ab, &abd);
        assert_eq!(abd, AbsoluteMoniker::from_relative(&ab, &b_to_d).unwrap());
    }

    #[test]
    fn absolute_moniker_from_relative_failure() {
        // This test assumes the following topology:
        //        a
        //     b     c
        //  d

        // Absolute moniker does not point to the right path
        let a = AbsoluteMoniker::parse_string_without_instances("/a").unwrap();
        let ab = AbsoluteMoniker::parse_string_without_instances("/a/b").unwrap();
        let ac = AbsoluteMoniker::parse_string_without_instances("/a/c").unwrap();
        let abd = AbsoluteMoniker::parse_string_without_instances("/a/b/d").unwrap();

        let d_to_c = RelativeMoniker::from_absolute(&abd, &ac);
        // error: `d_to_c`'s up_path is longer than `a`'s path
        assert!(d_to_c.up_path().len() > a.path().len());
        assert!(matches!(
            AbsoluteMoniker::from_relative(&a, &d_to_c),
            Err(MonikerError::InvalidMoniker { rep: _ })
        ));

        let b_to_a = RelativeMoniker::from_absolute(&ab, &a);
        // error: `b_to_a`'s up_path is the same length as `a`'s path, but up_path doesn't overlap with `a`'s path
        assert!(b_to_a.up_path().len() == a.path().len());
        assert!(matches!(
            AbsoluteMoniker::from_relative(&a, &b_to_a),
            Err(MonikerError::InvalidMoniker { rep: _ })
        ));
    }

    #[test]
    fn relative_monikers_parse() {
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
                .map(|s| ChildMoniker::parse(s).unwrap())
                .collect::<Vec<ChildMoniker>>();
            let down_path = down_path
                .into_iter()
                .map(|s| ChildMoniker::parse(s).unwrap())
                .collect::<Vec<ChildMoniker>>();
            assert_eq!(
                RelativeMoniker::new(up_path, down_path),
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
            let res: Result<RelativeMoniker, MonikerError> = invalid_string_to_parse.try_into();
            assert!(
                res.is_err(),
                "didn't expect to correctly parse this: {:?}",
                invalid_string_to_parse
            );
        }
    }
}
