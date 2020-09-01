// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        act::{Actions, ActionsSchema},
        metrics::{
            fetch::{InspectFetcher, KeyValueFetcher, SelectorString, TextFetcher},
            Metric, Metrics,
        },
        validate::{validate, Trials, TrialsSchema},
    },
    anyhow::{bail, format_err, Context, Error},
    num_derive::FromPrimitive,
    serde_derive::Deserialize,
    std::{collections::HashMap, convert::TryFrom},
};

pub(crate) mod parse;

// These numbers are used in the wasm-bindgen bridge so they are explicit and
// permanent. They don't need to be sequential. This enum must be consistent
// with the Source enum in //src/diagnostics/lib/triage/wasm/src/lib.rs.
#[derive(Debug, Clone, Copy, FromPrimitive)]
pub enum Source {
    Inspect = 0,
    Klog = 1,
    Syslog = 2,
    Bootlog = 3,
    Annotations = 4,
}

/// Schema for JSON triage configuration. This structure is parsed directly from the configuration
/// files using serde_json.
#[derive(Deserialize, Default, Debug)]
#[serde(deny_unknown_fields)]
pub struct ConfigFileSchema {
    /// Map of named Selectors. Each Selector selects a value from Diagnostic data.
    #[serde(rename = "select")]
    pub file_selectors: Option<HashMap<String, String>>,
    /// Map of named Evals. Each Eval calculates a value.
    #[serde(rename = "eval")]
    pub file_evals: Option<HashMap<String, String>>,
    /// Map of named Actions. Each Action uses a boolean value to trigger a warning.
    #[serde(rename = "act")]
    pub file_actions: Option<ActionsSchema>,
    /// Map of named Tests. Each test applies sample data to lists of actions that should or
    /// should not trigger.
    #[serde(rename = "test")]
    pub file_tests: Option<TrialsSchema>,
}

impl TryFrom<String> for ConfigFileSchema {
    type Error = anyhow::Error;

    fn try_from(s: String) -> Result<Self, Self::Error> {
        match serde_json5::from_str::<ConfigFileSchema>(&s) {
            Ok(config) => Ok(config),
            Err(e) => return Err(format_err!("Error {}", e)),
        }
    }
}

pub enum DataFetcher {
    Inspect(InspectFetcher),
    Text(TextFetcher),
    KeyValue(KeyValueFetcher),
    None,
}

/// The path of the Diagnostic files and the data contained within them.
pub struct DiagnosticData {
    pub name: String,
    pub source: Source,
    pub data: DataFetcher,
}

impl DiagnosticData {
    pub fn new(name: String, source: Source, contents: String) -> Result<DiagnosticData, Error> {
        let data = match source {
            Source::Inspect => DataFetcher::Inspect(
                InspectFetcher::try_from(&*contents).context("Parsing inspect.json")?,
            ),
            Source::Syslog | Source::Klog | Source::Bootlog => {
                DataFetcher::Text(TextFetcher::try_from(&*contents).context("Parsing plain text")?)
            }
            Source::Annotations => DataFetcher::KeyValue(
                KeyValueFetcher::try_from(&*contents).context("Parsing annotations")?,
            ),
        };
        Ok(DiagnosticData { name, source, data })
    }

    pub fn new_empty(name: String, source: Source) -> DiagnosticData {
        DiagnosticData { name, source, data: DataFetcher::None }
    }
}

pub struct ParseResult {
    pub metrics: Metrics,
    pub actions: Actions,
    pub tests: Trials,
}

