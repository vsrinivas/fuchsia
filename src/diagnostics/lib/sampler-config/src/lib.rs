// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_diagnostics::StringSelector,
    serde::{de::Unexpected, Deserialize, Deserializer},
    serde_json5,
    std::fs,
    std::path::Path,
};

/// Configuration for a single project to map inspect data to its cobalt metrics.
#[derive(Deserialize, Debug, PartialEq)]
pub struct ProjectConfig {
    /// Project ID that metrics are being sampled and forwarded on behalf of.
    pub project_id: u32,

    /// Customer ID that metrics are being sampled and forwarded on behalf of.
    /// This will default to 1 if not specified.
    #[serde(default)]
    pub customer_id: u32,

    /// The frequency with which metrics are sampled, in seconds.
    pub poll_rate_sec: i64,

    /// The collection of mappings from inspect to cobalt.
    pub metrics: Vec<MetricConfig>,
}

/// Configuration for a single metric to map from an inspect property
/// to a cobalt metric.
#[derive(Deserialize, Debug, PartialEq)]
pub struct MetricConfig {
    /// Selector identifying the metric to
    /// sample via the diagnostics platform.
    #[serde(rename = "selector")]
    pub selectors: SelectorList,
    /// Cobalt metric id to map the selector to.
    pub metric_id: u32,
    /// Data type to transform the metric to.
    pub metric_type: DataType,
    /// Event codes defining the dimensions of the
    /// cobalt metric. Note: Order matters, and
    /// must match the order of the defined dimensions
    /// in the cobalt metric file.
    pub event_codes: Vec<u32>,
    /// Optional boolean specifying whether to upload
    /// the specified metric only once, the first time
    /// it becomes available to the sampler.
    pub upload_once: Option<bool>,
    /// Optional boolean specifying whether to use Cobalt v1.0
    /// protocol. This value may either be absent or true.
    pub use_legacy_cobalt: Option<bool>,
}

/// The supported V1.0 Cobalt Metrics
#[derive(Deserialize, Debug, PartialEq, Eq, Copy, Clone)]
pub enum DataType {
    // Maps cached diffs from Uint or Int inspect types.
    // NOTE: This does not use duration tracking. Durations
    //       are always set to 0.
    Occurrence,
    // Maps raw Int inspect types.
    Integer,
    // Maps cached diffs from IntHistogram inspect type.
    IntHistogram,
    // TODO(lukenicholson): Expand sampler support for new
    // data types.
    // Maps raw Double inspect types.
    // FloatCustomEvent,
    // Maps raw Uint inspect types.
    // IndexCustomEvent,
}

// SelectorList is adapted from SelectorEntry in src/diagnostics/lib/triage/src/config.rs

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

// TODO(fxbug.dev/87709) - this could be more memory-efficient by using slices into the string.
#[derive(Clone, Debug, PartialEq)]
pub struct ParsedSelector {
    pub selector_string: String,
    pub selector: fidl_fuchsia_diagnostics::Selector,
    pub moniker: String,
}

pub fn parse_selector_for_test(selector_str: &str) -> Option<ParsedSelector> {
    Some(parse_selector::<serde::de::value::Error>(selector_str).unwrap())
}

