// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs,
    cm_types::{symmetrical_enums, Url},
    cml::error::{Error, Location},
    fidl::encoding::encode_persistent_with_context,
    fidl_fuchsia_component_decl as fdecl, fidl_fuchsia_component_internal as component_internal,
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
    builtin_capabilities: Option<Vec<cml::Capability>>,
    use_builtin_process_launcher: Option<bool>,
    maintain_utc_clock: Option<bool>,
    num_threads: Option<u32>,
    root_component_url: Option<Url>,
    component_id_index_path: Option<String>,
    log_destination: Option<LogDestination>,
    log_all_events: Option<bool>,
    builtin_boot_resolver: Option<BuiltinBootResolver>,
    reboot_on_terminate_enabled: Option<bool>,
    realm_builder_resolver_and_runner: Option<RealmBuilderResolverAndRunner>,
    disable_introspection: Option<bool>,
}

#[derive(Deserialize, Debug)]
#[serde(rename_all = "snake_case")]
enum LogDestination {
    Syslog,
    Klog,
}

symmetrical_enums!(LogDestination, component_internal::LogDestination, Syslog, Klog);

#[derive(Deserialize, Debug)]
#[serde(rename_all = "snake_case")]
enum BuiltinBootResolver {
    None,
    Boot,
}

symmetrical_enums!(BuiltinBootResolver, component_internal::BuiltinBootResolver, None, Boot);

#[derive(Deserialize, Debug)]
#[serde(rename_all = "snake_case")]
enum RealmBuilderResolverAndRunner {
    None,
    Namespace,
}

symmetrical_enums!(
    RealmBuilderResolverAndRunner,
    component_internal::RealmBuilderResolverAndRunner,
    None,
    Namespace
);

#[derive(Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct SecurityPolicy {
    job_policy: Option<JobPolicyAllowlists>,
    capability_policy: Option<Vec<CapabilityAllowlistEntry>>,
    debug_registration_policy: Option<Vec<DebugRegistrationAllowlistEntry>>,
    child_policy: Option<ChildPolicyAllowlists>,
}

#[derive(Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct JobPolicyAllowlists {
    ambient_mark_vmo_exec: Option<Vec<String>>,
    main_process_critical: Option<Vec<String>>,
    create_raw_processes: Option<Vec<String>>,
}

#[derive(Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct ChildPolicyAllowlists {
    reboot_on_terminate: Option<Vec<String>>,
}

#[derive(Deserialize, Debug, Clone)]
#[serde(rename_all = "lowercase")]
pub enum CapabilityTypeName {
    Directory,
    Event,
    Protocol,
    Service,
    Storage,
    Runner,
    Resolver,
}

impl Into<component_internal::AllowlistedCapability> for CapabilityTypeName {
    fn into(self) -> component_internal::AllowlistedCapability {
        match &self {
            CapabilityTypeName::Directory => component_internal::AllowlistedCapability::Directory(
                component_internal::AllowlistedDirectory::EMPTY,
            ),
            CapabilityTypeName::Event => component_internal::AllowlistedCapability::Event(
                component_internal::AllowlistedEvent::EMPTY,
            ),
            CapabilityTypeName::Protocol => component_internal::AllowlistedCapability::Protocol(
                component_internal::AllowlistedProtocol::EMPTY,
            ),
            CapabilityTypeName::Service => component_internal::AllowlistedCapability::Service(
                component_internal::AllowlistedService::EMPTY,
            ),
            CapabilityTypeName::Storage => component_internal::AllowlistedCapability::Storage(
                component_internal::AllowlistedStorage::EMPTY,
            ),
            CapabilityTypeName::Runner => component_internal::AllowlistedCapability::Runner(
                component_internal::AllowlistedRunner::EMPTY,
            ),
            CapabilityTypeName::Resolver => component_internal::AllowlistedCapability::Resolver(
                component_internal::AllowlistedResolver::EMPTY,
            ),
        }
    }
}

