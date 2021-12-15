// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_diagnostics::StringSelector,
    fuchsia_inspect as inspect,
    selectors::{self, FastError},
    serde::{de::Unexpected, Deserialize, Deserializer},
    std::sync::Arc,
};

// SelectorList and StringList are adapted from SelectorEntry in
// src/diagnostics/lib/triage/src/config.rs

/// A selector entry in the configuration file is either a single string
/// or a vector of string selectors. Either case is converted to a vector
/// with at least one element.
///
/// Each element is optional so selectors can be removed when they're
/// known not to be needed. If one selector matches data, the others are
/// removed. After an upload_once is uploaded, all selectors are removed.
/// On initial parse, all elements will be Some<_>.
#[derive(Clone, Debug, PartialEq)]
pub struct SelectorList(pub Vec<Option<ParsedSelector>>);

impl std::ops::Deref for SelectorList {
    type Target = Vec<Option<ParsedSelector>>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl std::ops::DerefMut for SelectorList {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

impl IntoIterator for SelectorList {
    type Item = Option<ParsedSelector>;
    type IntoIter = std::vec::IntoIter<Self::Item>;

    fn into_iter(self) -> Self::IntoIter {
        self.0.into_iter()
    }
}

/// ParsedSelector stores the information Sampler needs to use the selector.
// TODO(fxbug.dev/87709) - this could be more memory-efficient by using slices into the string.
#[derive(Clone, Debug, PartialEq)]
pub struct ParsedSelector {
    /// The original string, needed to initialize the ArchiveAccessor
    pub selector_string: String,
    /// The parsed selector, needed to fetch the value out of the returned hierarchy
    pub selector: fidl_fuchsia_diagnostics::Selector,
    /// The moniker of the selector, to find which hierarchy may contain the data
    pub moniker: String,
    /// How many times this selector has found and uploaded data
    pub upload_count: Arc<inspect::UintProperty>,
}

pub(crate) fn parse_selector<E>(selector_str: &str) -> Result<ParsedSelector, E>
where
    E: serde::de::Error,
{
    let selector = selectors::parse_selector::<FastError>(selector_str)
        .or(Err(E::invalid_value(Unexpected::Str(selector_str), &"need a valid selector")))?;
    let component_selector = selector.component_selector.as_ref().ok_or(E::invalid_value(
        Unexpected::Str(selector_str),
        &"selector must specify component",
    ))?;
    let moniker_segments = component_selector.moniker_segments.as_ref().ok_or(E::invalid_value(
        Unexpected::Str(selector_str),
        &"selector must specify component",
    ))?;
    let moniker_strings = moniker_segments
        .iter()
        .map(|segment| match segment {
            StringSelector::StringPattern(_) => Err(E::invalid_value(
                Unexpected::Str(selector_str),
                &"component monikers cannot contain wildcards",
            )),
            StringSelector::ExactMatch(text) => Ok(text),
            _ => Err(E::invalid_value(Unexpected::Str(selector_str), &"Unexpected moniker type")),
        })
        .collect::<Result<Vec<_>, _>>()?;
    let moniker = moniker_strings.iter().map(|s| s.as_str()).collect::<Vec<_>>().join("/");
    Ok(ParsedSelector {
        selector,
        selector_string: selector_str.to_string(),
        moniker,
        upload_count: Arc::new(inspect::UintProperty::default()),
    })
}

impl<'de> Deserialize<'de> for SelectorList {
    fn deserialize<D>(d: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        struct SelectorVec(std::marker::PhantomData<Vec<Option<ParsedSelector>>>);

        impl<'de> serde::de::Visitor<'de> for SelectorVec {
            type Value = Vec<Option<ParsedSelector>>;

            fn expecting(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
                f.write_str("either a single selector or an array of selectors")
            }

            fn visit_str<E>(self, value: &str) -> Result<Self::Value, E>
            where
                E: serde::de::Error,
            {
                Ok(vec![Some(parse_selector::<E>(value)?)])
            }

            fn visit_seq<A>(self, mut value: A) -> Result<Self::Value, A::Error>
            where
                A: serde::de::SeqAccess<'de>,
            {
                let mut out = vec![];
                while let Some(s) = value.next_element::<String>()? {
                    out.push(Some(parse_selector::<A::Error>(&s)?));
                }
                if out.is_empty() {
                    use serde::de::Error;
                    Err(A::Error::invalid_length(0, &"expected at least one selector"))
                } else {
                    Ok(out)
                }
            }
        }

        Ok(SelectorList(d.deserialize_any(SelectorVec(std::marker::PhantomData))?))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use anyhow::Error;
    use fidl_fuchsia_diagnostics::{StringSelector, TreeSelector};

    fn require_string(data: &StringSelector, required: &str) {
        match data {
            StringSelector::ExactMatch(string) => assert_eq!(string, required),
            _ => assert!(false, "Expected an exact match"),
        }
    }

    fn require_strings(data: &Vec<StringSelector>, required: Vec<&str>) {
        assert_eq!(data.len(), required.len());
        for (data, required) in data.iter().zip(required.iter()) {
            require_string(data, required);
        }
    }

    #[fuchsia::test]
    fn parse_valid_single_selector() -> Result<(), Error> {
        let json = "\"core/foo:root/branch:leaf\"";
        let selectors: SelectorList = serde_json5::from_str(json)?;
        assert_eq!(selectors.len(), 1);
        let ParsedSelector { selector_string, selector, moniker, .. } =
            selectors[0].as_ref().unwrap();
        assert_eq!(selector_string, "core/foo:root/branch:leaf");
        assert_eq!(moniker, "core/foo");
        let moniker =
            selector.component_selector.as_ref().unwrap().moniker_segments.as_ref().unwrap();
        require_strings(moniker, vec!["core", "foo"]);
        match &selector.tree_selector {
            Some(TreeSelector::PropertySelector(selector)) => {
                require_strings(&selector.node_path, vec!["root", "branch"]);
                require_string(&selector.target_properties, "leaf");
            }
            _ => assert!(false, "Expected a property selector"),
        }
        Ok(())
    }

    #[fuchsia::test]
    fn parse_valid_multiple_selectors() -> Result<(), Error> {
        let json = "[ \"core/foo:root/branch:leaf\", \"core/bar:root/twig:leaf\"]";
        let selectors: SelectorList = serde_json5::from_str(json)?;
        assert_eq!(selectors.len(), 2);
        let ParsedSelector { selector_string, selector, moniker, .. } =
            selectors[0].as_ref().unwrap();
        assert_eq!(selector_string, "core/foo:root/branch:leaf");
        assert_eq!(moniker, "core/foo");
        let moniker =
            selector.component_selector.as_ref().unwrap().moniker_segments.as_ref().unwrap();
        require_strings(moniker, vec!["core", "foo"]);
        match &selector.tree_selector {
            Some(TreeSelector::PropertySelector(selector)) => {
                require_strings(&selector.node_path, vec!["root", "branch"]);
                require_string(&selector.target_properties, "leaf");
            }
            _ => assert!(false, "Expected a property selector"),
        }
        let ParsedSelector { selector_string, selector, moniker, .. } =
            selectors[1].as_ref().unwrap();
        assert_eq!(selector_string, "core/bar:root/twig:leaf");
        assert_eq!(moniker, "core/bar");
        let moniker =
            selector.component_selector.as_ref().unwrap().moniker_segments.as_ref().unwrap();
        require_strings(moniker, vec!["core", "bar"]);
        match &selector.tree_selector {
            Some(TreeSelector::PropertySelector(selector)) => {
                require_strings(&selector.node_path, vec!["root", "twig"]);
                require_string(&selector.target_properties, "leaf");
            }
            _ => assert!(false, "Expected a property selector"),
        }
        Ok(())
    }

    #[fuchsia::test]
    fn refuse_invalid_selectors() {
        let bad_selector = "\"core/foo:wrong:root/branch:leaf\"";
        let not_string = "42";
        let bad_list = "[ \"core/foo:root/branch:leaf\", \"core/bar:wrong:root/twig:leaf\"]";
        serde_json5::from_str::<SelectorList>(bad_selector).expect_err("this should fail");
        serde_json5::from_str::<SelectorList>(not_string).expect_err("this should fail");
        serde_json5::from_str::<SelectorList>(bad_list).expect_err("this should fail");
    }
}
