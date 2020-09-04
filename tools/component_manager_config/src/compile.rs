// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs,
    cml::error::{Error, Location},
    fidl::encoding::encode_persistent,
    fidl_fuchsia_component_internal as component_internal,
    serde::Deserialize,
    serde_json5,
    std::{
        collections::HashSet,
        convert::{TryFrom, TryInto},
        fs::{self, File},
        io::Write,
        path::PathBuf,
    },
};

#[derive(Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
struct Config {
    debug: Option<bool>,
    list_children_batch_size: Option<u32>,
    security_policy: Option<SecurityPolicy>,
    namespace_capabilities: Option<Vec<cml::Capability>>,
}

#[derive(Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct SecurityPolicy {
    job_policy: Option<JobPolicyAllowlists>,
}

#[derive(Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct JobPolicyAllowlists {
    ambient_mark_vmo_exec: Option<Vec<String>>,

    main_process_critical: Option<Vec<String>>,
}

impl TryFrom<Config> for component_internal::Config {
    type Error = Error;

    fn try_from(config: Config) -> Result<Self, Error> {
        // Validate "namespace_capabilities".
        if let Some(capabilities) = config.namespace_capabilities.as_ref() {
            let mut used_ids = HashSet::new();
            for capability in capabilities {
                Config::validate_capability(capability, &mut used_ids)?;
            }
        }

        Ok(Self {
            debug: config.debug,
            list_children_batch_size: config.list_children_batch_size,
            security_policy: Some(translate_security_policy(config.security_policy)),
            namespace_capabilities: config
                .namespace_capabilities
                .as_ref()
                .map(|c| cml::translate::translate_capabilities(c))
                .transpose()?,
        })
    }
}

// TODO: Instead of returning a "default" security_policy when it's not specified in JSON,
// could we return None instead?
fn translate_security_policy(
    security_policy: Option<SecurityPolicy>,
) -> component_internal::SecurityPolicy {
    component_internal::SecurityPolicy {
        job_policy: Some(translate_job_policy(security_policy.and_then(|p| p.job_policy))),
    }
}

fn translate_job_policy(
    job_policy: Option<JobPolicyAllowlists>,
) -> component_internal::JobPolicyAllowlists {
    job_policy.map_or_else(
        || component_internal::JobPolicyAllowlists {
            ambient_mark_vmo_exec: None,
            main_process_critical: None,
        },
        |p| component_internal::JobPolicyAllowlists {
            ambient_mark_vmo_exec: p.ambient_mark_vmo_exec,
            main_process_critical: p.main_process_critical,
        },
    )
}

macro_rules! extend_if_unset {
    ( $($target:expr, $value:expr, $field:ident)+ ) => {
        $(
            $target.$field = match (&$target.$field, &$value.$field) {
                (Some(_), Some(_)) => {
                    return Err(Error::parse(
                        format!("Conflicting field found: {:?}", stringify!($field)),
                        None,
                        None,
                    ))
                }
                (None, Some(_)) => $value.$field,
                _ => $target.$field,
            };
        )+
    };
}

impl Config {
    fn from_json_file(path: &PathBuf) -> Result<Self, Error> {
        let data = fs::read_to_string(path)?;
        serde_json5::from_str(&data).map_err(|e| {
            let serde_json5::Error::Message { location, msg } = e;
            let location = location.map(|l| Location { line: l.line, column: l.column });
            Error::parse(msg, location, Some(path))
        })
    }

    fn extend(mut self, another: Config) -> Result<Self, Error> {
        extend_if_unset!(self, another, debug);
        extend_if_unset!(self, another, list_children_batch_size);
        extend_if_unset!(self, another, security_policy);
        extend_if_unset!(self, another, namespace_capabilities);
        Ok(self)
    }

    fn validate_capability(
        capability: &cml::Capability,
        used_ids: &mut HashSet<String>,
    ) -> Result<(), Error> {
        if capability.directory.is_some() && capability.path.is_none() {
            return Err(Error::validate("\"path\" should be present with \"directory\""));
        }
        if capability.directory.is_some() && capability.rights.is_none() {
            return Err(Error::validate("\"rights\" should be present with \"directory\""));
        }
        if capability.storage.is_some() {
            return Err(Error::validate("\"storage\" is not supported for namespace capabilities"));
        }
        if capability.runner.is_some() {
            return Err(Error::validate("\"runner\" is not supported for namespace capabilities"));
        }
        if capability.resolver.is_some() {
            return Err(Error::validate(
                "\"resolver\" is not supported for namespace capabilities",
            ));
        }

        // Disallow multiple capability ids of the same name.
        let capability_ids =
            cml::CapabilityId::from_clause(capability, cml::RoutingClauseType::Capability)?;
        for capability_id in capability_ids {
            if !used_ids.insert(capability_id.to_string()) {
                return Err(Error::validate(format!(
                    "\"{}\" is a duplicate \"capability\" name",
                    capability_id,
                )));
            }
        }

        Ok(())
    }
}

#[derive(Debug, Default, FromArgs)]
/// Create a binary config and populate it with data from .json file.
struct Args {
    /// path to a JSON configuration file
    #[argh(option)]
    input: Vec<PathBuf>,