#[derive(Deserialize, Debug, Clone)]
#[serde(rename_all = "lowercase")]
pub enum DebugRegistrationTypeName {
    Protocol,
}

impl Into<component_internal::AllowlistedDebugRegistration> for DebugRegistrationTypeName {
    fn into(self) -> component_internal::AllowlistedDebugRegistration {
        match &self {
            DebugRegistrationTypeName::Protocol => {
                component_internal::AllowlistedDebugRegistration::Protocol(
                    component_internal::AllowlistedProtocol::EMPTY,
                )
            }
        }
    }
}

#[derive(Deserialize, Debug, Clone)]
#[serde(rename_all = "lowercase")]
pub enum CapabilityFrom {
    Capability,
    Component,
    Framework,
}

impl Into<fdecl::Ref> for CapabilityFrom {
    fn into(self) -> fdecl::Ref {
        match &self {
            CapabilityFrom::Capability => {
                fdecl::Ref::Capability(fdecl::CapabilityRef { name: "".into() })
            }
            CapabilityFrom::Component => fdecl::Ref::Self_(fdecl::SelfRef {}),
            CapabilityFrom::Framework => fdecl::Ref::Framework(fdecl::FrameworkRef {}),
        }
    }
}

#[derive(Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct CapabilityAllowlistEntry {
    source_moniker: Option<String>,
    source_name: Option<String>,
    source: Option<CapabilityFrom>,
    capability: Option<CapabilityTypeName>,
    target_monikers: Option<Vec<String>>,
}

#[derive(Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct DebugRegistrationAllowlistEntry {
    source_moniker: Option<String>,
    source_name: Option<String>,
    debug: Option<DebugRegistrationTypeName>,
    target_moniker: Option<String>,
    environment_name: Option<String>,
}

impl TryFrom<Config> for component_internal::Config {
    type Error = Error;

    fn try_from(config: Config) -> Result<Self, Error> {
        // Validate "namespace_capabilities".
        if let Some(capabilities) = config.namespace_capabilities.as_ref() {
            let mut used_ids = HashSet::new();
            for capability in capabilities {
                Config::validate_namespace_capability(capability, &mut used_ids)?;
            }
        }
        // Validate "builtin_capabilities".
        if let Some(capabilities) = config.builtin_capabilities.as_ref() {
            let mut used_ids = HashSet::new();
            for capability in capabilities {
                Config::validate_builtin_capability(capability, &mut used_ids)?;
            }
        }

        Ok(Self {
            debug: config.debug,
            disable_introspection: config.disable_introspection,
            use_builtin_process_launcher: config.use_builtin_process_launcher,
            maintain_utc_clock: config.maintain_utc_clock,
            list_children_batch_size: config.list_children_batch_size,
            security_policy: Some(translate_security_policy(config.security_policy)),
            namespace_capabilities: config
                .namespace_capabilities
                .as_ref()
                .map(|c| cml::translate::translate_capabilities(c, false))
                .transpose()?,
            builtin_capabilities: config
                .builtin_capabilities
                .as_ref()
                .map(|c| cml::translate::translate_capabilities(c, true))
                .transpose()?,
            num_threads: config.num_threads,
            root_component_url: match config.root_component_url {
                Some(root_component_url) => Some(root_component_url.as_str().to_string()),
                None => None,
            },
            component_id_index_path: config.component_id_index_path,
            log_destination: config.log_destination.map(|d| d.into()),
            log_all_events: config.log_all_events,
            builtin_boot_resolver: config.builtin_boot_resolver.map(Into::into),
            reboot_on_terminate_enabled: config.reboot_on_terminate_enabled,
            realm_builder_resolver_and_runner: config
                .realm_builder_resolver_and_runner
                .map(Into::into),
            ..Self::EMPTY
        })
    }
}

