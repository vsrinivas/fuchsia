// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs,
    cm_types::{symmetrical_enums, Url},
    cml::error::{Error, Location},
    fidl::encoding::encode_persistent,
    fidl_fuchsia_component_internal as component_internal, fidl_fuchsia_sys2 as fsys,
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
    use_builtin_process_launcher: Option<bool>,
    maintain_utc_clock: Option<bool>,
    num_threads: Option<u32>,
    builtin_pkg_resolver: Option<BuiltinPkgResolver>,
    out_dir_contents: Option<OutDirContents>,
    root_component_url: Option<Url>,
}

#[derive(Deserialize, Debug)]
#[serde(rename_all = "snake_case")]
enum BuiltinPkgResolver {
    None,
    AppmgrBridge,
    PkgfsBase,
}

symmetrical_enums!(
    BuiltinPkgResolver,
    component_internal::BuiltinPkgResolver,
    None,
    AppmgrBridge,
    PkgfsBase
);

impl std::default::Default for BuiltinPkgResolver {
    fn default() -> Self {
        BuiltinPkgResolver::None
    }
}

#[derive(Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct SecurityPolicy {
    job_policy: Option<JobPolicyAllowlists>,
    capability_policy: Option<Vec<CapabilityAllowlistEntry>>,
}

#[derive(Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct JobPolicyAllowlists {
    ambient_mark_vmo_exec: Option<Vec<String>>,
    main_process_critical: Option<Vec<String>>,
}

#[derive(Deserialize, Copy, Clone, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(u32)]
pub enum OutDirContents {
    None,
    Hub,
    Svc,
}

symmetrical_enums!(OutDirContents, component_internal::OutDirContents, None, Hub, Svc);

impl std::default::Default for OutDirContents {
    fn default() -> Self {
        OutDirContents::None
    }
}

pub trait AllowlistedCapabilityExt {
    fn get_source_name(&self) -> String;
    fn get_source(&self) -> Option<&fsys::Ref>;
}

#[derive(Deserialize, Debug, Clone)]
#[serde(rename_all = "lowercase", tag = "type")]
pub enum AllowlistedCapability {
    Directory { source_name: String },
    Event { source_name: String, source: CapabilityFrom },
    Protocol { source_name: String },
    Service { source_name: String },
    Storage { source_name: String },
    Runner { source_name: String },
    Resolver { source_name: String },
}

impl Into<component_internal::AllowlistedCapability> for AllowlistedCapability {
    fn into(self) -> component_internal::AllowlistedCapability {
        match &self {
            AllowlistedCapability::Directory { source_name } => {
                component_internal::AllowlistedCapability::Directory(
                    component_internal::AllowlistedDirectory {
                        source_name: Some(source_name.clone()),
                    },
                )
            }
            AllowlistedCapability::Event { source_name, source } => {
                component_internal::AllowlistedCapability::Event(
                    component_internal::AllowlistedEvent {
                        source_name: Some(source_name.clone()),
                        source: Some(source.clone().into()),
                    },
                )
            }
            AllowlistedCapability::Protocol { source_name } => {
                component_internal::AllowlistedCapability::Protocol(
                    component_internal::AllowlistedProtocol {
                        source_name: Some(source_name.clone()),
                    },
                )
            }
            AllowlistedCapability::Service { source_name } => {
                component_internal::AllowlistedCapability::Service(
                    component_internal::AllowlistedService {
                        source_name: Some(source_name.clone()),
                    },
                )
            }
            AllowlistedCapability::Storage { source_name } => {
                component_internal::AllowlistedCapability::Storage(
                    component_internal::AllowlistedStorage {
                        source_name: Some(source_name.clone()),
                    },
                )
            }
            AllowlistedCapability::Runner { source_name } => {
                component_internal::AllowlistedCapability::Runner(
                    component_internal::AllowlistedRunner {
                        source_name: Some(source_name.clone()),
                    },
                )
            }
            AllowlistedCapability::Resolver { source_name } => {
                component_internal::AllowlistedCapability::Resolver(
                    component_internal::AllowlistedResolver {
                        source_name: Some(source_name.clone()),
                    },
                )
            }
        }
    }
}

