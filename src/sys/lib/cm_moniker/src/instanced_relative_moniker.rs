// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// A relative moniker describes the identity of a component instance in terms of its path
/// relative to an (unspecified) parent component in the component instance tree.
/// In other words, relative monikers describe a sequence of downward parent-to-child traversals.
///
/// A self-referenced relative moniker is a moniker with an empty path.
///
/// Instanced relative monikers are only used internally within the component manager.  Externally,
/// components are referenced by encoded relative moniker so as to minimize the amount of
/// information which is disclosed about the overall structure of the component instance tree.
///
/// Display notation: ".", "./down1", "./down1/down2", ...
use {
    crate::instanced_child_moniker::InstancedChildMoniker,
    moniker::{ChildMoniker, ChildMonikerBase, MonikerError, RelativeMoniker, RelativeMonikerBase},
    std::{convert::TryFrom, fmt},
};

#[derive(Debug, Eq, PartialEq, Clone, Hash, Default)]
pub struct InstancedRelativeMoniker {
    path: Vec<InstancedChildMoniker>,
}

impl InstancedRelativeMoniker {
    /// Create and allocate a `RelativeMoniker`, without instance ids
    /// from this instanced moniker
    pub fn without_instance_ids(&self) -> RelativeMoniker {
        let path: Vec<ChildMoniker> = self.path().iter().map(|c| c.without_instance_id()).collect();
        RelativeMoniker::new(path)
    }

    /// Transforms an `InstancedRelativeMoniker` into a representation where all dynamic children
    /// have `0` value instance ids.
    pub fn with_zero_value_instance_ids(&self) -> InstancedRelativeMoniker {
        let path = self
            .path()
            .iter()
            .map(|c| {
                InstancedChildMoniker::try_new(c.name(), c.collection(), 0)
                    .expect("down path moniker is guaranteed to be valid")
            })
            .collect();
        InstancedRelativeMoniker::new(path)
    }
}

impl RelativeMonikerBase for InstancedRelativeMoniker {
    type Part = InstancedChildMoniker;

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

impl fmt::Display for InstancedRelativeMoniker {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.format(f)
    }
}

impl TryFrom<&str> for InstancedRelativeMoniker {
    type Error = MonikerError;

    fn try_from(input: &str) -> Result<Self, MonikerError> {
        Self::parse_str(input)
    }
}

impl TryFrom<Vec<&str>> for InstancedRelativeMoniker {
    type Error = MonikerError;

    fn try_from(rep: Vec<&str>) -> Result<Self, MonikerError> {
        Self::parse(&rep)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::instanced_abs_moniker::InstancedAbsoluteMoniker,
        moniker::{ChildMonikerBase, MonikerError},
        std::convert::TryInto,
    };

    #[test]
    fn instanced_relative_monikers() {
        let me = InstancedRelativeMoniker::new(vec![]);
        assert_eq!(true, me.is_self());
        assert_eq!(".", format!("{}", me));

        let descendant = InstancedRelativeMoniker::new(vec![
            InstancedChildMoniker::try_new("a", None, 1).unwrap(),
            InstancedChildMoniker::try_new("b", None, 2).unwrap(),
        ]);
        assert_eq!(false, descendant.is_self());
        assert_eq!("./a:1/b:2", format!("{}", descendant));
    }

    #[test]
    fn instanced_relative_monikers_scope_down() {
        let me = InstancedRelativeMoniker::scope_down::<InstancedAbsoluteMoniker>(
            &vec![].into(),
            &vec![].into(),
        )
        .unwrap();
        assert_eq!(true, me.is_self());
        assert_eq!(".", format!("{}", me));

        let me = InstancedRelativeMoniker::scope_down::<InstancedAbsoluteMoniker>(
            &vec!["a:1", "b:2", "c:3"].into(),
            &vec!["a:1", "b:2", "c:3"].into(),
        )
        .unwrap();
        assert_eq!(true, me.is_self());
        assert_eq!(".", format!("{}", me));

        InstancedRelativeMoniker::scope_down::<InstancedAbsoluteMoniker>(
            &vec!["a:1", "b:2"].into(),
            &vec![].into(),
        )
        .unwrap_err();

        InstancedRelativeMoniker::scope_down::<InstancedAbsoluteMoniker>(
            &vec!["a:1", "b:2", "c:3", "d:4"].into(),
            &vec!["a:1", "b:2"].into(),
        )
        .unwrap_err();

        let descendant = InstancedRelativeMoniker::scope_down::<InstancedAbsoluteMoniker>(
            &vec![].into(),
            &vec!["a:1", "b:2"].into(),
        )
        .unwrap();
        assert_eq!(false, descendant.is_self());
        assert_eq!("./a:1/b:2", format!("{}", descendant));

        let descendant = InstancedRelativeMoniker::scope_down::<InstancedAbsoluteMoniker>(
            &vec!["a:1", "b:2"].into(),
            &vec!["a:1", "b:2", "c:3", "d:4"].into(),
        )
        .unwrap();
        assert_eq!(false, descendant.is_self());
        assert_eq!("./c:3/d:4", format!("{}", descendant));

        InstancedRelativeMoniker::scope_down::<InstancedAbsoluteMoniker>(
            &vec!["a:1"].into(),
            &vec!["b:2"].into(),
        )
        .unwrap_err();

        InstancedRelativeMoniker::scope_down::<InstancedAbsoluteMoniker>(
            &vec!["c:3", "a:1"].into(),
            &vec!["c:3", "b:2"].into(),
        )
        .unwrap_err();

        InstancedRelativeMoniker::scope_down::<InstancedAbsoluteMoniker>(
            &vec!["a0:1", "a:1"].into(),
            &vec!["b0:2", "b:2"].into(),
        )
        .unwrap_err();

        InstancedRelativeMoniker::scope_down::<InstancedAbsoluteMoniker>(
            &vec!["c:3", "d:4", "a0:1", "a:1"].into(),
            &vec!["c:3", "d:4", "b0:2", "b:2"].into(),
        )
        .unwrap_err();
    }

    #[test]
    fn instanced_relative_monikers_parse() {
        for (path, string_to_parse) in vec![
            (vec![], "."),
            (vec![], "./"),
            (vec!["a:1"], "./a:1"),
            (vec!["a:1", "b:2"], "./a:1/b:2"),
            (vec!["a:test:1"], "./a:test:1"),
            (vec!["a:test:1", "b:test:2"], "./a:test:1/b:test:2"),
        ] {
            let path = path
                .into_iter()
                .map(|s| InstancedChildMoniker::parse(s).unwrap())
                .collect::<Vec<InstancedChildMoniker>>();
            assert_eq!(InstancedRelativeMoniker::new(path), string_to_parse.try_into().unwrap());
        }

        for invalid_string_to_parse in
            vec!["/", "\\", ".\\", "/test:1", ".test:1", ".//", "./missing:instance-id"]
        {
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
