// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::MetricValue,
    anyhow::{anyhow, bail, Context, Error, Result},
    fuchsia_inspect_node_hierarchy::NodeHierarchy,
    lazy_static::lazy_static,
    regex::Regex,
    selectors,
    serde_derive::Deserialize,
    serde_json::{map::Map as JsonMap, Value as JsonValue},
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

pub struct KeyValueFetcher {
    pub map: JsonMap<String, JsonValue>,
}

impl TryFrom<&str> for KeyValueFetcher {
    type Error = anyhow::Error;

    fn try_from(json_text: &str) -> Result<Self, Self::Error> {
        let raw_json =
            json_text.parse::<JsonValue>().context("Couldn't parse KeyValue text as JSON.")?;
        match raw_json {
            JsonValue::Object(map) => Ok(KeyValueFetcher { map }),
            _ => bail!("Bad json KeyValue data needs to be Object (map)."),
        }
    }
}

impl TryFrom<&JsonMap<String, JsonValue>> for KeyValueFetcher {
    type Error = anyhow::Error;

    fn try_from(map: &JsonMap<String, JsonValue>) -> Result<Self, Self::Error> {
        // This doesn't fail today, but that's an implementation detail; don't count on it.
        Ok(KeyValueFetcher { map: map.clone() })
    }
}

lazy_static! {
    static ref EMPTY_KEY_VALUE_FETCHER: KeyValueFetcher = KeyValueFetcher { map: JsonMap::new() };
}

impl KeyValueFetcher {
    pub fn ref_empty() -> &'static Self {
        &EMPTY_KEY_VALUE_FETCHER
    }

    pub fn len(&self) -> usize {
        self.map.len()
    }

    pub fn fetch(&self, key: &str) -> MetricValue {
        match self.map.get(key) {
            Some(value) => MetricValue::from(value),
            None => MetricValue::Missing(format!("Key '{}' not found in annotations", key)),
        }
    }
}

pub struct TextFetcher {
    pub lines: Vec<String>,
}

impl From<&str> for TextFetcher {
    fn from(log_buffer: &str) -> Self {
        TextFetcher { lines: log_buffer.split('\n').map(|s| s.to_string()).collect::<Vec<_>>() }
    }
}

lazy_static! {
    static ref EMPTY_TEXT_FETCHER: TextFetcher = TextFetcher { lines: Vec::new() };
}

impl TextFetcher {
    pub fn ref_empty() -> &'static Self {
        &EMPTY_TEXT_FETCHER
    }

    pub fn contains(&self, pattern: &str) -> bool {
        let re = match Regex::new(pattern) {
            Ok(re) => re,
            _ => return false,
        };
        self.lines.iter().any(|s| re.is_match(s))
    }
}

pub struct InspectFetcher {
    pub components: Vec<ComponentInspectInfo>,
    pub component_errors: Vec<anyhow::Error>,
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
            let value = extract_json_value(component, "moniker").or_else(|_| {
                extract_json_value(component, "path")
                    .or_else(|_| bail!("Neither 'path' nor 'moniker' found in Inspect component"))
            })?;
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
                let raw_contents = extract_json_value(raw_component, "payload").or_else(|_| {
                    extract_json_value(raw_component, "contents").or_else(|_| {
                        bail!("Neither 'payload' nor 'contents' found in Inspect component")
                    })
                })?;
                let processed_data: NodeHierarchy = serde_json::from_value(raw_contents.clone())
                    .with_context(|| {
                        format!(
                            "Unable to deserialize Inspect contents for {} to node hierarchy",
                            path
                        )
                    })?;
                Ok(ComponentInspectInfo { moniker, processed_data })
            })
            .collect::<Vec<_>>();

        let mut component_errors = vec![];
        let components = components
            .into_iter()
            .filter_map(|v| match v {
                Ok(component) => Some(component),
                Err(e) => {
                    component_errors.push(e);
                    None
                }
            })
            .collect::<Vec<_>>();
        Ok(Self { components, component_errors })
    }
}

lazy_static! {
    static ref EMPTY_INSPECT_FETCHER: InspectFetcher =
        InspectFetcher { components: Vec::new(), component_errors: Vec::new() };
}