    /// path to the output binary config file
    #[argh(option)]
    output: PathBuf,
}

pub fn from_args() -> Result<(), Error> {
    compile(argh::from_env())
}

fn compile(args: Args) -> Result<(), Error> {
    let config_json = args
        .input
        .iter()
        .map(Config::from_json_file)
        .take_while(Result::is_ok)
        .map(Result::unwrap)
        .try_fold(Config::default(), |acc, next| acc.extend(next))?;
    let mut config_fidl: component_internal::Config = config_json.try_into()?;
    let bytes = encode_persistent(&mut config_fidl).map_err(|e| Error::FidlEncoding(e))?;
    let mut file = File::create(args.output).map_err(|e| Error::Io(e))?;
    file.write_all(&bytes).map_err(|e| Error::Io(e))?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        fidl::encoding::decode_persistent, fidl_fuchsia_io2 as fio2, fidl_fuchsia_sys2 as fsys,
        matches::assert_matches, std::io::Read, tempfile::TempDir,
    };

    fn compile_str(input: &str) -> Result<component_internal::Config, Error> {
        let tmp_dir = TempDir::new().unwrap();
        let input_path = tmp_dir.path().join("config.json");
        let output_path = tmp_dir.path().join("config.fidl");
        File::create(&input_path).unwrap().write_all(input.as_bytes()).unwrap();
        let args = Args { output: output_path.clone(), input: vec![input_path] };
        compile(args)?;
        let mut bytes = Vec::new();
        File::open(output_path)?.read_to_end(&mut bytes)?;
        let config: component_internal::Config = decode_persistent(&bytes)?;
        Ok(config)
    }

    #[test]
    fn test_compile() {
        let input = r#"{
            debug: true,
            list_children_batch_size: 123,
            security_policy: {
                job_policy: {
                    main_process_critical: [ "/", "/bar" ],
                    ambient_mark_vmo_exec: ["/foo"],
                }
            },
            namespace_capabilities: [
                {
                    protocol: "foo_svc",
                },
                {
                    directory: "bar_dir",
                    path: "/bar",
                    rights: [ "connect" ],
                },
            ],
        }"#;
        let config = compile_str(input).expect("failed to compile");
        assert_eq!(
            config,
            component_internal::Config {
                debug: Some(true),
                list_children_batch_size: Some(123),
                security_policy: Some(component_internal::SecurityPolicy {
                    job_policy: Some(component_internal::JobPolicyAllowlists {
                        main_process_critical: Some(vec!["/".to_string(), "/bar".to_string()]),
                        ambient_mark_vmo_exec: Some(vec!["/foo".to_string()]),
                    }),
                }),
                namespace_capabilities: Some(vec![
                    fsys::CapabilityDecl::Protocol(fsys::ProtocolDecl {
                        name: Some("foo_svc".into()),
                        source_path: Some("/svc/foo_svc".into()),
                    }),
                    fsys::CapabilityDecl::Directory(fsys::DirectoryDecl {
                        name: Some("bar_dir".into()),
                        source_path: Some("/bar".into()),
                        rights: Some(fio2::Operations::Connect),
                    }),
                ]),
            }
        );
    }

    #[test]
    fn test_validate_namespace_capabilities() {
        {
            let input = r#"{
            namespace_capabilities: [
                {
                    protocol: "foo",
                },
                {
                    directory: "foo",
                    path: "/foo",
                    rights: [ "connect" ],
                },
            ],
        }"#;
            assert_matches!(
                compile_str(input),
                Err(Error::Validate { schema_name: None, err, .. } )
                    if &err == "\"foo\" is a duplicate \"capability\" name"
            );
        }
        {
            let input = r#"{
            namespace_capabilities: [
                {
                    directory: "foo",
                    path: "/foo",
                },
            ],
        }"#;
            assert_matches!(
                compile_str(input),
                Err(Error::Validate { schema_name: None, err, .. } )
                    if &err == "\"rights\" should be present with \"directory\""
            );
        }
        {
            let input = r#"{
            namespace_capabilities: [
                {
                    directory: "foo",
                    rights: [ "connect" ],
                },
            ],
        }"#;
            assert_matches!(
                compile_str(input),
                Err(Error::Validate { schema_name: None, err, .. } )
                    if &err == "\"path\" should be present with \"directory\""
            );
        }
    }

    #[test]
    fn test_compile_conflict() {
        let tmp_dir = TempDir::new().unwrap();
        let output_path = tmp_dir.path().join("config");

        let input_path = tmp_dir.path().join("foo.json");
        let input = "{\"debug\": true,}";
        File::create(&input_path).unwrap().write_all(input.as_bytes()).unwrap();

        let another_input_path = tmp_dir.path().join("bar.json");
        let another_input = "{\"debug\": false,}";
        File::create(&another_input_path).unwrap().write_all(another_input.as_bytes()).unwrap();

        let args =
            Args { output: output_path.clone(), input: vec![input_path, another_input_path] };
        assert_matches!(compile(args), Err(Error::Parse { .. }));
    }
}
