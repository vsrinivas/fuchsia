// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        act::{Actions, ActionsSchema},
        metrics::{Metric, Metrics},
        validate::{validate, TestsSchema},
        Options,
    },
    anyhow::{bail, format_err, Error},
    serde::Deserialize,
    serde_json as json,
    std::{collections::HashMap, fs, path::Path, str::FromStr},
};

pub mod parse;

/// Schema for JSON triage configuration. This structure is parsed directly from the configuration
/// files using serde_json.
#[derive(Deserialize, Default, Debug)]
pub struct ConfigFileSchema {
    /// Map of named Selectors. Each Selector selects a value from Diagnostic data.
    #[serde(rename = "select")]
    pub file_selectors: HashMap<String, String>,
    /// Map of named Evals. Each Eval calculates a value.
    #[serde(rename = "eval")]
    pub file_evals: HashMap<String, String>,
    /// Map of named Actions. Each Action uses a boolean value to trigger a warning.
    #[serde(rename = "act")]
    pub file_actions: ActionsSchema,
    /// Map of named Tests. Each test applies sample data to lists of actions that should or
    /// should not trigger.
    #[serde(rename = "test")]
    pub file_tests: TestsSchema,
}

impl ConfigFileSchema {
    pub fn parse(s: String) -> Result<ConfigFileSchema, Error> {
        match json5::from_str::<ConfigFileSchema>(&s) {
            Ok(config) => Ok(config),
            Err(e) => return Err(format_err!("Error {}", e)),
        }
    }
}

#[derive(Debug, PartialEq)]
pub enum OutputFormat {
    Text,
    CSV,
}

impl FromStr for OutputFormat {
    type Err = anyhow::Error;
    fn from_str(output_format: &str) -> Result<Self, Self::Err> {
        match output_format {
            "csv" => Ok(OutputFormat::CSV),
            "text" => Ok(OutputFormat::Text),
            incorrect => {
                Err(format_err!("Invalid output type '{}' - must be 'csv' or 'text'", incorrect))
            }
        }
    }
}

/// Permanent storage of program execution context. This lives as long as the program, so we can
/// store references to it in various other structs.
pub struct StateHolder {
    pub metrics: Metrics,
    pub actions: Actions,
    pub inspect_contexts: Vec<InspectContext>,
    pub output_format: OutputFormat,
}

/// Top-level schema of the inspect.json file found in bugreport.zip.
pub struct InspectData {
    entries: Vec<json::Value>,
}

impl InspectData {
    pub fn new(entries: Vec<json::Value>) -> InspectData {
        InspectData { entries }
    }

    /// Parses a JSON string formatted as an array of [json::Value]'s.
    pub fn from(data: String) -> Result<InspectData, Error> {
        let raw_json = match data.parse::<json::Value>() {
            Ok(data) => data,
            Err(_) => return Err(format_err!("Couldn't parse Inspect file '{}' as JSON", data)),
        };
        match raw_json {
            json::Value::Array(entries) => Ok(InspectData { entries }),
            _ => return Err(format_err!("Array expected in inspect.json format")),
        }
    }

    pub fn as_json(&self) -> &Vec<json::Value> {
        &self.entries
    }
}

/// The path of the inspect file and the data contained within it.
pub struct InspectContext {
    pub source: String,
    pub data: InspectData,
}

impl InspectContext {
    pub fn initialize_from_file(filename: String) -> Result<InspectContext, Error> {
        let text = match fs::read_to_string(&filename) {
            Ok(data) => data,
            Err(_) => {
                return Err(format_err!("Couldn't read Inspect file '{}' to string", filename))
            }
        };

        let inspect_data = match InspectData::from(text) {
            Ok(data) => data,
            Err(e) => return Err(e),
        };

        Ok(InspectContext { source: filename, data: inspect_data })
    }
}

/// Parses the inspect.json file and all the config files.
pub fn initialize(options: Options) -> Result<StateHolder, Error> {
    let Options { directories, output_format, inspect, config_files, tags, exclude_tags, .. } =
        options;

    let action_tag_directive = action_tag_directive_from_tags(tags, exclude_tags);

    let mut inspect_contexts = Vec::new();
    inspect_contexts.push(InspectContext::initialize_from_file(inspect.unwrap())?);

    for directory in directories {
        let inspect_file = Path::new(&directory).join("inspect.json").into_os_string();
        match InspectContext::initialize_from_file(inspect_file.to_string_lossy().to_string()) {
            Ok(c) => inspect_contexts.push(c),
            Err(e) => println!("{}", e),
        };
    }

    if config_files.len() == 0 {
        bail!("Need at least one config file; use --config");
    }
    let mut actions = HashMap::new();
    let mut metrics = HashMap::new();
    let mut tests = HashMap::new();
    for file_name in config_files {
        let namespace = base_name(&file_name)?;
        let file_data = match fs::read_to_string(file_name.clone()) {
            Ok(data) => data,
            Err(_) => {
                bail!("Couldn't read config file '{}' to string", file_name);
            }
        };
        let file_config = match ConfigFileSchema::parse(file_data) {
            Ok(c) => c,
            Err(e) => bail!("Parsing file '{}': {}", file_name, e),
        };
        let ConfigFileSchema { file_actions, file_selectors, file_evals, file_tests } = file_config;

        let file_actions = filter_actions(file_actions, &action_tag_directive);
        let mut file_metrics = HashMap::new();
        for (key, value) in file_selectors.into_iter() {
            file_metrics.insert(key, Metric::Selector(value));
        }
        for (key, value) in file_evals.into_iter() {
            if file_metrics.contains_key(&key) {
                bail!("Duplicate metric name {} in file {}", key, namespace);
            }
            file_metrics.insert(key, Metric::Eval(value));
        }
        metrics.insert(namespace.clone(), file_metrics);
        actions.insert(namespace.clone(), file_actions);
        tests.insert(namespace, file_tests);
    }
    validate(&metrics, &actions, &tests)?;

    Ok(StateHolder { metrics, actions, inspect_contexts, output_format })
}