impl ParseResult {
    pub fn new(
        configs: &HashMap<String, String>,
        action_tag_directive: &ActionTagDirective,
    ) -> Result<ParseResult, Error> {
        let mut actions = HashMap::new();
        let mut metrics = HashMap::new();
        let mut tests = HashMap::new();

        for (namespace, file_data) in configs {
            let file_config = match ConfigFileSchema::try_from(file_data.clone()) {
                Ok(c) => c,
                Err(e) => bail!("Parsing file '{}': {}", namespace, e),
            };
            let ConfigFileSchema { file_actions, file_selectors, file_evals, file_tests } =
                file_config;
            // Other code assumes that each name will have an entry in all categories.
            let file_actions = file_actions.unwrap_or_else(|| HashMap::new());
            let file_selectors = file_selectors.unwrap_or_else(|| HashMap::new());
            let file_evals = file_evals.unwrap_or_else(|| HashMap::new());
            let file_tests = file_tests.unwrap_or_else(|| HashMap::new());
            let file_actions = filter_actions(file_actions, &action_tag_directive);
            let mut file_metrics = HashMap::new();
            for (key, value) in file_selectors.into_iter() {
                file_metrics.insert(key, Metric::Selector(SelectorString::try_from(value)?));
            }
            for (key, value) in file_evals.into_iter() {
                if file_metrics.contains_key(&key) {
                    bail!("Duplicate metric name {} in file {}", key, namespace);
                }
                file_metrics.insert(key, Metric::Eval(value));
            }
            metrics.insert(namespace.clone(), file_metrics);
            actions.insert(namespace.clone(), file_actions);
            tests.insert(namespace.clone(), file_tests);
        }

        Ok(ParseResult { actions, metrics, tests })
    }

    pub fn all_selectors(&self) -> Vec<String> {
        let mut result = Vec::new();
        for (_, metric_set) in self.metrics.iter() {
            for (_, metric) in metric_set.iter() {
                if let Metric::Selector(selector) = metric {
                    result.push(selector.full_selector.to_owned());
                }
            }
        }
        result
    }

    pub fn validate(&self) -> Result<(), Error> {
        validate(self)
    }
}

/// A value which directs how to include Actions based on their tags.
pub enum ActionTagDirective {
    /// Include all of the Actions in the Config
    AllowAll,

    /// Only include the Actions which match the given tags
    Include(Vec<String>),

    /// Include all tags excluding the given tags
    Exclude(Vec<String>),
}

impl ActionTagDirective {
    /// Creates a new ActionTagDirective based on the following rules,
    ///
    /// - AllowAll iff tags is empty and exclude_tags is empty.
    /// - Include if tags is not empty and exclude_tags is empty.
    /// - Include if tags is not empty and exclude_tags is not empty, in this
    ///   situation the exclude_ags will be ignored since include implies excluding
    ///   all other tags.
    /// - Exclude iff tags is empty and exclude_tags is not empty.
    pub fn from_tags(tags: Vec<String>, exclude_tags: Vec<String>) -> ActionTagDirective {
        match (tags.is_empty(), exclude_tags.is_empty()) {
            // tags are not empty
            (false, _) => ActionTagDirective::Include(tags),
            // tags are empty, exclude_tags are not empty
            (true, false) => ActionTagDirective::Exclude(exclude_tags),
            _ => ActionTagDirective::AllowAll,
        }
    }
}

