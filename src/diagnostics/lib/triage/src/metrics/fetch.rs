// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::MetricValue,
    crate::config::{DataFetcher, DiagnosticData, Source},
    anyhow::{anyhow, bail, Context, Error, Result},
    diagnostics_hierarchy::DiagnosticsHierarchy,
    lazy_static::lazy_static,
    regex::Regex,
    selectors,
    serde_derive::Deserialize,
    serde_json::{map::Map as JsonMap, Value as JsonValue},
    std::{collections::HashMap, convert::TryFrom, str::FromStr, sync::Arc},
};

/// [Fetcher] is a source of values to feed into the calculations. It may contain data either
/// from snapshot.zip files (e.g. inspect.json data that can be accessed via "select" entries)
/// or supplied in the specification of a trial.
pub enum Fetcher<'a> {
    FileData(FileDataFetcher<'a>),
    TrialData(TrialDataFetcher<'a>),
}

/// [FileDataFetcher] contains fetchers for data in snapshot.zip files.
#[derive(Clone)]
pub struct FileDataFetcher<'a> {
    pub inspect: &'a InspectFetcher,
    pub syslog: &'a TextFetcher,
    pub klog: &'a TextFetcher,
    pub bootlog: &'a TextFetcher,
    pub annotations: &'a KeyValueFetcher,
}

impl<'a> FileDataFetcher<'a> {
    pub fn new(data: &'a Vec<DiagnosticData>) -> FileDataFetcher<'a> {
        let mut fetcher = FileDataFetcher {
            inspect: InspectFetcher::ref_empty(),
            syslog: TextFetcher::ref_empty(),
            klog: TextFetcher::ref_empty(),
            bootlog: TextFetcher::ref_empty(),
            annotations: KeyValueFetcher::ref_empty(),
        };
        for DiagnosticData { source, data, .. } in data.iter() {
            match source {
                Source::Inspect => {
                    if let DataFetcher::Inspect(data) = data {
                        fetcher.inspect = data;
                    }
                }
                Source::Syslog => {
                    if let DataFetcher::Text(data) = data {
                        fetcher.syslog = data;
                    }
                }
                Source::Klog => {
                    if let DataFetcher::Text(data) = data {
                        fetcher.klog = data;
                    }
                }
                Source::Bootlog => {
                    if let DataFetcher::Text(data) = data {
                        fetcher.bootlog = data;
                    }
                }
                Source::Annotations => {
                    if let DataFetcher::KeyValue(data) = data {
                        fetcher.annotations = data;
                    }
                }
            }
        }
        fetcher
    }

    pub(crate) fn fetch(&self, selector: &SelectorString) -> MetricValue {
        match selector.selector_type {
            // Selectors return a vector. Non-wildcarded Inspect selectors will return a
            // single element, except in the case of multiple components with the same
            // entry in the "moniker" field, where multiple matches are possible.
            SelectorType::Inspect => MetricValue::Vector(self.inspect.fetch(&selector)),
        }
    }

    // Return a vector of errors encountered by contained fetchers.
    pub fn errors(&self) -> Vec<String> {
        self.inspect.component_errors.iter().map(|e| format!("{}", e)).collect()
    }
}

/// [TrialDataFetcher] stores the key-value lookup for metric names whose values are given as
/// part of a trial (under the "test" section of the .triage files).
#[derive(Clone)]
pub struct TrialDataFetcher<'a> {
    values: &'a HashMap<String, JsonValue>,
    pub(crate) klog: &'a TextFetcher,
    pub(crate) syslog: &'a TextFetcher,
    pub(crate) bootlog: &'a TextFetcher,
    pub(crate) annotations: &'a KeyValueFetcher,
}

lazy_static! {
    static ref EMPTY_JSONVALUES: HashMap<String, JsonValue> = HashMap::new();
}

impl<'a> TrialDataFetcher<'a> {
    pub fn new(values: &'a HashMap<String, JsonValue>) -> TrialDataFetcher<'a> {
        TrialDataFetcher {
            values,
            klog: TextFetcher::ref_empty(),
            syslog: TextFetcher::ref_empty(),
            bootlog: TextFetcher::ref_empty(),
            annotations: KeyValueFetcher::ref_empty(),
        }
    }

    pub fn new_empty() -> TrialDataFetcher<'static> {
        TrialDataFetcher {
            values: &EMPTY_JSONVALUES,
            klog: TextFetcher::ref_empty(),
            syslog: TextFetcher::ref_empty(),
            bootlog: TextFetcher::ref_empty(),
            annotations: KeyValueFetcher::ref_empty(),
        }
    }

