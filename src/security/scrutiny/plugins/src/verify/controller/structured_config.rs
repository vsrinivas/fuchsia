// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::verify::collection::V2ComponentModel;
use anyhow::{Context as _, Result};
use config_encoder::{ConfigField, ConfigFields};
use config_value_file::field::config_value_from_json_value;
use scrutiny::prelude::*;
use serde::{Deserialize, Serialize};
use serde_json::json;
use std::{
    collections::{BTreeMap, HashSet},
    fmt::Write,
    fs::File,
    io::BufReader,
    path::PathBuf,
    sync::Arc,
};

/// A controller to extract all of the configuration values in a given build's component topology.
#[derive(Default)]
pub struct ExtractStructuredConfigController {}

impl DataController for ExtractStructuredConfigController {
    fn query(&self, model: Arc<DataModel>, _query: serde_json::Value) -> Result<serde_json::Value> {
        let V2ComponentModel { component_model, deps, .. } =
            &*model.get::<V2ComponentModel>().context("getting component model")?;
        let config_by_url = component_model
            .collect_config_by_url()
            .context("collecting configuration from model")?;

        let components =
            config_by_url.into_iter().map(|(url, fields)| (url, fields.into())).collect();
        Ok(serde_json::json!(ExtractStructuredConfigResponse { components, deps: deps.to_owned() }))
    }
}

/// Configuration extracted from a particular component topology.
#[derive(Debug, Deserialize, Serialize)]
pub struct ExtractStructuredConfigResponse {
    /// A map from component URL to configuration values.
    pub components: BTreeMap<String, ComponentConfig>,
    /// Files read in the process of extracting configuration, for build integration.
    pub deps: HashSet<PathBuf>,
}

/// Configuration for a single component.
#[derive(Debug, Deserialize, Serialize)]
pub struct ComponentConfig {
    #[serde(flatten)]
    pub fields: BTreeMap<String, serde_json::Value>,
}

impl From<ConfigFields> for ComponentConfig {
    fn from(fields: ConfigFields) -> Self {
        Self {
            fields: fields
                .fields
                .into_iter()
                .map(|field| (field.key, config_value_to_json_value(field.value)))
                .collect(),
        }
    }
}

// We can't make this the behavior of Serialize because then we wouldn't be able to Deserialize
// as a round-trip, and we can't add this as an Into impl on cm_rust::Value because we don't want
// to add a serde_json dependency to that crate.
fn config_value_to_json_value(value: cm_rust::Value) -> serde_json::Value {
    use cm_rust::{SingleValue, Value, VectorValue};
    match value {
        Value::Single(sv) => match sv {
            SingleValue::Bool(b) => b.into(),
            SingleValue::Uint8(n) => n.into(),
            SingleValue::Uint16(n) => n.into(),
            SingleValue::Uint32(n) => n.into(),
            SingleValue::Uint64(n) => n.into(),
            SingleValue::Int8(n) => n.into(),
            SingleValue::Int16(n) => n.into(),
            SingleValue::Int32(n) => n.into(),
            SingleValue::Int64(n) => n.into(),
            SingleValue::String(s) => s.into(),
        },
        Value::Vector(vv) => match vv {
            VectorValue::BoolVector(bv) => bv.into(),
            VectorValue::Uint8Vector(nv) => nv.into(),
            VectorValue::Uint16Vector(nv) => nv.into(),
            VectorValue::Uint32Vector(nv) => nv.into(),
            VectorValue::Uint64Vector(nv) => nv.into(),
            VectorValue::Int8Vector(nv) => nv.into(),
            VectorValue::Int16Vector(nv) => nv.into(),
            VectorValue::Int32Vector(nv) => nv.into(),
            VectorValue::Int64Vector(nv) => nv.into(),
            VectorValue::StringVector(sv) => sv.into(),
        },
    }
}

/// A controller which verifies that the system complies with the provided structured configuration
/// policy.
#[derive(Default)]
pub struct VerifyStructuredConfigController;

impl DataController for VerifyStructuredConfigController {
    fn query(&self, model: Arc<DataModel>, query: serde_json::Value) -> Result<serde_json::Value> {
        let request: StructuredConfigRequest =
            serde_json::from_value(query).context("parsing request to verify structured config")?;
        let mut policy_reader = BufReader::new(
            File::open(&request.policy)
                .with_context(|| format!("opening policy at {}", request.policy.display()))?,
        );
        let policy: StructuredConfigPolicy =
            serde_json5::from_reader(&mut policy_reader).context("parsing policy JSON")?;

        let V2ComponentModel { component_model, deps, .. } =
            &*model.get::<V2ComponentModel>().context("getting component model")?;
        let config_by_url = component_model
            .collect_config_by_url()
            .context("collecting configuration from model")?;

        let errors = policy.verify(config_by_url);

        let mut deps = deps.clone();
        deps.insert(request.policy);
        Ok(json!(VerifyStructuredConfigResponse { deps, errors }))
    }
}