fn parse_selector<E>(selector_str: &str) -> Result<ParsedSelector, E>
where
    E: serde::de::Error,
{
    let selector = selectors::parse_selector(selector_str)
        .or(Err(E::invalid_value(Unexpected::Str(selector_str), &"not a valid selector")))?;
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
    Ok(ParsedSelector { selector, selector_string: selector_str.to_string(), moniker })
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

/// Parses a configuration file for a single project into a ProjectConfig.
pub fn parse_config(path: impl AsRef<Path>) -> Result<ProjectConfig, Error> {
    let path = path.as_ref();
    let json_string: String =
        fs::read_to_string(path).with_context(|| format!("parsing config: {}", path.display()))?;
    let mut config: ProjectConfig = serde_json5::from_str(&json_string)?;
    // If not specified, use the default Fuchsia customer.
    if config.customer_id == 0 {
        config.customer_id = 1;
    }
    Ok(config)
}

/// Container for all configurations needed to instantiate the Sampler infrastructure.
/// Includes:
///      - Project configurations.
///      - Minimum sample rate.
#[derive(Debug)]
pub struct SamplerConfig {
    pub project_configs: Vec<ProjectConfig>,
    pub minimum_sample_rate_sec: i64,
}

impl SamplerConfig {
    /// Parse the ProjectConfigurations for every project from config data.
    pub fn from_directory(
        minimum_sample_rate_sec: i64,
        dir: impl AsRef<Path>,
    ) -> Result<Self, Error> {
        let suffix = std::ffi::OsStr::new("json");
        let readdir = dir.as_ref().read_dir();
        let mut project_configs: Vec<ProjectConfig> = Vec::new();
        match readdir {
            Err(e) => {
                return Err(format_err!(
                    "Failed to read directory {}, Error: {:?}",
                    dir.as_ref().to_string_lossy(),
                    e
                ));
            }
            Ok(mut readdir) => {
                while let Some(Ok(entry)) = readdir.next() {
                    let path = entry.path();
                    if path.extension() == Some(&suffix) {
                        match parse_config(&path) {
                            Ok(project_config) => {
                                project_configs.push(project_config);
                            }
                            Err(e) => {
                                return Err(format_err!(
                                    "Failed to parse {}: {}",
                                    path.to_string_lossy(),
                                    e.to_string()
                                ));
                            }
                        }
                    }
                }
            }
        }
        Ok(Self { minimum_sample_rate_sec, project_configs })
    }
}

#[cfg(test)]
mod tests {
    use super::SamplerConfig;
    use std::fs;

    #[fuchsia::test]
    fn parse_valid_configs() {
        let dir = tempfile::tempdir().unwrap();
        let config_path = dir.path().join("config");
        fs::create_dir(&config_path).unwrap();
        fs::write(config_path.join("ok.json"), r#"{
  "project_id": 5,
  "poll_rate_sec": 60,
  "metrics": [
    {
      // Test comment for json5 portability.
      "selector": "bootstrap/archivist:root/all_archive_accessor:inspect_batch_iterator_get_next_requests",
      "metric_id": 1,
      "metric_type": "Occurrence",
      "event_codes": [0, 0]
    }
  ]
}
"#).unwrap();
        fs::write(config_path.join("ignored.txt"), "This file is ignored").unwrap();
        fs::write(
            config_path.join("also_ok.json"),
            r#"{
  "project_id": 5,
  "poll_rate_sec": 3,
  "metrics": [
    {
      "selector": "single_counter_test_component.cmx:root:counter",
      "metric_id": 1,
      "metric_type": "Occurrence",
      "event_codes": [0, 0]
    }
  ]
}
"#,
        )
        .unwrap();

        let config = SamplerConfig::from_directory(10, &config_path);
        assert!(config.is_ok());
        assert_eq!(config.unwrap().project_configs.len(), 2);
    }

    #[fuchsia::test]
    fn parse_one_valid_one_invalid_config() {
        let dir = tempfile::tempdir().unwrap();
        let config_path = dir.path().join("config");
        fs::create_dir(&config_path).unwrap();
        fs::write(config_path.join("ok.json"), r#"{
  "project_id": 5,
  "poll_rate_sec": 60,
  "metrics": [
    {
      // Test comment for json5 portability.
      "selector": "bootstrap/archivist:root/all_archive_accessor:inspect_batch_iterator_get_next_requests",
      "metric_id": 1,
      "metric_type": "Occurrence",
      "event_codes": [0, 0]
    }
  ]
}
"#).unwrap();
        fs::write(config_path.join("ignored.txt"), "This file is ignored").unwrap();
        fs::write(
            config_path.join("invalid.json"),
            r#"{
  "project_id": 5,
  "poll_rate_sec": 3,
  "invalid_field": "bad bad bad"
}
"#,
        )
        .unwrap();

        let config = SamplerConfig::from_directory(10, &config_path);
        assert!(config.is_err());
    }

    #[fuchsia::test]
    fn parse_optional_args() {
        let dir = tempfile::tempdir().unwrap();
        let config_path = dir.path().join("config");
        fs::create_dir(&config_path).unwrap();
        fs::write(config_path.join("true.json"), r#"{
  "project_id": 5,
  "poll_rate_sec": 60,
  "metrics": [
    {
      // Test comment for json5 portability.
      "selector": "bootstrap/archivist:root/all_archive_accessor:inspect_batch_iterator_get_next_requests",
      "metric_id": 1,
      "metric_type": "Occurrence",
      "event_codes": [0, 0],
      "upload_once": true,
    }
  ]
}
"#).unwrap();

        fs::write(
            config_path.join("false.json"), r#"{
  "project_id": 5,
  "poll_rate_sec": 60,
  "metrics": [
    {
      // Test comment for json5 portability.
      "selector": "bootstrap/archivist:root/all_archive_accessor:inspect_batch_iterator_get_next_requests",
      "metric_id": 1,
      "metric_type": "Occurrence",
      "event_codes": [0, 0],
      "upload_once": false,
    }
  ]
}
"#).unwrap();

        let config = SamplerConfig::from_directory(10, &config_path);
        assert!(config.is_ok());
        assert_eq!(config.unwrap().project_configs.len(), 2);
    }

    #[fuchsia::test]
    fn default_customer_id() {
        let dir = tempfile::tempdir().unwrap();
        let config_path = dir.path().join("config");
        fs::create_dir(&config_path).unwrap();
        fs::write(config_path.join("1default.json"), r#"{
  "project_id": 5,
  "poll_rate_sec": 60,
  "metrics": [
    {
      "selector": "bootstrap/archivist:root/all_archive_accessor:inspect_batch_iterator_get_next_requests",
      "metric_id": 1,
      "metric_type": "Occurrence",
      "event_codes": [0, 0]
    }
  ]
}
"#).unwrap();
        fs::write(
            config_path.join("2with_customer_id.json"),
            r#"{
  "customer_id": 6,
  "project_id": 5,
  "poll_rate_sec": 3,
  "metrics": [
    {
      "selector": "single_counter_test_component.cmx:root:counter",
      "metric_id": 1,
      "metric_type": "Occurrence",
      "event_codes": [0, 0]
    }
  ]
}
"#,
        )
        .unwrap();

        let config = SamplerConfig::from_directory(10, &config_path);
        assert!(config.is_ok());
        assert_eq!(config.as_ref().unwrap().project_configs.len(), 2);
        assert_eq!(config.as_ref().unwrap().project_configs[0].customer_id, 1);
        assert_eq!(config.as_ref().unwrap().project_configs[1].customer_id, 6);
    }
}
