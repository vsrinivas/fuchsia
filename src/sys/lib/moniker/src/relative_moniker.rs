// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// A relative moniker describes the identity of a component instance in terms of its path
/// relative to an (unspecified) parent component in the component instance tree.
/// In other words, relative monikers describe a sequence of downward parent-to-child traversals.
///
/// A self-referenced relative moniker is a moniker with an empty path.
///
/// Display notation: ".", "./down1", "./down1/down2", ...
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

    fn new(part: Vec<Self::Part>) -> Self;

    fn parse(path: &Vec<&str>) -> Result<Self, MonikerError> {
        let path: Result<Vec<Self::Part>, MonikerError> =
            path.iter().map(|x| Self::Part::parse(x)).collect();
        Ok(Self::new(path?))
    }

    fn parse_str(input: &str) -> Result<Self, MonikerError> {
        if input == "." || input == "./" {
            return Ok(Self::new(vec![]));
        }
        if input.chars().nth(0) != Some('.') {
            return Err(MonikerError::invalid_moniker(input));
        }
        if input.chars().nth(1) != Some('/') {
            return Err(MonikerError::invalid_moniker(input));
        }

        let path =
            input[2..].split('/').map(Self::Part::parse).collect::<Result<_, MonikerError>>()?;

        Ok(Self::new(path))
    }

    // Creates a relative moniker from a parent's scope to a child instance.
    // The parent scope must contain the child, otherwise an error is returned.
    fn scope_down<T: AbsoluteMonikerBase<Part = Self::Part>>(
        parent_scope: &T,
        child: &T,
    ) -> Result<Self, MonikerError> {
        if !parent_scope.contains_in_realm(child) {
            return Err(MonikerError::ParentDoesNotContainChild {
                parent: parent_scope.to_string(),
                child: child.to_string(),
            });
        }

        let parent_len = parent_scope.path().len();
        let mut children = child.path().clone();
        children.drain(0..parent_len);
        Ok(Self::new(children))
    }

    fn path(&self) -> &Vec<Self::Part>;

    fn path_mut(&mut self) -> &mut Vec<Self::Part>;

    fn leaf(&self) -> Option<&Self::Part> {
        self.path().last()
    }

    fn is_self(&self) -> bool {
        self.path().is_empty()
    }

    fn parent(&self) -> Option<Self> {
        if self.is_self() {
            None
        } else {
            let l = self.path().len() - 1;
            Some(Self::new(self.path()[..l].to_vec()))
        }
    }

    fn child(&self, child: Self::Part) -> Self {
        let mut path = self.path().clone();
        path.push(child);
        Self::new(path)
    }

    fn format(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, ".")?;
        for segment in self.path() {
            write!(f, "/{}", segment)?
        }
        Ok(())
    }
}

#[derive(Debug, Eq, PartialEq, Clone, Hash, Default)]
pub struct RelativeMoniker {
    path: Vec<ChildMoniker>,
}

impl RelativeMonikerBase for RelativeMoniker {
    type Part = ChildMoniker;

    fn new(path: Vec<Self::Part>) -> Self {
        Self { path }
    }

    fn path(&self) -> &Vec<Self::Part> {
        &self.path
    }

    fn path_mut(&mut self) -> &mut Vec<Self::Part> {
        &mut self.path
    }
}

impl fmt::Display for RelativeMoniker {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.format(f)
    }
}

impl TryFrom<Vec<&str>> for RelativeMoniker {
    type Error = MonikerError;

    fn try_from(rep: Vec<&str>) -> Result<Self, MonikerError> {
        Self::parse(&rep)
    }
}

impl TryFrom<&str> for RelativeMoniker {
    type Error = MonikerError;

    fn try_from(input: &str) -> Result<Self, MonikerError> {
        Self::parse_str(input)
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::abs_moniker::AbsoluteMoniker};

    #[test]
    fn instanced_relative_monikers() {
        let me = RelativeMoniker::new(vec![]);
        assert_eq!(true, me.is_self());
        assert_eq!(".", format!("{}", me));

        let descendant = RelativeMoniker::new(vec![
            ChildMoniker::try_new("a", None).unwrap(),
            ChildMoniker::try_new("b", None).unwrap(),
        ]);
        assert_eq!(false, descendant.is_self());
        assert_eq!("./a/b", format!("{}", descendant));
    }