// TODO: Instead of returning a "default" security_policy when it's not specified in JSON,
// could we return None instead?
fn translate_security_policy(
    security_policy: Option<SecurityPolicy>,
) -> component_internal::SecurityPolicy {
    let SecurityPolicy { job_policy, capability_policy, debug_registration_policy, child_policy } =
        security_policy.unwrap_or_default();
    component_internal::SecurityPolicy {
        job_policy: job_policy.map(translate_job_policy),
        capability_policy: capability_policy.map(translate_capability_policy),
        debug_registration_policy: debug_registration_policy
            .map(translate_debug_registration_policy),
        child_policy: child_policy.map(translate_child_policy),
        ..component_internal::SecurityPolicy::EMPTY
    }
}

fn translate_job_policy(
    job_policy: JobPolicyAllowlists,
) -> component_internal::JobPolicyAllowlists {
    component_internal::JobPolicyAllowlists {
        ambient_mark_vmo_exec: job_policy.ambient_mark_vmo_exec,
        main_process_critical: job_policy.main_process_critical,
        create_raw_processes: job_policy.create_raw_processes,
        ..component_internal::JobPolicyAllowlists::EMPTY
    }
}

fn translate_child_policy(
    child_policy: ChildPolicyAllowlists,
) -> component_internal::ChildPolicyAllowlists {
    component_internal::ChildPolicyAllowlists {
        reboot_on_terminate: child_policy.reboot_on_terminate,
        ..component_internal::ChildPolicyAllowlists::EMPTY
    }
}

fn translate_capability_policy(
    capability_policy: Vec<CapabilityAllowlistEntry>,
) -> component_internal::CapabilityPolicyAllowlists {
    let allowlist = capability_policy
        .iter()
        .map(|e| component_internal::CapabilityAllowlistEntry {
            source_moniker: e.source_moniker.clone(),
            source_name: e.source_name.clone(),
            source: e.source.clone().map_or_else(|| None, |t| Some(t.into())),
            capability: e.capability.clone().map_or_else(|| None, |t| Some(t.into())),
            target_monikers: e.target_monikers.clone(),
            ..component_internal::CapabilityAllowlistEntry::EMPTY
        })
        .collect::<Vec<_>>();
    component_internal::CapabilityPolicyAllowlists {
        allowlist: Some(allowlist),
        ..component_internal::CapabilityPolicyAllowlists::EMPTY
    }
}