// TODO(https://fxbug.dev/107706) Use a type that constrains values to valid component URLs.
type ComponentUrl = String;

/// Schema for a given product's structured configuration policy.
///
/// Components are indexed by URL.
///
/// Both components and their fields can be marked `transitional: true` which will allow
/// verification to pass if the transitional component or field is not present. If the component or
/// field is present, associated field policies are still enforced.
///
/// Policy is enforced for any components which are listed here, and components which are not listed
/// will not have any enforcement applied. It is not required for all components with configuration
/// to be listed in the policy.
#[derive(Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct StructuredConfigPolicy {
    /// Map from component URL to policy for that component.
    components: BTreeMap<ComponentUrl, ComponentPolicy>,
}

impl StructuredConfigPolicy {
    /// Verify that the policy is correctly implemented by the provided components. Returns a map
    /// from component URLs to verification errors.
    fn verify(
        &self,
        config_by_url: BTreeMap<String, ConfigFields>,
    ) -> BTreeMap<String, Vec<VerifyConfigError>> {
        let mut errors: BTreeMap<String, Vec<VerifyConfigError>> = Default::default();

        for (url, policy) in &self.components {
            if let Some(config) = config_by_url.get(url) {
                errors.entry(url.clone()).or_default().extend(policy.verify(config));
            } else if !policy.transitional {
                errors.entry(url.clone()).or_default().push(VerifyConfigError::ComponentNotFound);
            }
        }

        errors
    }
}

/// Policy for a particular component's configuration.
#[derive(Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
struct ComponentPolicy {
    /// Policy for each configuration field of the component. The component is allowed to have
    /// configuration fields which are not spelled out in the policy, and policy is only enforced
    /// for those fields listed.
    fields: BTreeMap<String, FieldPolicy>,

    /// Set to true to allow this component to not resolve without an error, field policy will still
    /// be enforced if the component is present.
    #[serde(default)]
    transitional: bool,
}

impl ComponentPolicy {
    fn verify(&self, config: &ConfigFields) -> Vec<VerifyConfigError> {
        let mut errors = vec![];

        for (name, policy) in &self.fields {
            if let Some(field) = config.fields.iter().find(|f| &f.key == name) {
                if let Err(e) = policy.verify(field) {
                    errors.push(e);
                }
            } else if !policy.is_transitional() {
                errors.push(VerifyConfigError::NoSuchField { field: name.clone() });
            }
        }

        errors
    }
}

/// Policy for allowed values of a particular configuration field.
///
/// Field policy can be expressed either as a JSON object with `expected_value` and `transitional`
/// fields, or as a literal JSON value if the field does not need to be marked as transitional.
#[derive(Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields, untagged)]
enum FieldPolicy {
    Explicit {
        /// Expected configuration value. It's an error if the resolved value of the field does not
        /// equal the policy-provided value.
        expected_value: serde_json::Value,

        /// Set to true to allow this field to not resolve without an error.
        #[serde(default)]
        transitional: bool,
    },

    /// Allows policy authors to just write a json value if the key is expected. Behaves the same as
    /// an explicit policy with `transitional: false`.
    Concise(serde_json::Value),
}

impl FieldPolicy {
    fn is_transitional(&self) -> bool {
        match self {
            Self::Explicit { transitional, .. } => *transitional,
            Self::Concise(..) => false,
        }
    }
    fn verify(&self, field: &ConfigField) -> Result<(), VerifyConfigError> {
        let expected_json = match self {
            Self::Explicit { expected_value, .. } => expected_value,
            Self::Concise(v) => v,
        };
        let expected =
            config_value_from_json_value(expected_json, &field.value.ty()).map_err(|e| {
                VerifyConfigError::TypeMismatch { field: field.key.clone(), error: e.to_string() }
            })?;
        if expected != field.value {
            Err(VerifyConfigError::ValueMismatch {
                field: field.key.clone(),
                expected,
                observed: field.value.clone(),
            })
        } else {
            Ok(())
        }
    }
}