impl InspectFetcher {
    pub fn ref_empty() -> &'static Self {
        &EMPTY_INSPECT_FETCHER
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
        if !found_component {
            return Ok(vec![MetricValue::Missing(format!(
                "No component found matching selector {}",
                selector_string.to_string()
            ))]);
        }
        if values.is_empty() {
            return Ok(Vec::new());
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
    use {super::*, crate::assert_missing, anyhow::Error};

    #[test]
    fn inspect_fetcher_new_works() -> Result<(), Error> {
        assert!(InspectFetcher::try_from("foo").is_err(), "'foo' isn't valid JSON");
        assert!(InspectFetcher::try_from(r#"{"a":5}"#).is_err(), "Needed an array");
        assert!(InspectFetcher::try_from("[]").is_ok(), "A JSON array should have worked");
        Ok(())
    }

    #[test]
    fn test_fetch() -> Result<(), Error> {
        // This tests both the moniker/payload and path/content (old-style) Inspect formats.
        let json_options = vec![
            r#"[
        {"moniker":"asdf/foo/qwer",
         "payload":{"root":{"dataInt":5, "child":{"dataFloat":2.3}}}},
        {"moniker":"zxcv/bar/hjkl",
         "payload":{"base":{"dataInt":42, "array":[2,3,4], "yes": true}}},
        {"moniker":"fail_component",
         "payload": null}
        ]"#,
            r#"[
        {"moniker":"asdf/foo/qwer",
         "payload":{"root":{"dataInt":5, "child":{"dataFloat":2.3}}}},
        {"path":"hub/r/zxcv/1/r/bar/2/c/hjkl.cmx/1",
         "contents":{"base":{"dataInt":42, "array":[2,3,4], "yes": true}}},
        {"moniker":"fail_component",
         "payload": null}
        ]"#,
        ];

        for json in json_options.into_iter() {
            let inspect = InspectFetcher::try_from(json)?;
            assert_eq!(
                vec!["Unable to deserialize Inspect contents for fail_component to node hierarchy"],
                inspect.component_errors.iter().map(|e| format!("{}", e)).collect::<Vec<_>>()
            );
            macro_rules! assert_wrong {
                ($selector:expr, $error:expr) => {
                    let error = inspect.fetch_str($selector);
                    assert_eq!(error.len(), 1);
                    assert_missing!(&error[0], &$error);
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
                inspect.fetch_str("INSPECT:zxcv/*/hjk*:base:yes"),
                vec![MetricValue::Bool(true)]
            );
            assert_eq!(inspect.fetch_str("INSPECT:*/foo/*:root.dataInt"), vec![]);
            assert_wrong!(
                "INSPECT:*/fo/*:root.dataInt",
                "No component found matching selector */fo/*:root.dataInt"
            );
            assert_wrong!("INSPECT:*/foo/*:root:data:Int",
                "Fetch SelectorString { full_selector: \"INSPECT:*/foo/*:root:data:Int\", selector_type: Inspect, body: \"*/foo/*:root:data:Int\" } -> Selector format requires at least 2 subselectors delimited by a `:`.");
            assert_eq!(inspect.fetch_str("INSPECT:*/foo/*:root/kid:dataInt"), vec![]);
            assert_eq!(inspect.fetch_str("INSPECT:*/bar/*:base/array:dataInt"), vec![]);
            assert_eq!(
                inspect.fetch_str("INSPECT:*/bar/*:base:array"),
                vec![MetricValue::Vector(vec![
                    MetricValue::Int(2),
                    MetricValue::Int(3),
                    MetricValue::Int(4)
                ])]
            );
        }
        Ok(())
    }

    #[test]
    fn inspect_ref_empty() -> Result<(), Error> {
        // Make sure it doesn't crash, can be called multiple times and they both work right.
        let fetcher1 = InspectFetcher::ref_empty();
        let fetcher2 = InspectFetcher::ref_empty();
        match fetcher1.try_fetch("not a selector") {
            Err(_) => {}
            _ => bail!("Should not have accepted a bad selector"),
        }
        match fetcher2.try_fetch("not a selector") {
            Err(_) => {}
            _ => bail!("Should not have accepted a bad selector"),
        }
        match fetcher1.try_fetch("a:b:c").unwrap()[0] {
            MetricValue::Missing(_) => {}
            _ => bail!("Should have Missing'd a valid selector"),
        }
        match fetcher2.try_fetch("a:b:c").unwrap()[0] {
            MetricValue::Missing(_) => {}
            _ => bail!("Should have Missing'd a valid selector"),
        }
        Ok(())
    }

    #[test]
    fn text_fetcher_works() {
        let fetcher = TextFetcher::from("abcfoo\ndefgfoo");
        assert!(fetcher.contains("d*g"));
        assert!(fetcher.contains("foo"));
        assert!(!fetcher.contains("food"));
        // Make sure ref_empty() doesn't crash and can be used multiple times.
        let fetcher1 = TextFetcher::ref_empty();
        let fetcher2 = TextFetcher::ref_empty();
        assert!(!fetcher1.contains("a"));
        assert!(!fetcher2.contains("a"));
    }

    // KeyValueFetcher is tested in metrics::test::annotations_work()
}
