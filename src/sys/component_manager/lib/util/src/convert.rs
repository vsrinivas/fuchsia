// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Functions used to convert types between `fuchsia.component.decl` and `fuchsia.sys2`
/// namespace.
use {fidl_fuchsia_component_decl as fdecl, fidl_fuchsia_sys2 as fsys};

// Temporary mappings until migration from fuchsia.sys2 -> fuchsia.component is complete.
#[macro_export]
macro_rules! enum_to_fsys {
    ($label:ident, $a:ty , $b:ty, $($id: ident),*) => {
        paste::paste! {
            pub fn [<$label _ to _ fsys>](input: $a) -> $b {
                match input {
                    $( <$a>::$id => <$b>::$id, )*
                }
            }
        }
    };
}

// Generates `startup_mode_to_fsys`.
enum_to_fsys!(startup_mode, fdecl::StartupMode, fsys::StartupMode, Lazy, Eager);

// Generates `on_terminate_to_fsys`.
enum_to_fsys!(on_terminate, fdecl::OnTerminate, fsys::OnTerminate, None, Reboot);

pub fn child_decl_to_fsys(input: fdecl::Child) -> fsys::ChildDecl {
    fsys::ChildDecl {
        name: input.name,
        url: input.url,
        startup: input.startup.map(startup_mode_to_fsys),
        environment: input.environment,
        on_terminate: input.on_terminate.map(on_terminate_to_fsys),
        ..fsys::ChildDecl::EMPTY
    }
}

pub fn collection_ref_to_fsys(input: fdecl::CollectionRef) -> fsys::CollectionRef {
    fsys::CollectionRef { name: input.name }
}

pub fn child_ref_to_fsys(input: fdecl::ChildRef) -> fsys::ChildRef {
    fsys::ChildRef { name: input.name, collection: input.collection }
}

// To seamlessly map between
// `fuchsia.sys2.ChildDecl` -> `fuchsia.component.decl.Child` (and `OfferDecl` types)
// this crate offers aliases that match old name.
#[allow(dead_code)] // The 'Offer*Decl' types are only used for tests below.
pub(crate) mod decl {
    #[allow(unused_imports)]
    pub(crate) use fidl_fuchsia_component_decl::*;

    pub(crate) type ComponentDecl = fidl_fuchsia_component_decl::Component;

    pub(crate) type ProgramDecl = fidl_fuchsia_component_decl::Program;

    pub(crate) type UseDecl = fidl_fuchsia_component_decl::Use;
    pub(crate) type UseServiceDecl = fidl_fuchsia_component_decl::UseService;
    pub(crate) type UseProtocolDecl = fidl_fuchsia_component_decl::UseProtocol;
    pub(crate) type UseDirectoryDecl = fidl_fuchsia_component_decl::UseDirectory;
    pub(crate) type UseStorageDecl = fidl_fuchsia_component_decl::UseStorage;
    pub(crate) type UseEventDecl = fidl_fuchsia_component_decl::UseEvent;
    pub(crate) type UseEventStreamDecl = fidl_fuchsia_component_decl::UseEventStream;

    pub(crate) type ExposeDecl = fidl_fuchsia_component_decl::Expose;
    pub(crate) type ExposeServiceDecl = fidl_fuchsia_component_decl::ExposeService;
    pub(crate) type ExposeProtocolDecl = fidl_fuchsia_component_decl::ExposeProtocol;
    pub(crate) type ExposeDirectoryDecl = fidl_fuchsia_component_decl::ExposeDirectory;
    pub(crate) type ExposeRunnerDecl = fidl_fuchsia_component_decl::ExposeRunner;
    pub(crate) type ExposeResolverDecl = fidl_fuchsia_component_decl::ExposeResolver;

    pub(crate) type OfferDecl = fidl_fuchsia_component_decl::Offer;
    pub(crate) type OfferStorageDecl = fidl_fuchsia_component_decl::OfferStorage;
    pub(crate) type OfferResolverDecl = fidl_fuchsia_component_decl::OfferResolver;
    pub(crate) type OfferRunnerDecl = fidl_fuchsia_component_decl::OfferRunner;
    pub(crate) type OfferServiceDecl = fidl_fuchsia_component_decl::OfferService;
    pub(crate) type OfferProtocolDecl = fidl_fuchsia_component_decl::OfferProtocol;
    pub(crate) type OfferDirectoryDecl = fidl_fuchsia_component_decl::OfferDirectory;
    pub(crate) type OfferEventDecl = fidl_fuchsia_component_decl::OfferEvent;