#[derive(Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
struct StructuredConfigRequest {
    /// The input path to a json5 file containing `StructuredConfigPolicy`.
    pub policy: PathBuf,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct VerifyStructuredConfigResponse {
    pub deps: HashSet<PathBuf>,
    pub errors: BTreeMap<ComponentUrl, Vec<VerifyConfigError>>,
}

impl VerifyStructuredConfigResponse {
    pub fn check_errors(&self) -> Result<()> {
        aggregate_policy_errors(&self.errors)
    }
}

fn aggregate_policy_errors(errors: &BTreeMap<String, Vec<VerifyConfigError>>) -> Result<()> {
    let mut message = String::new();

    for (component, errors) in errors {
        if errors.is_empty() {
            continue;
        }
        writeln!(&mut message, "└── {component}")?;
        for error in errors {
            writeln!(&mut message, "      └── {error}")?;
        }
    }

    if message.is_empty() {
        Ok(())
    } else {
        Err(anyhow::format_err!("Failed to validate config policy:\n\n{}", message))
    }
}

#[derive(Debug, Deserialize, PartialEq, Serialize, thiserror::Error)]
pub enum VerifyConfigError {
    #[error("Component not found in the image.")]
    ComponentNotFound,
    #[error("Component does not have a config field named `{field}`.")]
    NoSuchField { field: String },
    #[error("`{field}` has a different type than expected: {error}")]
    TypeMismatch {
        field: String,
        error: String, // Can't use the original error because scrutiny needs Serialize.
    },
    #[error("`{field}` has a different value ({observed}) than expected ({expected}).")]
    ValueMismatch { field: String, expected: cm_rust::Value, observed: cm_rust::Value },
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;
    use cm_rust::{ConfigChecksum, SingleValue, Value};
    use maplit::btreemap;
    use serde_json::json;

    #[test]
    fn basic_passing_policy() {
        let policy: StructuredConfigPolicy = serde_json::from_value(json! {{
            "components": {
                "fuchsia-pkg://foo": {
                    "fields": {
                        "a": 5,
                    },
                },
            },
        }})
        .unwrap();
        let config = btreemap! {
            "fuchsia-pkg://foo".to_string() => ConfigFields {
                checksum: ConfigChecksum::Sha256([0u8; 32]),
                fields: vec![
                    ConfigField {
                        key: "a".to_string(),
                        value: Value::Single(SingleValue::Uint8(5)),
                    }
                ],
            }
        };

        aggregate_policy_errors(&policy.verify(config)).unwrap();
    }

    #[test]
    fn extra_fields_pass() {
        let policy: StructuredConfigPolicy = serde_json::from_value(json! {{
            "components": {
                "fuchsia-pkg://foo": {
                    "fields": {
                        "a": 5,
                    },
                },
            },
        }})
        .unwrap();
        let config = btreemap! {
            "fuchsia-pkg://foo".to_string() => ConfigFields {
                checksum: ConfigChecksum::Sha256([0u8; 32]),
                fields: vec![
                    ConfigField {
                        key: "a".to_string(),
                        value: Value::Single(SingleValue::Uint8(5)),
                    },
                    ConfigField {
                        key: "b".to_string(),
                        value: Value::Single(SingleValue::Uint8(10)),
                    },
                ],
            }
        };

        aggregate_policy_errors(&policy.verify(config)).unwrap();
    }

    #[test]
    fn policy_violation_fails() {
        let policy: StructuredConfigPolicy = serde_json::from_value(json! {{
            "components": {
                "fuchsia-pkg://foo": {
                    "fields": {
                        "a": 5,
                    },
                },
            },
        }})
        .unwrap();
        let config = btreemap! {
            "fuchsia-pkg://foo".to_string() => ConfigFields {
                checksum: ConfigChecksum::Sha256([0u8; 32]),
                fields: vec![
                    ConfigField {
                        key: "a".to_string(),
                        value: Value::Single(SingleValue::Uint8(10)),
                    }
                ],
            }
        };

        let res = policy.verify(config);
        assert_eq!(
            res["fuchsia-pkg://foo"][0],
            VerifyConfigError::ValueMismatch {
                field: "a".to_string(),
                expected: Value::Single(SingleValue::Uint8(5)),
                observed: Value::Single(SingleValue::Uint8(10)),
            }
        );
        aggregate_policy_errors(&res).unwrap_err();
    }

    #[test]
    fn policy_violation_with_transitional_component_fails() {
        let policy: StructuredConfigPolicy = serde_json::from_value(json! {{
            "components": {
                "fuchsia-pkg://foo": {
                    "transitional": true,
                    "fields": {
                        "a": 5,
                    },
                },
            },
        }})
        .unwrap();
        let config = btreemap! {
            "fuchsia-pkg://foo".to_string() => ConfigFields {
                checksum: ConfigChecksum::Sha256([0u8; 32]),
                fields: vec![
                    ConfigField {
                        key: "a".to_string(),
                        value: Value::Single(SingleValue::Uint8(10)),
                    }
                ],
            }
        };

        let res = policy.verify(config);
        assert_eq!(
            res["fuchsia-pkg://foo"][0],
            VerifyConfigError::ValueMismatch {
                field: "a".to_string(),
                expected: Value::Single(SingleValue::Uint8(5)),
                observed: Value::Single(SingleValue::Uint8(10)),
            }
        );
        aggregate_policy_errors(&res).unwrap_err();
    }