fn base_name(path: &String) -> Result<String, Error> {
    let file_path = Path::new(path);
    if let Some(s) = file_path.file_stem() {
        if let Some(s) = s.to_str() {
            return Ok(s.to_owned());
        }
    }
    bail!("Bad path {} - can't find file_stem", path)
}

/// A value which directs how to include Actions based on their tags.
enum ActionTagDirective {
    /// Include all of the Actions in the Config
    AllowAll,

    /// Only include the Actions which match the given tags
    Include(Vec<String>),

    /// Include all tags excluding the given tags
    Exclude(Vec<String>),
}

/// Creates a new ActionTagDirective based on the following rules,
///
/// - AllowAll iff tags is empty and exclude_tags is empty.
/// - Include if tags is not empty and exclude_tags is empty.
/// - Include if tags is not empty and exclude_tags is not empty, in this
///   situation the exclude_ags will be ignored since include implies excluding
///   all other tags.
/// - Exclude iff tags is empty and exclude_tags is not empty.
fn action_tag_directive_from_tags(
    tags: Vec<String>,
    exclude_tags: Vec<String>,
) -> ActionTagDirective {
    match (tags.is_empty(), exclude_tags.is_empty()) {
        // tags are not empty
        (false, _) => ActionTagDirective::Include(tags),
        // tags are empty, exclude_tags are not empty
        (true, false) => ActionTagDirective::Exclude(exclude_tags),
        _ => ActionTagDirective::AllowAll,
    }
}

/// Exfiltrates the actions in the ActionsSchema.
///
/// This method will enumerate the actions in the ActionsSchema and determine
/// which Actions are included based on the directive. Actions only contain a
/// single tag so an Include directive implies that all other tags should be
/// excluded and an Exclude directive implies that all other tags should be
/// included.
fn filter_actions(actions: ActionsSchema, action_directive: &ActionTagDirective) -> ActionsSchema {
    match action_directive {
        ActionTagDirective::AllowAll => actions,
        ActionTagDirective::Include(tags) => actions
            .into_iter()
            .filter(|(_, a)| match &a.tag {
                Some(tag) => tags.contains(tag),
                None => false,
            })
            .collect(),
        ActionTagDirective::Exclude(tags) => actions
            .into_iter()
            .filter(|(_, a)| match &a.tag {
                Some(tag) => !tags.contains(tag),
                None => true,
            })
            .collect(),
    }
}

#[cfg(test)]
mod test {
    use {super::*, crate::act::Action, anyhow::Error};

    // initialize() will be tested in the integration test: "fx triage --test"
    // TODO(cphoenix) - set up dirs under test/ and test initialize() here.

    #[test]
    fn base_name_works() -> Result<(), Error> {
        assert_eq!(base_name(&"foo/bar/baz.ext".to_string())?, "baz".to_string());
        Ok(())
    }

    #[test]
    fn inspect_data_from_works() -> Result<(), Error> {
        assert!(InspectData::from("foo".to_string()).is_err(), "'foo' isn't valid JSON");
        assert!(InspectData::from(r#"{"a":5}"#.to_string()).is_err(), "Needed an array");
        assert!(InspectData::from("[]".to_string()).is_ok(), "A JSON array should have worked");
        Ok(())
    }

    #[test]
    fn output_format_from_string() -> Result<(), Error> {
        assert_eq!(OutputFormat::from_str("csv")?, OutputFormat::CSV);
        assert_eq!(OutputFormat::from_str("text")?, OutputFormat::Text);
        assert!(OutputFormat::from_str("").is_err(), "Should have returned 'Err' on ''");
        assert!(OutputFormat::from_str("CSV").is_err(), "Should have returned 'Err' on 'CSV'");
        assert!(OutputFormat::from_str("Text").is_err(), "Should have returned 'Err' on 'Text'");
        assert!(
            OutputFormat::from_str("GARBAGE").is_err(),
            "Should have returned 'Err' on 'GARBAGE'"
        );
        Ok(())
    }

    #[test]
    fn action_tag_directive_from_tags_allow_all() {
        let result = action_tag_directive_from_tags(vec![], vec![]);
        match result {
            ActionTagDirective::AllowAll => (),
            _ => panic!("failed to create correct ActionTagDirective"),
        }
    }

    #[test]
    fn action_tag_directive_from_tags_include() {
        let result =
            action_tag_directive_from_tags(vec!["t1".to_string(), "t2".to_string()], vec![]);
        match result {
            ActionTagDirective::Include(tags) => {
                assert_eq!(tags, vec!["t1".to_string(), "t2".to_string()])
            }
            _ => panic!("failed to create correct ActionTagDirective"),
        }
    }

    #[test]
    fn action_tag_directive_from_tags_include_override_exclude() {
        let result = action_tag_directive_from_tags(
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
            action_tag_directive_from_tags(vec![], vec!["t1".to_string(), "t2".to_string()]);
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
                    let action = Action {
                        trigger: $trigger.to_string(),
                        print: $print.to_string(),
                        tag: $tag
                    };
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
            assert_eq!(a.trigger, $trigger.to_string());
            assert_eq!(a.print, $print.to_string());
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
}
