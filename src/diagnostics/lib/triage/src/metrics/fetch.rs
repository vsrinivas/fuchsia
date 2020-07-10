// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::MetricValue,
    anyhow::{anyhow, bail, Context, Error, Result},
    fuchsia_inspect_node_hierarchy::NodeHierarchy,
    selectors,
    serde_derive::Deserialize,
    serde_json::Value as JsonValue,
    std::{convert::TryFrom, str::FromStr, sync::Arc},
};

/// Selector type used to determine how to query target file.
#[derive(Deserialize, Debug, Clone, PartialEq)]
pub enum SelectorType {
    /// Selector for Inspect Tree ("inspect.json" files).
    Inspect,
}

impl FromStr for SelectorType {
    type Err = anyhow::Error;
    fn from_str(selector_type: &str) -> Result<Self, Self::Err> {
        match selector_type {
            "INSPECT" => Ok(SelectorType::Inspect),
            incorrect => bail!("Invalid selector type '{}' - must be INSPECT", incorrect),
        }
    }
}

#[derive(Deserialize, Debug, Clone, PartialEq)]
pub struct SelectorString {
    full_selector: String,
    pub selector_type: SelectorType,
    body: String,
}

impl SelectorString {
    pub fn body(&self) -> &str {
        &self.body
    }

    /// Return true if the incoming data should be parsed as a selector. Otherwise, return false.
    ///
    /// The only strings that should be parsed as a selector today are those starting with
    /// "INSPECT:"
    ///
    /// This only validates that the incoming string should be parsed as a selector, not that it
    /// will actually parse without errors.
    pub fn is_selector(s: &str) -> bool {
        // Check that there is at least one ':' delimited segment, and that it parses as a selector
        // type.
        let mut split = s.splitn(1, ':');

        match (split.next(), split.next()) {
            (Some(v), Some(_)) => SelectorType::from_str(v).is_ok(),
            _ => false,
        }
    }
}

impl TryFrom<String> for SelectorString {
    type Error = anyhow::Error;

    fn try_from(full_selector: String) -> Result<Self, Self::Error> {
        let mut string_parts = full_selector.splitn(2, ':');
        let selector_type =
            SelectorType::from_str(string_parts.next().ok_or(anyhow!("Empty selector"))?)?;
        let body = string_parts.next().ok_or(anyhow!("Selector needs a :"))?.to_owned();
        Ok(SelectorString { full_selector, selector_type, body })
    }
}

pub struct ComponentInspectInfo {
    processed_data: NodeHierarchy,
    moniker: Vec<String>,
}

pub struct InspectFetcher {
    pub components: Vec<ComponentInspectInfo>,
}

impl TryFrom<&str> for InspectFetcher {
    type Error = anyhow::Error;

    fn try_from(json_text: &str) -> Result<Self, Self::Error> {
        let raw_json =
            json_text.parse::<JsonValue>().context("Couldn't parse Inspect text as JSON.")?;
        match raw_json {
            JsonValue::Array(list) => Self::try_from(list),
            _ => bail!("Bad json inspect data needs to be array."),
        }
    }
}

impl TryFrom<Vec<JsonValue>> for InspectFetcher {
    type Error = anyhow::Error;

    fn try_from(component_vec: Vec<JsonValue>) -> Result<Self, Self::Error> {
        fn extract_json_value<'a>(
            component: &'a JsonValue,
            key: &'_ str,
        ) -> Result<&'a JsonValue, Error> {
            component.get(key).ok_or_else(|| anyhow!("'{}' not found in Inspect component", key))
        }

        fn path_from(component: &JsonValue) -> Result<String, Error> {
            let value = match extract_json_value(component, "moniker") {
                Err(_) => extract_json_value(component, "path"),
                v => v,
            }?;
            Ok(value
                .as_str()
                .ok_or_else(|| anyhow!("Inspect component path wasn't a valid string"))?
                .to_owned())
        }

        fn moniker_from(path_string: &String) -> Result<Vec<String>, Error> {
            selectors::parse_path_to_moniker(path_string)
                .context("Path string needs to be a moniker")
        }

        let components: Vec<_> = component_vec
            .iter()
            .map(|raw_component| {
                let path = path_from(raw_component)?;
                let moniker = moniker_from(&path)?;
                let raw_contents = match extract_json_value(raw_component, "payload") {
                    Err(_) => extract_json_value(raw_component, "contents"),
                    v => v,
                }?;
                let processed_data: NodeHierarchy = serde_json::from_value(raw_contents.clone())
                    .with_context(|| {
                        format!(
                            "Unable to deserialize Inspect contents for {} to node hierarchy",
                            path
                        )
                    })?;
                Ok(ComponentInspectInfo { moniker, processed_data })
            })
            .collect::<Result<Vec<_>, Error>>()?;
        Ok(Self { components })
    }
}

impl InspectFetcher {
    #[cfg(test)]
    pub fn new_empty() -> Self {
        Self { components: Vec::new() }
    }