    pub fn set_syslog(&mut self, fetcher: &'a TextFetcher) {
        self.syslog = fetcher;
    }

    pub fn set_klog(&mut self, fetcher: &'a TextFetcher) {
        self.klog = fetcher;
    }

    pub fn set_bootlog(&mut self, fetcher: &'a TextFetcher) {
        self.bootlog = fetcher;
    }

    pub fn set_annotations(&mut self, fetcher: &'a KeyValueFetcher) {
        self.annotations = fetcher;
    }

    pub(crate) fn fetch(&self, name: &str) -> MetricValue {
        match self.values.get(name) {
            Some(value) => MetricValue::from(value),
            None => MetricValue::Missing(format!("Value {} not overridden in test", name)),
        }
    }

    pub(crate) fn has_entry(&self, name: &str) -> bool {
        self.values.contains_key(name)
    }
}

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
    pub(crate) full_selector: String,
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
    processed_data: DiagnosticsHierarchy,
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
                let processed_data: DiagnosticsHierarchy =
                    serde_json::from_value(raw_contents.clone()).with_context(|| {
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
            for value in diagnostics_hierarchy::select_from_hierarchy(
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
    use {
        super::*,
        crate::{
            assert_missing,
            metrics::{variable::VariableName, Metric, MetricState},
        },
        anyhow::Error,
        serde_json::Value as JsonValue,
    };

    lazy_static! {
        static ref LOCAL_M: HashMap<String, JsonValue> = {
            let mut m = HashMap::new();
            m.insert("foo".to_owned(), JsonValue::try_from(42).unwrap());
            m.insert("a::b".to_owned(), JsonValue::try_from(7).unwrap());
            m
        };
        static ref FOO_42_AB_7_TRIAL_FETCHER: TrialDataFetcher<'static> =
            TrialDataFetcher::new(&LOCAL_M);
        static ref LOCAL_F: Vec<DiagnosticData> = {
            let s = r#"[{
                "data_source": "Inspect",
                "moniker": "bar.cmx",
                "payload": { "root": { "bar": 99 }}
            }]"#;
            vec![DiagnosticData::new("i".to_string(), Source::Inspect, s.to_string()).unwrap()]
        };
        static ref BAR_99_FILE_FETCHER: FileDataFetcher<'static> = FileDataFetcher::new(&LOCAL_F);
        static ref BAR_SELECTOR: SelectorString =
            SelectorString::try_from("INSPECT:bar.cmx:root:bar".to_owned()).unwrap();
        static ref WRONG_SELECTOR: SelectorString =
            SelectorString::try_from("INSPECT:bar.cmx:root:oops".to_owned()).unwrap();
    }

    macro_rules! variable {
        ($name:expr) => {
            &VariableName::new($name.to_string())
        };
    }

    // Unlike the assert_missing macro, this matches any Missing() regardless of its payload
    // message. It flags `message` if `value` is not Missing(_).
    fn require_missing(value: MetricValue, message: &'static str) {
        match value {
            MetricValue::Missing(_) => {}
            _ => assert!(false, message),
        }
    }

    #[test]
    fn test_file_fetch() {
        assert_eq!(
            BAR_99_FILE_FETCHER.fetch(&BAR_SELECTOR),
            MetricValue::Vector(vec![MetricValue::Int(99)])
        );
        assert_eq!(BAR_99_FILE_FETCHER.fetch(&WRONG_SELECTOR), MetricValue::Vector(vec![]),);
    }

    #[test]
    fn test_trial_fetch() {
        assert!(FOO_42_AB_7_TRIAL_FETCHER.has_entry("foo"));
        assert!(FOO_42_AB_7_TRIAL_FETCHER.has_entry("a::b"));
        assert!(!FOO_42_AB_7_TRIAL_FETCHER.has_entry("a:b"));
        assert!(!FOO_42_AB_7_TRIAL_FETCHER.has_entry("oops"));
        assert_eq!(FOO_42_AB_7_TRIAL_FETCHER.fetch("foo"), MetricValue::Int(42));
        require_missing(
            FOO_42_AB_7_TRIAL_FETCHER.fetch("oops"),
            "Trial fetcher found bogus selector",
        );
    }

    #[test]
    fn test_eval_with_file() {
        let mut file_map = HashMap::new();
        file_map.insert("bar".to_owned(), Metric::Selector(BAR_SELECTOR.clone()));
        file_map.insert("bar_plus_one".to_owned(), Metric::Eval("bar+1".to_owned()));
        file_map.insert("oops_plus_one".to_owned(), Metric::Eval("oops+1".to_owned()));
        let mut other_file_map = HashMap::new();
        other_file_map.insert("bar".to_owned(), Metric::Eval("42".to_owned()));
        let mut metrics = HashMap::new();
        metrics.insert("bar_file".to_owned(), file_map);
        metrics.insert("other_file".to_owned(), other_file_map);
        let file_state =
            MetricState::new(&metrics, Fetcher::FileData(BAR_99_FILE_FETCHER.clone()), None);
        assert_eq!(
            file_state.evaluate_variable("bar_file", variable!("bar_plus_one")),
            MetricValue::Int(100)
        );
        require_missing(
            file_state.evaluate_variable("bar_file", variable!("oops_plus_one")),
            "File found nonexistent name",
        );
        assert_eq!(
            file_state.evaluate_variable("bar_file", variable!("bar")),
            MetricValue::Vector(vec![MetricValue::Int(99)])
        );
        assert_eq!(
            file_state.evaluate_variable("other_file", variable!("bar")),
            MetricValue::Int(42)
        );
        assert_eq!(
            file_state.evaluate_variable("other_file", variable!("other_file::bar")),
            MetricValue::Int(42)
        );
        assert_eq!(
            file_state.evaluate_variable("other_file", variable!("bar_file::bar")),
            MetricValue::Vector(vec![MetricValue::Int(99)])
        );
        require_missing(
            file_state.evaluate_variable("other_file", variable!("bar_plus_one")),
            "Shouldn't have found bar_plus_one in other_file",
        );
        require_missing(
            file_state.evaluate_variable("missing_file", variable!("bar_plus_one")),
            "Shouldn't have found bar_plus_one in missing_file",
        );
        require_missing(
            file_state.evaluate_variable("bar_file", variable!("other_file::bar_plus_one")),
            "Shouldn't have found other_file::bar_plus_one",
        );
    }

    #[test]
    fn test_eval_with_trial() {
        let mut trial_map = HashMap::new();
        // The (broken) "foo" selector should be ignored in favor of the "foo" fetched value.
        trial_map.insert("foo".to_owned(), Metric::Selector(BAR_SELECTOR.clone()));
        trial_map.insert("foo_plus_one".to_owned(), Metric::Eval("foo+1".to_owned()));
        trial_map.insert("oops_plus_one".to_owned(), Metric::Eval("oops+1".to_owned()));
        trial_map.insert("ab_plus_one".to_owned(), Metric::Eval("a::b+1".to_owned()));
        trial_map.insert("ac_plus_one".to_owned(), Metric::Eval("a::c+1".to_owned()));
        // The file "a" should be completely ignored when testing foo_file.
        let mut a_map = HashMap::new();
        a_map.insert("b".to_owned(), Metric::Eval("2".to_owned()));
        a_map.insert("c".to_owned(), Metric::Eval("3".to_owned()));
        a_map.insert("foo".to_owned(), Metric::Eval("4".to_owned()));
        let mut metrics = HashMap::new();
        metrics.insert("foo_file".to_owned(), trial_map);
        metrics.insert("a".to_owned(), a_map);
        let trial_state =
            MetricState::new(&metrics, Fetcher::TrialData(FOO_42_AB_7_TRIAL_FETCHER.clone()), None);
        // foo from values shadows foo selector.
        assert_eq!(
            trial_state.evaluate_variable("foo_file", variable!("foo")),
            MetricValue::Int(42)
        );
        // Value shadowing also works in expressions.
        assert_eq!(
            trial_state.evaluate_variable("foo_file", variable!("foo_plus_one")),
            MetricValue::Int(43)
        );
        // foo can shadow eval as well as selector.
        assert_eq!(trial_state.evaluate_variable("a", variable!("foo")), MetricValue::Int(42));
        // A value that's not there should be "Missing" (e.g. not crash)
        require_missing(
            trial_state.evaluate_variable("foo_file", variable!("oops_plus_one")),
            "Trial found nonexistent name",
        );
        // a::b ignores the "b" in file "a" and uses "a::b" from values.
        assert_eq!(
            trial_state.evaluate_variable("foo_file", variable!("ab_plus_one")),
            MetricValue::Int(8)
        );
        // a::c should return Missing, not look up c in file a.
        require_missing(
            trial_state.evaluate_variable("foo_file", variable!("ac_plus_one")),
            "Trial should not have read c from file a",
        );
    }

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
