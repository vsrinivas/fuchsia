// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

macro_rules! test_validate {
        (
            $(
                $test_name:ident => {
                    input = $input:expr,
                    result = $result:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    validate_test($input, $result);
                }
            )+
        }
    }

macro_rules! test_validate_any_result {
        (
            $(
                $test_name:ident => {
                    input = $input:expr,
                    results = $results:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    validate_test_any_result($input, $results);
                }
            )+
        }
    }

macro_rules! test_validate_capabilities {
        (
            $(
                $test_name:ident => {
                    input = $input:expr,
                    as_builtin = $as_builtin:expr,
                    result = $result:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    validate_capabilities_test($input, $as_builtin, $result);
                }
            )+
        }
    }

macro_rules! test_dependency {
        (
            $(
                ($test_name:ident, $namespace:ident) => {
                    ty = $ty:expr,
                    offer_decl = $offer_decl:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    let mut decl = new_component_decl();
                    let dependencies = vec![
                        ("a", "b"),
                        ("b", "a"),
                    ];
                    let offers = dependencies.into_iter().map(|(from,to)| {
                        let mut offer_decl = $offer_decl;
                        offer_decl.source = Some($namespace::Ref::Child(
                           $namespace::ChildRef { name: from.to_string(), collection: None },
                        ));
                        offer_decl.target = Some($namespace::Ref::Child(
                           $namespace::ChildRef { name: to.to_string(), collection: None },
                        ));
                        $ty(offer_decl)
                    }).collect();
                    let children = ["a", "b"].iter().map(|name| {
                        $namespace::ChildDecl {
                            name: Some(name.to_string()),
                            url: Some(format!("fuchsia-pkg://fuchsia.com/pkg#meta/{}.cm", name)),
                            startup: Some($namespace::StartupMode::Lazy),
                            on_terminate: None,
                            environment: None,
                            ..$namespace::ChildDecl::EMPTY
                        }
                    }).collect();
                    decl.offers = Some(offers);
                    decl.children = Some(children);
                    let result = Err(ErrorList::new(vec![
                        Error::dependency_cycle(
                            directed_graph::Error::CyclesDetected([vec!["child a", "child b", "child a"]].iter().cloned().collect()).format_cycle()),
                    ]));
                    validate_test(decl, result);
                }
            )+
        }
    }

macro_rules! test_weak_dependency {
        (
            $(
                ($test_name:ident, $namespace:ident) => {
                    ty = $ty:expr,
                    offer_decl = $offer_decl:expr,
                },
            )+
        ) => {
            $(
                #[test_case($namespace::DependencyType::Weak)]
                #[test_case($namespace::DependencyType::WeakForMigration)]
                fn $test_name(weak_dep: $namespace::DependencyType) {
                    let mut decl = new_component_decl();
                    let offers = vec![
                        {
                            let mut offer_decl = $offer_decl;
                            offer_decl.source = Some($namespace::Ref::Child(
                               $namespace::ChildRef { name: "a".to_string(), collection: None },
                            ));
                            offer_decl.target = Some($namespace::Ref::Child(
                               $namespace::ChildRef { name: "b".to_string(), collection: None },
                            ));
                            offer_decl.dependency_type = Some($namespace::DependencyType::Strong);
                            $ty(offer_decl)
                        },
                        {
                            let mut offer_decl = $offer_decl;
                            offer_decl.source = Some($namespace::Ref::Child(
                               $namespace::ChildRef { name: "b".to_string(), collection: None },
                            ));
                            offer_decl.target = Some($namespace::Ref::Child(
                               $namespace::ChildRef { name: "a".to_string(), collection: None },
                            ));
                            offer_decl.dependency_type = Some(weak_dep);
                            $ty(offer_decl)
                        },
                    ];
                    let children = ["a", "b"].iter().map(|name| {
                        $namespace::ChildDecl {
                            name: Some(name.to_string()),
                            url: Some(format!("fuchsia-pkg://fuchsia.com/pkg#meta/{}.cm", name)),
                            startup: Some($namespace::StartupMode::Lazy),
                            on_terminate: None,
                            environment: None,
                            ..$namespace::ChildDecl::EMPTY
                        }
                    }).collect();
                    decl.offers = Some(offers);
                    decl.children = Some(children);
                    let result = Ok(());
                    validate_test(decl, result);
                }
            )+
        }
    }