    fn try_fetch(&self, selector_string: &str) -> Result<Vec<MetricValue>, Error> {
        let arc_selector = Arc::new(selectors::parse_selector(selector_string)?);
        let mut values = Vec::new();
        let mut found_component = false;
        for component in self.components.iter() {
            if !selectors::match_component_moniker_against_selector(
                &component.moniker,
                &arc_selector,
            )? {
                continue;
            }
            found_component = true;
            let selector = selectors::parse_selector(selector_string)?;
            for value in fuchsia_inspect_node_hierarchy::select_from_node_hierarchy(
                component.processed_data.clone(),
                selector,
            )?
            .into_iter()
            {
                values.push(value)
            }
        }
        if !found_component || values.is_empty() {
            return Ok(vec![MetricValue::Missing(format!(
                "No value found matching selector {}",
                selector_string.to_string()
            ))]);
        }
        Ok(values.into_iter().map(|value| MetricValue::from(value.property)).collect())
    }

    pub fn fetch(&self, selector: &SelectorString) -> Vec<MetricValue> {
        match self.try_fetch(selector.body()) {
            Ok(v) => v,
            Err(e) => vec![MetricValue::Missing(format!("Fetch {:?} -> {}", selector, e))],
        }
    }

    #[cfg(test)]
    fn fetch_str(&self, selector_str: &str) -> Vec<MetricValue> {
        match SelectorString::try_from(selector_str.to_owned()) {
            Ok(selector) => self.fetch(&selector),
            Err(e) => vec![MetricValue::Missing(format!("Bad selector {}: {}", selector_str, e))],
        }
    }
}

#[cfg(test)]
mod test {
    use {super::*, anyhow::Error};

    #[test]
    fn inspect_fetcher_new_works() -> Result<(), Error> {
        assert!(InspectFetcher::try_from("foo").is_err(), "'foo' isn't valid JSON");
        assert!(InspectFetcher::try_from(r#"{"a":5}"#).is_err(), "Needed an array");
        assert!(InspectFetcher::try_from("[]").is_ok(), "A JSON array should have worked");
        Ok(())
    }

    #[test]
    fn test_fetch() -> Result<(), Error> {
        let json_options = vec![
            r#"[
        {"path":"asdf/foo/qwer",
         "contents":{"root":{"dataInt":5, "child":{"dataFloat":2.3}}}},
        {"path":"zxcv/bar/hjkl",
         "contents":{"base":{"dataInt":42, "array":[2,3,4], "yes": true}}}
        ]"#,
            r#"[
        {"moniker":"asdf/foo/qwer",
         "payload":{"root":{"dataInt":5, "child":{"dataFloat":2.3}}}},
        {"moniker":"zxcv/bar/hjkl",
         "payload":{"base":{"dataInt":42, "array":[2,3,4], "yes": true}}}
        ]"#,
        ];

        for json in json_options.into_iter() {
            let inspect = InspectFetcher::try_from(json)?;
            macro_rules! assert_wrong {
                ($selector:expr, $error:expr) => {
                    assert_eq!(
                        inspect.fetch_str($selector),
                        vec![MetricValue::Missing($error.to_string())]
                    )
                };
            }
            assert_wrong!("INSPET:*/foo/*:root:dataInt", "Bad selector INSPET:*/foo/*:root:dataInt: Invalid selector type \'INSPET\' - must be INSPECT");
            assert_eq!(
                inspect.fetch_str("INSPECT:*/foo/*:root:dataInt"),
                vec![MetricValue::Int(5)]
            );
            assert_eq!(
                inspect.fetch_str("INSPECT:*/foo/*:root/child:dataFloat"),
                vec![MetricValue::Float(2.3)]
            );
            assert_eq!(
                inspect.fetch_str("INSPECT:*/bar/*:base:yes"),
                vec![MetricValue::Bool(true)]
            );
            assert_wrong!(
                "INSPECT:*/foo/*:root.dataInt",
                "No value found matching selector */foo/*:root.dataInt"
            );
            assert_wrong!(
                "INSPECT:*/fo/*:root.dataInt",
                "No value found matching selector */fo/*:root.dataInt"
            );
            assert_wrong!("INSPECT:*/foo/*:root:data:Int", "Fetch SelectorString { full_selector: \"INSPECT:*/foo/*:root:data:Int\", selector_type: Inspect, body: \"*/foo/*:root:data:Int\" } -> Selector format requires at least 2 subselectors delimited by a `:`.");
            assert_wrong!(
                "INSPECT:*/foo/*:root/kid:dataInt",
                "No value found matching selector */foo/*:root/kid:dataInt"
            );
            assert_wrong!(
                "INSPECT:*/bar/*:base/array:dataInt",
                "No value found matching selector */bar/*:base/array:dataInt"
            );
            assert_wrong!("INSPECT:*/bar/*:base:array", "Arrays not supported yet");
        }
        Ok(())
    }
}