    pub(crate) type CapabilityDecl = fidl_fuchsia_component_decl::Capability;
    pub(crate) type ServiceDecl = fidl_fuchsia_component_decl::Service;
    pub(crate) type ProtocolDecl = fidl_fuchsia_component_decl::Protocol;
    pub(crate) type DirectoryDecl = fidl_fuchsia_component_decl::Directory;
    pub(crate) type StorageDecl = fidl_fuchsia_component_decl::Storage;
    pub(crate) type RunnerDecl = fidl_fuchsia_component_decl::Runner;
    pub(crate) type ResolverDecl = fidl_fuchsia_component_decl::Resolver;
    pub(crate) type EventDecl = fidl_fuchsia_component_decl::Event;

    pub(crate) type ChildDecl = fidl_fuchsia_component_decl::Child;
    pub(crate) type CollectionDecl = fidl_fuchsia_component_decl::Collection;
    pub(crate) type EnvironmentDecl = fidl_fuchsia_component_decl::Environment;
    pub(crate) type ConfigDecl = fidl_fuchsia_component_decl::Config;
}

#[cfg(test)]
#[allow(deprecated)]
mod tests {
    use {
        super::decl as fdecl, fidl_fuchsia_data as fdata, fidl_fuchsia_io2 as fio2,
        fidl_fuchsia_sys2 as fsys,
    };