fn translate_debug_registration_policy(
    debug_registration_policy: Vec<DebugRegistrationAllowlistEntry>,
) -> component_internal::DebugRegistrationPolicyAllowlists {
    let allowlist = debug_registration_policy
        .iter()
        .map(|e| component_internal::DebugRegistrationAllowlistEntry {
            source_moniker: e.source_moniker.clone(),
            source_name: e.source_name.clone(),
            debug: e.debug.clone().map_or_else(|| None, |t| Some(t.into())),
            target_moniker: e.target_moniker.clone(),
            environment_name: e.environment_name.clone(),
            ..component_internal::DebugRegistrationAllowlistEntry::EMPTY
        })
        .collect::<Vec<_>>();
    component_internal::DebugRegistrationPolicyAllowlists {
        allowlist: Some(allowlist),
        ..component_internal::DebugRegistrationPolicyAllowlists::EMPTY
    }
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
        extend_if_unset!(self, another, disable_introspection);
        extend_if_unset!(self, another, use_builtin_process_launcher);
        extend_if_unset!(self, another, maintain_utc_clock);
        extend_if_unset!(self, another, list_children_batch_size);
        extend_if_unset!(self, another, security_policy);
        extend_if_unset!(self, another, namespace_capabilities);
        extend_if_unset!(self, another, builtin_capabilities);
        extend_if_unset!(self, another, num_threads);
        extend_if_unset!(self, another, root_component_url);
        extend_if_unset!(self, another, component_id_index_path);
        extend_if_unset!(self, another, log_destination);
        extend_if_unset!(self, another, log_all_events);
        extend_if_unset!(self, another, builtin_boot_resolver);
        extend_if_unset!(self, another, reboot_on_terminate_enabled);
        extend_if_unset!(self, another, realm_builder_resolver_and_runner);
        Ok(self)
    }

    fn validate_namespace_capability(
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
        if capability.event.is_some() {
            return Err(Error::validate("\"event\" is not supported for namespace capabilities"));
        }

        // Disallow multiple capability ids of the same name.
        let capability_ids = cml::CapabilityId::from_capability(capability)?;
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

    fn validate_builtin_capability(
        capability: &cml::Capability,
        used_ids: &mut HashSet<String>,
    ) -> Result<(), Error> {
        if capability.storage.is_some() {
            return Err(Error::validate("\"storage\" is not supported for built-in capabilities"));
        }
        if capability.directory.is_some() && capability.rights.is_none() {
            return Err(Error::validate("\"rights\" should be present with \"directory\""));
        }
        if capability.path.is_some() {
            return Err(Error::validate(
                "\"path\" should not be present for built-in capabilities",
            ));
        }

        // Disallow multiple capability ids of the same name.
        let capability_ids = cml::CapabilityId::from_capability(capability)?;
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
    let bytes = encode_persistent_with_context(
        &fidl::encoding::Context { wire_format_version: fidl::encoding::WireFormatVersion::V2 },
        &mut config_fidl,
    )
    .map_err(|e| Error::FidlEncoding(e))?;
    let mut file = File::create(args.output).map_err(|e| Error::Io(e))?;
    file.write_all(&bytes).map_err(|e| Error::Io(e))?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        assert_matches::assert_matches, fidl::encoding::decode_persistent,
        fidl_fuchsia_component_decl as fdecl, fidl_fuchsia_io as fio, std::io::Read,
        tempfile::TempDir,
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
            disable_introspection: true,
            list_children_batch_size: 123,
            maintain_utc_clock: false,
            use_builtin_process_launcher: true,
            security_policy: {
                job_policy: {
                    main_process_critical: [ "/", "/bar" ],
                    ambient_mark_vmo_exec: ["/foo"],
                    create_raw_processes: ["/baz"],
                },
                capability_policy: [
                    {
                        source_moniker: "<component_manager>",
                        source: "component",
                        source_name: "fuchsia.kernel.RootResource",
                        capability: "protocol",
                        target_monikers: ["/root", "/root/bootstrap", "/root/core"],
                    },
                    {
                        source_moniker: "/foo/bar",
                        source_name: "running",
                        source: "framework",
                        capability: "event",
                        target_monikers: ["/foo/bar", "/foo/bar/baz"],
                    },
                ],
                debug_registration_policy: [
                    {
                        source_moniker: "/foo/bar",
                        source_name: "fuchsia.kernel.RootResource",
                        debug: "protocol",
                        target_moniker: "/foo",
                        environment_name: "my_env",
                    },
                ],
                child_policy: {
                    reboot_on_terminate: [ "/buz" ],
                },
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
            builtin_capabilities: [
                {
                    protocol: "foo_protocol",
                },
                {
                    directory: "foo_dir",
                    rights: [ "connect" ],
                },
                {
                    service: "foo_svc",
                },
                {
                    runner: "foo_runner",
                },
                {
                    resolver: "foo_resolver",
                },
                {
                    event: "foo_event",
                },
                {
                    event_stream: "foo_event_stream",
                }
            ],
            num_threads: 321,
            root_component_url: "fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx",
            component_id_index_path: "/this/is/an/absolute/path",
            log_destination: "klog",
            log_all_events: true,
            builtin_boot_resolver: "boot",
            reboot_on_terminate_enabled: true,
            realm_builder_resolver_and_runner: "namespace",
        }"#;
        let config = compile_str(input).expect("failed to compile");
        assert_eq!(
            config,
            component_internal::Config {
                debug: Some(true),
                disable_introspection: Some(true),
                maintain_utc_clock: Some(false),
                use_builtin_process_launcher: Some(true),
                list_children_batch_size: Some(123),
                security_policy: Some(component_internal::SecurityPolicy {
                    job_policy: Some(component_internal::JobPolicyAllowlists {
                        main_process_critical: Some(vec!["/".to_string(), "/bar".to_string()]),
                        ambient_mark_vmo_exec: Some(vec!["/foo".to_string()]),
                        create_raw_processes: Some(vec!["/baz".to_string()]),
                        ..component_internal::JobPolicyAllowlists::EMPTY
                    }),
                    capability_policy: Some(component_internal::CapabilityPolicyAllowlists {
                        allowlist: Some(vec![
                            component_internal::CapabilityAllowlistEntry {
                                source_moniker: Some("<component_manager>".to_string()),
                                source_name: Some("fuchsia.kernel.RootResource".to_string()),
                                source: Some(fdecl::Ref::Self_(fdecl::SelfRef {})),
                                capability: Some(
                                    component_internal::AllowlistedCapability::Protocol(
                                        component_internal::AllowlistedProtocol::EMPTY
                                    )
                                ),
                                target_monikers: Some(vec![
                                    "/root".to_string(),
                                    "/root/bootstrap".to_string(),
                                    "/root/core".to_string()
                                ]),
                                ..component_internal::CapabilityAllowlistEntry::EMPTY
                            },
                            component_internal::CapabilityAllowlistEntry {
                                source_moniker: Some("/foo/bar".to_string()),
                                capability: Some(component_internal::AllowlistedCapability::Event(
                                    component_internal::AllowlistedEvent::EMPTY
                                )),
                                source_name: Some("running".to_string()),
                                source: Some(fdecl::Ref::Framework(fdecl::FrameworkRef {})),
                                target_monikers: Some(vec![
                                    "/foo/bar".to_string(),
                                    "/foo/bar/baz".to_string()
                                ]),
                                ..component_internal::CapabilityAllowlistEntry::EMPTY
                            },
                        ]),
                        ..component_internal::CapabilityPolicyAllowlists::EMPTY
                    }),
                    debug_registration_policy: Some(
                        component_internal::DebugRegistrationPolicyAllowlists {
                            allowlist: Some(vec![
                                component_internal::DebugRegistrationAllowlistEntry {
                                    source_moniker: Some("/foo/bar".to_string()),
                                    source_name: Some("fuchsia.kernel.RootResource".to_string()),
                                    debug: Some(
                                        component_internal::AllowlistedDebugRegistration::Protocol(
                                            component_internal::AllowlistedProtocol::EMPTY
                                        )
                                    ),
                                    target_moniker: Some("/foo".to_string()),
                                    environment_name: Some("my_env".to_string()),
                                    ..component_internal::DebugRegistrationAllowlistEntry::EMPTY
                                }
                            ]),
                            ..component_internal::DebugRegistrationPolicyAllowlists::EMPTY
                        }
                    ),
                    child_policy: Some(component_internal::ChildPolicyAllowlists {
                        reboot_on_terminate: Some(vec!["/buz".to_string()]),
                        ..component_internal::ChildPolicyAllowlists::EMPTY
                    }),
                    ..component_internal::SecurityPolicy::EMPTY
                }),
                namespace_capabilities: Some(vec![
                    fdecl::Capability::Protocol(fdecl::Protocol {
                        name: Some("foo_svc".into()),
                        source_path: Some("/svc/foo_svc".into()),
                        ..fdecl::Protocol::EMPTY
                    }),
                    fdecl::Capability::Directory(fdecl::Directory {
                        name: Some("bar_dir".into()),
                        source_path: Some("/bar".into()),
                        rights: Some(fio::Operations::CONNECT),
                        ..fdecl::Directory::EMPTY
                    }),
                ]),
                builtin_capabilities: Some(vec![
                    fdecl::Capability::Protocol(fdecl::Protocol {
                        name: Some("foo_protocol".into()),
                        source_path: None,
                        ..fdecl::Protocol::EMPTY
                    }),
                    fdecl::Capability::Directory(fdecl::Directory {
                        name: Some("foo_dir".into()),
                        source_path: None,
                        rights: Some(fio::Operations::CONNECT),
                        ..fdecl::Directory::EMPTY
                    }),
                    fdecl::Capability::Service(fdecl::Service {
                        name: Some("foo_svc".into()),
                        source_path: None,
                        ..fdecl::Service::EMPTY
                    }),
                    fdecl::Capability::Runner(fdecl::Runner {
                        name: Some("foo_runner".into()),
                        source_path: None,
                        ..fdecl::Runner::EMPTY
                    }),
                    fdecl::Capability::Resolver(fdecl::Resolver {
                        name: Some("foo_resolver".into()),
                        source_path: None,
                        ..fdecl::Resolver::EMPTY
                    }),
                    fdecl::Capability::Event(fdecl::Event {
                        name: Some("foo_event".into()),
                        ..fdecl::Event::EMPTY
                    }),
                    fdecl::Capability::EventStream(fdecl::EventStream {
                        name: Some("foo_event_stream".into()),
                        ..fdecl::EventStream::EMPTY
                    }),
                ]),
                num_threads: Some(321),
                root_component_url: Some("fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx".to_string()),
                component_id_index_path: Some("/this/is/an/absolute/path".to_string()),
                log_destination: Some(component_internal::LogDestination::Klog),
                log_all_events: Some(true),
                builtin_boot_resolver: Some(component_internal::BuiltinBootResolver::Boot),
                reboot_on_terminate_enabled: Some(true),
                realm_builder_resolver_and_runner: Some(
                    component_internal::RealmBuilderResolverAndRunner::Namespace
                ),
                ..component_internal::Config::EMPTY
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
        {
            let input = r#"{
            namespace_capabilities: [
                {
                    event: "foo",
                },
            ],
        }"#;
            assert_matches!(
                compile_str(input),
                Err(Error::Validate { schema_name: None, err, .. } )
                    if &err == "\"event\" is not supported for namespace capabilities"
            );
        }
    }

    #[test]
    fn test_validate_builtin_capabilities() {
        {
            let input = r#"{
            builtin_capabilities: [
                {
                    protocol: "foo",
                },
                {
                    directory: "foo",
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
            builtin_capabilities: [
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
            builtin_capabilities: [
                {
                    storage: "foo",
                },
            ],
        }"#;
            assert_matches!(
                compile_str(input),
                Err(Error::Validate { schema_name: None, err, .. } )
                    if &err == "\"storage\" is not supported for built-in capabilities"
            );
        }
        {
            let input = r#"{
            builtin_capabilities: [
                {
                    runner: "foo",
                    path: "/foo",
                },
            ],
        }"#;
            assert_matches!(
                compile_str(input),
                Err(Error::Validate { schema_name: None, err, .. } )
                    if &err == "\"path\" should not be present for built-in capabilities"
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
        let another_input =
            "{\"list_children_batch_size\": 42, \"reboot_on_terminate_enabled\": true}";
        File::create(&another_input_path).unwrap().write_all(another_input.as_bytes()).unwrap();

        let args =
            Args { output: output_path.clone(), input: vec![input_path, another_input_path] };
        compile(args)?;

        let mut bytes = Vec::new();
        File::open(output_path)?.read_to_end(&mut bytes)?;
        let config: component_internal::Config = decode_persistent(&bytes)?;
        assert_eq!(config.debug, Some(true));
        assert_eq!(config.list_children_batch_size, Some(42));
        assert_eq!(config.reboot_on_terminate_enabled, Some(true));
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