    #[test]
    fn policy_violation_with_transitional_field_fails() {
        let policy: StructuredConfigPolicy = serde_json::from_value(json! {{
            "components": {
                "fuchsia-pkg://foo": {
                    "fields": {
                        "a": {
                            "expected_value": 5,
                            "transitional": true,
                        },
                    },
                },
            },
        }})
        .unwrap();
        let config = btreemap! {
            "fuchsia-pkg://foo".to_string() => ConfigFields {
                checksum: ConfigChecksum::Sha256([0u8; 32]),
                fields: vec![
                    ConfigField {
                        key: "a".to_string(),
                        value: Value::Single(SingleValue::Uint8(10)),
                    }
                ],
            }
        };

        let res = policy.verify(config);
        assert_eq!(
            res["fuchsia-pkg://foo"][0],
            VerifyConfigError::ValueMismatch {
                field: "a".to_string(),
                expected: Value::Single(SingleValue::Uint8(5)),
                observed: Value::Single(SingleValue::Uint8(10)),
            }
        );
        aggregate_policy_errors(&res).unwrap_err();
    }

    #[test]
    fn missing_component_fails() {
        let policy: StructuredConfigPolicy = serde_json::from_value(json! {{
            "components": {
                "fuchsia-pkg://foo": {
                    "fields": {
                        "a": 5,
                    },
                },
            },
        }})
        .unwrap();
        let config = btreemap! {};

        let res = policy.verify(config);
        assert_eq!(res["fuchsia-pkg://foo"][0], VerifyConfigError::ComponentNotFound);
        aggregate_policy_errors(&res).unwrap_err();
    }

    #[test]
    fn missing_transitional_component_passes() {
        let policy: StructuredConfigPolicy = serde_json::from_value(json! {{
            "components": {
                "fuchsia-pkg://foo": {
                    "transitional": true,
                    "fields": {
                        "a": 5,
                    },
                },
            },
        }})
        .unwrap();
        let config = btreemap! {};

        aggregate_policy_errors(&policy.verify(config)).unwrap();
    }

    #[test]
    fn missing_field_fails() {
        let policy: StructuredConfigPolicy = serde_json::from_value(json! {{
            "components": {
                "fuchsia-pkg://foo": {
                    "fields": {
                        "a": {
                            "expected_value": 5,
                        },
                    },
                },
            },
        }})
        .unwrap();
        let config = btreemap! {
            "fuchsia-pkg://foo".to_string() => ConfigFields {
                checksum: ConfigChecksum::Sha256([0u8; 32]),
                fields: vec![],
            }
        };

        let res = policy.verify(config);
        assert_eq!(
            res["fuchsia-pkg://foo"][0],
            VerifyConfigError::NoSuchField { field: "a".to_string() }
        );
        aggregate_policy_errors(&res).unwrap_err();
    }

    #[test]
    fn missing_transitional_field_passes() {
        let policy: StructuredConfigPolicy = serde_json::from_value(json! {{
            "components": {
                "fuchsia-pkg://foo": {
                    "fields": {
                        "a": {
                            "expected_value": 5,
                            "transitional": true,
                        },
                    },
                },
            },
        }})
        .unwrap();
        let config = btreemap! {
            "fuchsia-pkg://foo".to_string() => ConfigFields {
                checksum: ConfigChecksum::Sha256([0u8; 32]),
                fields: vec![],
            }
        };

        aggregate_policy_errors(&policy.verify(config)).unwrap();
    }

    #[test]
    fn wrong_field_type_fails() {
        let policy: StructuredConfigPolicy = serde_json::from_value(json! {{
            "components": {
                "fuchsia-pkg://foo": {
                    "fields": {
                        "a": 5,
                    },
                },
            },
        }})
        .unwrap();
        let config = btreemap! {
            "fuchsia-pkg://foo".to_string() => ConfigFields {
                checksum: ConfigChecksum::Sha256([0u8; 32]),
                fields: vec![
                    ConfigField {
                        key: "a".to_string(),
                        value: Value::Single(SingleValue::String("not an int".to_string())),
                    }
                ],
            }
        };

        let res = policy.verify(config);
        aggregate_policy_errors(&res).unwrap_err();
        assert_matches!(
            &res["fuchsia-pkg://foo"][0],
            VerifyConfigError::TypeMismatch { field, .. } if field == "a"
        );
    }
}