#[derive(Deserialize, Debug, Clone)]
#[serde(rename_all = "lowercase")]
pub enum CapabilityFrom {
    Component,
    Framework,
}

impl Into<fsys::Ref> for CapabilityFrom {
    fn into(self) -> fsys::Ref {
        match &self {
            CapabilityFrom::Component => fsys::Ref::Self_(fsys::SelfRef {}),
            CapabilityFrom::Framework => fsys::Ref::Framework(fsys::FrameworkRef {}),
        }
    }
}

#[derive(Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct CapabilityAllowlistEntry {
    source_moniker: Option<String>,
    capability: Option<AllowlistedCapability>,
    target_monikers: Option<Vec<String>>,
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
            use_builtin_process_launcher: config.use_builtin_process_launcher,
            maintain_utc_clock: config.maintain_utc_clock,
            list_children_batch_size: config.list_children_batch_size,
            security_policy: Some(translate_security_policy(config.security_policy)),
            builtin_pkg_resolver: match config.builtin_pkg_resolver {
                Some(builtin_pkg_resolver) => Some(builtin_pkg_resolver.into()),
                None => None,
            },
            namespace_capabilities: config
                .namespace_capabilities
                .as_ref()
                .map(|c| cml::translate::translate_capabilities(c))
                .transpose()?,
            num_threads: config.num_threads,
            out_dir_contents: match config.out_dir_contents {
                Some(out_dir_contents) => Some(out_dir_contents.into()),
                None => None,
            },
            root_component_url: match config.root_component_url {
                Some(root_component_url) => Some(root_component_url.as_str().to_string()),
                None => None,
            },
        })
    }
}

// TODO: Instead of returning a "default" security_policy when it's not specified in JSON,
// could we return None instead?
fn translate_security_policy(
    security_policy: Option<SecurityPolicy>,
) -> component_internal::SecurityPolicy {
    let SecurityPolicy { job_policy, capability_policy } = security_policy.unwrap_or_default();
    component_internal::SecurityPolicy {
        job_policy: job_policy.map(translate_job_policy),
        capability_policy: capability_policy.map(translate_capability_policy),
    }
}

fn translate_job_policy(
    job_policy: JobPolicyAllowlists,
) -> component_internal::JobPolicyAllowlists {
    component_internal::JobPolicyAllowlists {
        ambient_mark_vmo_exec: job_policy.ambient_mark_vmo_exec,
        main_process_critical: job_policy.main_process_critical,
    }
}