/// Exfiltrates the actions in the ActionsSchema.
///
/// This method will enumerate the actions in the ActionsSchema and determine
/// which Actions are included based on the directive. Actions only contain a
/// single tag so an Include directive implies that all other tags should be
/// excluded and an Exclude directive implies that all other tags should be
/// included.
pub fn filter_actions(
    actions: ActionsSchema,
    action_directive: &ActionTagDirective,
) -> ActionsSchema {
    match action_directive {
        ActionTagDirective::AllowAll => actions,
        ActionTagDirective::Include(tags) => actions
            .into_iter()
            .filter(|(_, a)| match &a.get_tag() {
                Some(tag) => tags.contains(tag),
                None => false,
            })
            .collect(),
        ActionTagDirective::Exclude(tags) => actions
            .into_iter()
            .filter(|(_, a)| match &a.get_tag() {
                Some(tag) => !tags.contains(tag),
                None => true,
            })
            .collect(),
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::act::{Action, Warning},
        anyhow::Error,
        maplit::hashmap,
    };

    // initialize() will be tested in the integration test: "fx test triage_lib_test"
    // TODO(cphoenix) - set up dirs under test/ and test initialize() here.

    #[test]
    fn inspect_data_from_works() -> Result<(), Error> {
        assert!(InspectFetcher::try_from("foo").is_err(), "'foo' isn't valid JSON");
        assert!(InspectFetcher::try_from(r#"{"a":5}"#).is_err(), "Needed an array");
        assert!(InspectFetcher::try_from("[]").is_ok(), "A JSON array should have worked");
        Ok(())
    }

    #[test]
    fn action_tag_directive_from_tags_allow_all() {
        let result = ActionTagDirective::from_tags(vec![], vec![]);
        match result {
            ActionTagDirective::AllowAll => (),
            _ => panic!("failed to create correct ActionTagDirective"),
        }
    }

    #[test]
    fn action_tag_directive_from_tags_include() {
        let result =
            ActionTagDirective::from_tags(vec!["t1".to_string(), "t2".to_string()], vec![]);
        match result {
            ActionTagDirective::Include(tags) => {
                assert_eq!(tags, vec!["t1".to_string(), "t2".to_string()])
            }
            _ => panic!("failed to create correct ActionTagDirective"),
        }
    }

    #[test]
    fn action_tag_directive_from_tags_include_override_exclude() {
        let result = ActionTagDirective::from_tags(
            vec!["t1".to_string(), "t2".to_string()],
            vec!["t3".to_string()],
        );
        match result {
            ActionTagDirective::Include(tags) => {
                assert_eq!(tags, vec!["t1".to_string(), "t2".to_string()])
            }
            _ => panic!("failed to create correct ActionTagDirective"),
        }
    }

    #[test]
    fn action_tag_directive_from_tags_exclude() {
        let result =
            ActionTagDirective::from_tags(vec![], vec!["t1".to_string(), "t2".to_string()]);
        match result {
            ActionTagDirective::Exclude(tags) => {
                assert_eq!(tags, vec!["t1".to_string(), "t2".to_string()])
            }
            _ => panic!("failed to create correct ActionTagDirective"),
        }
    }

    // helper macro to create an ActionsSchema
    macro_rules! actions_schema {
        ( $($key:expr => $trigger:expr, $print:expr, $tag:expr),+ ) => {
            {
                let mut m =  ActionsSchema::new();
                $(
                    let action = Action::Warning(Warning {
                        trigger: Metric::Eval($trigger.to_string()),
                        print: $print.to_string(),
                        tag: $tag
                    });
                    m.insert($key.to_string(), action);
                )+
                m
            }
        }
    }

    // helper macro to create an ActionsSchema
    macro_rules! assert_has_action {
        ($result:expr, $key:expr, $trigger:expr, $print:expr) => {
            let a = $result.get(&$key.to_string());
            assert!(a.is_some());
            let a = a.unwrap();
            match a {
                Action::Warning(a) => {
                    if let Metric::Eval(trigger_eval) = &a.trigger {
                        assert_eq!(trigger_eval, $trigger);
                    } else {
                        assert!(false, "Trigger {:?} was not an expression to Eval", a.trigger);
                    }
                    assert_eq!(a.print, $print.to_string());
                }
                _ => {
                    assert!(false);
                }
            }
        };
    }

    #[test]
    fn filter_actions_allow_all() {
        let result = filter_actions(
            actions_schema! {
                "no_tag" => "t1", "foo", None,
                "tagged" => "t2", "bar", Some("tag".to_string())
            },
            &ActionTagDirective::AllowAll,
        );
        assert_eq!(result.len(), 2);
    }

    #[test]
    fn filter_actions_include_one_tag() {
        let result = filter_actions(
            actions_schema! {
                "1" => "t1", "p1", Some("ignore".to_string()),
                "2" => "t2", "p2", Some("tag".to_string()),
                "3" => "t3", "p3", Some("tag".to_string())
            },
            &ActionTagDirective::Include(vec!["tag".to_string()]),
        );
        assert_eq!(result.len(), 2);
        assert_has_action!(result, "2", "t2", "p2");
        assert_has_action!(result, "3", "t3", "p3");
    }

    #[test]
    fn filter_actions_include_many_tags() {
        let result = filter_actions(
            actions_schema! {
                "1" => "t1", "p1", Some("ignore".to_string()),
                "2" => "t2", "p2", Some("tag1".to_string()),
                "3" => "t3", "p3", Some("tag2".to_string()),
                "4" => "t4", "p4", Some("tag2".to_string())
            },
            &ActionTagDirective::Include(vec!["tag1".to_string(), "tag2".to_string()]),
        );
        assert_eq!(result.len(), 3);
        assert_has_action!(result, "2", "t2", "p2");
        assert_has_action!(result, "3", "t3", "p3");
        assert_has_action!(result, "4", "t4", "p4");
    }

    #[test]
    fn filter_actions_exclude_one_tag() {
        let result = filter_actions(
            actions_schema! {
                "1" => "t1", "p1", Some("ignore".to_string()),
                "2" => "t2", "p2", Some("tag".to_string()),
                "3" => "t3", "p3", Some("tag".to_string())
            },
            &ActionTagDirective::Exclude(vec!["tag".to_string()]),
        );
        assert_eq!(result.len(), 1);
        assert_has_action!(result, "1", "t1", "p1");
    }

    #[test]
    fn filter_actions_exclude_many() {
        let result = filter_actions(
            actions_schema! {
                "1" => "t1", "p1", Some("ignore".to_string()),
                "2" => "t2", "p2", Some("tag1".to_string()),
                "3" => "t3", "p3", Some("tag2".to_string()),
                "4" => "t4", "p4", Some("tag2".to_string())
            },
            &ActionTagDirective::Exclude(vec!["tag1".to_string(), "tag2".to_string()]),
        );
        assert_eq!(result.len(), 1);
        assert_has_action!(result, "1", "t1", "p1");
    }

    #[test]
    fn filter_actions_include_does_not_include_empty_tag() {
        let result = filter_actions(
            actions_schema! {
                "1" => "t1", "p1", None,
                "2" => "t2", "p2", Some("tag".to_string())
            },
            &ActionTagDirective::Include(vec!["tag".to_string()]),
        );
        assert_eq!(result.len(), 1);
        assert_has_action!(result, "2", "t2", "p2");
    }

    #[test]
    fn filter_actions_exclude_does_include_empty_tag() {
        let result = filter_actions(
            actions_schema! {
                "1" => "t1", "p1", None,
                "2" => "t2", "p2", Some("tag".to_string())
            },
            &ActionTagDirective::Exclude(vec!["tag".to_string()]),
        );
        assert_eq!(result.len(), 1);
        assert_has_action!(result, "1", "t1", "p1");
    }

    #[test]
    fn all_selectors_works() {
        macro_rules! s {
            ($s:expr) => {
                $s.to_string()
            };
        }
        let file_map = hashmap![
            s!("file1") => s!(r#"{ select: {selector1: "INSPECT:name:path:label"}}"#),
            s!("file2") =>
                s!(r#"{ select: {selector1: "INSPECT:word:stuff:identifier"}, eval: {e: "2+2"} }"#),
        ];
        let parse = ParseResult::new(&file_map, &ActionTagDirective::AllowAll).unwrap();
        let selectors = parse.all_selectors();
        assert_eq!(selectors.len(), 2);
        assert!(selectors.contains(&s!("INSPECT:name:path:label")));
        assert!(selectors.contains(&s!("INSPECT:word:stuff:identifier")));
        // Internal logic test: Make sure we're not returning eval entries.
        assert!(!selectors.contains(&s!("2+2")));
    }
}
