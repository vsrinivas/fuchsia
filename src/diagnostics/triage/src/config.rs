// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        act::{Actions, ActionsSchema},
        metrics::{Metrics, MetricsSchema},
        validate::{validate, TestsSchema},
        Options,
    },
    anyhow::{format_err, Error},
    serde_derive::Deserialize,
    serde_json as json,
    std::{collections::HashMap, fs, path::Path},
};

pub mod parse;

/// Schema for JSON triage configuration. This structure is parsed directly from the configuration
/// files using serde_json.
///
/// Field names will appear as map keys in the human-written files, so they're named simply as,
/// for example, "metrics" instead of something more descriptive like "file_metrics"
#[derive(Deserialize, Default, Debug)]
pub struct ConfigFileSchema {
    /// Map of named Metrics. Each Metric selects or calculates a value.
    pub metrics: MetricsSchema,
    /// Map of named Actions. Each Action uses a boolean value to trigger a warning.
    pub actions: ActionsSchema,
    /// Map of named Tests. Each test applies sample data to lists of actions that should or
    /// should not trigger.
    pub tests: TestsSchema,
}

impl ConfigFileSchema {
    pub fn parse(s: String) -> Result<ConfigFileSchema, Error> {
        match serde_json::from_str::<ConfigFileSchema>(&s) {
            Ok(config) => Ok(config),
            Err(e) => return Err(format_err!("Error {}", e)),
        }
    }
}

/// Permanent storage of program execution context. This lives as long as the program, so we can
/// store references to it in various other structs.
pub struct StateHolder {
    pub metrics: Metrics,
    pub actions: Actions,
    pub inspect_data: InspectData,
}

/// Top-level schema of the inspect.json file found in bugreport.zip.
pub type InspectData = Vec<json::Value>;

/// Parses the inspect.json file and all the config files.
pub fn initialize(options: Options) -> Result<StateHolder, Error> {
    let Options { inspect, config_files, .. } = options;
    let inspect_text = match fs::read_to_string(inspect.clone()) {
        Ok(data) => data,
        Err(_) => return Err(format_err!("Couldn't read Inspect file '{}' to string", inspect)),
    };
    let inspect_data = parse_inspect(inspect_text)?;

    if config_files.len() == 0 {
        return Err(format_err!("Need at least one config file; use --config"));
    }
    let mut actions = HashMap::new();
    let mut metrics = HashMap::new();
    let mut tests = HashMap::new();
    for file_name in config_files {
        let namespace = base_name(&file_name)?;
        let file_data = match fs::read_to_string(file_name.clone()) {
            Ok(data) => data,
            Err(_) => {
                return Err(format_err!("Couldn't read config file '{}' to string", file_name))
            }
        };
        let file_config = match ConfigFileSchema::parse(file_data) {
            Ok(c) => c,
            Err(e) => return Err(format_err!("Parsing file '{}': {}", file_name, e)),
        };
        let ConfigFileSchema { actions: file_actions, metrics: file_metrics, tests: file_tests } =
            file_config;
        metrics.insert(namespace.clone(), file_metrics);
        actions.insert(namespace.clone(), file_actions);
        tests.insert(namespace, file_tests);
    }
    validate(&metrics, &actions, &tests)?;
    Ok(StateHolder { metrics, actions, inspect_data })
}

/// Parses a JSON string formatted as an array of [json::Value]'s.
pub(crate) fn parse_inspect(data: String) -> Result<Vec<json::Value>, Error> {
    let raw_json = match data.parse::<json::Value>() {
        Ok(data) => data,
        Err(_) => return Err(format_err!("Couldn't parse Inspect file '{}' as JSON", data)),
    };
    match raw_json {
        json::Value::Array(entries) => Ok(entries),
        _ => return Err(format_err!("Array expected in inspect.json format")),
    }
}

fn base_name(path: &String) -> Result<String, Error> {
    let file_path = Path::new(path);
    if let Some(s) = file_path.file_stem() {
        if let Some(s) = s.to_str() {
            return Ok(s.to_owned());
        }
    }
    return Err(format_err!("Bad path {} - can't find file_stem", path));
}

#[cfg(test)]
mod test {
    use {super::*, anyhow::Error};

    // initialize() will be tested in the integration test: "fx triage --test"
    // TODO(cphoenix) - set up dirs under test/ and test initialize() here.

    #[test]
    fn base_name_works() -> Result<(), Error> {
        assert_eq!(base_name(&"foo/bar/baz.ext".to_string())?, "baz".to_string());
        Ok(())
    }

    #[test]
    fn parse_inspect_works() -> Result<(), Error> {
        assert!(parse_inspect("foo".to_string()).is_err(), "'foo' isn't valid JSON");
        assert!(parse_inspect(r#"{"a":5}"#.to_string()).is_err(), "Needed an array");
        assert!(parse_inspect("[]".to_string()).is_ok(), "A JSON array should have worked");
        Ok(())
    }
}