macro_rules! cm_fidl_validator_test_suite {
    ($mod:ident, $namespace:ident) => {
        #[cfg(test)]
        mod $mod {
            #[allow(unused_imports)]
            use {
                crate::{
                    convert as fdecl,
                    error::{Error, ErrorList},
                },
                fidl_fuchsia_data as fdata,
                fidl_fuchsia_io2 as fio2,
                fidl_fuchsia_sys2 as fsys,
                test_case::test_case,
            };

            fn validate_test(input: $namespace::ComponentDecl, expected_res: Result<(), ErrorList>) {
                let res = crate::$namespace::validate(&input);
                assert_eq!(res, expected_res);
            }

            fn validate_test_any_result(input: $namespace::ComponentDecl, expected_res: Vec<Result<(), ErrorList>>) {
                let res = format!("{:?}", crate::$namespace::validate(&input));
                let expected_res_debug = format!("{:?}", expected_res);

                let matched_exp =
                    expected_res.into_iter().find(|expected| res == format!("{:?}", expected));

                assert!(
                    matched_exp.is_some(),
                    "assertion failed: Expected one of:\n{:?}\nActual:\n{:?}",
                    expected_res_debug,
                    res
                );
            }

            fn validate_capabilities_test(
                input: Vec<$namespace::CapabilityDecl>,
                as_builtin: bool,
                expected_res: Result<(), ErrorList>,
            ) {
                let res = crate::$namespace::validate_capabilities(&input, as_builtin);
                assert_eq!(res, expected_res);
            }

            fn new_component_decl() -> $namespace::ComponentDecl {
                $namespace::ComponentDecl {
                    program: None,
                    uses: None,
                    exposes: None,
                    offers: None,
                    facets: None,
                    capabilities: None,
                    children: None,
                    collections: None,
                    environments: None,
                    ..$namespace::ComponentDecl::EMPTY
                }
            }


            test_validate_any_result! {
                test_validate_use_disallows_nested_dirs => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.uses = Some(vec![
                            $namespace::UseDecl::Directory($namespace::UseDirectoryDecl {
                                dependency_type: Some($namespace::DependencyType::Strong),
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                source_name: Some("abc".to_string()),
                                target_path: Some("/foo/bar".to_string()),
                                rights: Some(fio2::Operations::Connect),
                                subdir: None,
                                ..$namespace::UseDirectoryDecl::EMPTY
                            }),
                            $namespace::UseDecl::Directory($namespace::UseDirectoryDecl {
                                dependency_type: Some($namespace::DependencyType::Strong),
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                source_name: Some("abc".to_string()),
                                target_path: Some("/foo/bar/baz".to_string()),
                                rights: Some(fio2::Operations::Connect),
                                subdir: None,
                                ..$namespace::UseDirectoryDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    results = vec![
                        Err(ErrorList::new(vec![
                            Error::invalid_path_overlap(
                                "UseDirectoryDecl", "/foo/bar/baz", "UseDirectoryDecl", "/foo/bar"),
                        ])),
                        Err(ErrorList::new(vec![
                            Error::invalid_path_overlap(
                                "UseDirectoryDecl", "/foo/bar", "UseDirectoryDecl", "/foo/bar/baz"),
                        ])),
                    ],
                },
                test_validate_use_disallows_common_prefixes_protocol => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.uses = Some(vec![
                            $namespace::UseDecl::Directory($namespace::UseDirectoryDecl {
                                dependency_type: Some($namespace::DependencyType::Strong),
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                source_name: Some("abc".to_string()),
                                target_path: Some("/foo/bar".to_string()),
                                rights: Some(fio2::Operations::Connect),
                                subdir: None,
                                ..$namespace::UseDirectoryDecl::EMPTY
                            }),
                            $namespace::UseDecl::Protocol($namespace::UseProtocolDecl {
                                dependency_type: Some($namespace::DependencyType::Strong),
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                source_name: Some("crow".to_string()),
                                target_path: Some("/foo/bar/fuchsia.2".to_string()),
                                ..$namespace::UseProtocolDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    results = vec![
                        Err(ErrorList::new(vec![
                            Error::invalid_path_overlap(
                                "UseProtocolDecl", "/foo/bar/fuchsia.2", "UseDirectoryDecl", "/foo/bar"),
                        ])),
                        Err(ErrorList::new(vec![
                            Error::invalid_path_overlap(
                                "UseDirectoryDecl", "/foo/bar", "UseProtocolDecl", "/foo/bar/fuchsia.2"),
                        ])),
                    ],
                },
                test_validate_use_disallows_common_prefixes_service => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.uses = Some(vec![
                            $namespace::UseDecl::Directory($namespace::UseDirectoryDecl {
                                dependency_type: Some($namespace::DependencyType::Strong),
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                source_name: Some("abc".to_string()),
                                target_path: Some("/foo/bar".to_string()),
                                rights: Some(fio2::Operations::Connect),
                                subdir: None,
                                ..$namespace::UseDirectoryDecl::EMPTY
                            }),
                            $namespace::UseDecl::Service($namespace::UseServiceDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                source_name: Some("space".to_string()),
                                target_path: Some("/foo/bar/baz/fuchsia.logger.Log".to_string()),
                                dependency_type: Some($namespace::DependencyType::Strong),
                                ..$namespace::UseServiceDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    results = vec![
                        Err(ErrorList::new(vec![
                            Error::invalid_path_overlap(
                                "UseServiceDecl", "/foo/bar/baz/fuchsia.logger.Log", "UseDirectoryDecl", "/foo/bar"),
                        ])),
                        Err(ErrorList::new(vec![
                            Error::invalid_path_overlap(
                                "UseDirectoryDecl", "/foo/bar", "UseServiceDecl", "/foo/bar/baz/fuchsia.logger.Log"),
                        ])),
                    ],
                },
            }

            test_validate! {
                // uses
                test_validate_uses_empty => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.program = Some($namespace::ProgramDecl {
                            runner: Some("elf".to_string()),
                            info: Some(fdata::Dictionary {
                                entries: None,
                                ..fdata::Dictionary::EMPTY
                            }),
                            ..$namespace::ProgramDecl::EMPTY
                        });
                        decl.uses = Some(vec![
                            $namespace::UseDecl::Service($namespace::UseServiceDecl {
                                source: None,
                                source_name: None,
                                target_path: None,
                                dependency_type: Some($namespace::DependencyType::Strong),
                                ..$namespace::UseServiceDecl::EMPTY
                            }),
                            $namespace::UseDecl::Protocol($namespace::UseProtocolDecl {
                                dependency_type: Some($namespace::DependencyType::Strong),
                                source: None,
                                source_name: None,
                                target_path: None,
                                ..$namespace::UseProtocolDecl::EMPTY
                            }),
                            $namespace::UseDecl::Directory($namespace::UseDirectoryDecl {
                                dependency_type: Some($namespace::DependencyType::Strong),
                                source: None,
                                source_name: None,
                                target_path: None,
                                rights: None,
                                subdir: None,
                                ..$namespace::UseDirectoryDecl::EMPTY
                            }),
                            $namespace::UseDecl::Storage($namespace::UseStorageDecl {
                                source_name: None,
                                target_path: None,
                                ..$namespace::UseStorageDecl::EMPTY
                            }),
                            $namespace::UseDecl::Storage($namespace::UseStorageDecl {
                                source_name: Some("cache".to_string()),
                                target_path: None,
                                ..$namespace::UseStorageDecl::EMPTY
                            }),
                            $namespace::UseDecl::Event($namespace::UseEventDecl {
                                dependency_type: Some($namespace::DependencyType::Strong),
                                source: None,
                                source_name: None,
                                target_name: None,
                                filter: None,
                                mode: None,
                                ..$namespace::UseEventDecl::EMPTY
                            }),
                            $namespace::UseDecl::EventStream($namespace::UseEventStreamDecl {
                                name: None,
                                subscriptions: None,
                                ..$namespace::UseEventStreamDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::missing_field("UseEventDecl", "source"),
                        Error::missing_field("UseEventDecl", "source_name"),
                        Error::missing_field("UseEventDecl", "target_name"),
                        Error::missing_field("UseEventDecl", "mode"),
                        Error::missing_field("UseServiceDecl", "source"),
                        Error::missing_field("UseServiceDecl", "source_name"),
                        Error::missing_field("UseServiceDecl", "target_path"),
                        Error::missing_field("UseProtocolDecl", "source"),
                        Error::missing_field("UseProtocolDecl", "source_name"),
                        Error::missing_field("UseProtocolDecl", "target_path"),
                        Error::missing_field("UseDirectoryDecl", "source"),
                        Error::missing_field("UseDirectoryDecl", "source_name"),
                        Error::missing_field("UseDirectoryDecl", "target_path"),
                        Error::missing_field("UseDirectoryDecl", "rights"),
                        Error::missing_field("UseStorageDecl", "source_name"),
                        Error::missing_field("UseStorageDecl", "target_path"),
                        Error::missing_field("UseStorageDecl", "target_path"),
                        Error::missing_field("UseEventStreamDecl", "name"),
                        Error::missing_field("UseEventStreamDecl", "subscriptions"),
                    ])),
                },
                test_validate_uses_invalid_identifiers_service => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.uses = Some(vec![
                            $namespace::UseDecl::Service($namespace::UseServiceDecl {
                                source: Some($namespace::Ref::Self_($namespace::SelfRef {})),
                                source_name: Some("foo/".to_string()),
                                target_path: Some("/".to_string()),
                                dependency_type: Some($namespace::DependencyType::Strong),
                                ..$namespace::UseServiceDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_field("UseServiceDecl", "source_name"),
                        Error::invalid_field("UseServiceDecl", "target_path"),
                    ])),
                },
                test_validate_uses_invalid_identifiers_protocol => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.uses = Some(vec![
                            $namespace::UseDecl::Protocol($namespace::UseProtocolDecl {
                                dependency_type: Some($namespace::DependencyType::Strong),
                                source: Some($namespace::Ref::Self_($namespace::SelfRef {})),
                                source_name: Some("foo/".to_string()),
                                target_path: Some("/".to_string()),
                                ..$namespace::UseProtocolDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_field("UseProtocolDecl", "source_name"),
                        Error::invalid_field("UseProtocolDecl", "target_path"),
                    ])),
                },
                test_validate_uses_invalid_identifiers => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.uses = Some(vec![
                            $namespace::UseDecl::Directory($namespace::UseDirectoryDecl {
                                dependency_type: Some($namespace::DependencyType::Strong),
                                source: Some($namespace::Ref::Self_($namespace::SelfRef {})),
                                source_name: Some("foo/".to_string()),
                                target_path: Some("/".to_string()),
                                rights: Some(fio2::Operations::Connect),
                                subdir: Some("/foo".to_string()),
                                ..$namespace::UseDirectoryDecl::EMPTY
                            }),
                            $namespace::UseDecl::Storage($namespace::UseStorageDecl {
                                source_name: Some("/cache".to_string()),
                                target_path: Some("/".to_string()),
                                ..$namespace::UseStorageDecl::EMPTY
                            }),
                            $namespace::UseDecl::Storage($namespace::UseStorageDecl {
                                source_name: Some("temp".to_string()),
                                target_path: Some("tmp".to_string()),
                                ..$namespace::UseStorageDecl::EMPTY
                            }),
                            $namespace::UseDecl::Event($namespace::UseEventDecl {
                                dependency_type: Some($namespace::DependencyType::Strong),
                                source: Some($namespace::Ref::Self_($namespace::SelfRef {})),
                                source_name: Some("/foo".to_string()),
                                target_name: Some("/foo".to_string()),
                                filter: Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY }),
                                mode: Some($namespace::EventMode::Async),
                                ..$namespace::UseEventDecl::EMPTY
                            }),
                            $namespace::UseDecl::Event($namespace::UseEventDecl {
                                dependency_type: Some($namespace::DependencyType::Strong),
                                source: Some($namespace::Ref::Framework($namespace::FrameworkRef {})),
                                source_name: Some("started".to_string()),
                                target_name: Some("started".to_string()),
                                filter: Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY }),
                                mode: Some($namespace::EventMode::Async),
                                ..$namespace::UseEventDecl::EMPTY
                            }),
                            $namespace::UseDecl::EventStream($namespace::UseEventStreamDecl {
                                name: Some("bar".to_string()),
                                subscriptions: Some(vec!["a".to_string(), "b".to_string()].into_iter().map(|name| $namespace::EventSubscription {
                                    event_name: Some(name),
                                    mode: Some($namespace::EventMode::Async),
                                    ..$namespace::EventSubscription::EMPTY
                                }).collect()),
                                ..$namespace::UseEventStreamDecl::EMPTY
                            }),
                            $namespace::UseDecl::EventStream($namespace::UseEventStreamDecl {
                                name: Some("bleep".to_string()),
                                subscriptions: Some(vec![$namespace::EventSubscription {
                                    event_name: Some("started".to_string()),
                                    mode: Some($namespace::EventMode::Sync),
                                    ..$namespace::EventSubscription::EMPTY
                                }]),
                                ..$namespace::UseEventStreamDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_field("UseEventDecl", "source"),
                        Error::invalid_field("UseEventDecl", "source_name"),
                        Error::invalid_field("UseEventDecl", "target_name"),
                        Error::invalid_field("UseDirectoryDecl", "source_name"),
                        Error::invalid_field("UseDirectoryDecl", "target_path"),
                        Error::invalid_field("UseDirectoryDecl", "subdir"),
                        Error::invalid_field("UseStorageDecl", "source_name"),
                        Error::invalid_field("UseStorageDecl", "target_path"),
                        Error::invalid_field("UseStorageDecl", "target_path"),
                        Error::event_stream_event_not_found("UseEventStreamDecl", "events", "a".to_string()),
                        Error::event_stream_event_not_found("UseEventStreamDecl", "events", "b".to_string()),
                        Error::event_stream_unsupported_mode("UseEventStreamDecl", "events", "started".to_string(), "Sync".to_string()),
                    ])),
                },
                test_validate_uses_missing_source => {
                    input = {
                        $namespace::ComponentDecl {
                            uses: Some(vec![
                                $namespace::UseDecl::Protocol($namespace::UseProtocolDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Capability($namespace::CapabilityRef {
                                        name: "this-storage-doesnt-exist".to_string(),
                                    })),
                                    source_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                                    target_path: Some("/svc/fuchsia.sys2.StorageAdmin".to_string()),
                                    ..$namespace::UseProtocolDecl::EMPTY
                                })
                            ]),
                            ..new_component_decl()
                        }
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_capability("UseProtocolDecl", "source", "this-storage-doesnt-exist"),
                    ])),
                },
                test_validate_uses_invalid_child => {
                    input = {
                        $namespace::ComponentDecl {
                            uses: Some(vec![
                                $namespace::UseDecl::Protocol($namespace::UseProtocolDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Child($namespace::ChildRef{ name: "no-such-child".to_string(), collection: None})),
                                    source_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                                    target_path: Some("/svc/fuchsia.sys2.StorageAdmin".to_string()),
                                    ..$namespace::UseProtocolDecl::EMPTY
                                }),
                                $namespace::UseDecl::Service($namespace::UseServiceDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef{ name: "no-such-child".to_string(), collection: None})),
                                    source_name: Some("service_name".to_string()),
                                    target_path: Some("/svc/service_name".to_string()),
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    ..$namespace::UseServiceDecl::EMPTY
                                }),
                                $namespace::UseDecl::Directory($namespace::UseDirectoryDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Child($namespace::ChildRef{ name: "no-such-child".to_string(), collection: None})),
                                    source_name: Some("DirectoryName".to_string()),
                                    target_path: Some("/data/DirectoryName".to_string()),
                                    rights: Some(fio2::Operations::Connect),
                                    subdir: None,
                                    ..$namespace::UseDirectoryDecl::EMPTY
                                }),
                                $namespace::UseDecl::Event($namespace::UseEventDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Child($namespace::ChildRef{ name: "no-such-child".to_string(), collection: None})),
                                    source_name: Some("abc".to_string()),
                                    target_name: Some("abc".to_string()),
                                    filter: Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY }),
                                    mode: Some($namespace::EventMode::Async),
                                    ..$namespace::UseEventDecl::EMPTY
                                }),
                            ]),
                            ..new_component_decl()
                        }
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_child("UseEventDecl", "source", "no-such-child"),
                        Error::invalid_child("UseProtocolDecl", "source", "no-such-child"),
                        Error::invalid_child("UseServiceDecl", "source", "no-such-child"),
                        Error::invalid_child("UseDirectoryDecl", "source", "no-such-child"),
                    ])),
                },
                test_validate_use_from_child_offer_to_child_strong_cycle => {
                    input = {
                        $namespace::ComponentDecl {
                            capabilities: Some(vec![
                                $namespace::CapabilityDecl::Service($namespace::ServiceDecl {
                                    name: Some("a".to_string()),
                                    source_path: Some("/a".to_string()),
                                    ..$namespace::ServiceDecl::EMPTY
                                })]),
                            uses: Some(vec![
                                $namespace::UseDecl::Protocol($namespace::UseProtocolDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Child($namespace::ChildRef{ name: "child".to_string(), collection: None})),
                                    source_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                                    target_path: Some("/svc/fuchsia.sys2.StorageAdmin".to_string()),
                                    ..$namespace::UseProtocolDecl::EMPTY
                                }),
                                $namespace::UseDecl::Service($namespace::UseServiceDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef{ name: "child".to_string(), collection: None})),
                                    source_name: Some("service_name".to_string()),
                                    target_path: Some("/svc/service_name".to_string()),
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    ..$namespace::UseServiceDecl::EMPTY
                                }),
                                $namespace::UseDecl::Directory($namespace::UseDirectoryDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Child($namespace::ChildRef{ name: "child".to_string(), collection: None})),
                                    source_name: Some("DirectoryName".to_string()),
                                    target_path: Some("/data/DirectoryName".to_string()),
                                    rights: Some(fio2::Operations::Connect),
                                    subdir: None,
                                    ..$namespace::UseDirectoryDecl::EMPTY
                                }),
                                $namespace::UseDecl::Event($namespace::UseEventDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Child($namespace::ChildRef{ name: "child".to_string(), collection: None})),
                                    source_name: Some("abc".to_string()),
                                    target_name: Some("abc".to_string()),
                                    filter: Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY }),
                                    mode: Some($namespace::EventMode::Async),
                                    ..$namespace::UseEventDecl::EMPTY
                                })
                            ]),
                            offers: Some(vec![
                                $namespace::OfferDecl::Service($namespace::OfferServiceDecl {
                                    source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                    source_name: Some("a".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef { name: "child".to_string(), collection: None })),
                                    target_name: Some("a".to_string()),
                                    ..$namespace::OfferServiceDecl::EMPTY
                                })
                            ]),
                            children: Some(vec![
                                $namespace::ChildDecl {
                                    name: Some("child".to_string()),
                                    url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                                    startup: Some($namespace::StartupMode::Lazy),
                                    on_terminate: None,
                                    ..$namespace::ChildDecl::EMPTY
                                }
                            ]),
                            ..new_component_decl()
                        }
                    },
                    result = Err(ErrorList::new(vec![
                        Error::dependency_cycle("{{self -> child child -> self}}".to_string()),
                    ])),
                },
                test_validate_use_from_child_storage_no_cycle => {
                    input = {
                        $namespace::ComponentDecl {
                            capabilities: Some(vec![
                                $namespace::CapabilityDecl::Storage($namespace::StorageDecl {
                                    name: Some("cdata".to_string()),
                                    source: Some($namespace::Ref::Child($namespace::ChildRef { name: "child2".to_string(), collection: None } )),
                                    backing_dir: Some("minfs".to_string()),
                                    storage_id: Some($namespace::StorageId::StaticInstanceIdOrMoniker),
                                    ..$namespace::StorageDecl::EMPTY
                                }),
                                $namespace::CapabilityDecl::Storage($namespace::StorageDecl {
                                    name: Some("pdata".to_string()),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                    backing_dir: Some("minfs".to_string()),
                                    storage_id: Some($namespace::StorageId::StaticInstanceIdOrMoniker),
                                    ..$namespace::StorageDecl::EMPTY
                                }),
                            ]),
                            uses: Some(vec![
                                $namespace::UseDecl::Protocol($namespace::UseProtocolDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Child($namespace::ChildRef{ name: "child1".to_string(), collection: None})),
                                    source_name: Some("a".to_string()),
                                    target_path: Some("/svc/a".to_string()),
                                    ..$namespace::UseProtocolDecl::EMPTY
                                }),
                            ]),
                            offers: Some(vec![
                                $namespace::OfferDecl::Storage($namespace::OfferStorageDecl {
                                    source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                    source_name: Some("cdata".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef { name: "child1".to_string(), collection: None })),
                                    target_name: Some("cdata".to_string()),
                                    ..$namespace::OfferStorageDecl::EMPTY
                                }),
                                $namespace::OfferDecl::Storage($namespace::OfferStorageDecl {
                                    source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                    source_name: Some("pdata".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef { name: "child1".to_string(), collection: None })),
                                    target_name: Some("pdata".to_string()),
                                    ..$namespace::OfferStorageDecl::EMPTY
                                }),
                            ]),
                            children: Some(vec![
                                $namespace::ChildDecl {
                                    name: Some("child1".to_string()),
                                    url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                                    startup: Some($namespace::StartupMode::Lazy),
                                    on_terminate: None,
                                    ..$namespace::ChildDecl::EMPTY
                                },
                                $namespace::ChildDecl {
                                    name: Some("child2".to_string()),
                                    url: Some("fuchsia-pkg://fuchsia.com/foo2".to_string()),
                                    startup: Some($namespace::StartupMode::Lazy),
                                    on_terminate: None,
                                    ..$namespace::ChildDecl::EMPTY
                                }
                            ]),
                            ..new_component_decl()
                        }
                    },
                    result = Ok(()),
                },
                test_validate_use_from_child_storage_cycle => {
                    input = {
                        $namespace::ComponentDecl {
                            capabilities: Some(vec![
                                $namespace::CapabilityDecl::Storage($namespace::StorageDecl {
                                    name: Some("data".to_string()),
                                    source: Some($namespace::Ref::Self_($namespace::SelfRef {})),
                                    backing_dir: Some("minfs".to_string()),
                                    storage_id: Some($namespace::StorageId::StaticInstanceIdOrMoniker),
                                    ..$namespace::StorageDecl::EMPTY
                                }),
                            ]),
                            uses: Some(vec![
                                $namespace::UseDecl::Protocol($namespace::UseProtocolDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Child($namespace::ChildRef{ name: "child".to_string(), collection: None})),
                                    source_name: Some("a".to_string()),
                                    target_path: Some("/svc/a".to_string()),
                                    ..$namespace::UseProtocolDecl::EMPTY
                                }),
                            ]),
                            offers: Some(vec![
                                $namespace::OfferDecl::Storage($namespace::OfferStorageDecl {
                                    source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                    source_name: Some("data".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef { name: "child".to_string(), collection: None })),
                                    target_name: Some("data".to_string()),
                                    ..$namespace::OfferStorageDecl::EMPTY
                                }),
                            ]),
                            children: Some(vec![
                                $namespace::ChildDecl {
                                    name: Some("child".to_string()),
                                    url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                                    startup: Some($namespace::StartupMode::Lazy),
                                    on_terminate: None,
                                    ..$namespace::ChildDecl::EMPTY
                                },
                            ]),
                            ..new_component_decl()
                        }
                    },
                    result = Err(ErrorList::new(vec![
                        Error::dependency_cycle("{{self -> child child -> self}}".to_string()),
                    ])),
                },
                test_validate_storage_strong_cycle_between_children => {
                    input = {
                        $namespace::ComponentDecl {
                            capabilities: Some(vec![
                                $namespace::CapabilityDecl::Storage($namespace::StorageDecl {
                                    name: Some("data".to_string()),
                                    source: Some($namespace::Ref::Child($namespace::ChildRef { name: "child1".to_string(), collection: None } )),
                                    backing_dir: Some("minfs".to_string()),
                                    storage_id: Some($namespace::StorageId::StaticInstanceIdOrMoniker),
                                    ..$namespace::StorageDecl::EMPTY
                                })
                            ]),
                            offers: Some(vec![
                                $namespace::OfferDecl::Storage($namespace::OfferStorageDecl {
                                    source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                    source_name: Some("data".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef { name: "child2".to_string(), collection: None })),
                                    target_name: Some("data".to_string()),
                                    ..$namespace::OfferStorageDecl::EMPTY
                                }),
                                $namespace::OfferDecl::Service($namespace::OfferServiceDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef { name: "child2".to_string(), collection: None })),
                                    source_name: Some("a".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef { name: "child1".to_string(), collection: None })),
                                    target_name: Some("a".to_string()),
                                    ..$namespace::OfferServiceDecl::EMPTY
                                }),
                            ]),
                            children: Some(vec![
                                $namespace::ChildDecl {
                                    name: Some("child1".to_string()),
                                    url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                                    startup: Some($namespace::StartupMode::Lazy),
                                    on_terminate: None,
                                    ..$namespace::ChildDecl::EMPTY
                                },
                                $namespace::ChildDecl {
                                    name: Some("child2".to_string()),
                                    url: Some("fuchsia-pkg://fuchsia.com/foo2".to_string()),
                                    startup: Some($namespace::StartupMode::Lazy),
                                    on_terminate: None,
                                    ..$namespace::ChildDecl::EMPTY
                                }
                            ]),
                            ..new_component_decl()
                        }
                    },
                    result = Err(ErrorList::new(vec![
                        Error::dependency_cycle("{{child child1 -> child child2 -> child child1}}".to_string()),
                    ])),
                },
                test_validate_strong_cycle_between_children_through_environment_debug => {
                    input = {
                        $namespace::ComponentDecl {
                            environments: Some(vec![
                                $namespace::EnvironmentDecl {
                                    name: Some("env".to_string()),
                                    extends: Some($namespace::EnvironmentExtends::Realm),
                                    debug_capabilities: Some(vec![
                                        $namespace::DebugRegistration::Protocol($namespace::DebugProtocolRegistration {
                                            source: Some($namespace::Ref::Child($namespace::ChildRef { name: "child1".to_string(), collection: None })),
                                            source_name: Some("fuchsia.foo.Bar".to_string()),
                                            target_name: Some("fuchsia.foo.Bar".to_string()),
                                            ..$namespace::DebugProtocolRegistration::EMPTY
                                        }),
                                    ]),
                                    ..$namespace::EnvironmentDecl::EMPTY
                                },
                            ]),
                            offers: Some(vec![
                                $namespace::OfferDecl::Service($namespace::OfferServiceDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef { name: "child2".to_string(), collection: None })),
                                    source_name: Some("a".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef { name: "child1".to_string(), collection: None })),
                                    target_name: Some("a".to_string()),
                                    ..$namespace::OfferServiceDecl::EMPTY
                                }),
                            ]),
                            children: Some(vec![
                                $namespace::ChildDecl {
                                    name: Some("child1".to_string()),
                                    url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                                    startup: Some($namespace::StartupMode::Lazy),
                                    on_terminate: None,
                                    ..$namespace::ChildDecl::EMPTY
                                },
                                $namespace::ChildDecl {
                                    name: Some("child2".to_string()),
                                    url: Some("fuchsia-pkg://fuchsia.com/foo2".to_string()),
                                    startup: Some($namespace::StartupMode::Lazy),
                                    environment: Some("env".to_string()),
                                    on_terminate: None,
                                    ..$namespace::ChildDecl::EMPTY
                                }
                            ]),
                            ..new_component_decl()
                        }
                    },
                    result = Err(ErrorList::new(vec![
                        Error::dependency_cycle("{{child child1 -> environment env -> child child2 -> child child1}}".to_string()),
                    ])),
                },
                test_validate_strong_cycle_between_children_through_environment_runner => {
                    input = {
                        $namespace::ComponentDecl {
                            environments: Some(vec![
                                $namespace::EnvironmentDecl {
                                    name: Some("env".to_string()),
                                    extends: Some($namespace::EnvironmentExtends::Realm),
                                    runners: Some(vec![
                                        $namespace::RunnerRegistration {
                                            source: Some($namespace::Ref::Child($namespace::ChildRef { name: "child1".to_string(), collection: None })),
                                            source_name: Some("coff".to_string()),
                                            target_name: Some("coff".to_string()),
                                            ..$namespace::RunnerRegistration::EMPTY
                                        }
                                    ]),
                                    ..$namespace::EnvironmentDecl::EMPTY
                                },
                            ]),
                            offers: Some(vec![
                                $namespace::OfferDecl::Service($namespace::OfferServiceDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef { name: "child2".to_string(), collection: None })),
                                    source_name: Some("a".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef { name: "child1".to_string(), collection: None })),
                                    target_name: Some("a".to_string()),
                                    ..$namespace::OfferServiceDecl::EMPTY
                                }),
                            ]),
                            children: Some(vec![
                                $namespace::ChildDecl {
                                    name: Some("child1".to_string()),
                                    url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                                    startup: Some($namespace::StartupMode::Lazy),
                                    on_terminate: None,
                                    ..$namespace::ChildDecl::EMPTY
                                },
                                $namespace::ChildDecl {
                                    name: Some("child2".to_string()),
                                    url: Some("fuchsia-pkg://fuchsia.com/foo2".to_string()),
                                    startup: Some($namespace::StartupMode::Lazy),
                                    environment: Some("env".to_string()),
                                    on_terminate: None,
                                    ..$namespace::ChildDecl::EMPTY
                                }
                            ]),
                            ..new_component_decl()
                        }
                    },
                    result = Err(ErrorList::new(vec![
                        Error::dependency_cycle("{{child child1 -> environment env -> child child2 -> child child1}}".to_string()),
                    ])),
                },
                test_validate_strong_cycle_between_children_through_environment_resolver => {
                    input = {
                        $namespace::ComponentDecl {
                            environments: Some(vec![
                                $namespace::EnvironmentDecl {
                                    name: Some("env".to_string()),
                                    extends: Some($namespace::EnvironmentExtends::Realm),
                                    resolvers: Some(vec![
                                        $namespace::ResolverRegistration {
                                            resolver: Some("gopher".to_string()),
                                            source: Some($namespace::Ref::Child($namespace::ChildRef { name: "child1".to_string(), collection: None })),
                                            scheme: Some("gopher".to_string()),
                                            ..$namespace::ResolverRegistration::EMPTY
                                        }
                                    ]),
                                    ..$namespace::EnvironmentDecl::EMPTY
                                },
                            ]),
                            offers: Some(vec![
                                $namespace::OfferDecl::Service($namespace::OfferServiceDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef { name: "child2".to_string(), collection: None })),
                                    source_name: Some("a".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef { name: "child1".to_string(), collection: None })),
                                    target_name: Some("a".to_string()),
                                    ..$namespace::OfferServiceDecl::EMPTY
                                }),
                            ]),
                            children: Some(vec![
                                $namespace::ChildDecl {
                                    name: Some("child1".to_string()),
                                    url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                                    startup: Some($namespace::StartupMode::Lazy),
                                    on_terminate: None,
                                    ..$namespace::ChildDecl::EMPTY
                                },
                                $namespace::ChildDecl {
                                    name: Some("child2".to_string()),
                                    url: Some("fuchsia-pkg://fuchsia.com/foo2".to_string()),
                                    startup: Some($namespace::StartupMode::Lazy),
                                    environment: Some("env".to_string()),
                                    on_terminate: None,
                                    ..$namespace::ChildDecl::EMPTY
                                }
                            ]),
                            ..new_component_decl()
                        }
                    },
                    result = Err(ErrorList::new(vec![
                        Error::dependency_cycle("{{child child1 -> environment env -> child child2 -> child child1}}".to_string()),
                    ])),
                },
                test_validate_strong_cycle_between_self_and_two_children => {
                    input = {
                        $namespace::ComponentDecl {
                            capabilities: Some(vec![
                                $namespace::CapabilityDecl::Protocol($namespace::ProtocolDecl {
                                    name: Some("fuchsia.foo.Bar".to_string()),
                                    source_path: Some("/svc/fuchsia.foo.Bar".to_string()),
                                    ..$namespace::ProtocolDecl::EMPTY
                                })
                            ]),
                            offers: Some(vec![
                                $namespace::OfferDecl::Protocol($namespace::OfferProtocolDecl {
                                    source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                    source_name: Some("fuchsia.foo.Bar".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef { name: "child1".to_string(), collection: None })),
                                    target_name: Some("fuchsia.foo.Bar".to_string()),
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    ..$namespace::OfferProtocolDecl::EMPTY
                                }),
                                $namespace::OfferDecl::Protocol($namespace::OfferProtocolDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef { name: "child1".to_string(), collection: None })),
                                    source_name: Some("fuchsia.bar.Baz".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef { name: "child2".to_string(), collection: None })),
                                    target_name: Some("fuchsia.bar.Baz".to_string()),
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    ..$namespace::OfferProtocolDecl::EMPTY
                                }),
                            ]),
                            uses: Some(vec![
                                $namespace::UseDecl::Protocol($namespace::UseProtocolDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef{ name: "child2".to_string(), collection: None})),
                                    source_name: Some("fuchsia.baz.Foo".to_string()),
                                    target_path: Some("/svc/fuchsia.baz.Foo".to_string()),
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    ..$namespace::UseProtocolDecl::EMPTY
                                }),
                            ]),
                            children: Some(vec![
                                $namespace::ChildDecl {
                                    name: Some("child1".to_string()),
                                    url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                                    startup: Some($namespace::StartupMode::Lazy),
                                    on_terminate: None,
                                    ..$namespace::ChildDecl::EMPTY
                                },
                                $namespace::ChildDecl {
                                    name: Some("child2".to_string()),
                                    url: Some("fuchsia-pkg://fuchsia.com/foo2".to_string()),
                                    startup: Some($namespace::StartupMode::Lazy),
                                    on_terminate: None,
                                    ..$namespace::ChildDecl::EMPTY
                                }
                            ]),
                            ..new_component_decl()
                        }
                    },
                    result = Err(ErrorList::new(vec![
                        Error::dependency_cycle("{{self -> child child1 -> child child2 -> self}}".to_string()),
                    ])),
                },
                test_validate_strong_cycle_with_self_storage => {
                    input = {
                        $namespace::ComponentDecl {
                            capabilities: Some(vec![
                                $namespace::CapabilityDecl::Storage($namespace::StorageDecl {
                                    name: Some("data".to_string()),
                                    source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                    backing_dir: Some("minfs".to_string()),
                                    storage_id: Some($namespace::StorageId::StaticInstanceIdOrMoniker),
                                    ..$namespace::StorageDecl::EMPTY
                                }),
                                $namespace::CapabilityDecl::Directory($namespace::DirectoryDecl {
                                    name: Some("minfs".to_string()),
                                    source_path: Some("/minfs".to_string()),
                                    rights: Some(fio2::RW_STAR_DIR),
                                    ..$namespace::DirectoryDecl::EMPTY
                                }),
                            ]),
                            offers: Some(vec![
                                $namespace::OfferDecl::Storage($namespace::OfferStorageDecl {
                                    source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                    source_name: Some("data".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef { name: "child".to_string(), collection: None })),
                                    target_name: Some("data".to_string()),
                                    ..$namespace::OfferStorageDecl::EMPTY
                                }),
                            ]),
                            uses: Some(vec![
                                $namespace::UseDecl::Protocol($namespace::UseProtocolDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Child($namespace::ChildRef{ name: "child".to_string(), collection: None})),
                                    source_name: Some("fuchsia.foo.Bar".to_string()),
                                    target_path: Some("/svc/fuchsia.foo.Bar".to_string()),
                                    ..$namespace::UseProtocolDecl::EMPTY
                                }),
                            ]),
                            children: Some(vec![
                                $namespace::ChildDecl {
                                    name: Some("child".to_string()),
                                    url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                                    startup: Some($namespace::StartupMode::Lazy),
                                    ..$namespace::ChildDecl::EMPTY
                                },
                            ]),
                            ..new_component_decl()
                        }
                    },
                    result = Err(ErrorList::new(vec![
                        Error::dependency_cycle("{{self -> child child -> self}}".to_string()),
                    ])),
                },
                test_validate_strong_cycle_with_self_storage_admin_protocol => {
                    input = {
                        $namespace::ComponentDecl {
                            capabilities: Some(vec![
                                $namespace::CapabilityDecl::Storage($namespace::StorageDecl {
                                    name: Some("data".to_string()),
                                    source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                    backing_dir: Some("minfs".to_string()),
                                    storage_id: Some($namespace::StorageId::StaticInstanceIdOrMoniker),
                                    ..$namespace::StorageDecl::EMPTY
                                }),
                                $namespace::CapabilityDecl::Directory($namespace::DirectoryDecl {
                                    name: Some("minfs".to_string()),
                                    source_path: Some("/minfs".to_string()),
                                    rights: Some(fio2::RW_STAR_DIR),
                                    ..$namespace::DirectoryDecl::EMPTY
                                }),
                            ]),
                            offers: Some(vec![
                                $namespace::OfferDecl::Protocol($namespace::OfferProtocolDecl {
                                    source: Some($namespace::Ref::Capability($namespace::CapabilityRef { name: "data".to_string() })),
                                    source_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef { name: "child".to_string(), collection: None })),
                                    target_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    ..$namespace::OfferProtocolDecl::EMPTY
                                }),
                            ]),
                            uses: Some(vec![
                                $namespace::UseDecl::Protocol($namespace::UseProtocolDecl {
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    source: Some($namespace::Ref::Child($namespace::ChildRef{ name: "child".to_string(), collection: None})),
                                    source_name: Some("fuchsia.foo.Bar".to_string()),
                                    target_path: Some("/svc/fuchsia.foo.Bar".to_string()),
                                    ..$namespace::UseProtocolDecl::EMPTY
                                }),
                            ]),
                            children: Some(vec![
                                $namespace::ChildDecl {
                                    name: Some("child".to_string()),
                                    url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                                    startup: Some($namespace::StartupMode::Lazy),
                                    ..$namespace::ChildDecl::EMPTY
                                },
                            ]),
                            ..new_component_decl()
                        }
                    },
                    result = Err(ErrorList::new(vec![
                        Error::dependency_cycle("{{self -> child child -> self}}".to_string()),
                    ])),
                },
                test_validate_use_from_child_offer_to_child_weak_cycle => {
                    input = {
                        $namespace::ComponentDecl {
                            capabilities: Some(vec![
                                $namespace::CapabilityDecl::Service($namespace::ServiceDecl {
                                    name: Some("a".to_string()),
                                    source_path: Some("/a".to_string()),
                                    ..$namespace::ServiceDecl::EMPTY
                                })]),
                            uses: Some(vec![
                                $namespace::UseDecl::Protocol($namespace::UseProtocolDecl {
                                    dependency_type: Some($namespace::DependencyType::Weak),
                                    source: Some($namespace::Ref::Child($namespace::ChildRef{ name: "child".to_string(), collection: None})),
                                    source_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                                    target_path: Some("/svc/fuchsia.sys2.StorageAdmin".to_string()),
                                    ..$namespace::UseProtocolDecl::EMPTY
                                }),
                                $namespace::UseDecl::Service($namespace::UseServiceDecl {
                                    source: Some($namespace::Ref::Child($namespace::ChildRef{ name: "child".to_string(), collection: None})),
                                    source_name: Some("service_name".to_string()),
                                    target_path: Some("/svc/service_name".to_string()),
                                    dependency_type: Some($namespace::DependencyType::Weak),
                                    ..$namespace::UseServiceDecl::EMPTY
                                }),
                                $namespace::UseDecl::Directory($namespace::UseDirectoryDecl {
                                    dependency_type: Some($namespace::DependencyType::WeakForMigration),
                                    source: Some($namespace::Ref::Child($namespace::ChildRef{ name: "child".to_string(), collection: None})),
                                    source_name: Some("DirectoryName".to_string()),
                                    target_path: Some("/data/DirectoryName".to_string()),
                                    rights: Some(fio2::Operations::Connect),
                                    subdir: None,
                                    ..$namespace::UseDirectoryDecl::EMPTY
                                }),
                                $namespace::UseDecl::Event($namespace::UseEventDecl {
                                    dependency_type: Some($namespace::DependencyType::WeakForMigration),
                                    source: Some($namespace::Ref::Child($namespace::ChildRef{ name: "child".to_string(), collection: None})),
                                    source_name: Some("abc".to_string()),
                                    target_name: Some("abc".to_string()),
                                    filter: Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY }),
                                    mode: Some($namespace::EventMode::Async),
                                    ..$namespace::UseEventDecl::EMPTY
                                })
                            ]),
                            offers: Some(vec![
                                $namespace::OfferDecl::Service($namespace::OfferServiceDecl {
                                    source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                    source_name: Some("a".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef { name: "child".to_string(), collection: None })),
                                    target_name: Some("a".to_string()),
                                    ..$namespace::OfferServiceDecl::EMPTY
                                })
                            ]),
                            children: Some(vec![
                                $namespace::ChildDecl {
                                    name: Some("child".to_string()),
                                    url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                                    startup: Some($namespace::StartupMode::Lazy),
                                    on_terminate: None,
                                    ..$namespace::ChildDecl::EMPTY
                                }
                            ]),
                            ..new_component_decl()
                        }
                    },
                    result = Ok(()),
                },
                test_validate_use_from_not_child_weak => {
                    input = {
                        $namespace::ComponentDecl {
                            uses: Some(vec![
                                $namespace::UseDecl::Protocol($namespace::UseProtocolDecl {
                                    dependency_type: Some($namespace::DependencyType::Weak),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                    source_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                                    target_path: Some("/svc/fuchsia.sys2.StorageAdmin".to_string()),
                                    ..$namespace::UseProtocolDecl::EMPTY
                                }),
                            ]),
                            ..new_component_decl()
                        }
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_field("UseProtocolDecl", "dependency_type"),
                    ])),
                },
                test_validate_has_events_in_event_stream => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.uses = Some(vec![
                            $namespace::UseDecl::EventStream($namespace::UseEventStreamDecl {
                                name: Some("bar".to_string()),
                                subscriptions: None,
                                ..$namespace::UseEventStreamDecl::EMPTY
                            }),
                            $namespace::UseDecl::EventStream($namespace::UseEventStreamDecl {
                                name: Some("barbar".to_string()),
                                subscriptions: Some(vec![]),
                                ..$namespace::UseEventStreamDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::missing_field("UseEventStreamDecl", "subscriptions"),
                        Error::empty_field("UseEventStreamDecl", "subscriptions"),
                    ])),
                },
                test_validate_uses_no_runner => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.program = Some($namespace::ProgramDecl {
                            runner: None,
                            info: Some(fdata::Dictionary {
                                entries: None,
                                ..fdata::Dictionary::EMPTY
                            }),
                            ..$namespace::ProgramDecl::EMPTY
                        });
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::missing_field("ProgramDecl", "runner"),
                    ])),
                },
                test_validate_uses_long_identifiers => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.program = Some($namespace::ProgramDecl {
                            runner: Some("elf".to_string()),
                            info: Some(fdata::Dictionary {
                                entries: None,
                                ..fdata::Dictionary::EMPTY
                            }),
                            ..$namespace::ProgramDecl::EMPTY
                        });
                        decl.uses = Some(vec![
                            $namespace::UseDecl::Service($namespace::UseServiceDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                source_name: Some(format!("{}", "a".repeat(101))),
                                target_path: Some(format!("/s/{}", "b".repeat(1024))),
                                dependency_type: Some($namespace::DependencyType::Strong),
                                ..$namespace::UseServiceDecl::EMPTY
                            }),
                            $namespace::UseDecl::Protocol($namespace::UseProtocolDecl {
                                dependency_type: Some($namespace::DependencyType::Strong),
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                source_name: Some(format!("{}", "a".repeat(101))),
                                target_path: Some(format!("/p/{}", "c".repeat(1024))),
                                ..$namespace::UseProtocolDecl::EMPTY
                            }),
                            $namespace::UseDecl::Directory($namespace::UseDirectoryDecl {
                                dependency_type: Some($namespace::DependencyType::Strong),
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                source_name: Some(format!("{}", "a".repeat(101))),
                                target_path: Some(format!("/d/{}", "d".repeat(1024))),
                                rights: Some(fio2::Operations::Connect),
                                subdir: None,
                                ..$namespace::UseDirectoryDecl::EMPTY
                            }),
                            $namespace::UseDecl::Storage($namespace::UseStorageDecl {
                                source_name: Some("cache".to_string()),
                                target_path: Some(format!("/{}", "e".repeat(1024))),
                                ..$namespace::UseStorageDecl::EMPTY
                            }),
                            $namespace::UseDecl::Event($namespace::UseEventDecl {
                                dependency_type: Some($namespace::DependencyType::Strong),
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                source_name: Some(format!("{}", "a".repeat(101))),
                                target_name: Some(format!("{}", "a".repeat(101))),
                                filter: None,
                                mode: Some($namespace::EventMode::Sync),
                                ..$namespace::UseEventDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::field_too_long("UseEventDecl", "source_name"),
                        Error::field_too_long("UseEventDecl", "target_name"),
                        Error::field_too_long("UseServiceDecl", "source_name"),
                        Error::field_too_long("UseServiceDecl", "target_path"),
                        Error::field_too_long("UseProtocolDecl", "source_name"),
                        Error::field_too_long("UseProtocolDecl", "target_path"),
                        Error::field_too_long("UseDirectoryDecl", "source_name"),
                        Error::field_too_long("UseDirectoryDecl", "target_path"),
                        Error::field_too_long("UseStorageDecl", "target_path"),
                    ])),
                },
                test_validate_conflicting_paths => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.uses = Some(vec![
                            $namespace::UseDecl::Service($namespace::UseServiceDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                source_name: Some("foo".to_string()),
                                target_path: Some("/bar".to_string()),
                                dependency_type: Some($namespace::DependencyType::Strong),
                                ..$namespace::UseServiceDecl::EMPTY
                            }),
                            $namespace::UseDecl::Protocol($namespace::UseProtocolDecl {
                                dependency_type: Some($namespace::DependencyType::Strong),
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                source_name: Some("space".to_string()),
                                target_path: Some("/bar".to_string()),
                                ..$namespace::UseProtocolDecl::EMPTY
                            }),
                            $namespace::UseDecl::Directory($namespace::UseDirectoryDecl {
                                dependency_type: Some($namespace::DependencyType::Strong),
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                source_name: Some("crow".to_string()),
                                target_path: Some("/bar".to_string()),
                                rights: Some(fio2::Operations::Connect),
                                subdir: None,
                                ..$namespace::UseDirectoryDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::duplicate_field("UseProtocolDecl", "path", "/bar"),
                        Error::duplicate_field("UseDirectoryDecl", "path", "/bar"),
                    ])),
                },
                test_validate_events_can_come_before_or_after_event_stream => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.uses = Some(vec![
                            $namespace::UseDecl::Event($namespace::UseEventDecl {
                                dependency_type: Some($namespace::DependencyType::Strong),
                                source: Some($namespace::Ref::Framework($namespace::FrameworkRef {})),
                                source_name: Some("started".to_string()),
                                target_name: Some("started".to_string()),
                                filter: Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY }),
                                mode: Some($namespace::EventMode::Async),
                                ..$namespace::UseEventDecl::EMPTY
                            }),
                            $namespace::UseDecl::EventStream($namespace::UseEventStreamDecl {
                                name: Some("bar".to_string()),
                                subscriptions: Some(
                                    vec!["started".to_string(), "stopped".to_string()]
                                        .into_iter()
                                        .map(|name| $namespace::EventSubscription {
                                            event_name: Some(name),
                                            mode: Some($namespace::EventMode::Async),
                                            ..$namespace::EventSubscription::EMPTY
                                        })
                                        .collect()
                                    ),
                                ..$namespace::UseEventStreamDecl::EMPTY
                            }),
                            $namespace::UseDecl::Event($namespace::UseEventDecl {
                                dependency_type: Some($namespace::DependencyType::Strong),
                                source: Some($namespace::Ref::Framework($namespace::FrameworkRef {})),
                                source_name: Some("stopped".to_string()),
                                target_name: Some("stopped".to_string()),
                                filter: Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY }),
                                mode: Some($namespace::EventMode::Async),
                                ..$namespace::UseEventDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Ok(()),
                },
                test_validate_uses_invalid_self_source => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.uses = Some(vec![
                            $namespace::UseDecl::Event($namespace::UseEventDecl {
                                source: Some($namespace::Ref::Self_($namespace::SelfRef {})),
                                source_name: Some("started".to_string()),
                                target_name: Some("foo_started".to_string()),
                                mode: Some($namespace::EventMode::Async),
                                ..$namespace::UseEventDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_field("UseEventDecl", "source"),
                    ])),
                },
                // exposes
                test_validate_exposes_empty => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.exposes = Some(vec![
                            $namespace::ExposeDecl::Service($namespace::ExposeServiceDecl {
                                source: None,
                                source_name: None,
                                target_name: None,
                                target: None,
                                ..$namespace::ExposeServiceDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Protocol($namespace::ExposeProtocolDecl {
                                source: None,
                                source_name: None,
                                target_name: None,
                                target: None,
                                ..$namespace::ExposeProtocolDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Directory($namespace::ExposeDirectoryDecl {
                                source: None,
                                source_name: None,
                                target_name: None,
                                target: None,
                                rights: None,
                                subdir: None,
                                ..$namespace::ExposeDirectoryDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Runner($namespace::ExposeRunnerDecl {
                                source: None,
                                source_name: None,
                                target: None,
                                target_name: None,
                                ..$namespace::ExposeRunnerDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Resolver($namespace::ExposeResolverDecl {
                                source: None,
                                source_name: None,
                                target: None,
                                target_name: None,
                                ..$namespace::ExposeResolverDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::missing_field("ExposeServiceDecl", "source"),
                        Error::missing_field("ExposeServiceDecl", "target"),
                        Error::missing_field("ExposeServiceDecl", "source_name"),
                        Error::missing_field("ExposeServiceDecl", "target_name"),
                        Error::missing_field("ExposeProtocolDecl", "source"),
                        Error::missing_field("ExposeProtocolDecl", "target"),
                        Error::missing_field("ExposeProtocolDecl", "source_name"),
                        Error::missing_field("ExposeProtocolDecl", "target_name"),
                        Error::missing_field("ExposeDirectoryDecl", "source"),
                        Error::missing_field("ExposeDirectoryDecl", "target"),
                        Error::missing_field("ExposeDirectoryDecl", "source_name"),
                        Error::missing_field("ExposeDirectoryDecl", "target_name"),
                        Error::missing_field("ExposeRunnerDecl", "source"),
                        Error::missing_field("ExposeRunnerDecl", "target"),
                        Error::missing_field("ExposeRunnerDecl", "source_name"),
                        Error::missing_field("ExposeRunnerDecl", "target_name"),
                        Error::missing_field("ExposeResolverDecl", "source"),
                        Error::missing_field("ExposeResolverDecl", "target"),
                        Error::missing_field("ExposeResolverDecl", "source_name"),
                        Error::missing_field("ExposeResolverDecl", "target_name"),
                    ])),
                },
                test_validate_exposes_extraneous => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.exposes = Some(vec![
                            $namespace::ExposeDecl::Service($namespace::ExposeServiceDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "logger".to_string(),
                                    collection: Some("modular".to_string()),
                                })),
                                source_name: Some("logger".to_string()),
                                target_name: Some("logger".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                ..$namespace::ExposeServiceDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Protocol($namespace::ExposeProtocolDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "logger".to_string(),
                                    collection: Some("modular".to_string()),
                                })),
                                source_name: Some("legacy_logger".to_string()),
                                target_name: Some("legacy_logger".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                ..$namespace::ExposeProtocolDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Directory($namespace::ExposeDirectoryDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "netstack".to_string(),
                                    collection: Some("modular".to_string()),
                                })),
                                source_name: Some("data".to_string()),
                                target_name: Some("data".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                rights: Some(fio2::Operations::Connect),
                                subdir: None,
                                ..$namespace::ExposeDirectoryDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Runner($namespace::ExposeRunnerDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "netstack".to_string(),
                                    collection: Some("modular".to_string()),
                                })),
                                source_name: Some("elf".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                target_name: Some("elf".to_string()),
                                ..$namespace::ExposeRunnerDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Resolver($namespace::ExposeResolverDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "netstack".to_string(),
                                    collection: Some("modular".to_string()),
                                })),
                                source_name: Some("pkg".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                target_name: Some("pkg".to_string()),
                                ..$namespace::ExposeResolverDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::extraneous_field("ExposeServiceDecl", "source.child.collection"),
                        Error::extraneous_field("ExposeProtocolDecl", "source.child.collection"),
                        Error::extraneous_field("ExposeDirectoryDecl", "source.child.collection"),
                        Error::extraneous_field("ExposeRunnerDecl", "source.child.collection"),
                        Error::extraneous_field("ExposeResolverDecl", "source.child.collection"),
                    ])),
                },
                test_validate_exposes_invalid_identifiers => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.exposes = Some(vec![
                            $namespace::ExposeDecl::Service($namespace::ExposeServiceDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "^bad".to_string(),
                                    collection: None,
                                })),
                                source_name: Some("foo/".to_string()),
                                target_name: Some("/".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                ..$namespace::ExposeServiceDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Protocol($namespace::ExposeProtocolDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "^bad".to_string(),
                                    collection: None,
                                })),
                                source_name: Some("foo/".to_string()),
                                target_name: Some("/".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                ..$namespace::ExposeProtocolDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Directory($namespace::ExposeDirectoryDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "^bad".to_string(),
                                    collection: None,
                                })),
                                source_name: Some("foo/".to_string()),
                                target_name: Some("/".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                rights: Some(fio2::Operations::Connect),
                                subdir: Some("/foo".to_string()),
                                ..$namespace::ExposeDirectoryDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Runner($namespace::ExposeRunnerDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "^bad".to_string(),
                                    collection: None,
                                })),
                                source_name: Some("/path".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                target_name: Some("elf!".to_string()),
                                ..$namespace::ExposeRunnerDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Resolver($namespace::ExposeResolverDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "^bad".to_string(),
                                    collection: None,
                                })),
                                source_name: Some("/path".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                target_name: Some("pkg!".to_string()),
                                ..$namespace::ExposeResolverDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_field("ExposeServiceDecl", "source.child.name"),
                        Error::invalid_field("ExposeServiceDecl", "source_name"),
                        Error::invalid_field("ExposeServiceDecl", "target_name"),
                        Error::invalid_field("ExposeProtocolDecl", "source.child.name"),
                        Error::invalid_field("ExposeProtocolDecl", "source_name"),
                        Error::invalid_field("ExposeProtocolDecl", "target_name"),
                        Error::invalid_field("ExposeDirectoryDecl", "source.child.name"),
                        Error::invalid_field("ExposeDirectoryDecl", "source_name"),
                        Error::invalid_field("ExposeDirectoryDecl", "target_name"),
                        Error::invalid_field("ExposeDirectoryDecl", "subdir"),
                        Error::invalid_field("ExposeRunnerDecl", "source.child.name"),
                        Error::invalid_field("ExposeRunnerDecl", "source_name"),
                        Error::invalid_field("ExposeRunnerDecl", "target_name"),
                        Error::invalid_field("ExposeResolverDecl", "source.child.name"),
                        Error::invalid_field("ExposeResolverDecl", "source_name"),
                        Error::invalid_field("ExposeResolverDecl", "target_name"),
                    ])),
                },
                test_validate_exposes_invalid_source_target => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.children = Some(vec![$namespace::ChildDecl{
                            name: Some("logger".to_string()),
                            url: Some("fuchsia-pkg://fuchsia.com/logger#meta/logger.cm".to_string()),
                            startup: Some($namespace::StartupMode::Lazy),
                            on_terminate: None,
                            environment: None,
                            ..$namespace::ChildDecl::EMPTY
                        }]);
                        decl.exposes = Some(vec![
                            $namespace::ExposeDecl::Service($namespace::ExposeServiceDecl {
                                source: None,
                                source_name: Some("a".to_string()),
                                target_name: Some("b".to_string()),
                                target: None,
                                ..$namespace::ExposeServiceDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Protocol($namespace::ExposeProtocolDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                source_name: Some("c".to_string()),
                                target_name: Some("d".to_string()),
                                target: Some($namespace::Ref::Self_($namespace::SelfRef {})),
                                ..$namespace::ExposeProtocolDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Directory($namespace::ExposeDirectoryDecl {
                                source: Some($namespace::Ref::Collection($namespace::CollectionRef {name: "z".to_string()})),
                                source_name: Some("e".to_string()),
                                target_name: Some("f".to_string()),
                                target: Some($namespace::Ref::Collection($namespace::CollectionRef {name: "z".to_string()})),
                                rights: Some(fio2::Operations::Connect),
                                subdir: None,
                                ..$namespace::ExposeDirectoryDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Directory($namespace::ExposeDirectoryDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                source_name: Some("g".to_string()),
                                target_name: Some("h".to_string()),
                                target: Some($namespace::Ref::Framework($namespace::FrameworkRef {})),
                                rights: Some(fio2::Operations::Connect),
                                subdir: None,
                                ..$namespace::ExposeDirectoryDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Runner($namespace::ExposeRunnerDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                source_name: Some("i".to_string()),
                                target: Some($namespace::Ref::Framework($namespace::FrameworkRef {})),
                                target_name: Some("j".to_string()),
                                ..$namespace::ExposeRunnerDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Resolver($namespace::ExposeResolverDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                source_name: Some("k".to_string()),
                                target: Some($namespace::Ref::Framework($namespace::FrameworkRef {})),
                                target_name: Some("l".to_string()),
                                ..$namespace::ExposeResolverDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Directory($namespace::ExposeDirectoryDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "logger".to_string(),
                                    collection: None,
                                })),
                                source_name: Some("m".to_string()),
                                target_name: Some("n".to_string()),
                                target: Some($namespace::Ref::Framework($namespace::FrameworkRef {})),
                                rights: Some(fio2::Operations::Connect),
                                subdir: None,
                                ..$namespace::ExposeDirectoryDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::missing_field("ExposeServiceDecl", "source"),
                        Error::missing_field("ExposeServiceDecl", "target"),
                        Error::invalid_field("ExposeProtocolDecl", "source"),
                        Error::invalid_field("ExposeProtocolDecl", "target"),
                        Error::invalid_field("ExposeDirectoryDecl", "source"),
                        Error::invalid_field("ExposeDirectoryDecl", "target"),
                        Error::invalid_field("ExposeDirectoryDecl", "source"),
                        Error::invalid_field("ExposeDirectoryDecl", "target"),
                        Error::invalid_field("ExposeRunnerDecl", "source"),
                        Error::invalid_field("ExposeRunnerDecl", "target"),
                        Error::invalid_field("ExposeResolverDecl", "source"),
                        Error::invalid_field("ExposeResolverDecl", "target"),
                        Error::invalid_field("ExposeDirectoryDecl", "target"),
                    ])),
                },
                test_validate_exposes_invalid_source_collection => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.collections = Some(vec![$namespace::CollectionDecl{
                            name: Some("col".to_string()),
                            durability: Some($namespace::Durability::Transient),
                            allowed_offers: None,
                            ..$namespace::CollectionDecl::EMPTY
                        }]);
                        decl.exposes = Some(vec![
                            $namespace::ExposeDecl::Protocol($namespace::ExposeProtocolDecl {
                                source: Some($namespace::Ref::Collection($namespace::CollectionRef { name: "col".to_string() })),
                                source_name: Some("a".to_string()),
                                target_name: Some("a".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                ..$namespace::ExposeProtocolDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Directory($namespace::ExposeDirectoryDecl {
                                source: Some($namespace::Ref::Collection($namespace::CollectionRef {name: "col".to_string()})),
                                source_name: Some("b".to_string()),
                                target_name: Some("b".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                rights: Some(fio2::Operations::Connect),
                                subdir: None,
                                ..$namespace::ExposeDirectoryDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Runner($namespace::ExposeRunnerDecl {
                                source: Some($namespace::Ref::Collection($namespace::CollectionRef {name: "col".to_string()})),
                                source_name: Some("c".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                target_name: Some("c".to_string()),
                                ..$namespace::ExposeRunnerDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Resolver($namespace::ExposeResolverDecl {
                                source: Some($namespace::Ref::Collection($namespace::CollectionRef {name: "col".to_string()})),
                                source_name: Some("d".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                target_name: Some("d".to_string()),
                                ..$namespace::ExposeResolverDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_field("ExposeProtocolDecl", "source"),
                        Error::invalid_field("ExposeDirectoryDecl", "source"),
                        Error::invalid_field("ExposeRunnerDecl", "source"),
                        Error::invalid_field("ExposeResolverDecl", "source"),
                    ])),
                },
                test_validate_exposes_sources_collection => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.collections = Some(vec![
                            $namespace::CollectionDecl {
                                name: Some("col".to_string()),
                                durability: Some($namespace::Durability::Transient),
                                allowed_offers: Some($namespace::AllowedOffers::StaticOnly),
                                ..$namespace::CollectionDecl::EMPTY
                            }
                        ]);
                        decl.exposes = Some(vec![
                            $namespace::ExposeDecl::Service($namespace::ExposeServiceDecl {
                                source: Some($namespace::Ref::Collection($namespace::CollectionRef { name: "col".to_string() })),
                                source_name: Some("a".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                target_name: Some("a".to_string()),
                                ..$namespace::ExposeServiceDecl::EMPTY
                            })
                        ]);
                        decl
                    },
                    result = Ok(()),
                },
                test_validate_exposes_long_identifiers => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.exposes = Some(vec![
                            $namespace::ExposeDecl::Service($namespace::ExposeServiceDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "b".repeat(101),
                                    collection: None,
                                })),
                                source_name: Some(format!("{}", "a".repeat(1025))),
                                target_name: Some(format!("{}", "b".repeat(1025))),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                ..$namespace::ExposeServiceDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Protocol($namespace::ExposeProtocolDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "b".repeat(101),
                                    collection: None,
                                })),
                                source_name: Some(format!("{}", "a".repeat(101))),
                                target_name: Some(format!("{}", "b".repeat(101))),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                ..$namespace::ExposeProtocolDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Directory($namespace::ExposeDirectoryDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "b".repeat(101),
                                    collection: None,
                                })),
                                source_name: Some(format!("{}", "a".repeat(101))),
                                target_name: Some(format!("{}", "b".repeat(101))),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                rights: Some(fio2::Operations::Connect),
                                subdir: None,
                                ..$namespace::ExposeDirectoryDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Runner($namespace::ExposeRunnerDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "b".repeat(101),
                                    collection: None,
                                })),
                                source_name: Some("a".repeat(101)),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                target_name: Some("b".repeat(101)),
                                ..$namespace::ExposeRunnerDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Resolver($namespace::ExposeResolverDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "b".repeat(101),
                                    collection: None,
                                })),
                                source_name: Some("a".repeat(101)),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                target_name: Some("b".repeat(101)),
                                ..$namespace::ExposeResolverDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::field_too_long("ExposeServiceDecl", "source.child.name"),
                        Error::field_too_long("ExposeServiceDecl", "source_name"),
                        Error::field_too_long("ExposeServiceDecl", "target_name"),
                        Error::field_too_long("ExposeProtocolDecl", "source.child.name"),
                        Error::field_too_long("ExposeProtocolDecl", "source_name"),
                        Error::field_too_long("ExposeProtocolDecl", "target_name"),
                        Error::field_too_long("ExposeDirectoryDecl", "source.child.name"),
                        Error::field_too_long("ExposeDirectoryDecl", "source_name"),
                        Error::field_too_long("ExposeDirectoryDecl", "target_name"),
                        Error::field_too_long("ExposeRunnerDecl", "source.child.name"),
                        Error::field_too_long("ExposeRunnerDecl", "source_name"),
                        Error::field_too_long("ExposeRunnerDecl", "target_name"),
                        Error::field_too_long("ExposeResolverDecl", "source.child.name"),
                        Error::field_too_long("ExposeResolverDecl", "source_name"),
                        Error::field_too_long("ExposeResolverDecl", "target_name"),
                    ])),
                },
                test_validate_exposes_invalid_child => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.exposes = Some(vec![
                            $namespace::ExposeDecl::Service($namespace::ExposeServiceDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "netstack".to_string(),
                                    collection: None,
                                })),
                                source_name: Some("fuchsia.logger.Log".to_string()),
                                target_name: Some("fuchsia.logger.Log".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                ..$namespace::ExposeServiceDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Protocol($namespace::ExposeProtocolDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "netstack".to_string(),
                                    collection: None,
                                })),
                                source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                ..$namespace::ExposeProtocolDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Directory($namespace::ExposeDirectoryDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "netstack".to_string(),
                                    collection: None,
                                })),
                                source_name: Some("data".to_string()),
                                target_name: Some("data".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                rights: Some(fio2::Operations::Connect),
                                subdir: None,
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
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_child("ExposeServiceDecl", "source", "netstack"),
                        Error::invalid_child("ExposeProtocolDecl", "source", "netstack"),
                        Error::invalid_child("ExposeDirectoryDecl", "source", "netstack"),
                        Error::invalid_child("ExposeRunnerDecl", "source", "netstack"),
                        Error::invalid_child("ExposeResolverDecl", "source", "netstack"),
                    ])),
                },
                test_validate_exposes_invalid_source_capability => {
                    input = {
                        $namespace::ComponentDecl {
                            exposes: Some(vec![
                                $namespace::ExposeDecl::Protocol($namespace::ExposeProtocolDecl {
                                    source: Some($namespace::Ref::Capability($namespace::CapabilityRef {
                                        name: "this-storage-doesnt-exist".to_string(),
                                    })),
                                    source_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                                    target_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                                    target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                    ..$namespace::ExposeProtocolDecl::EMPTY
                                }),
                            ]),
                            ..new_component_decl()
                        }
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_capability("ExposeProtocolDecl", "source", "this-storage-doesnt-exist"),
                    ])),
                },
                test_validate_exposes_duplicate_target => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.exposes = Some(vec![
                            $namespace::ExposeDecl::Service($namespace::ExposeServiceDecl {
                                source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                source_name: Some("netstack".to_string()),
                                target_name: Some("fuchsia.net.Stack".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                ..$namespace::ExposeServiceDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Service($namespace::ExposeServiceDecl {
                                source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                source_name: Some("netstack2".to_string()),
                                target_name: Some("fuchsia.net.Stack".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                ..$namespace::ExposeServiceDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Protocol($namespace::ExposeProtocolDecl {
                                source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                source_name: Some("fonts".to_string()),
                                target_name: Some("fuchsia.fonts.Provider".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                ..$namespace::ExposeProtocolDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Protocol($namespace::ExposeProtocolDecl {
                                source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                source_name: Some("fonts2".to_string()),
                                target_name: Some("fuchsia.fonts.Provider".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                ..$namespace::ExposeProtocolDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Directory($namespace::ExposeDirectoryDecl {
                                source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                source_name: Some("assets".to_string()),
                                target_name: Some("stuff".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                rights: None,
                                subdir: None,
                                ..$namespace::ExposeDirectoryDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Directory($namespace::ExposeDirectoryDecl {
                                source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                source_name: Some("assets2".to_string()),
                                target_name: Some("stuff".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                rights: None,
                                subdir: None,
                                ..$namespace::ExposeDirectoryDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Runner($namespace::ExposeRunnerDecl {
                                source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                source_name: Some("source_elf".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                target_name: Some("elf".to_string()),
                                ..$namespace::ExposeRunnerDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Runner($namespace::ExposeRunnerDecl {
                                source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                source_name: Some("source_elf".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                target_name: Some("elf".to_string()),
                                ..$namespace::ExposeRunnerDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Resolver($namespace::ExposeResolverDecl {
                                source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                source_name: Some("source_pkg".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                target_name: Some("pkg".to_string()),
                                ..$namespace::ExposeResolverDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Resolver($namespace::ExposeResolverDecl {
                                source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                source_name: Some("source_pkg".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                target_name: Some("pkg".to_string()),
                                ..$namespace::ExposeResolverDecl::EMPTY
                            }),
                        ]);
                        decl.capabilities = Some(vec![
                            $namespace::CapabilityDecl::Service($namespace::ServiceDecl {
                                name: Some("netstack".to_string()),
                                source_path: Some("/path".to_string()),
                                ..$namespace::ServiceDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Service($namespace::ServiceDecl {
                                name: Some("netstack2".to_string()),
                                source_path: Some("/path".to_string()),
                                ..$namespace::ServiceDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Protocol($namespace::ProtocolDecl {
                                name: Some("fonts".to_string()),
                                source_path: Some("/path".to_string()),
                                ..$namespace::ProtocolDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Protocol($namespace::ProtocolDecl {
                                name: Some("fonts2".to_string()),
                                source_path: Some("/path".to_string()),
                                ..$namespace::ProtocolDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Directory($namespace::DirectoryDecl {
                                name: Some("assets".to_string()),
                                source_path: Some("/path".to_string()),
                                rights: Some(fio2::Operations::Connect),
                                ..$namespace::DirectoryDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Directory($namespace::DirectoryDecl {
                                name: Some("assets2".to_string()),
                                source_path: Some("/path".to_string()),
                                rights: Some(fio2::Operations::Connect),
                                ..$namespace::DirectoryDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Runner($namespace::RunnerDecl {
                                name: Some("source_elf".to_string()),
                                source_path: Some("/path".to_string()),
                                ..$namespace::RunnerDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Resolver($namespace::ResolverDecl {
                                name: Some("source_pkg".to_string()),
                                source_path: Some("/path".to_string()),
                                ..$namespace::ResolverDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        // Duplicate services are allowed.
                        Error::duplicate_field("ExposeProtocolDecl", "target_name",
                                            "fuchsia.fonts.Provider"),
                        Error::duplicate_field("ExposeDirectoryDecl", "target_name",
                                            "stuff"),
                        Error::duplicate_field("ExposeRunnerDecl", "target_name",
                                            "elf"),
                        Error::duplicate_field("ExposeResolverDecl", "target_name", "pkg"),
                    ])),
                },
                // TODO: Add analogous test for offer
                test_validate_exposes_invalid_capability_from_self => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.exposes = Some(vec![
                            $namespace::ExposeDecl::Service($namespace::ExposeServiceDecl {
                                source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                source_name: Some("fuchsia.netstack.Netstack".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                target_name: Some("foo".to_string()),
                                ..$namespace::ExposeServiceDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Protocol($namespace::ExposeProtocolDecl {
                                source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                source_name: Some("fuchsia.netstack.Netstack".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                target_name: Some("bar".to_string()),
                                ..$namespace::ExposeProtocolDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Directory($namespace::ExposeDirectoryDecl {
                                source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                source_name: Some("dir".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                target_name: Some("assets".to_string()),
                                rights: None,
                                subdir: None,
                                ..$namespace::ExposeDirectoryDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Runner($namespace::ExposeRunnerDecl {
                                source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                source_name: Some("source_elf".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                target_name: Some("elf".to_string()),
                                ..$namespace::ExposeRunnerDecl::EMPTY
                            }),
                            $namespace::ExposeDecl::Resolver($namespace::ExposeResolverDecl {
                                source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                source_name: Some("source_pkg".to_string()),
                                target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                target_name: Some("pkg".to_string()),
                                ..$namespace::ExposeResolverDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_capability("ExposeServiceDecl", "source", "fuchsia.netstack.Netstack"),
                        Error::invalid_capability("ExposeProtocolDecl", "source", "fuchsia.netstack.Netstack"),
                        Error::invalid_capability("ExposeDirectoryDecl", "source", "dir"),
                        Error::invalid_capability("ExposeRunnerDecl", "source", "source_elf"),
                        Error::invalid_capability("ExposeResolverDecl", "source", "source_pkg"),
                    ])),
                },

                // offers
                test_validate_offers_empty => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.offers = Some(vec![
                            $namespace::OfferDecl::Service($namespace::OfferServiceDecl {
                                source: None,
                                source_name: None,
                                target: None,
                                target_name: None,
                                ..$namespace::OfferServiceDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Protocol($namespace::OfferProtocolDecl {
                                source: None,
                                source_name: None,
                                target: None,
                                target_name: None,
                                dependency_type: None,
                                ..$namespace::OfferProtocolDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Directory($namespace::OfferDirectoryDecl {
                                source: None,
                                source_name: None,
                                target: None,
                                target_name: None,
                                rights: None,
                                subdir: None,
                                dependency_type: None,
                                ..$namespace::OfferDirectoryDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Storage($namespace::OfferStorageDecl {
                                source_name: None,
                                source: None,
                                target: None,
                                target_name: None,
                                ..$namespace::OfferStorageDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Runner($namespace::OfferRunnerDecl {
                                source: None,
                                source_name: None,
                                target: None,
                                target_name: None,
                                ..$namespace::OfferRunnerDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Event($namespace::OfferEventDecl {
                                source: None,
                                source_name: None,
                                target: None,
                                target_name: None,
                                filter: None,
                                mode: None,
                                ..$namespace::OfferEventDecl::EMPTY
                            })
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::missing_field("OfferServiceDecl", "source"),
                        Error::missing_field("OfferServiceDecl", "source_name"),
                        Error::missing_field("OfferServiceDecl", "target"),
                        Error::missing_field("OfferServiceDecl", "target_name"),
                        Error::missing_field("OfferProtocolDecl", "source"),
                        Error::missing_field("OfferProtocolDecl", "source_name"),
                        Error::missing_field("OfferProtocolDecl", "target"),
                        Error::missing_field("OfferProtocolDecl", "target_name"),
                        Error::missing_field("OfferProtocolDecl", "dependency_type"),
                        Error::missing_field("OfferDirectoryDecl", "source"),
                        Error::missing_field("OfferDirectoryDecl", "source_name"),
                        Error::missing_field("OfferDirectoryDecl", "target"),
                        Error::missing_field("OfferDirectoryDecl", "target_name"),
                        Error::missing_field("OfferDirectoryDecl", "dependency_type"),
                        Error::missing_field("OfferStorageDecl", "source_name"),
                        Error::missing_field("OfferStorageDecl", "source"),
                        Error::missing_field("OfferStorageDecl", "target"),
                        Error::missing_field("OfferRunnerDecl", "source"),
                        Error::missing_field("OfferRunnerDecl", "source_name"),
                        Error::missing_field("OfferRunnerDecl", "target"),
                        Error::missing_field("OfferRunnerDecl", "target_name"),
                        Error::missing_field("OfferEventDecl", "source_name"),
                        Error::missing_field("OfferEventDecl", "source"),
                        Error::missing_field("OfferEventDecl", "target"),
                        Error::missing_field("OfferEventDecl", "target_name"),
                        Error::missing_field("OfferEventDecl", "mode"),
                    ])),
                },
                test_validate_offers_long_identifiers => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.offers = Some(vec![
                            $namespace::OfferDecl::Service($namespace::OfferServiceDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "a".repeat(101),
                                    collection: None,
                                })),
                                source_name: Some(format!("{}", "a".repeat(101))),
                                target: Some($namespace::Ref::Child(
                                $namespace::ChildRef {
                                    name: "b".repeat(101),
                                    collection: None,
                                }
                                )),
                                target_name: Some(format!("{}", "b".repeat(101))),
                                ..$namespace::OfferServiceDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Service($namespace::OfferServiceDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                source_name: Some("a".to_string()),
                                target: Some($namespace::Ref::Collection(
                                $namespace::CollectionRef {
                                    name: "b".repeat(101),
                                }
                                )),
                                target_name: Some(format!("{}", "b".repeat(101))),
                                ..$namespace::OfferServiceDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Protocol($namespace::OfferProtocolDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "a".repeat(101),
                                    collection: None,
                                })),
                                source_name: Some(format!("{}", "a".repeat(101))),
                                target: Some($namespace::Ref::Child(
                                $namespace::ChildRef {
                                    name: "b".repeat(101),
                                    collection: None,
                                }
                                )),
                                target_name: Some(format!("{}", "b".repeat(101))),
                                dependency_type: Some($namespace::DependencyType::Strong),
                                ..$namespace::OfferProtocolDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Protocol($namespace::OfferProtocolDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                source_name: Some("a".to_string()),
                                target: Some($namespace::Ref::Collection(
                                $namespace::CollectionRef {
                                    name: "b".repeat(101),
                                }
                                )),
                                target_name: Some(format!("{}", "b".repeat(101))),
                                dependency_type: Some($namespace::DependencyType::Weak),
                                ..$namespace::OfferProtocolDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Directory($namespace::OfferDirectoryDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "a".repeat(101),
                                    collection: None,
                                })),
                                source_name: Some(format!("{}", "a".repeat(101))),
                                target: Some($namespace::Ref::Child(
                                $namespace::ChildRef {
                                    name: "b".repeat(101),
                                    collection: None,
                                }
                                )),
                                target_name: Some(format!("{}", "b".repeat(101))),
                                rights: Some(fio2::Operations::Connect),
                                subdir: None,
                                dependency_type: Some($namespace::DependencyType::Strong),
                                ..$namespace::OfferDirectoryDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Directory($namespace::OfferDirectoryDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                source_name: Some("a".to_string()),
                                target: Some($namespace::Ref::Collection(
                                $namespace::CollectionRef {
                                    name: "b".repeat(101),
                                }
                                )),
                                target_name: Some(format!("{}", "b".repeat(101))),
                                rights: Some(fio2::Operations::Connect),
                                subdir: None,
                                dependency_type: Some($namespace::DependencyType::Weak),
                                ..$namespace::OfferDirectoryDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Storage($namespace::OfferStorageDecl {
                                source_name: Some("data".to_string()),
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                target: Some($namespace::Ref::Child(
                                    $namespace::ChildRef {
                                        name: "b".repeat(101),
                                        collection: None,
                                    }
                                )),
                                target_name: Some("data".to_string()),
                                ..$namespace::OfferStorageDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Storage($namespace::OfferStorageDecl {
                                source_name: Some("data".to_string()),
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                target: Some($namespace::Ref::Collection(
                                    $namespace::CollectionRef { name: "b".repeat(101) }
                                )),
                                target_name: Some("data".to_string()),
                                ..$namespace::OfferStorageDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Runner($namespace::OfferRunnerDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "a".repeat(101),
                                    collection: None,
                                })),
                                source_name: Some("b".repeat(101)),
                                target: Some($namespace::Ref::Collection(
                                $namespace::CollectionRef {
                                    name: "c".repeat(101),
                                }
                                )),
                                target_name: Some("d".repeat(101)),
                                ..$namespace::OfferRunnerDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Resolver($namespace::OfferResolverDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "a".repeat(101),
                                    collection: None,
                                })),
                                source_name: Some("b".repeat(101)),
                                target: Some($namespace::Ref::Collection(
                                    $namespace::CollectionRef {
                                        name: "c".repeat(101),
                                    }
                                )),
                                target_name: Some("d".repeat(101)),
                                ..$namespace::OfferResolverDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Event($namespace::OfferEventDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                source_name: Some(format!("{}", "a".repeat(101))),
                                target: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "a".repeat(101),
                                    collection: None
                                })),
                                target_name: Some(format!("{}", "a".repeat(101))),
                                filter: Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY }),
                                mode: Some($namespace::EventMode::Async),
                                ..$namespace::OfferEventDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::field_too_long("OfferServiceDecl", "source.child.name"),
                        Error::field_too_long("OfferServiceDecl", "source_name"),
                        Error::field_too_long("OfferServiceDecl", "target.child.name"),
                        Error::field_too_long("OfferServiceDecl", "target_name"),
                        Error::field_too_long("OfferServiceDecl", "target.collection.name"),
                        Error::field_too_long("OfferServiceDecl", "target_name"),
                        Error::field_too_long("OfferProtocolDecl", "source.child.name"),
                        Error::field_too_long("OfferProtocolDecl", "source_name"),
                        Error::field_too_long("OfferProtocolDecl", "target.child.name"),
                        Error::field_too_long("OfferProtocolDecl", "target_name"),
                        Error::field_too_long("OfferProtocolDecl", "target.collection.name"),
                        Error::field_too_long("OfferProtocolDecl", "target_name"),
                        Error::field_too_long("OfferDirectoryDecl", "source.child.name"),
                        Error::field_too_long("OfferDirectoryDecl", "source_name"),
                        Error::field_too_long("OfferDirectoryDecl", "target.child.name"),
                        Error::field_too_long("OfferDirectoryDecl", "target_name"),
                        Error::field_too_long("OfferDirectoryDecl", "target.collection.name"),
                        Error::field_too_long("OfferDirectoryDecl", "target_name"),
                        Error::field_too_long("OfferStorageDecl", "target.child.name"),
                        Error::field_too_long("OfferStorageDecl", "target.collection.name"),
                        Error::field_too_long("OfferRunnerDecl", "source.child.name"),
                        Error::field_too_long("OfferRunnerDecl", "source_name"),
                        Error::field_too_long("OfferRunnerDecl", "target.collection.name"),
                        Error::field_too_long("OfferRunnerDecl", "target_name"),
                        Error::field_too_long("OfferResolverDecl", "source.child.name"),
                        Error::field_too_long("OfferResolverDecl", "source_name"),
                        Error::field_too_long("OfferResolverDecl", "target.collection.name"),
                        Error::field_too_long("OfferResolverDecl", "target_name"),
                        Error::field_too_long("OfferEventDecl", "source_name"),
                        Error::field_too_long("OfferEventDecl", "target.child.name"),
                        Error::field_too_long("OfferEventDecl", "target_name"),
                    ])),
                },
                test_validate_offers_extraneous => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.offers = Some(vec![
                            $namespace::OfferDecl::Service($namespace::OfferServiceDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "logger".to_string(),
                                    collection: Some("modular".to_string()),
                                })),
                                source_name: Some("fuchsia.logger.Log".to_string()),
                                target: Some($namespace::Ref::Child(
                                    $namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: Some("modular".to_string()),
                                    }
                                )),
                                target_name: Some("fuchsia.logger.Log".to_string()),
                                ..$namespace::OfferServiceDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Protocol($namespace::OfferProtocolDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "logger".to_string(),
                                    collection: Some("modular".to_string()),
                                })),
                                source_name: Some("fuchsia.logger.Log".to_string()),
                                target: Some($namespace::Ref::Child(
                                    $namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: Some("modular".to_string()),
                                    }
                                )),
                                target_name: Some("fuchsia.logger.Log".to_string()),
                                dependency_type: Some($namespace::DependencyType::Strong),
                                ..$namespace::OfferProtocolDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Directory($namespace::OfferDirectoryDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "logger".to_string(),
                                    collection: Some("modular".to_string()),
                                })),
                                source_name: Some("assets".to_string()),
                                target: Some($namespace::Ref::Child(
                                    $namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: Some("modular".to_string()),
                                    }
                                )),
                                target_name: Some("assets".to_string()),
                                rights: Some(fio2::Operations::Connect),
                                subdir: None,
                                dependency_type: Some($namespace::DependencyType::Weak),
                                ..$namespace::OfferDirectoryDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Storage($namespace::OfferStorageDecl {
                                source_name: Some("data".to_string()),
                                source: Some($namespace::Ref::Parent($namespace::ParentRef{ })),
                                target: Some($namespace::Ref::Child(
                                    $namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: Some("modular".to_string()),
                                    }
                                )),
                                target_name: Some("data".to_string()),
                                ..$namespace::OfferStorageDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Runner($namespace::OfferRunnerDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "logger".to_string(),
                                    collection: Some("modular".to_string()),
                                })),
                                source_name: Some("elf".to_string()),
                                target: Some($namespace::Ref::Child(
                                    $namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: Some("modular".to_string()),
                                    }
                                )),
                                target_name: Some("elf".to_string()),
                                ..$namespace::OfferRunnerDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Resolver($namespace::OfferResolverDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "logger".to_string(),
                                    collection: Some("modular".to_string()),
                                })),
                                source_name: Some("pkg".to_string()),
                                target: Some($namespace::Ref::Child(
                                    $namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: Some("modular".to_string()),
                                    }
                                )),
                                target_name: Some("pkg".to_string()),
                                ..$namespace::OfferResolverDecl::EMPTY
                            }),
                        ]);
                        decl.capabilities = Some(vec![
                            $namespace::CapabilityDecl::Protocol($namespace::ProtocolDecl {
                                name: Some("fuchsia.logger.Log".to_string()),
                                source_path: Some("/svc/logger".to_string()),
                                ..$namespace::ProtocolDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Directory($namespace::DirectoryDecl {
                                name: Some("assets".to_string()),
                                source_path: Some("/data/assets".to_string()),
                                rights: Some(fio2::Operations::Connect),
                                ..$namespace::DirectoryDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::extraneous_field("OfferServiceDecl", "source.child.collection"),
                        Error::extraneous_field("OfferServiceDecl", "target.child.collection"),
                        Error::extraneous_field("OfferProtocolDecl", "source.child.collection"),
                        Error::extraneous_field("OfferProtocolDecl", "target.child.collection"),
                        Error::extraneous_field("OfferDirectoryDecl", "source.child.collection"),
                        Error::extraneous_field("OfferDirectoryDecl", "target.child.collection"),
                        Error::extraneous_field("OfferStorageDecl", "target.child.collection"),
                        Error::extraneous_field("OfferRunnerDecl", "source.child.collection"),
                        Error::extraneous_field("OfferRunnerDecl", "target.child.collection"),
                        Error::extraneous_field("OfferResolverDecl", "source.child.collection"),
                        Error::extraneous_field("OfferResolverDecl", "target.child.collection"),
                    ])),
                },
                test_validate_offers_invalid_identifiers => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.offers = Some(vec![
                            $namespace::OfferDecl::Service($namespace::OfferServiceDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "^bad".to_string(),
                                    collection: None,
                                })),
                                source_name: Some("foo/".to_string()),
                                target: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "%bad".to_string(),
                                    collection: None,
                                })),
                                target_name: Some("/".to_string()),
                                ..$namespace::OfferServiceDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Protocol($namespace::OfferProtocolDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "^bad".to_string(),
                                    collection: None,
                                })),
                                source_name: Some("foo/".to_string()),
                                target: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "%bad".to_string(),
                                    collection: None,
                                })),
                                target_name: Some("/".to_string()),
                                dependency_type: Some($namespace::DependencyType::Strong),
                                ..$namespace::OfferProtocolDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Directory($namespace::OfferDirectoryDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "^bad".to_string(),
                                    collection: None,
                                })),
                                source_name: Some("foo/".to_string()),
                                target: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "%bad".to_string(),
                                    collection: None,
                                })),
                                target_name: Some("/".to_string()),
                                rights: Some(fio2::Operations::Connect),
                                subdir: Some("/foo".to_string()),
                                dependency_type: Some($namespace::DependencyType::Strong),
                                ..$namespace::OfferDirectoryDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Runner($namespace::OfferRunnerDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "^bad".to_string(),
                                    collection: None,
                                })),
                                source_name: Some("/path".to_string()),
                                target: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "%bad".to_string(),
                                    collection: None,
                                })),
                                target_name: Some("elf!".to_string()),
                                ..$namespace::OfferRunnerDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Resolver($namespace::OfferResolverDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "^bad".to_string(),
                                    collection: None,
                                })),
                                source_name: Some("/path".to_string()),
                                target: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "%bad".to_string(),
                                    collection: None,
                                })),
                                target_name: Some("pkg!".to_string()),
                                ..$namespace::OfferResolverDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Event($namespace::OfferEventDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                source_name: Some("/path".to_string()),
                                target: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "%bad".to_string(),
                                    collection: None,
                                })),
                                target_name: Some("/path".to_string()),
                                filter: Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY }),
                                mode: Some($namespace::EventMode::Sync),
                                ..$namespace::OfferEventDecl::EMPTY
                            })
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_field("OfferServiceDecl", "source.child.name"),
                        Error::invalid_field("OfferServiceDecl", "source_name"),
                        Error::invalid_field("OfferServiceDecl", "target.child.name"),
                        Error::invalid_field("OfferServiceDecl", "target_name"),
                        Error::invalid_field("OfferProtocolDecl", "source.child.name"),
                        Error::invalid_field("OfferProtocolDecl", "source_name"),
                        Error::invalid_field("OfferProtocolDecl", "target.child.name"),
                        Error::invalid_field("OfferProtocolDecl", "target_name"),
                        Error::invalid_field("OfferDirectoryDecl", "source.child.name"),
                        Error::invalid_field("OfferDirectoryDecl", "source_name"),
                        Error::invalid_field("OfferDirectoryDecl", "target.child.name"),
                        Error::invalid_field("OfferDirectoryDecl", "target_name"),
                        Error::invalid_field("OfferDirectoryDecl", "subdir"),
                        Error::invalid_field("OfferRunnerDecl", "source.child.name"),
                        Error::invalid_field("OfferRunnerDecl", "source_name"),
                        Error::invalid_field("OfferRunnerDecl", "target.child.name"),
                        Error::invalid_field("OfferRunnerDecl", "target_name"),
                        Error::invalid_field("OfferResolverDecl", "source.child.name"),
                        Error::invalid_field("OfferResolverDecl", "source_name"),
                        Error::invalid_field("OfferResolverDecl", "target.child.name"),
                        Error::invalid_field("OfferResolverDecl", "target_name"),
                        Error::invalid_field("OfferEventDecl", "source_name"),
                        Error::invalid_field("OfferEventDecl", "target.child.name"),
                        Error::invalid_field("OfferEventDecl", "target_name"),
                    ])),
                },
                test_validate_offers_target_equals_source => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.offers = Some(vec![
                            $namespace::OfferDecl::Service($namespace::OfferServiceDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "logger".to_string(),
                                    collection: None,
                                })),
                                source_name: Some("logger".to_string()),
                                target: Some($namespace::Ref::Child(
                                $namespace::ChildRef {
                                    name: "logger".to_string(),
                                    collection: None,
                                }
                                )),
                                target_name: Some("logger".to_string()),
                                ..$namespace::OfferServiceDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Protocol($namespace::OfferProtocolDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "logger".to_string(),
                                    collection: None,
                                })),
                                source_name: Some("legacy_logger".to_string()),
                                target: Some($namespace::Ref::Child(
                                $namespace::ChildRef {
                                    name: "logger".to_string(),
                                    collection: None,
                                }
                                )),
                                target_name: Some("legacy_logger".to_string()),
                                dependency_type: Some($namespace::DependencyType::Weak),
                                ..$namespace::OfferProtocolDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Directory($namespace::OfferDirectoryDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "logger".to_string(),
                                    collection: None,
                                })),
                                source_name: Some("assets".to_string()),
                                target: Some($namespace::Ref::Child(
                                $namespace::ChildRef {
                                    name: "logger".to_string(),
                                    collection: None,
                                }
                                )),
                                target_name: Some("assets".to_string()),
                                rights: Some(fio2::Operations::Connect),
                                subdir: None,
                                dependency_type: Some($namespace::DependencyType::Strong),
                                ..$namespace::OfferDirectoryDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Runner($namespace::OfferRunnerDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "logger".to_string(),
                                    collection: None,
                                })),
                                source_name: Some("web".to_string()),
                                target: Some($namespace::Ref::Child(
                                $namespace::ChildRef {
                                    name: "logger".to_string(),
                                    collection: None,
                                }
                                )),
                                target_name: Some("web".to_string()),
                                ..$namespace::OfferRunnerDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Resolver($namespace::OfferResolverDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "logger".to_string(),
                                    collection: None,
                                })),
                                source_name: Some("pkg".to_string()),
                                target: Some($namespace::Ref::Child(
                                $namespace::ChildRef {
                                    name: "logger".to_string(),
                                    collection: None,
                                }
                                )),
                                target_name: Some("pkg".to_string()),
                                ..$namespace::OfferResolverDecl::EMPTY
                            }),
                        ]);
                        decl.children = Some(vec![$namespace::ChildDecl{
                            name: Some("logger".to_string()),
                            url: Some("fuchsia-pkg://fuchsia.com/logger#meta/logger.cm".to_string()),
                            startup: Some($namespace::StartupMode::Lazy),
                            on_terminate: None,
                            environment: None,
                            ..$namespace::ChildDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::offer_target_equals_source("OfferServiceDecl", "logger"),
                        Error::offer_target_equals_source("OfferProtocolDecl", "logger"),
                        Error::offer_target_equals_source("OfferDirectoryDecl", "logger"),
                        Error::offer_target_equals_source("OfferRunnerDecl", "logger"),
                        Error::offer_target_equals_source("OfferResolverDecl", "logger"),
                    ])),
                },
                test_validate_offers_storage_target_equals_source => {
                    input = $namespace::ComponentDecl {
                        offers: Some(vec![
                            $namespace::OfferDecl::Storage($namespace::OfferStorageDecl {
                                source_name: Some("data".to_string()),
                                source: Some($namespace::Ref::Self_($namespace::SelfRef { })),
                                target: Some($namespace::Ref::Child(
                                    $namespace::ChildRef {
                                        name: "logger".to_string(),
                                        collection: None,
                                    }
                                )),
                                target_name: Some("data".to_string()),
                                ..$namespace::OfferStorageDecl::EMPTY
                            })
                        ]),
                        capabilities: Some(vec![
                            $namespace::CapabilityDecl::Storage($namespace::StorageDecl {
                                name: Some("data".to_string()),
                                backing_dir: Some("minfs".to_string()),
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "logger".to_string(),
                                    collection: None,
                                })),
                                subdir: None,
                                storage_id: Some($namespace::StorageId::StaticInstanceIdOrMoniker),
                                ..$namespace::StorageDecl::EMPTY
                            }),
                        ]),
                        children: Some(vec![
                            $namespace::ChildDecl {
                                name: Some("logger".to_string()),
                                url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                                startup: Some($namespace::StartupMode::Lazy),
                                on_terminate: None,
                                environment: None,
                                ..$namespace::ChildDecl::EMPTY
                            },
                        ]),
                        ..new_component_decl()
                    },
                    result = Err(ErrorList::new(vec![
                        Error::dependency_cycle("{{child logger -> child logger}}".to_string()),
                    ])),
                },
                test_validate_offers_invalid_child => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.offers = Some(vec![
                            $namespace::OfferDecl::Service($namespace::OfferServiceDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "logger".to_string(),
                                    collection: None,
                                })),
                                source_name: Some("fuchsia.logger.Log".to_string()),
                                target: Some($namespace::Ref::Child(
                                $namespace::ChildRef {
                                    name: "netstack".to_string(),
                                    collection: None,
                                }
                                )),
                                target_name: Some("fuchsia.logger.Log".to_string()),
                                ..$namespace::OfferServiceDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Protocol($namespace::OfferProtocolDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "logger".to_string(),
                                    collection: None,
                                })),
                                source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                target: Some($namespace::Ref::Child(
                                $namespace::ChildRef {
                                    name: "netstack".to_string(),
                                    collection: None,
                                }
                                )),
                                target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                dependency_type: Some($namespace::DependencyType::Strong),
                                ..$namespace::OfferProtocolDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Directory($namespace::OfferDirectoryDecl {
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "logger".to_string(),
                                    collection: None,
                                })),
                                source_name: Some("assets".to_string()),
                                target: Some($namespace::Ref::Collection(
                                $namespace::CollectionRef { name: "modular".to_string() }
                                )),
                                target_name: Some("assets".to_string()),
                                rights: Some(fio2::Operations::Connect),
                                subdir: None,
                                dependency_type: Some($namespace::DependencyType::Weak),
                                ..$namespace::OfferDirectoryDecl::EMPTY
                            }),
                        ]);
                        decl.capabilities = Some(vec![
                            $namespace::CapabilityDecl::Storage($namespace::StorageDecl {
                                name: Some("memfs".to_string()),
                                backing_dir: Some("memfs".to_string()),
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "logger".to_string(),
                                    collection: None,
                                })),
                                subdir: None,
                                storage_id: Some($namespace::StorageId::StaticInstanceIdOrMoniker),
                                ..$namespace::StorageDecl::EMPTY
                            }),
                        ]);
                        decl.children = Some(vec![
                            $namespace::ChildDecl {
                                name: Some("netstack".to_string()),
                                url: Some("fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm".to_string()),
                                startup: Some($namespace::StartupMode::Lazy),
                                on_terminate: None,
                                environment: None,
                                ..$namespace::ChildDecl::EMPTY
                            },
                        ]);
                        decl.collections = Some(vec![
                            $namespace::CollectionDecl {
                                name: Some("modular".to_string()),
                                durability: Some($namespace::Durability::Persistent),
                                allowed_offers: Some($namespace::AllowedOffers::StaticAndDynamic),
                                environment: None,
                                ..$namespace::CollectionDecl::EMPTY
                            },
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_child("StorageDecl", "source", "logger"),
                        Error::invalid_child("OfferServiceDecl", "source", "logger"),
                        Error::invalid_child("OfferProtocolDecl", "source", "logger"),
                        Error::invalid_child("OfferDirectoryDecl", "source", "logger"),
                    ])),
                },
                test_validate_offers_invalid_source_capability => {
                    input = {
                        $namespace::ComponentDecl {
                            offers: Some(vec![
                                $namespace::OfferDecl::Protocol($namespace::OfferProtocolDecl {
                                    source: Some($namespace::Ref::Capability($namespace::CapabilityRef {
                                        name: "this-storage-doesnt-exist".to_string(),
                                    })),
                                    source_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                                    target: Some($namespace::Ref::Child(
                                    $namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    }
                                    )),
                                    target_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                                    dependency_type: Some($namespace::DependencyType::Strong),
                                    ..$namespace::OfferProtocolDecl::EMPTY
                                }),
                            ]),
                            ..new_component_decl()
                        }
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_capability("OfferProtocolDecl", "source", "this-storage-doesnt-exist"),
                        Error::invalid_child("OfferProtocolDecl", "target", "netstack"),
                    ])),
                },
                test_validate_offers_target => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.offers = Some(vec![
                            $namespace::OfferDecl::Service($namespace::OfferServiceDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                source_name: Some("logger".to_string()),
                                target: Some($namespace::Ref::Child(
                                $namespace::ChildRef {
                                    name: "netstack".to_string(),
                                    collection: None,
                                }
                                )),
                                target_name: Some("fuchsia.logger.Log".to_string()),
                                ..$namespace::OfferServiceDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Service($namespace::OfferServiceDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                source_name: Some("logger2".to_string()),
                                target: Some($namespace::Ref::Child(
                                $namespace::ChildRef {
                                    name: "netstack".to_string(),
                                    collection: None,
                                }
                                )),
                                target_name: Some("fuchsia.logger.Log".to_string()),
                                ..$namespace::OfferServiceDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Protocol($namespace::OfferProtocolDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                target: Some($namespace::Ref::Child(
                                $namespace::ChildRef {
                                    name: "netstack".to_string(),
                                    collection: None,
                                }
                                )),
                                target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                dependency_type: Some($namespace::DependencyType::Strong),
                                ..$namespace::OfferProtocolDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Protocol($namespace::OfferProtocolDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                target: Some($namespace::Ref::Child(
                                $namespace::ChildRef {
                                    name: "netstack".to_string(),
                                    collection: None,
                                }
                                )),
                                target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                dependency_type: Some($namespace::DependencyType::Strong),
                                ..$namespace::OfferProtocolDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Directory($namespace::OfferDirectoryDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                source_name: Some("assets".to_string()),
                                target: Some($namespace::Ref::Collection(
                                $namespace::CollectionRef { name: "modular".to_string() }
                                )),
                                target_name: Some("assets".to_string()),
                                rights: Some(fio2::Operations::Connect),
                                subdir: None,
                                dependency_type: Some($namespace::DependencyType::Strong),
                                ..$namespace::OfferDirectoryDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Directory($namespace::OfferDirectoryDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                source_name: Some("assets".to_string()),
                                target: Some($namespace::Ref::Collection(
                                $namespace::CollectionRef { name: "modular".to_string() }
                                )),
                                target_name: Some("assets".to_string()),
                                rights: Some(fio2::Operations::Connect),
                                subdir: None,
                                dependency_type: Some($namespace::DependencyType::Weak),
                                ..$namespace::OfferDirectoryDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Runner($namespace::OfferRunnerDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                source_name: Some("elf".to_string()),
                                target: Some($namespace::Ref::Collection(
                                $namespace::CollectionRef { name: "modular".to_string() }
                                )),
                                target_name: Some("duplicated".to_string()),
                                ..$namespace::OfferRunnerDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Runner($namespace::OfferRunnerDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                source_name: Some("elf".to_string()),
                                target: Some($namespace::Ref::Collection(
                                $namespace::CollectionRef { name: "modular".to_string() }
                                )),
                                target_name: Some("duplicated".to_string()),
                                ..$namespace::OfferRunnerDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Resolver($namespace::OfferResolverDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                source_name: Some("pkg".to_string()),
                                target: Some($namespace::Ref::Collection(
                                $namespace::CollectionRef { name: "modular".to_string() }
                                )),
                                target_name: Some("duplicated".to_string()),
                                ..$namespace::OfferResolverDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Event($namespace::OfferEventDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                source_name: Some("stopped".to_string()),
                                target: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "netstack".to_string(),
                                    collection: None,
                                })),
                                target_name: Some("started".to_string()),
                                filter: None,
                                mode: Some($namespace::EventMode::Async),
                                ..$namespace::OfferEventDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Event($namespace::OfferEventDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                source_name: Some("started_on_x".to_string()),
                                target: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "netstack".to_string(),
                                    collection: None,
                                })),
                                target_name: Some("started".to_string()),
                                filter: None,
                                mode: Some($namespace::EventMode::Async),
                                ..$namespace::OfferEventDecl::EMPTY
                            }),
                        ]);
                        decl.children = Some(vec![
                            $namespace::ChildDecl{
                                name: Some("netstack".to_string()),
                                url: Some("fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm".to_string()),
                                startup: Some($namespace::StartupMode::Eager),
                                on_terminate: None,
                                environment: None,
                                ..$namespace::ChildDecl::EMPTY
                            },
                        ]);
                        decl.collections = Some(vec![
                            $namespace::CollectionDecl{
                                name: Some("modular".to_string()),
                                durability: Some($namespace::Durability::Persistent),
                                allowed_offers: Some($namespace::AllowedOffers::StaticOnly),
                                environment: None,
                                ..$namespace::CollectionDecl::EMPTY
                            },
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        // Duplicate services are allowed.
                        Error::duplicate_field("OfferProtocolDecl", "target_name", "fuchsia.logger.LegacyLog"),
                        Error::duplicate_field("OfferDirectoryDecl", "target_name", "assets"),
                        Error::duplicate_field("OfferRunnerDecl", "target_name", "duplicated"),
                        Error::duplicate_field("OfferResolverDecl", "target_name", "duplicated"),
                        Error::duplicate_field("OfferEventDecl", "target_name", "started"),
                    ])),
                },
                test_validate_offers_target_invalid => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.offers = Some(vec![
                            $namespace::OfferDecl::Service($namespace::OfferServiceDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                source_name: Some("logger".to_string()),
                                target: Some($namespace::Ref::Child(
                                $namespace::ChildRef {
                                    name: "netstack".to_string(),
                                    collection: None,
                                }
                                )),
                                target_name: Some("fuchsia.logger.Log".to_string()),
                                ..$namespace::OfferServiceDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Service($namespace::OfferServiceDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                source_name: Some("logger".to_string()),
                                target: Some($namespace::Ref::Collection(
                                $namespace::CollectionRef { name: "modular".to_string(), }
                                )),
                                target_name: Some("fuchsia.logger.Log".to_string()),
                                ..$namespace::OfferServiceDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Protocol($namespace::OfferProtocolDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                source_name: Some("legacy_logger".to_string()),
                                target: Some($namespace::Ref::Child(
                                $namespace::ChildRef {
                                    name: "netstack".to_string(),
                                    collection: None,
                                }
                                )),
                                target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                dependency_type: Some($namespace::DependencyType::Weak),
                                ..$namespace::OfferProtocolDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Protocol($namespace::OfferProtocolDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                source_name: Some("legacy_logger".to_string()),
                                target: Some($namespace::Ref::Collection(
                                $namespace::CollectionRef { name: "modular".to_string(), }
                                )),
                                target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                dependency_type: Some($namespace::DependencyType::Strong),
                                ..$namespace::OfferProtocolDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Directory($namespace::OfferDirectoryDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                source_name: Some("assets".to_string()),
                                target: Some($namespace::Ref::Child(
                                $namespace::ChildRef {
                                    name: "netstack".to_string(),
                                    collection: None,
                                }
                                )),
                                target_name: Some("data".to_string()),
                                rights: Some(fio2::Operations::Connect),
                                subdir: None,
                                dependency_type: Some($namespace::DependencyType::Strong),
                                ..$namespace::OfferDirectoryDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Directory($namespace::OfferDirectoryDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                source_name: Some("assets".to_string()),
                                target: Some($namespace::Ref::Collection(
                                $namespace::CollectionRef { name: "modular".to_string(), }
                                )),
                                target_name: Some("data".to_string()),
                                rights: Some(fio2::Operations::Connect),
                                subdir: None,
                                dependency_type: Some($namespace::DependencyType::Weak),
                                ..$namespace::OfferDirectoryDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Storage($namespace::OfferStorageDecl {
                                source_name: Some("data".to_string()),
                                source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                target: Some($namespace::Ref::Child(
                                    $namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    }
                                )),
                                target_name: Some("data".to_string()),
                                ..$namespace::OfferStorageDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Storage($namespace::OfferStorageDecl {
                                source_name: Some("data".to_string()),
                                source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                target: Some($namespace::Ref::Collection(
                                    $namespace::CollectionRef { name: "modular".to_string(), }
                                )),
                                target_name: Some("data".to_string()),
                                ..$namespace::OfferStorageDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Runner($namespace::OfferRunnerDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                source_name: Some("elf".to_string()),
                                target: Some($namespace::Ref::Child(
                                    $namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    }
                                )),
                                target_name: Some("elf".to_string()),
                                ..$namespace::OfferRunnerDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Runner($namespace::OfferRunnerDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                source_name: Some("elf".to_string()),
                                target: Some($namespace::Ref::Collection(
                                $namespace::CollectionRef { name: "modular".to_string(), }
                                )),
                                target_name: Some("elf".to_string()),
                                ..$namespace::OfferRunnerDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Resolver($namespace::OfferResolverDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                source_name: Some("pkg".to_string()),
                                target: Some($namespace::Ref::Child(
                                    $namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    }
                                )),
                                target_name: Some("pkg".to_string()),
                                ..$namespace::OfferResolverDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Resolver($namespace::OfferResolverDecl {
                                source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                source_name: Some("pkg".to_string()),
                                target: Some($namespace::Ref::Collection(
                                $namespace::CollectionRef { name: "modular".to_string(), }
                                )),
                                target_name: Some("pkg".to_string()),
                                ..$namespace::OfferResolverDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Event($namespace::OfferEventDecl {
                                source_name: Some("started".to_string()),
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                target_name: Some("started".to_string()),
                                target: Some($namespace::Ref::Child(
                                    $namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    }
                                )),
                                filter: None,
                                mode: Some($namespace::EventMode::Async),
                                ..$namespace::OfferEventDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Event($namespace::OfferEventDecl {
                                source_name: Some("started".to_string()),
                                source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                target_name: Some("started".to_string()),
                                target: Some($namespace::Ref::Collection(
                                $namespace::CollectionRef { name: "modular".to_string(), }
                                )),
                                filter: None,
                                mode: Some($namespace::EventMode::Async),
                                ..$namespace::OfferEventDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_child("OfferServiceDecl", "target", "netstack"),
                        Error::invalid_collection("OfferServiceDecl", "target", "modular"),
                        Error::invalid_child("OfferProtocolDecl", "target", "netstack"),
                        Error::invalid_collection("OfferProtocolDecl", "target", "modular"),
                        Error::invalid_child("OfferDirectoryDecl", "target", "netstack"),
                        Error::invalid_collection("OfferDirectoryDecl", "target", "modular"),
                        Error::invalid_child("OfferStorageDecl", "target", "netstack"),
                        Error::invalid_collection("OfferStorageDecl", "target", "modular"),
                        Error::invalid_child("OfferRunnerDecl", "target", "netstack"),
                        Error::invalid_collection("OfferRunnerDecl", "target", "modular"),
                        Error::invalid_child("OfferResolverDecl", "target", "netstack"),
                        Error::invalid_collection("OfferResolverDecl", "target", "modular"),
                        Error::invalid_child("OfferEventDecl", "target", "netstack"),
                        Error::invalid_collection("OfferEventDecl", "target", "modular"),
                    ])),
                },
                test_validate_offers_invalid_source_collection => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.collections = Some(vec![
                            $namespace::CollectionDecl {
                                name: Some("col".to_string()),
                                durability: Some($namespace::Durability::Transient),
                                allowed_offers: Some($namespace::AllowedOffers::StaticOnly),
                                ..$namespace::CollectionDecl::EMPTY
                            }
                        ]);
                        decl.children = Some(vec![
                            $namespace::ChildDecl {
                                name: Some("child".to_string()),
                                url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                                startup: Some($namespace::StartupMode::Lazy),
                                on_terminate: None,
                                ..$namespace::ChildDecl::EMPTY
                            }
                        ]);
                        decl.offers = Some(vec![
                            $namespace::OfferDecl::Protocol($namespace::OfferProtocolDecl {
                                source: Some($namespace::Ref::Collection($namespace::CollectionRef { name: "col".to_string() })),
                                source_name: Some("a".to_string()),
                                target: Some($namespace::Ref::Child($namespace::ChildRef { name: "child".to_string(), collection: None })),
                                target_name: Some("a".to_string()),
                                dependency_type: Some($namespace::DependencyType::Strong),
                                ..$namespace::OfferProtocolDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Directory($namespace::OfferDirectoryDecl {
                                source: Some($namespace::Ref::Collection($namespace::CollectionRef { name: "col".to_string() })),
                                source_name: Some("b".to_string()),
                                target: Some($namespace::Ref::Child($namespace::ChildRef { name: "child".to_string(), collection: None })),
                                target_name: Some("b".to_string()),
                                rights: Some(fio2::Operations::Connect),
                                subdir: None,
                                dependency_type: Some($namespace::DependencyType::Strong),
                                ..$namespace::OfferDirectoryDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Storage($namespace::OfferStorageDecl {
                                source: Some($namespace::Ref::Collection($namespace::CollectionRef { name: "col".to_string() })),
                                source_name: Some("c".to_string()),
                                target: Some($namespace::Ref::Child($namespace::ChildRef { name: "child".to_string(), collection: None })),
                                target_name: Some("c".to_string()),
                                ..$namespace::OfferStorageDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Runner($namespace::OfferRunnerDecl {
                                source: Some($namespace::Ref::Collection($namespace::CollectionRef { name: "col".to_string() })),
                                source_name: Some("d".to_string()),
                                target: Some($namespace::Ref::Child($namespace::ChildRef { name: "child".to_string(), collection: None })),
                                target_name: Some("d".to_string()),
                                ..$namespace::OfferRunnerDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Resolver($namespace::OfferResolverDecl {
                                source: Some($namespace::Ref::Collection($namespace::CollectionRef { name: "col".to_string() })),
                                source_name: Some("e".to_string()),
                                target: Some($namespace::Ref::Child($namespace::ChildRef { name: "child".to_string(), collection: None })),
                                target_name: Some("e".to_string()),
                                ..$namespace::OfferResolverDecl::EMPTY
                            }),
                            $namespace::OfferDecl::Event($namespace::OfferEventDecl {
                                source: Some($namespace::Ref::Collection($namespace::CollectionRef { name: "col".to_string() })),
                                source_name: Some("f".to_string()),
                                target: Some($namespace::Ref::Child($namespace::ChildRef { name: "child".to_string(), collection: None })),
                                target_name: Some("f".to_string()),
                                filter: None,
                                mode: Some($namespace::EventMode::Async),
                                ..$namespace::OfferEventDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_field("OfferProtocolDecl", "source"),
                        Error::invalid_field("OfferDirectoryDecl", "source"),
                        Error::invalid_field("OfferStorageDecl", "source"),
                        Error::invalid_field("OfferRunnerDecl", "source"),
                        Error::invalid_field("OfferResolverDecl", "source"),
                        Error::invalid_field("OfferEventDecl", "source"),
                    ])),
                },
                test_validate_offers_source_collection => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.collections = Some(vec![
                            $namespace::CollectionDecl {
                                name: Some("col".to_string()),
                                durability: Some($namespace::Durability::Transient),
                                allowed_offers: Some($namespace::AllowedOffers::StaticOnly),
                                ..$namespace::CollectionDecl::EMPTY
                            }
                        ]);
                        decl.children = Some(vec![
                            $namespace::ChildDecl {
                                name: Some("child".to_string()),
                                url: Some("fuchsia-pkg://fuchsia.com/foo".to_string()),
                                startup: Some($namespace::StartupMode::Lazy),
                                on_terminate: None,
                                ..$namespace::ChildDecl::EMPTY
                            }
                        ]);
                        decl.offers = Some(vec![
                            $namespace::OfferDecl::Service($namespace::OfferServiceDecl {
                                source: Some($namespace::Ref::Collection($namespace::CollectionRef { name: "col".to_string() })),
                                source_name: Some("a".to_string()),
                                target: Some($namespace::Ref::Child($namespace::ChildRef { name: "child".to_string(), collection: None })),
                                target_name: Some("a".to_string()),
                                ..$namespace::OfferServiceDecl::EMPTY
                            })
                        ]);
                        decl
                    },
                    result = Ok(()),
                },
                test_validate_offers_event_from_realm => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.offers = Some(
                            vec![
                                $namespace::Ref::Self_($namespace::SelfRef {}),
                                $namespace::Ref::Child($namespace::ChildRef {name: "netstack".to_string(), collection: None }),
                                $namespace::Ref::Collection($namespace::CollectionRef {name: "modular".to_string() }),
                            ]
                            .into_iter()
                            .enumerate()
                            .map(|(i, source)| {
                                $namespace::OfferDecl::Event($namespace::OfferEventDecl {
                                    source: Some(source),
                                    source_name: Some("started".to_string()),
                                    target: Some($namespace::Ref::Child($namespace::ChildRef {
                                        name: "netstack".to_string(),
                                        collection: None,
                                    })),
                                    target_name: Some(format!("started_{}", i)),

                                    filter: Some(fdata::Dictionary { entries: None, ..fdata::Dictionary::EMPTY }),
                                    mode: Some($namespace::EventMode::Sync),
                                    ..$namespace::OfferEventDecl::EMPTY
                                })
                            })
                            .collect());
                        decl.children = Some(vec![
                            $namespace::ChildDecl{
                                name: Some("netstack".to_string()),
                                url: Some("fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm".to_string()),
                                startup: Some($namespace::StartupMode::Eager),
                                on_terminate: None,
                                environment: None,
                                ..$namespace::ChildDecl::EMPTY
                            },
                        ]);
                        decl.collections = Some(vec![
                            $namespace::CollectionDecl {
                                name: Some("modular".to_string()),
                                durability: Some($namespace::Durability::Persistent),
                                allowed_offers: Some($namespace::AllowedOffers::StaticOnly),
                                environment: None,
                                ..$namespace::CollectionDecl::EMPTY
                            },
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_field("OfferEventDecl", "source"),
                        Error::invalid_field("OfferEventDecl", "source"),
                        Error::invalid_field("OfferEventDecl", "source"),
                    ])),
                },
                test_validate_offers_long_dependency_cycle => {
                    input = {
                        let mut decl = new_component_decl();
                        let dependencies = vec![
                            ("d", "b"),
                            ("a", "b"),
                            ("b", "c"),
                            ("b", "d"),
                            ("c", "a"),
                        ];
                        let offers = dependencies.into_iter().map(|(from,to)|
                            $namespace::OfferDecl::Protocol($namespace::OfferProtocolDecl {
                                source: Some($namespace::Ref::Child(
                                $namespace::ChildRef { name: from.to_string(), collection: None },
                                )),
                                source_name: Some(format!("thing_{}", from)),
                                target: Some($namespace::Ref::Child(
                                $namespace::ChildRef { name: to.to_string(), collection: None },
                                )),
                                target_name: Some(format!("thing_{}", from)),
                                dependency_type: Some($namespace::DependencyType::Strong),
                                ..$namespace::OfferProtocolDecl::EMPTY
                            })).collect();
                        let children = ["a", "b", "c", "d"].iter().map(|name| {
                            $namespace::ChildDecl {
                                name: Some(name.to_string()),
                                url: Some(format!("fuchsia-pkg://fuchsia.com/pkg#meta/{}.cm", name)),
                                startup: Some($namespace::StartupMode::Lazy),
                                on_terminate: None,
                                environment: None,
                                ..$namespace::ChildDecl::EMPTY
                            }
                        }).collect();
                        decl.offers = Some(offers);
                        decl.children = Some(children);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::dependency_cycle(directed_graph::Error::CyclesDetected([vec!["child a", "child b", "child c", "child a"], vec!["child b", "child d", "child b"]].iter().cloned().collect()).format_cycle()),
                    ])),
                },

                // environments
                test_validate_environment_empty => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.environments = Some(vec![$namespace::EnvironmentDecl {
                            name: None,
                            extends: None,
                            runners: None,
                            resolvers: None,
                            stop_timeout_ms: None,
                            debug_capabilities: None,
                            ..$namespace::EnvironmentDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::missing_field("EnvironmentDecl", "name"),
                        Error::missing_field("EnvironmentDecl", "extends"),
                    ])),
                },

                test_validate_environment_no_stop_timeout => {
                    input = {  let mut decl = new_component_decl();
                        decl.environments = Some(vec![$namespace::EnvironmentDecl {
                            name: Some("env".to_string()),
                            extends: Some($namespace::EnvironmentExtends::None),
                            runners: None,
                            resolvers: None,
                            stop_timeout_ms: None,
                            ..$namespace::EnvironmentDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![Error::missing_field("EnvironmentDecl", "stop_timeout_ms")])),
                },

                test_validate_environment_extends_stop_timeout => {
                    input = {  let mut decl = new_component_decl();
                        decl.environments = Some(vec![$namespace::EnvironmentDecl {
                            name: Some("env".to_string()),
                            extends: Some($namespace::EnvironmentExtends::Realm),
                            runners: None,
                            resolvers: None,
                            stop_timeout_ms: None,
                            ..$namespace::EnvironmentDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Ok(()),
                },
                test_validate_environment_long_identifiers => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.environments = Some(vec![$namespace::EnvironmentDecl {
                            name: Some("a".repeat(101)),
                            extends: Some($namespace::EnvironmentExtends::None),
                            runners: Some(vec![
                                $namespace::RunnerRegistration {
                                    source_name: Some("a".repeat(101)),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                    target_name: Some("a".repeat(101)),
                                    ..$namespace::RunnerRegistration::EMPTY
                                },
                            ]),
                            resolvers: Some(vec![
                                $namespace::ResolverRegistration {
                                    resolver: Some("a".repeat(101)),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                    scheme: Some("a".repeat(101)),
                                    ..$namespace::ResolverRegistration::EMPTY
                                },
                            ]),
                            stop_timeout_ms: Some(1234),
                            ..$namespace::EnvironmentDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::field_too_long("EnvironmentDecl", "name"),
                        Error::field_too_long("RunnerRegistration", "source_name"),
                        Error::field_too_long("RunnerRegistration", "target_name"),
                        Error::field_too_long("ResolverRegistration", "resolver"),
                        Error::field_too_long("ResolverRegistration", "scheme"),
                    ])),
                },
                test_validate_environment_empty_runner_resolver_fields => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.environments = Some(vec![$namespace::EnvironmentDecl {
                            name: Some("a".to_string()),
                            extends: Some($namespace::EnvironmentExtends::None),
                            runners: Some(vec![
                                $namespace::RunnerRegistration {
                                    source_name: None,
                                    source: None,
                                    target_name: None,
                                    ..$namespace::RunnerRegistration::EMPTY
                                },
                            ]),
                            resolvers: Some(vec![
                                $namespace::ResolverRegistration {
                                    resolver: None,
                                    source: None,
                                    scheme: None,
                                    ..$namespace::ResolverRegistration::EMPTY
                                },
                            ]),
                            stop_timeout_ms: Some(1234),
                            ..$namespace::EnvironmentDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::missing_field("RunnerRegistration", "source_name"),
                        Error::missing_field("RunnerRegistration", "source"),
                        Error::missing_field("RunnerRegistration", "target_name"),
                        Error::missing_field("ResolverRegistration", "resolver"),
                        Error::missing_field("ResolverRegistration", "source"),
                        Error::missing_field("ResolverRegistration", "scheme"),
                    ])),
                },
                test_validate_environment_invalid_fields => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.environments = Some(vec![$namespace::EnvironmentDecl {
                            name: Some("a".to_string()),
                            extends: Some($namespace::EnvironmentExtends::None),
                            runners: Some(vec![
                                $namespace::RunnerRegistration {
                                    source_name: Some("^a".to_string()),
                                    source: Some($namespace::Ref::Framework($namespace::FrameworkRef{})),
                                    target_name: Some("%a".to_string()),
                                    ..$namespace::RunnerRegistration::EMPTY
                                },
                            ]),
                            resolvers: Some(vec![
                                $namespace::ResolverRegistration {
                                    resolver: Some("^a".to_string()),
                                    source: Some($namespace::Ref::Framework($namespace::FrameworkRef{})),
                                    scheme: Some("9scheme".to_string()),
                                    ..$namespace::ResolverRegistration::EMPTY
                                },
                            ]),
                            stop_timeout_ms: Some(1234),
                            ..$namespace::EnvironmentDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_field("RunnerRegistration", "source_name"),
                        Error::invalid_field("RunnerRegistration", "source"),
                        Error::invalid_field("RunnerRegistration", "target_name"),
                        Error::invalid_field("ResolverRegistration", "resolver"),
                        Error::invalid_field("ResolverRegistration", "source"),
                        Error::invalid_field("ResolverRegistration", "scheme"),
                    ])),
                },
                test_validate_environment_missing_runner => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.environments = Some(vec![$namespace::EnvironmentDecl {
                            name: Some("a".to_string()),
                            extends: Some($namespace::EnvironmentExtends::None),
                            runners: Some(vec![
                                $namespace::RunnerRegistration {
                                    source_name: Some("dart".to_string()),
                                    source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                    target_name: Some("dart".to_string()),
                                    ..$namespace::RunnerRegistration::EMPTY
                                },
                            ]),
                            resolvers: None,
                            stop_timeout_ms: Some(1234),
                            ..$namespace::EnvironmentDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_runner("RunnerRegistration", "source_name", "dart"),
                    ])),
                },
                test_validate_environment_duplicate_registrations => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.environments = Some(vec![$namespace::EnvironmentDecl {
                            name: Some("a".to_string()),
                            extends: Some($namespace::EnvironmentExtends::None),
                            runners: Some(vec![
                                $namespace::RunnerRegistration {
                                    source_name: Some("dart".to_string()),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                    target_name: Some("dart".to_string()),
                                    ..$namespace::RunnerRegistration::EMPTY
                                },
                                $namespace::RunnerRegistration {
                                    source_name: Some("other-dart".to_string()),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                    target_name: Some("dart".to_string()),
                                    ..$namespace::RunnerRegistration::EMPTY
                                },
                            ]),
                            resolvers: Some(vec![
                                $namespace::ResolverRegistration {
                                    resolver: Some("pkg_resolver".to_string()),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                    scheme: Some("fuchsia-pkg".to_string()),
                                    ..$namespace::ResolverRegistration::EMPTY
                                },
                                $namespace::ResolverRegistration {
                                    resolver: Some("base_resolver".to_string()),
                                    source: Some($namespace::Ref::Parent($namespace::ParentRef{})),
                                    scheme: Some("fuchsia-pkg".to_string()),
                                    ..$namespace::ResolverRegistration::EMPTY
                                },
                            ]),
                            stop_timeout_ms: Some(1234),
                            ..$namespace::EnvironmentDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::duplicate_field("RunnerRegistration", "target_name", "dart"),
                        Error::duplicate_field("ResolverRegistration", "scheme", "fuchsia-pkg"),
                    ])),
                },
                test_validate_environment_from_missing_child => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.environments = Some(vec![$namespace::EnvironmentDecl {
                            name: Some("a".to_string()),
                            extends: Some($namespace::EnvironmentExtends::None),
                            runners: Some(vec![
                                $namespace::RunnerRegistration {
                                    source_name: Some("elf".to_string()),
                                    source: Some($namespace::Ref::Child($namespace::ChildRef{
                                        name: "missing".to_string(),
                                        collection: None,
                                    })),
                                    target_name: Some("elf".to_string()),
                                    ..$namespace::RunnerRegistration::EMPTY
                                },
                            ]),
                            resolvers: Some(vec![
                                $namespace::ResolverRegistration {
                                    resolver: Some("pkg_resolver".to_string()),
                                    source: Some($namespace::Ref::Child($namespace::ChildRef{
                                        name: "missing".to_string(),
                                        collection: None,
                                    })),
                                    scheme: Some("fuchsia-pkg".to_string()),
                                    ..$namespace::ResolverRegistration::EMPTY
                                },
                            ]),
                            stop_timeout_ms: Some(1234),
                            ..$namespace::EnvironmentDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_child("RunnerRegistration", "source", "missing"),
                        Error::invalid_child("ResolverRegistration", "source", "missing"),
                    ])),
                },
                test_validate_environment_runner_child_cycle => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.environments = Some(vec![$namespace::EnvironmentDecl {
                            name: Some("env".to_string()),
                            extends: Some($namespace::EnvironmentExtends::None),
                            runners: Some(vec![
                                $namespace::RunnerRegistration {
                                    source_name: Some("elf".to_string()),
                                    source: Some($namespace::Ref::Child($namespace::ChildRef{
                                        name: "child".to_string(),
                                        collection: None,
                                    })),
                                    target_name: Some("elf".to_string()),
                                    ..$namespace::RunnerRegistration::EMPTY
                                },
                            ]),
                            resolvers: None,
                            stop_timeout_ms: Some(1234),
                            ..$namespace::EnvironmentDecl::EMPTY
                        }]);
                        decl.children = Some(vec![$namespace::ChildDecl {
                            name: Some("child".to_string()),
                            startup: Some($namespace::StartupMode::Lazy),
                            on_terminate: None,
                            url: Some("fuchsia-pkg://child".to_string()),
                            environment: Some("env".to_string()),
                            ..$namespace::ChildDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::dependency_cycle(
                            directed_graph::Error::CyclesDetected([vec!["child child", "environment env", "child child"]].iter().cloned().collect()).format_cycle()
                        ),
                    ])),
                },
                test_validate_environment_resolver_child_cycle => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.environments = Some(vec![$namespace::EnvironmentDecl {
                            name: Some("env".to_string()),
                            extends: Some($namespace::EnvironmentExtends::None),
                            runners: None,
                            resolvers: Some(vec![
                                $namespace::ResolverRegistration {
                                    resolver: Some("pkg_resolver".to_string()),
                                    source: Some($namespace::Ref::Child($namespace::ChildRef{
                                        name: "child".to_string(),
                                        collection: None,
                                    })),
                                    scheme: Some("fuchsia-pkg".to_string()),
                                    ..$namespace::ResolverRegistration::EMPTY
                                },
                            ]),
                            stop_timeout_ms: Some(1234),
                            ..$namespace::EnvironmentDecl::EMPTY
                        }]);
                        decl.children = Some(vec![$namespace::ChildDecl {
                            name: Some("child".to_string()),
                            startup: Some($namespace::StartupMode::Lazy),
                            on_terminate: None,
                            url: Some("fuchsia-pkg://child".to_string()),
                            environment: Some("env".to_string()),
                            ..$namespace::ChildDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::dependency_cycle(
                            directed_graph::Error::CyclesDetected([vec!["child child", "environment env", "child child"]].iter().cloned().collect()).format_cycle()
                        ),
                    ])),
                },
                test_validate_environment_resolver_multiple_children_cycle => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.environments = Some(vec![$namespace::EnvironmentDecl {
                            name: Some("env".to_string()),
                            extends: Some($namespace::EnvironmentExtends::None),
                            runners: None,
                            resolvers: Some(vec![
                                $namespace::ResolverRegistration {
                                    resolver: Some("pkg_resolver".to_string()),
                                    source: Some($namespace::Ref::Child($namespace::ChildRef{
                                        name: "a".to_string(),
                                        collection: None,
                                    })),
                                    scheme: Some("fuchsia-pkg".to_string()),
                                    ..$namespace::ResolverRegistration::EMPTY
                                },
                            ]),
                            stop_timeout_ms: Some(1234),
                            ..$namespace::EnvironmentDecl::EMPTY
                        }]);
                        decl.children = Some(vec![
                            $namespace::ChildDecl {
                                name: Some("a".to_string()),
                                startup: Some($namespace::StartupMode::Lazy),
                                on_terminate: None,
                                url: Some("fuchsia-pkg://child-a".to_string()),
                                environment: None,
                                ..$namespace::ChildDecl::EMPTY
                            },
                            $namespace::ChildDecl {
                                name: Some("b".to_string()),
                                startup: Some($namespace::StartupMode::Lazy),
                                on_terminate: None,
                                url: Some("fuchsia-pkg://child-b".to_string()),
                                environment: Some("env".to_string()),
                                ..$namespace::ChildDecl::EMPTY
                            },
                        ]);
                        decl.offers = Some(vec![$namespace::OfferDecl::Service($namespace::OfferServiceDecl {
                            source: Some($namespace::Ref::Child($namespace::ChildRef {
                                name: "b".to_string(),
                                collection: None,
                            })),
                            source_name: Some("thing".to_string()),
                            target: Some($namespace::Ref::Child(
                            $namespace::ChildRef {
                                name: "a".to_string(),
                                collection: None,
                            }
                            )),
                            target_name: Some("thing".to_string()),
                            ..$namespace::OfferServiceDecl::EMPTY
                        })]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::dependency_cycle(
                            directed_graph::Error::CyclesDetected([vec!["child a", "environment env", "child b", "child a"]].iter().cloned().collect()).format_cycle()
                        ),
                    ])),
                },
                test_validate_environment_debug_empty => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.environments = Some(vec![
                            $namespace::EnvironmentDecl {
                                name: Some("a".to_string()),
                                extends: Some($namespace::EnvironmentExtends::None),
                                stop_timeout_ms: Some(2),
                                debug_capabilities:Some(vec![
                                    $namespace::DebugRegistration::Protocol($namespace::DebugProtocolRegistration {
                                        source: None,
                                        source_name: None,
                                        target_name: None,
                                        ..$namespace::DebugProtocolRegistration::EMPTY
                                    }),
                            ]),
                            ..$namespace::EnvironmentDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::missing_field("DebugProtocolRegistration", "source"),
                        Error::missing_field("DebugProtocolRegistration", "source_name"),
                        Error::missing_field("DebugProtocolRegistration", "target_name"),
                    ])),
                },
                test_validate_environment_debug_log_identifier => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.environments = Some(vec![
                            $namespace::EnvironmentDecl {
                                name: Some("a".to_string()),
                                extends: Some($namespace::EnvironmentExtends::None),
                                stop_timeout_ms: Some(2),
                                debug_capabilities:Some(vec![
                                    $namespace::DebugRegistration::Protocol($namespace::DebugProtocolRegistration {
                                        source: Some($namespace::Ref::Child($namespace::ChildRef {
                                            name: "a".repeat(101),
                                            collection: None,
                                        })),
                                        source_name: Some(format!("{}", "a".repeat(101))),
                                        target_name: Some(format!("{}", "b".repeat(101))),
                                        ..$namespace::DebugProtocolRegistration::EMPTY
                                    }),
                                    $namespace::DebugRegistration::Protocol($namespace::DebugProtocolRegistration {
                                        source: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                                        source_name: Some("a".to_string()),
                                        target_name: Some(format!("{}", "b".repeat(101))),
                                        ..$namespace::DebugProtocolRegistration::EMPTY
                                    }),
                            ]),
                            ..$namespace::EnvironmentDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::field_too_long("DebugProtocolRegistration", "source.child.name"),
                        Error::field_too_long("DebugProtocolRegistration", "source_name"),
                        Error::field_too_long("DebugProtocolRegistration", "target_name"),
                        Error::field_too_long("DebugProtocolRegistration", "target_name"),
                    ])),
                },
                test_validate_environment_debug_log_extraneous => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.environments = Some(vec![
                            $namespace::EnvironmentDecl {
                                name: Some("a".to_string()),
                                extends: Some($namespace::EnvironmentExtends::None),
                                stop_timeout_ms: Some(2),
                                debug_capabilities:Some(vec![
                                    $namespace::DebugRegistration::Protocol($namespace::DebugProtocolRegistration {
                                        source: Some($namespace::Ref::Child($namespace::ChildRef {
                                            name: "logger".to_string(),
                                            collection: Some("modular".to_string()),
                                        })),
                                        source_name: Some("fuchsia.logger.Log".to_string()),
                                        target_name: Some("fuchsia.logger.Log".to_string()),
                                        ..$namespace::DebugProtocolRegistration::EMPTY
                                    }),
                            ]),
                            ..$namespace::EnvironmentDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::extraneous_field("DebugProtocolRegistration", "source.child.collection"),
                    ])),
                },
                test_validate_environment_debug_log_invalid_identifiers => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.environments = Some(vec![
                            $namespace::EnvironmentDecl {
                                name: Some("a".to_string()),
                                extends: Some($namespace::EnvironmentExtends::None),
                                stop_timeout_ms: Some(2),
                                debug_capabilities:Some(vec![
                                    $namespace::DebugRegistration::Protocol($namespace::DebugProtocolRegistration {
                                        source: Some($namespace::Ref::Child($namespace::ChildRef {
                                            name: "^bad".to_string(),
                                            collection: None,
                                        })),
                                        source_name: Some("foo/".to_string()),
                                        target_name: Some("/".to_string()),
                                        ..$namespace::DebugProtocolRegistration::EMPTY
                                    }),
                            ]),
                            ..$namespace::EnvironmentDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_field("DebugProtocolRegistration", "source.child.name"),
                        Error::invalid_field("DebugProtocolRegistration", "source_name"),
                        Error::invalid_field("DebugProtocolRegistration", "target_name"),
                    ])),
                },
                test_validate_environment_debug_log_invalid_child => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.environments = Some(vec![
                            $namespace::EnvironmentDecl {
                                name: Some("a".to_string()),
                                extends: Some($namespace::EnvironmentExtends::None),
                                stop_timeout_ms: Some(2),
                                debug_capabilities:Some(vec![
                                    $namespace::DebugRegistration::Protocol($namespace::DebugProtocolRegistration {
                                        source: Some($namespace::Ref::Child($namespace::ChildRef {
                                            name: "logger".to_string(),
                                            collection: None,
                                        })),
                                        source_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                        target_name: Some("fuchsia.logger.LegacyLog".to_string()),
                                        ..$namespace::DebugProtocolRegistration::EMPTY
                                    }),
                            ]),
                            ..$namespace::EnvironmentDecl::EMPTY
                        }]);
                        decl.children = Some(vec![
                            $namespace::ChildDecl {
                                name: Some("netstack".to_string()),
                                url: Some("fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm".to_string()),
                                startup: Some($namespace::StartupMode::Lazy),
                                on_terminate: None,
                                environment: None,
                                ..$namespace::ChildDecl::EMPTY
                            },
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_child("DebugProtocolRegistration", "source", "logger"),

                    ])),
                },
                test_validate_environment_debug_source_capability => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.environments = Some(vec![
                            $namespace::EnvironmentDecl {
                                name: Some("a".to_string()),
                                extends: Some($namespace::EnvironmentExtends::None),
                                stop_timeout_ms: Some(2),
                                debug_capabilities:Some(vec![
                                    $namespace::DebugRegistration::Protocol($namespace::DebugProtocolRegistration {
                                        source: Some($namespace::Ref::Capability($namespace::CapabilityRef {
                                            name: "storage".to_string(),
                                        })),
                                        source_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                                        target_name: Some("fuchsia.sys2.StorageAdmin".to_string()),
                                        ..$namespace::DebugProtocolRegistration::EMPTY
                                    }),
                            ]),
                            ..$namespace::EnvironmentDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_field("DebugProtocolRegistration", "source"),
                    ])),
                },

                // children
                test_validate_children_empty => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.children = Some(vec![$namespace::ChildDecl{
                            name: None,
                            url: None,
                            startup: None,
                            on_terminate: None,
                            environment: None,
                            ..$namespace::ChildDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::missing_field("ChildDecl", "name"),
                        Error::missing_field("ChildDecl", "url"),
                        Error::missing_field("ChildDecl", "startup"),
                        // `on_terminate` is allowed to be None
                    ])),
                },
                test_validate_children_invalid_identifiers => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.children = Some(vec![$namespace::ChildDecl{
                            name: Some("^bad".to_string()),
                            url: Some("bad-scheme&://blah".to_string()),
                            startup: Some($namespace::StartupMode::Lazy),
                            on_terminate: None,
                            environment: None,
                            ..$namespace::ChildDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_field("ChildDecl", "name"),
                        Error::invalid_field("ChildDecl", "url"),
                    ])),
                },
                test_validate_children_long_identifiers => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.children = Some(vec![$namespace::ChildDecl{
                            name: Some("a".repeat(1025)),
                            url: Some(format!("fuchsia-pkg://{}", "a".repeat(4083))),
                            startup: Some($namespace::StartupMode::Lazy),
                            on_terminate: None,
                            environment: Some("a".repeat(1025)),
                            ..$namespace::ChildDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::field_too_long("ChildDecl", "name"),
                        Error::field_too_long("ChildDecl", "url"),
                        Error::field_too_long("ChildDecl", "environment"),
                        Error::invalid_environment("ChildDecl", "environment", "a".repeat(1025)),
                    ])),
                },
                test_validate_child_references_unknown_env => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.children = Some(vec![$namespace::ChildDecl{
                            name: Some("foo".to_string()),
                            url: Some("fuchsia-pkg://foo".to_string()),
                            startup: Some($namespace::StartupMode::Lazy),
                            on_terminate: None,
                            environment: Some("test_env".to_string()),
                            ..$namespace::ChildDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_environment("ChildDecl", "environment", "test_env"),
                    ])),
                },

                // collections
                test_validate_collections_empty => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.collections = Some(vec![$namespace::CollectionDecl{
                            name: None,
                            durability: None,
                            allowed_offers: None,
                            environment: None,
                            ..$namespace::CollectionDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::missing_field("CollectionDecl", "name"),
                        Error::missing_field("CollectionDecl", "durability"),
                    ])),
                },
                test_validate_collections_invalid_identifiers => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.collections = Some(vec![$namespace::CollectionDecl{
                            name: Some("^bad".to_string()),
                            durability: Some($namespace::Durability::Persistent),
                            allowed_offers: Some($namespace::AllowedOffers::StaticOnly),
                            environment: None,
                            ..$namespace::CollectionDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_field("CollectionDecl", "name"),
                    ])),
                },
                test_validate_collections_long_identifiers => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.collections = Some(vec![$namespace::CollectionDecl{
                            name: Some("a".repeat(1025)),
                            durability: Some($namespace::Durability::Transient),
                            allowed_offers: Some($namespace::AllowedOffers::StaticOnly),
                            environment: None,
                            ..$namespace::CollectionDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::field_too_long("CollectionDecl", "name"),
                    ])),
                },
                test_validate_collection_references_unknown_env => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.collections = Some(vec![$namespace::CollectionDecl {
                            name: Some("foo".to_string()),
                            durability: Some($namespace::Durability::Transient),
                            allowed_offers: Some($namespace::AllowedOffers::StaticOnly),
                            environment: Some("test_env".to_string()),
                            ..$namespace::CollectionDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_environment("CollectionDecl", "environment", "test_env"),
                    ])),
                },

                // capabilities
                test_validate_capabilities_empty => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.capabilities = Some(vec![
                            $namespace::CapabilityDecl::Service($namespace::ServiceDecl {
                                name: None,
                                source_path: None,
                                ..$namespace::ServiceDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Protocol($namespace::ProtocolDecl {
                                name: None,
                                source_path: None,
                                ..$namespace::ProtocolDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Directory($namespace::DirectoryDecl {
                                name: None,
                                source_path: None,
                                rights: None,
                                ..$namespace::DirectoryDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Storage($namespace::StorageDecl {
                                name: None,
                                source: None,
                                backing_dir: None,
                                subdir: None,
                                storage_id: None,
                                ..$namespace::StorageDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Runner($namespace::RunnerDecl {
                                name: None,
                                source_path: None,
                                ..$namespace::RunnerDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Resolver($namespace::ResolverDecl {
                                name: None,
                                source_path: None,
                                ..$namespace::ResolverDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::missing_field("ServiceDecl", "name"),
                        Error::missing_field("ServiceDecl", "source_path"),
                        Error::missing_field("ProtocolDecl", "name"),
                        Error::missing_field("ProtocolDecl", "source_path"),
                        Error::missing_field("DirectoryDecl", "name"),
                        Error::missing_field("DirectoryDecl", "source_path"),
                        Error::missing_field("DirectoryDecl", "rights"),
                        Error::missing_field("StorageDecl", "source"),
                        Error::missing_field("StorageDecl", "name"),
                        Error::missing_field("StorageDecl", "storage_id"),
                        Error::missing_field("StorageDecl", "backing_dir"),
                        Error::missing_field("RunnerDecl", "name"),
                        Error::missing_field("RunnerDecl", "source_path"),
                        Error::missing_field("ResolverDecl", "name"),
                        Error::missing_field("ResolverDecl", "source_path"),
                    ])),
                },
                test_validate_capabilities_invalid_identifiers => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.capabilities = Some(vec![
                            $namespace::CapabilityDecl::Service($namespace::ServiceDecl {
                                name: Some("^bad".to_string()),
                                source_path: Some("&bad".to_string()),
                                ..$namespace::ServiceDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Protocol($namespace::ProtocolDecl {
                                name: Some("^bad".to_string()),
                                source_path: Some("&bad".to_string()),
                                ..$namespace::ProtocolDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Directory($namespace::DirectoryDecl {
                                name: Some("^bad".to_string()),
                                source_path: Some("&bad".to_string()),
                                rights: Some(fio2::Operations::Connect),
                                ..$namespace::DirectoryDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Storage($namespace::StorageDecl {
                                name: Some("^bad".to_string()),
                                source: Some($namespace::Ref::Collection($namespace::CollectionRef {
                                    name: "/bad".to_string()
                                })),
                                backing_dir: Some("&bad".to_string()),
                                subdir: None,
                                storage_id: Some($namespace::StorageId::StaticInstanceIdOrMoniker),
                                ..$namespace::StorageDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Runner($namespace::RunnerDecl {
                                name: Some("^bad".to_string()),
                                source_path: Some("&bad".to_string()),
                                ..$namespace::RunnerDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Resolver($namespace::ResolverDecl {
                                name: Some("^bad".to_string()),
                                source_path: Some("&bad".to_string()),
                                ..$namespace::ResolverDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_field("ServiceDecl", "name"),
                        Error::invalid_field("ServiceDecl", "source_path"),
                        Error::invalid_field("ProtocolDecl", "name"),
                        Error::invalid_field("ProtocolDecl", "source_path"),
                        Error::invalid_field("DirectoryDecl", "name"),
                        Error::invalid_field("DirectoryDecl", "source_path"),
                        Error::invalid_field("StorageDecl", "source"),
                        Error::invalid_field("StorageDecl", "name"),
                        Error::invalid_field("StorageDecl", "backing_dir"),
                        Error::invalid_field("RunnerDecl", "name"),
                        Error::invalid_field("RunnerDecl", "source_path"),
                        Error::invalid_field("ResolverDecl", "name"),
                        Error::invalid_field("ResolverDecl", "source_path"),
                    ])),
                },
                test_validate_capabilities_invalid_child => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.capabilities = Some(vec![
                            $namespace::CapabilityDecl::Storage($namespace::StorageDecl {
                                name: Some("foo".to_string()),
                                source: Some($namespace::Ref::Collection($namespace::CollectionRef {
                                    name: "invalid".to_string(),
                                })),
                                backing_dir: Some("foo".to_string()),
                                subdir: None,
                                storage_id: Some($namespace::StorageId::StaticInstanceIdOrMoniker),
                                ..$namespace::StorageDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_field("StorageDecl", "source"),
                    ])),
                },
                test_validate_capabilities_long_identifiers => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.capabilities = Some(vec![
                            $namespace::CapabilityDecl::Service($namespace::ServiceDecl {
                                name: Some("a".repeat(101)),
                                source_path: Some(format!("/{}", "c".repeat(1024))),
                                ..$namespace::ServiceDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Protocol($namespace::ProtocolDecl {
                                name: Some("a".repeat(101)),
                                source_path: Some(format!("/{}", "c".repeat(1024))),
                                ..$namespace::ProtocolDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Directory($namespace::DirectoryDecl {
                                name: Some("a".repeat(101)),
                                source_path: Some(format!("/{}", "c".repeat(1024))),
                                rights: Some(fio2::Operations::Connect),
                                ..$namespace::DirectoryDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Storage($namespace::StorageDecl {
                                name: Some("a".repeat(101)),
                                source: Some($namespace::Ref::Child($namespace::ChildRef {
                                    name: "b".repeat(101),
                                    collection: None,
                                })),
                                backing_dir: Some(format!("{}", "c".repeat(101))),
                                subdir: None,
                                storage_id: Some($namespace::StorageId::StaticInstanceIdOrMoniker),
                                ..$namespace::StorageDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Runner($namespace::RunnerDecl {
                                name: Some("a".repeat(101)),
                                source_path: Some(format!("/{}", "c".repeat(1024))),
                                ..$namespace::RunnerDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Resolver($namespace::ResolverDecl {
                                name: Some("a".repeat(101)),
                                source_path: Some(format!("/{}", "b".repeat(1024))),
                                ..$namespace::ResolverDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::field_too_long("ServiceDecl", "name"),
                        Error::field_too_long("ServiceDecl", "source_path"),
                        Error::field_too_long("ProtocolDecl", "name"),
                        Error::field_too_long("ProtocolDecl", "source_path"),
                        Error::field_too_long("DirectoryDecl", "name"),
                        Error::field_too_long("DirectoryDecl", "source_path"),
                        Error::field_too_long("StorageDecl", "source.child.name"),
                        Error::field_too_long("StorageDecl", "name"),
                        Error::field_too_long("StorageDecl", "backing_dir"),
                        Error::field_too_long("RunnerDecl", "name"),
                        Error::field_too_long("RunnerDecl", "source_path"),
                        Error::field_too_long("ResolverDecl", "name"),
                        Error::field_too_long("ResolverDecl", "source_path"),
                    ])),
                },
                test_validate_capabilities_duplicate_name => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.capabilities = Some(vec![
                            $namespace::CapabilityDecl::Service($namespace::ServiceDecl {
                                name: Some("service".to_string()),
                                source_path: Some("/service".to_string()),
                                ..$namespace::ServiceDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Service($namespace::ServiceDecl {
                                name: Some("service".to_string()),
                                source_path: Some("/service".to_string()),
                                ..$namespace::ServiceDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Protocol($namespace::ProtocolDecl {
                                name: Some("protocol".to_string()),
                                source_path: Some("/protocol".to_string()),
                                ..$namespace::ProtocolDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Protocol($namespace::ProtocolDecl {
                                name: Some("protocol".to_string()),
                                source_path: Some("/protocol".to_string()),
                                ..$namespace::ProtocolDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Directory($namespace::DirectoryDecl {
                                name: Some("directory".to_string()),
                                source_path: Some("/directory".to_string()),
                                rights: Some(fio2::Operations::Connect),
                                ..$namespace::DirectoryDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Directory($namespace::DirectoryDecl {
                                name: Some("directory".to_string()),
                                source_path: Some("/directory".to_string()),
                                rights: Some(fio2::Operations::Connect),
                                ..$namespace::DirectoryDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Storage($namespace::StorageDecl {
                                name: Some("storage".to_string()),
                                source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                backing_dir: Some("directory".to_string()),
                                subdir: None,
                                storage_id: Some($namespace::StorageId::StaticInstanceIdOrMoniker),
                                ..$namespace::StorageDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Storage($namespace::StorageDecl {
                                name: Some("storage".to_string()),
                                source: Some($namespace::Ref::Self_($namespace::SelfRef{})),
                                backing_dir: Some("directory".to_string()),
                                subdir: None,
                                storage_id: Some($namespace::StorageId::StaticInstanceIdOrMoniker),
                                ..$namespace::StorageDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Runner($namespace::RunnerDecl {
                                name: Some("runner".to_string()),
                                source_path: Some("/runner".to_string()),
                                ..$namespace::RunnerDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Runner($namespace::RunnerDecl {
                                name: Some("runner".to_string()),
                                source_path: Some("/runner".to_string()),
                                ..$namespace::RunnerDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Resolver($namespace::ResolverDecl {
                                name: Some("resolver".to_string()),
                                source_path: Some("/resolver".to_string()),
                                ..$namespace::ResolverDecl::EMPTY
                            }),
                            $namespace::CapabilityDecl::Resolver($namespace::ResolverDecl {
                                name: Some("resolver".to_string()),
                                source_path: Some("/resolver".to_string()),
                                ..$namespace::ResolverDecl::EMPTY
                            }),
                        ]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::duplicate_field("ServiceDecl", "name", "service"),
                        Error::duplicate_field("ProtocolDecl", "name", "protocol"),
                        Error::duplicate_field("DirectoryDecl", "name", "directory"),
                        Error::duplicate_field("StorageDecl", "name", "storage"),
                        Error::duplicate_field("RunnerDecl", "name", "runner"),
                        Error::duplicate_field("ResolverDecl", "name", "resolver"),
                    ])),
                },

                test_validate_resolvers_missing_from_offer => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.offers = Some(vec![$namespace::OfferDecl::Resolver($namespace::OfferResolverDecl {
                            source: Some($namespace::Ref::Self_($namespace::SelfRef {})),
                            source_name: Some("a".to_string()),
                            target: Some($namespace::Ref::Child($namespace::ChildRef { name: "child".to_string(), collection: None })),
                            target_name: Some("a".to_string()),
                            ..$namespace::OfferResolverDecl::EMPTY
                        })]);
                        decl.children = Some(vec![$namespace::ChildDecl {
                            name: Some("child".to_string()),
                            url: Some("test:///child".to_string()),
                            startup: Some($namespace::StartupMode::Eager),
                            on_terminate: None,
                            environment: None,
                            ..$namespace::ChildDecl::EMPTY
                        }]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_capability("OfferResolverDecl", "source", "a"),
                    ])),
                },
                test_validate_resolvers_missing_from_expose => {
                    input = {
                        let mut decl = new_component_decl();
                        decl.exposes = Some(vec![$namespace::ExposeDecl::Resolver($namespace::ExposeResolverDecl {
                            source: Some($namespace::Ref::Self_($namespace::SelfRef {})),
                            source_name: Some("a".to_string()),
                            target: Some($namespace::Ref::Parent($namespace::ParentRef {})),
                            target_name: Some("a".to_string()),
                            ..$namespace::ExposeResolverDecl::EMPTY
                        })]);
                        decl
                    },
                    result = Err(ErrorList::new(vec![
                        Error::invalid_capability("ExposeResolverDecl", "source", "a"),
                    ])),
                },
            }

            test_validate_capabilities! {
                test_validate_capabilities_individually_ok => {
                    input = vec![
                        $namespace::CapabilityDecl::Protocol($namespace::ProtocolDecl {
                            name: Some("foo_svc".into()),
                            source_path: Some("/svc/foo".into()),
                            ..$namespace::ProtocolDecl::EMPTY
                        }),
                        $namespace::CapabilityDecl::Directory($namespace::DirectoryDecl {
                            name: Some("foo_dir".into()),
                            source_path: Some("/foo".into()),
                            rights: Some(fio2::Operations::Connect),
                            ..$namespace::DirectoryDecl::EMPTY
                        }),
                    ],
                    as_builtin = false,
                    result = Ok(()),
                },
                test_validate_capabilities_individually_err => {
                    input = vec![
                        $namespace::CapabilityDecl::Protocol($namespace::ProtocolDecl {
                            name: None,
                            source_path: None,
                            ..$namespace::ProtocolDecl::EMPTY
                        }),
                        $namespace::CapabilityDecl::Directory($namespace::DirectoryDecl {
                            name: None,
                            source_path: None,
                            rights: None,
                            ..$namespace::DirectoryDecl::EMPTY
                        }),
                        $namespace::CapabilityDecl::Event($namespace::EventDecl {
                            name: None,
                            ..$namespace::EventDecl::EMPTY
                        }),
                    ],
                    as_builtin = false,
                    result = Err(ErrorList::new(vec![
                        Error::missing_field("ProtocolDecl", "name"),
                        Error::missing_field("ProtocolDecl", "source_path"),
                        Error::missing_field("DirectoryDecl", "name"),
                        Error::missing_field("DirectoryDecl", "source_path"),
                        Error::missing_field("DirectoryDecl", "rights"),
                        Error::invalid_capability_type("ComponentDecl", "capability", "event")
                    ])),
                },
                test_validate_builtin_capabilities_individually_ok => {
                    input = vec![
                        $namespace::CapabilityDecl::Protocol($namespace::ProtocolDecl {
                            name: Some("foo_protocol".into()),
                            source_path: None,
                            ..$namespace::ProtocolDecl::EMPTY
                        }),
                        $namespace::CapabilityDecl::Directory($namespace::DirectoryDecl {
                            name: Some("foo_dir".into()),
                            source_path: None,
                            rights: Some(fio2::Operations::Connect),
                            ..$namespace::DirectoryDecl::EMPTY
                        }),
                        $namespace::CapabilityDecl::Service($namespace::ServiceDecl {
                            name: Some("foo_svc".into()),
                            source_path: None,
                            ..$namespace::ServiceDecl::EMPTY
                        }),
                        $namespace::CapabilityDecl::Runner($namespace::RunnerDecl {
                            name: Some("foo_runner".into()),
                            source_path: None,
                            ..$namespace::RunnerDecl::EMPTY
                        }),
                        $namespace::CapabilityDecl::Resolver($namespace::ResolverDecl {
                            name: Some("foo_resolver".into()),
                            source_path: None,
                            ..$namespace::ResolverDecl::EMPTY
                        }),
                        $namespace::CapabilityDecl::Event($namespace::EventDecl {
                            name: Some("foo_event".into()),
                            ..$namespace::EventDecl::EMPTY
                        }),
                    ],
                    as_builtin = true,
                    result = Ok(()),
                },
                test_validate_builtin_capabilities_individually_err => {
                    input = vec![
                        $namespace::CapabilityDecl::Protocol($namespace::ProtocolDecl {
                            name: None,
                            source_path: Some("/svc/foo".into()),
                            ..$namespace::ProtocolDecl::EMPTY
                        }),
                        $namespace::CapabilityDecl::Directory($namespace::DirectoryDecl {
                            name: None,
                            source_path: Some("/foo".into()),
                            rights: None,
                            ..$namespace::DirectoryDecl::EMPTY
                        }),
                        $namespace::CapabilityDecl::Service($namespace::ServiceDecl {
                            name: None,
                            source_path: Some("/svc/foo".into()),
                            ..$namespace::ServiceDecl::EMPTY
                        }),
                        $namespace::CapabilityDecl::Runner($namespace::RunnerDecl {
                            name: None,
                            source_path:  Some("/foo".into()),
                            ..$namespace::RunnerDecl::EMPTY
                        }),
                        $namespace::CapabilityDecl::Resolver($namespace::ResolverDecl {
                            name: None,
                            source_path:  Some("/foo".into()),
                            ..$namespace::ResolverDecl::EMPTY
                        }),
                        $namespace::CapabilityDecl::Event($namespace::EventDecl {
                            name: None,
                            ..$namespace::EventDecl::EMPTY
                        }),
                        $namespace::CapabilityDecl::Storage($namespace::StorageDecl {
                            name: None,
                            ..$namespace::StorageDecl::EMPTY
                        }),
                    ],
                    as_builtin = true,
                    result = Err(ErrorList::new(vec![
                        Error::missing_field("ProtocolDecl", "name"),
                        Error::extraneous_source_path("ProtocolDecl", "/svc/foo"),
                        Error::missing_field("DirectoryDecl", "name"),
                        Error::extraneous_source_path("DirectoryDecl", "/foo"),
                        Error::missing_field("DirectoryDecl", "rights"),
                        Error::missing_field("ServiceDecl", "name"),
                        Error::extraneous_source_path("ServiceDecl", "/svc/foo"),
                        Error::missing_field("RunnerDecl", "name"),
                        Error::extraneous_source_path("RunnerDecl", "/foo"),
                        Error::missing_field("ResolverDecl", "name"),
                        Error::extraneous_source_path("ResolverDecl", "/foo"),
                        Error::missing_field("EventDecl", "name"),
                        Error::invalid_capability_type("RuntimeConfig", "capability", "storage"),
                    ])),
                },
            }

            #[test]
            fn test_validate_dynamic_offers_empty() {
                assert_eq!(crate::$namespace::validate_dynamic_offers(&vec![]), Ok(()));
            }

            #[test]
            fn test_validate_dynamic_offers_okay() {
                assert_eq!(
                    crate::$namespace::validate_dynamic_offers(&vec![
                        $namespace::OfferDecl::Protocol($namespace::OfferProtocolDecl {
                            dependency_type: Some($namespace::DependencyType::Strong),
                            source: Some($namespace::Ref::Self_($namespace::SelfRef)),
                            source_name: Some("thing".to_string()),
                            target_name: Some("thing".to_string()),
                            ..$namespace::OfferProtocolDecl::EMPTY
                        }),
                        $namespace::OfferDecl::Service($namespace::OfferServiceDecl {
                            source: Some($namespace::Ref::Parent($namespace::ParentRef)),
                            source_name: Some("thang".to_string()),
                            target_name: Some("thang".to_string()),
                            ..$namespace::OfferServiceDecl::EMPTY
                        }),
                        $namespace::OfferDecl::Directory($namespace::OfferDirectoryDecl {
                            dependency_type: Some($namespace::DependencyType::Strong),
                            source: Some($namespace::Ref::Parent($namespace::ParentRef)),
                            source_name: Some("thung1".to_string()),
                            target_name: Some("thung1".to_string()),
                            ..$namespace::OfferDirectoryDecl::EMPTY
                        }),
                        $namespace::OfferDecl::Storage($namespace::OfferStorageDecl {
                            source: Some($namespace::Ref::Parent($namespace::ParentRef)),
                            source_name: Some("thung2".to_string()),
                            target_name: Some("thung2".to_string()),
                            ..$namespace::OfferStorageDecl::EMPTY
                        }),
                        $namespace::OfferDecl::Runner($namespace::OfferRunnerDecl {
                            source: Some($namespace::Ref::Parent($namespace::ParentRef)),
                            source_name: Some("thung3".to_string()),
                            target_name: Some("thung3".to_string()),
                            ..$namespace::OfferRunnerDecl::EMPTY
                        }),
                        $namespace::OfferDecl::Resolver($namespace::OfferResolverDecl {
                            source: Some($namespace::Ref::Parent($namespace::ParentRef)),
                            source_name: Some("thung4".to_string()),
                            target_name: Some("thung4".to_string()),
                            ..$namespace::OfferResolverDecl::EMPTY
                        }),
                        $namespace::OfferDecl::Event($namespace::OfferEventDecl {
                            source: Some($namespace::Ref::Parent($namespace::ParentRef)),
                            source_name: Some("thung5".to_string()),
                            target_name: Some("thung5".to_string()),
                            mode: Some($namespace::EventMode::Async),
                            ..$namespace::OfferEventDecl::EMPTY
                        }),
                    ]),
                    Ok(())
                );
            }

            #[test]
            fn test_validate_dynamic_offers_specify_target() {
                assert_eq!(
                    crate::$namespace::validate_dynamic_offers(&vec![
                        $namespace::OfferDecl::Protocol($namespace::OfferProtocolDecl {
                            dependency_type: Some($namespace::DependencyType::Strong),
                            source: Some($namespace::Ref::Self_($namespace::SelfRef)),
                            target: Some($namespace::Ref::Child($namespace::ChildRef {
                                name: "foo".to_string(),
                                collection: None
                            })),
                            source_name: Some("thing".to_string()),
                            target_name: Some("thing".to_string()),
                            ..$namespace::OfferProtocolDecl::EMPTY
                        }),
                        $namespace::OfferDecl::Service($namespace::OfferServiceDecl {
                            source: Some($namespace::Ref::Parent($namespace::ParentRef)),
                            target: Some($namespace::Ref::Child($namespace::ChildRef {
                                name: "bar".to_string(),
                                collection: Some("baz".to_string())
                            })),
                            source_name: Some("thang".to_string()),
                            target_name: Some("thang".to_string()),
                            ..$namespace::OfferServiceDecl::EMPTY
                        }),
                        $namespace::OfferDecl::Directory($namespace::OfferDirectoryDecl {
                            dependency_type: Some($namespace::DependencyType::Strong),
                            source: Some($namespace::Ref::Parent($namespace::ParentRef)),
                            target: Some($namespace::Ref::Framework($namespace::FrameworkRef)),
                            source_name: Some("thung1".to_string()),
                            target_name: Some("thung1".to_string()),
                            ..$namespace::OfferDirectoryDecl::EMPTY
                        }),
                        $namespace::OfferDecl::Storage($namespace::OfferStorageDecl {
                            source: Some($namespace::Ref::Parent($namespace::ParentRef)),
                            target: Some($namespace::Ref::Child($namespace::ChildRef {
                                name: "bar".to_string(),
                                collection: Some("baz".to_string())
                            })),
                            source_name: Some("thung2".to_string()),
                            target_name: Some("thung2".to_string()),
                            ..$namespace::OfferStorageDecl::EMPTY
                        }),
                        $namespace::OfferDecl::Runner($namespace::OfferRunnerDecl {
                            source: Some($namespace::Ref::Parent($namespace::ParentRef)),
                            target: Some($namespace::Ref::Child($namespace::ChildRef {
                                name: "bar".to_string(),
                                collection: Some("baz".to_string())
                            })),
                            source_name: Some("thung3".to_string()),
                            target_name: Some("thung3".to_string()),
                            ..$namespace::OfferRunnerDecl::EMPTY
                        }),
                        $namespace::OfferDecl::Resolver($namespace::OfferResolverDecl {
                            source: Some($namespace::Ref::Parent($namespace::ParentRef)),
                            target: Some($namespace::Ref::Child($namespace::ChildRef {
                                name: "bar".to_string(),
                                collection: Some("baz".to_string())
                            })),
                            source_name: Some("thung4".to_string()),
                            target_name: Some("thung4".to_string()),
                            ..$namespace::OfferResolverDecl::EMPTY
                        }),
                        $namespace::OfferDecl::Event($namespace::OfferEventDecl {
                            target: Some($namespace::Ref::Child($namespace::ChildRef {
                                name: "bar".to_string(),
                                collection: Some("baz".to_string())
                            })),
                            source: Some($namespace::Ref::Parent($namespace::ParentRef)),
                            source_name: Some("thung5".to_string()),
                            target_name: Some("thung5".to_string()),
                            mode: Some($namespace::EventMode::Async),
                            ..$namespace::OfferEventDecl::EMPTY
                        }),
                    ]),
                    Err(ErrorList::new(vec![
                        Error::extraneous_field("OfferProtocolDecl", "target"),
                        Error::extraneous_field("OfferServiceDecl", "target"),
                        Error::extraneous_field("OfferDirectoryDecl", "target"),
                        Error::extraneous_field("OfferStorageDecl", "target"),
                        Error::extraneous_field("OfferRunnerDecl", "target"),
                        Error::extraneous_field("OfferResolverDecl", "target"),
                        Error::extraneous_field("OfferEventDecl", "target"),
                    ]))
                );
            }

            #[test]
            fn test_validate_dynamic_offers_missing_stuff() {
                assert_eq!(
                    crate::$namespace::validate_dynamic_offers(&vec![
                        $namespace::OfferDecl::Protocol($namespace::OfferProtocolDecl::EMPTY),
                        $namespace::OfferDecl::Service($namespace::OfferServiceDecl::EMPTY),
                        $namespace::OfferDecl::Directory($namespace::OfferDirectoryDecl::EMPTY),
                        $namespace::OfferDecl::Storage($namespace::OfferStorageDecl::EMPTY),
                        $namespace::OfferDecl::Runner($namespace::OfferRunnerDecl::EMPTY),
                        $namespace::OfferDecl::Resolver($namespace::OfferResolverDecl::EMPTY),
                        $namespace::OfferDecl::Event($namespace::OfferEventDecl::EMPTY),
                    ]),
                    Err(ErrorList::new(vec![
                        Error::missing_field("OfferProtocolDecl", "source"),
                        Error::missing_field("OfferProtocolDecl", "source_name"),
                        Error::missing_field("OfferProtocolDecl", "target_name"),
                        Error::missing_field("OfferProtocolDecl", "dependency_type"),
                        Error::missing_field("OfferServiceDecl", "source"),
                        Error::missing_field("OfferServiceDecl", "source_name"),
                        Error::missing_field("OfferServiceDecl", "target_name"),
                        Error::missing_field("OfferDirectoryDecl", "source"),
                        Error::missing_field("OfferDirectoryDecl", "source_name"),
                        Error::missing_field("OfferDirectoryDecl", "target_name"),
                        Error::missing_field("OfferDirectoryDecl", "dependency_type"),
                        Error::missing_field("OfferStorageDecl", "source_name"),
                        Error::missing_field("OfferStorageDecl", "source"),
                        Error::missing_field("OfferRunnerDecl", "source"),
                        Error::missing_field("OfferRunnerDecl", "source_name"),
                        Error::missing_field("OfferRunnerDecl", "target_name"),
                        Error::missing_field("OfferResolverDecl", "source"),
                        Error::missing_field("OfferResolverDecl", "source_name"),
                        Error::missing_field("OfferResolverDecl", "target_name"),
                        Error::missing_field("OfferEventDecl", "source_name"),
                        Error::missing_field("OfferEventDecl", "source"),
                        Error::missing_field("OfferEventDecl", "target_name"),
                        Error::missing_field("OfferEventDecl", "mode"),
                    ]))
                );
            }

            test_dependency! {
                (test_validate_offers_protocol_dependency_cycle, $namespace) => {
                    ty = $namespace::OfferDecl::Protocol,
                    offer_decl = $namespace::OfferProtocolDecl {
                        source: None,  // Filled by macro
                        target: None,  // Filled by macro
                        source_name: Some(format!("thing")),
                        target_name: Some(format!("thing")),
                        dependency_type: Some($namespace::DependencyType::Strong),
                        ..$namespace::OfferProtocolDecl::EMPTY
                    },
                },
                (test_validate_offers_directory_dependency_cycle, $namespace) => {
                    ty = $namespace::OfferDecl::Directory,
                    offer_decl = $namespace::OfferDirectoryDecl {
                        source: None,  // Filled by macro
                        target: None,  // Filled by macro
                        source_name: Some(format!("thing")),
                        target_name: Some(format!("thing")),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                        dependency_type: Some($namespace::DependencyType::Strong),
                        ..$namespace::OfferDirectoryDecl::EMPTY
                    },
                },
                (test_validate_offers_service_dependency_cycle, $namespace) => {
                    ty = $namespace::OfferDecl::Service,
                    offer_decl = $namespace::OfferServiceDecl {
                        source: None,  // Filled by macro
                        target: None,  // Filled by macro
                        source_name: Some(format!("thing")),
                        target_name: Some(format!("thing")),
                        ..$namespace::OfferServiceDecl::EMPTY
                    },
                },
                (test_validate_offers_runner_dependency_cycle, $namespace) => {
                    ty = $namespace::OfferDecl::Runner,
                    offer_decl = $namespace::OfferRunnerDecl {
                        source: None,  // Filled by macro
                        target: None,  // Filled by macro
                        source_name: Some(format!("thing")),
                        target_name: Some(format!("thing")),
                        ..$namespace::OfferRunnerDecl::EMPTY
                    },
                },
                (test_validate_offers_resolver_dependency_cycle, $namespace) => {
                    ty = $namespace::OfferDecl::Resolver,
                    offer_decl = $namespace::OfferResolverDecl {
                        source: None,  // Filled by macro
                        target: None,  // Filled by macro
                        source_name: Some(format!("thing")),
                        target_name: Some(format!("thing")),
                        ..$namespace::OfferResolverDecl::EMPTY
                    },
                },
            }
            test_weak_dependency! {
                (test_validate_offers_protocol_weak_dependency_cycle, $namespace) => {
                    ty = $namespace::OfferDecl::Protocol,
                    offer_decl = $namespace::OfferProtocolDecl {
                        source: None,  // Filled by macro
                        target: None,  // Filled by macro
                        source_name: Some(format!("thing")),
                        target_name: Some(format!("thing")),
                        dependency_type: None, // Filled by macro
                        ..$namespace::OfferProtocolDecl::EMPTY
                    },
                },
                (test_validate_offers_directory_weak_dependency_cycle, $namespace) => {
                    ty = $namespace::OfferDecl::Directory,
                    offer_decl = $namespace::OfferDirectoryDecl {
                        source: None,  // Filled by macro
                        target: None,  // Filled by macro
                        source_name: Some(format!("thing")),
                        target_name: Some(format!("thing")),
                        rights: Some(fio2::Operations::Connect),
                        subdir: None,
                        dependency_type: None,  // Filled by macro
                        ..$namespace::OfferDirectoryDecl::EMPTY
                    },
                },
            }
        }
    }
}

cm_fidl_validator_test_suite!(test_fdecl, fdecl);