    macro_rules! fully_populated_decl {
        ($namespace:ident) => {
            $namespace::ComponentDecl {
                program: Some($namespace::ProgramDecl {
                    runner: Some("elf".to_string()),
                    info: Some(fdata::Dictionary {
                        entries: Some(vec![
                            fdata::DictionaryEntry {
                                key: "args".to_string(),
                                value: Some(Box::new(fdata::DictionaryValue::StrVec(vec![
                                    "foo".to_string(),
                                    "bar".to_string(),
                                ]))),
                            },
                            fdata::DictionaryEntry {
                                key: "binary".to_string(),
                                value: Some(Box::new(fdata::DictionaryValue::Str(
                                    "bin/app".to_string(),
                                ))),
                            },
                        ]),
                        ..fdata::Dictionary::EMPTY
                    }),
                    ..$namespace::ProgramDecl::EMPTY
                }),
                uses: Some(vec![
                    $namespace::UseDecl::Service($namespace::UseServiceDecl {
                        dependency_type: Some($namespace::DependencyType::Strong),
                        source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                        source_name: Some("netstack".to_string()),
                        target_path: Some("/svc/mynetstack".to_string()),
                        ..$namespace::UseServiceDecl::EMPTY
                    }),
                    $namespace::UseDecl::Protocol($namespace::UseProtocolDecl {
                        dependency_type: Some($namespace::DependencyType::Strong),
                        source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                        source_name: Some("legacy_netstack".to_string()),
                        target_path: Some("/svc/legacy_mynetstack".to_string()),
                        ..$namespace::UseProtocolDecl::EMPTY
                    }),
                    $namespace::UseDecl::Protocol($namespace::UseProtocolDecl {
                        dependency_type: Some($namespace::DependencyType::Strong),
                        source: Some($namespace::Ref::Child($namespace::ChildRef {
                            name: "echo".to_string(),
                            collection: None,
                        })),
                        source_name: Some("echo_service".to_string()),
                        target_path: Some("/svc/echo_service".to_string()),
                        ..$namespace::UseProtocolDecl::EMPTY
                    }),
                    $namespace::UseDecl::Directory($namespace::UseDirectoryDecl {
                        dependency_type: Some($namespace::DependencyType::Strong),
                        source: Some($namespace::Ref::Framework($namespace::FrameworkRef {})),
                        source_name: Some("dir".to_string()),
                        target_path: Some("/data".to_string()),
                        rights: Some(fio2::Operations::Connect),
                        subdir: Some("foo/bar".to_string()),
                        ..$namespace::UseDirectoryDecl::EMPTY
                    }),
                    $namespace::UseDecl::Storage($namespace::UseStorageDecl {
                        source_name: Some("cache".to_string()),
                        target_path: Some("/cache".to_string()),
                        ..$namespace::UseStorageDecl::EMPTY
                    }),
                    $namespace::UseDecl::Storage($namespace::UseStorageDecl {
                        source_name: Some("temp".to_string()),
                        target_path: Some("/temp".to_string()),
                        ..$namespace::UseStorageDecl::EMPTY
                    }),
                    $namespace::UseDecl::Event($namespace::UseEventDecl {
                        dependency_type: Some($namespace::DependencyType::Strong),
                        source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                        source_name: Some("directory_ready".to_string()),
                        target_name: Some("diagnostics_ready".to_string()),
                        filter: Some(fdata::Dictionary {
                            entries: Some(vec![fdata::DictionaryEntry {
                                key: "path".to_string(),
                                value: Some(Box::new(fdata::DictionaryValue::Str(
                                    "/diagnostics".to_string(),
                                ))),
                            }]),
                            ..fdata::Dictionary::EMPTY
                        }),
                        mode: Some($namespace::EventMode::Sync),
                        ..$namespace::UseEventDecl::EMPTY
                    }),
                ]),
                exposes: Some(vec![
                    $namespace::ExposeDecl::Protocol($namespace::ExposeProtocolDecl {
                        source: Some($namespace::Ref::Child($namespace::ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        source_name: Some("legacy_netstack".to_string()),
                        target_name: Some("legacy_mynetstack".to_string()),
                        target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                        ..$namespace::ExposeProtocolDecl::EMPTY
                    }),
                    $namespace::ExposeDecl::Directory($namespace::ExposeDirectoryDecl {
                        source: Some($namespace::Ref::Child($namespace::ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        source_name: Some("dir".to_string()),
                        target_name: Some("data".to_string()),
                        target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                        rights: Some(fio2::Operations::Connect),
                        subdir: Some("foo/bar".to_string()),
                        ..$namespace::ExposeDirectoryDecl::EMPTY
                    }),
                    $namespace::ExposeDecl::Runner($namespace::ExposeRunnerDecl {
                        source: Some($namespace::Ref::Child($namespace::ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        source_name: Some("elf".to_string()),
                        target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                        target_name: Some("elf".to_string()),
                        ..$namespace::ExposeRunnerDecl::EMPTY
                    }),
                    $namespace::ExposeDecl::Resolver($namespace::ExposeResolverDecl {
                        source: Some($namespace::Ref::Child($namespace::ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        source_name: Some("pkg".to_string()),
                        target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                        target_name: Some("pkg".to_string()),
                        ..$namespace::ExposeResolverDecl::EMPTY
                    }),
                    $namespace::ExposeDecl::Service($namespace::ExposeServiceDecl {
                        source: Some($namespace::Ref::Child($namespace::ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        source_name: Some("netstack1".to_string()),
                        target_name: Some("mynetstack".to_string()),
                        target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                        ..$namespace::ExposeServiceDecl::EMPTY
                    }),
                    $namespace::ExposeDecl::Service($namespace::ExposeServiceDecl {
                        source: Some($namespace::Ref::Child($namespace::ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        source_name: Some("netstack2".to_string()),
                        target_name: Some("mynetstack".to_string()),
                        target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                        ..$namespace::ExposeServiceDecl::EMPTY
                    }),
                ]),
                offers: Some(vec![
                    $namespace::OfferDecl::Protocol($namespace::OfferProtocolDecl {
                        source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                        source_name: Some("legacy_netstack".to_string()),
                        target: Some($namespace::Ref::Child($namespace::ChildRef {
                            name: "echo".to_string(),
                            collection: None,
                        })),
                        target_name: Some("legacy_mynetstack".to_string()),
                        dependency_type: Some($namespace::DependencyType::WeakForMigration),
                        ..$namespace::OfferProtocolDecl::EMPTY
                    }),
                    $namespace::OfferDecl::Directory($namespace::OfferDirectoryDecl {
                        source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                        source_name: Some("dir".to_string()),
                        target: Some($namespace::Ref::Collection($namespace::CollectionRef {
                            name: "modular".to_string(),
                        })),
                        target_name: Some("data".to_string()),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                        dependency_type: Some($namespace::DependencyType::Strong),
                        ..$namespace::OfferDirectoryDecl::EMPTY
                    }),
                    $namespace::OfferDecl::Storage($namespace::OfferStorageDecl {
                        source_name: Some("cache".to_string()),
                        source: Some($namespace::Ref::Self_($namespace::SelfRef {})),
                        target: Some($namespace::Ref::Collection($namespace::CollectionRef {
                            name: "modular".to_string(),
                        })),
                        target_name: Some("cache".to_string()),
                        ..$namespace::OfferStorageDecl::EMPTY
                    }),
                    $namespace::OfferDecl::Runner($namespace::OfferRunnerDecl {
                        source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                        source_name: Some("elf".to_string()),
                        target: Some($namespace::Ref::Child($namespace::ChildRef {
                            name: "echo".to_string(),
                            collection: None,
                        })),
                        target_name: Some("elf2".to_string()),
                        ..$namespace::OfferRunnerDecl::EMPTY
                    }),
                    $namespace::OfferDecl::Resolver($namespace::OfferResolverDecl {
                        source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                        source_name: Some("pkg".to_string()),
                        target: Some($namespace::Ref::Child($namespace::ChildRef {
                            name: "echo".to_string(),
                            collection: None,
                        })),
                        target_name: Some("pkg".to_string()),
                        ..$namespace::OfferResolverDecl::EMPTY
                    }),
                    $namespace::OfferDecl::Event($namespace::OfferEventDecl {
                        source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                        source_name: Some("started".to_string()),
                        target: Some($namespace::Ref::Child($namespace::ChildRef {
                            name: "echo".to_string(),
                            collection: None,
                        })),
                        target_name: Some("mystarted".to_string()),
                        filter: Some(fdata::Dictionary {
                            entries: Some(vec![fdata::DictionaryEntry {
                                key: "path".to_string(),
                                value: Some(Box::new(fdata::DictionaryValue::Str(
                                    "/a".to_string(),
                                ))),
                            }]),
                            ..fdata::Dictionary::EMPTY
                        }),
                        mode: Some($namespace::EventMode::Sync),
                        ..$namespace::OfferEventDecl::EMPTY
                    }),
                    $namespace::OfferDecl::Service($namespace::OfferServiceDecl {
                        source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                        source_name: Some("netstack1".to_string()),
                        target: Some($namespace::Ref::Child($namespace::ChildRef {
                            name: "echo".to_string(),
                            collection: None,
                        })),
                        target_name: Some("mynetstack".to_string()),
                        ..$namespace::OfferServiceDecl::EMPTY
                    }),
                    $namespace::OfferDecl::Service($namespace::OfferServiceDecl {
                        source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                        source_name: Some("netstack2".to_string()),
                        target: Some($namespace::Ref::Child($namespace::ChildRef {
                            name: "echo".to_string(),
                            collection: None,
                        })),
                        target_name: Some("mynetstack".to_string()),
                        ..$namespace::OfferServiceDecl::EMPTY
                    }),
                ]),
                capabilities: Some(vec![
                    $namespace::CapabilityDecl::Service($namespace::ServiceDecl {
                        name: Some("netstack".to_string()),
                        source_path: Some("/netstack".to_string()),
                        ..$namespace::ServiceDecl::EMPTY
                    }),
                    $namespace::CapabilityDecl::Protocol($namespace::ProtocolDecl {
                        name: Some("netstack2".to_string()),
                        source_path: Some("/netstack2".to_string()),
                        ..$namespace::ProtocolDecl::EMPTY
                    }),
                    $namespace::CapabilityDecl::Directory($namespace::DirectoryDecl {
                        name: Some("data".to_string()),
                        source_path: Some("/data".to_string()),
                        rights: Some(fio2::Operations::Connect),
                        ..$namespace::DirectoryDecl::EMPTY
                    }),
                    $namespace::CapabilityDecl::Storage($namespace::StorageDecl {
                        name: Some("cache".to_string()),
                        backing_dir: Some("data".to_string()),
                        source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                        subdir: Some("cache".to_string()),
                        storage_id: Some($namespace::StorageId::StaticInstanceId),
                        ..$namespace::StorageDecl::EMPTY
                    }),
                    $namespace::CapabilityDecl::Runner($namespace::RunnerDecl {
                        name: Some("elf".to_string()),
                        source_path: Some("/elf".to_string()),
                        ..$namespace::RunnerDecl::EMPTY
                    }),
                    $namespace::CapabilityDecl::Resolver($namespace::ResolverDecl {
                        name: Some("pkg".to_string()),
                        source_path: Some("/pkg_resolver".to_string()),
                        ..$namespace::ResolverDecl::EMPTY
                    }),
                ]),
                children: Some(vec![
                    $namespace::ChildDecl {
                        name: Some("netstack".to_string()),
                        url: Some(
                            "fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cm".to_string(),
                        ),
                        startup: Some($namespace::StartupMode::Lazy),
                        on_terminate: None,
                        environment: None,
                        ..$namespace::ChildDecl::EMPTY
                    },
                    $namespace::ChildDecl {
                        name: Some("gtest".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/gtest#meta/gtest.cm".to_string()),
                        startup: Some($namespace::StartupMode::Lazy),
                        on_terminate: Some($namespace::OnTerminate::None),
                        environment: None,
                        ..$namespace::ChildDecl::EMPTY
                    },
                    $namespace::ChildDecl {
                        name: Some("echo".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/echo#meta/echo.cm".to_string()),
                        startup: Some($namespace::StartupMode::Eager),
                        on_terminate: Some($namespace::OnTerminate::Reboot),
                        environment: Some("test_env".to_string()),
                        ..$namespace::ChildDecl::EMPTY
                    },
                ]),
                collections: Some(vec![
                    $namespace::CollectionDecl {
                        name: Some("modular".to_string()),
                        durability: Some($namespace::Durability::Persistent),
                        allowed_offers: Some($namespace::AllowedOffers::StaticOnly),
                        environment: None,
                        ..$namespace::CollectionDecl::EMPTY
                    },
                    $namespace::CollectionDecl {
                        name: Some("tests".to_string()),
                        durability: Some($namespace::Durability::Transient),
                        allowed_offers: Some($namespace::AllowedOffers::StaticAndDynamic),
                        environment: Some("test_env".to_string()),
                        ..$namespace::CollectionDecl::EMPTY
                    },
                ]),
                facets: Some(fdata::Dictionary {
                    entries: Some(vec![fdata::DictionaryEntry {
                        key: "author".to_string(),
                        value: Some(Box::new(fdata::DictionaryValue::Str("Fuchsia".to_string()))),
                    }]),
                    ..fdata::Dictionary::EMPTY
                }),
                environments: Some(vec![$namespace::EnvironmentDecl {
                    name: Some("test_env".to_string()),
                    extends: Some($namespace::EnvironmentExtends::Realm),
                    runners: Some(vec![$namespace::RunnerRegistration {
                        source_name: Some("runner".to_string()),
                        source: Some($namespace::Ref::Child($namespace::ChildRef {
                            name: "gtest".to_string(),
                            collection: None,
                        })),
                        target_name: Some("gtest-runner".to_string()),
                        ..$namespace::RunnerRegistration::EMPTY
                    }]),
                    resolvers: Some(vec![$namespace::ResolverRegistration {
                        resolver: Some("pkg_resolver".to_string()),
                        source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                        scheme: Some("fuchsia-pkg".to_string()),
                        ..$namespace::ResolverRegistration::EMPTY
                    }]),
                    debug_capabilities: Some(vec![$namespace::DebugRegistration::Protocol(
                        $namespace::DebugProtocolRegistration {
                            source_name: Some("some_protocol".to_string()),
                            source: Some($namespace::Ref::Child($namespace::ChildRef {
                                name: "gtest".to_string(),
                                collection: None,
                            })),
                            target_name: Some("some_protocol".to_string()),
                            ..$namespace::DebugProtocolRegistration::EMPTY
                        },
                    )]),
                    stop_timeout_ms: Some(4567),
                    ..$namespace::EnvironmentDecl::EMPTY
                }]),
                config: Some($namespace::ConfigDecl {
                    fields: Some(vec![$namespace::ConfigField {
                        key: Some("test".to_string()),
                        value_type: Some($namespace::ConfigValueType::Bool(
                            $namespace::ConfigBooleanType::EMPTY,
                        )),
                        ..$namespace::ConfigField::EMPTY
                    }]),
                    declaration_checksum: Some(vec![
                        0x9F, 0x86, 0xD0, 0x81, 0x88, 0x4C, 0x7D, 0x65, 0x9A, 0x2F, 0xEA, 0xA0,
                        0xC5, 0x5A, 0xD0, 0x15, 0xA3, 0xBF, 0x4F, 0x1B, 0x2B, 0x0B, 0x82, 0x2C,
                        0xD1, 0x5D, 0x6C, 0x15, 0xB0, 0xF0, 0x0A, 0x08,
                    ]),
                    ..$namespace::ConfigDecl::EMPTY
                }),
                // We include these fields because we want to ensure that this ABI
                // compatibility contract is enforced whenever the Component decl
                // types are updated.
                unknown_data: None,
                __non_exhaustive: (),
            }
        };
    }

    macro_rules! test_component_abi_compatibility {
        ($label:ident, $input_type:ident, $output_type:ident) => {
            paste::paste! {
                #[test]
                fn [<test _ component _ abi _ compatibility _ $label>]() {
                    let mut input = fully_populated_decl!($input_type);
                    let input = fidl::encoding::encode_persistent(&mut input)
                        .expect("failed to encode input type");
                    let output = fidl::encoding::decode_persistent::<$output_type::ComponentDecl>(&input)
                        .expect("failed to decode input type");
                    assert_eq!(output, fully_populated_decl!($output_type));
                }
            }
        };
    }

    test_component_abi_compatibility!(fdecl_to_fsys, fdecl, fsys);
    test_component_abi_compatibility!(fsys_to_fdecl, fsys, fdecl);
}