    #[test]
    fn relative_monikers_scope_down() {
        let me =
            RelativeMoniker::scope_down::<AbsoluteMoniker>(&vec![].into(), &vec![].into()).unwrap();
        assert_eq!(true, me.is_self());
        assert_eq!(".", format!("{}", me));

        let me = RelativeMoniker::scope_down::<AbsoluteMoniker>(
            &vec!["a:test1", "b:test2", "c:test3"].into(),
            &vec!["a:test1", "b:test2", "c:test3"].into(),
        )
        .unwrap();
        assert_eq!(true, me.is_self());
        assert_eq!(".", format!("{}", me));

        RelativeMoniker::scope_down::<AbsoluteMoniker>(
            &vec!["a:test1", "b:test2"].into(),
            &vec![].into(),
        )
        .unwrap_err();

        RelativeMoniker::scope_down::<AbsoluteMoniker>(
            &vec!["a:test1", "b:test2", "c:test3", "d:test4"].into(),
            &vec!["a:test1", "b:test2"].into(),
        )
        .unwrap_err();

        let descendant = RelativeMoniker::scope_down::<AbsoluteMoniker>(
            &vec![].into(),
            &vec!["a:test1", "b:test2"].into(),
        )
        .unwrap();
        assert_eq!(false, descendant.is_self());
        assert_eq!("./a:test1/b:test2", format!("{}", descendant));

        let descendant = RelativeMoniker::scope_down::<AbsoluteMoniker>(
            &vec!["a:test1", "b:test2"].into(),
            &vec!["a:test1", "b:test2", "c:test3", "d:test4"].into(),
        )
        .unwrap();
        assert_eq!(false, descendant.is_self());
        assert_eq!("./c:test3/d:test4", format!("{}", descendant));

        RelativeMoniker::scope_down::<AbsoluteMoniker>(
            &vec!["a:test1"].into(),
            &vec!["b:test2"].into(),
        )
        .unwrap_err();

        RelativeMoniker::scope_down::<AbsoluteMoniker>(
            &vec!["c:test3", "a:test1"].into(),
            &vec!["c:test3", "b:test2"].into(),
        )
        .unwrap_err();

        RelativeMoniker::scope_down::<AbsoluteMoniker>(
            &vec!["a0:test1", "a:test1"].into(),
            &vec!["b0:test2", "b:test2"].into(),
        )
        .unwrap_err();

        RelativeMoniker::scope_down::<AbsoluteMoniker>(
            &vec!["c:test3", "d:test4", "a0:test1", "a:test1"].into(),
            &vec!["c:test3", "d:test4", "b0:test2", "b:test2"].into(),
        )
        .unwrap_err();
    }

    #[test]
    fn relative_monikers_parse() {
        for (path, string_to_parse) in vec![
            (vec![], "."),
            (vec![], "./"),
            (vec!["a"], "./a"),
            (vec!["a", "b"], "./a/b"),
            (vec!["a:test1"], "./a:test1"),
            (vec!["a:test1", "b:test2"], "./a:test1/b:test2"),
        ] {
            let path = path
                .into_iter()
                .map(|s| ChildMoniker::parse(s).unwrap())
                .collect::<Vec<ChildMoniker>>();
            assert_eq!(RelativeMoniker::new(path), string_to_parse.try_into().unwrap());
        }

        for invalid_string_to_parse in
            vec!["/", "\\", ".\\", "/test", ".test", ".//", "./no:instance-id:test1"]
        {
            let res: Result<RelativeMoniker, MonikerError> = invalid_string_to_parse.try_into();
            assert!(
                res.is_err(),
                "didn't expect to correctly parse this: {:?}",
                invalid_string_to_parse
            );
        }
    }

    #[test]
    fn descendant_scope_down() {
        let scope_root: AbsoluteMoniker = vec!["a:test1", "b:test2"].into();
        let scope_child = vec!["a:test1", "b:test2", "c:test3", "d:test4"].into();

        let relative =
            RelativeMoniker::scope_down::<AbsoluteMoniker>(&scope_root, &scope_child).unwrap();
        assert_eq!(false, relative.is_self());
        assert_eq!("./c:test3/d:test4", format!("{}", relative));

        assert_eq!(scope_root.descendant(&relative), scope_child);
    }
}