fn translate_capability_policy(
    capability_policy: Vec<CapabilityAllowlistEntry>,
) -> component_internal::CapabilityPolicyAllowlists {
    let allowlist = capability_policy
        .iter()
        .map(|e| component_internal::CapabilityAllowlistEntry {
            source_moniker: e.source_moniker.clone(),
            capability: e.capability.clone().map_or_else(|| None, |t| Some(t.into())),
            target_monikers: e.target_monikers.clone(),
        })
        .collect::<Vec<_>>();
    component_internal::CapabilityPolicyAllowlists { allowlist: Some(allowlist) }
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
                (Some(_), None) => $target.$field,
                (&None, &None) => None,
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
        extend_if_unset!(self, another, use_builtin_process_launcher);
        extend_if_unset!(self, another, maintain_utc_clock);
        extend_if_unset!(self, another, list_children_batch_size);
        extend_if_unset!(self, another, security_policy);
        extend_if_unset!(self, another, namespace_capabilities);
        extend_if_unset!(self, another, num_threads);
        extend_if_unset!(self, another, builtin_pkg_resolver);
        extend_if_unset!(self, another, out_dir_contents);
        extend_if_unset!(self, another, root_component_url);
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
    let configs =
        args.input.iter().map(Config::from_json_file).collect::<Result<Vec<Config>, _>>()?;
    let config_json =
        configs.into_iter().try_fold(Config::default(), |acc, next| acc.extend(next))?;
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
            maintain_utc_clock: false,
            use_builtin_process_launcher: true,
            builtin_pkg_resolver: "pkgfs_base",
            security_policy: {
                job_policy: {
                    main_process_critical: [ "/", "/bar" ],
                    ambient_mark_vmo_exec: ["/foo"],
                },
                capability_policy: [
                    {
                        source_moniker: "<component_manager>",
                        capability: {
                            type: "protocol",
                            source_name: "fuchsia.boot.RootResource",
                        },
                        target_monikers: ["/root", "/root/bootstrap", "/root/core"],
                    },
                    {
                        source_moniker: "/foo/bar",
                        capability: {
                            type: "event",
                            source_name: "running",
                            source: "framework",
                        },
                        target_monikers: ["/foo/bar", "/foo/bar/baz"],
                    },
                ]
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
            num_threads: 321,
            out_dir_contents: "Svc",
            root_component_url: "fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx",
        }"#;
        let config = compile_str(input).expect("failed to compile");
        assert_eq!(
            config,
            component_internal::Config {
                debug: Some(true),
                maintain_utc_clock: Some(false),
                use_builtin_process_launcher: Some(true),
                list_children_batch_size: Some(123),
                builtin_pkg_resolver: Some(component_internal::BuiltinPkgResolver::PkgfsBase),
                security_policy: Some(component_internal::SecurityPolicy {
                    job_policy: Some(component_internal::JobPolicyAllowlists {
                        main_process_critical: Some(vec!["/".to_string(), "/bar".to_string()]),
                        ambient_mark_vmo_exec: Some(vec!["/foo".to_string()]),
                    }),
                    capability_policy: Some(component_internal::CapabilityPolicyAllowlists {
                        allowlist: Some(vec![
                            component_internal::CapabilityAllowlistEntry {
                                source_moniker: Some("<component_manager>".to_string()),
                                capability: Some(
                                    component_internal::AllowlistedCapability::Protocol(
                                        component_internal::AllowlistedProtocol {
                                            source_name: Some(
                                                "fuchsia.boot.RootResource".to_string()
                                            ),
                                        }
                                    )
                                ),
                                target_monikers: Some(vec![
                                    "/root".to_string(),
                                    "/root/bootstrap".to_string(),
                                    "/root/core".to_string()
                                ]),
                            },
                            component_internal::CapabilityAllowlistEntry {
                                source_moniker: Some("/foo/bar".to_string()),
                                capability: Some(component_internal::AllowlistedCapability::Event(
                                    component_internal::AllowlistedEvent {
                                        source_name: Some("running".to_string()),
                                        source: Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                                    }
                                )),
                                target_monikers: Some(vec![
                                    "/foo/bar".to_string(),
                                    "/foo/bar/baz".to_string()
                                ]),
                            },
                        ]),
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
                num_threads: Some(321),
                out_dir_contents: Some(component_internal::OutDirContents::Svc),
                root_component_url: Some("fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx".to_string()),
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

    #[test]
    fn test_merge() -> Result<(), Error> {
        let tmp_dir = TempDir::new().unwrap();
        let output_path = tmp_dir.path().join("config");

        let input_path = tmp_dir.path().join("foo.json");
        let input = "{\"debug\": true,}";
        File::create(&input_path).unwrap().write_all(input.as_bytes()).unwrap();

        let another_input_path = tmp_dir.path().join("bar.json");
        let another_input = "{\"list_children_batch_size\": 42,}";
        File::create(&another_input_path).unwrap().write_all(another_input.as_bytes()).unwrap();

        let args =
            Args { output: output_path.clone(), input: vec![input_path, another_input_path] };
        compile(args)?;

        let mut bytes = Vec::new();
        File::open(output_path)?.read_to_end(&mut bytes)?;
        let config: component_internal::Config = decode_persistent(&bytes)?;
        assert_eq!(config.debug, Some(true));
        assert_eq!(config.list_children_batch_size, Some(42));
        Ok(())
    }

    #[test]
    fn test_invalid_component_url() {
        let input = r#"{
            root_component_url: "not quite a valid Url",
        }"#;
        assert_matches!(compile_str(input), Err(Error::Parse { .. }));
    }
}
