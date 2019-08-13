// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        framework::RealFrameworkServiceHost,
        model::testing::{mocks::*, routing_test_helpers::*},
    },
    cm_rust::{
        self, CapabilityPath, ChildDecl, CollectionDecl, ComponentDecl, ExposeDecl,
        ExposeDirectoryDecl, ExposeLegacyServiceDecl, ExposeSource, OfferDecl, OfferDirectoryDecl,
        OfferDirectorySource, OfferLegacyServiceDecl, OfferServiceSource, OfferTarget, UseDecl,
        UseDirectoryDecl, UseLegacyServiceDecl, UseSource,
    },
    fidl_fuchsia_sys2 as fsys,
    std::convert::{TryFrom, TryInto},
};

///   a
///    \
///     b
///
/// b: uses framework service /svc/fuchsia.sys2.Realm
#[fuchsia_async::run_singlethreaded(test)]
async fn use_framework_service() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                uses: vec![UseDecl::LegacyService(UseLegacyServiceDecl {
                    source: UseSource::Framework,
                    source_path: CapabilityPath::try_from("/svc/fuchsia.sys2.Realm").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.Realm").unwrap(),
                })],
                ..default_component_decl()
            },
        ),
    ];
    let framework_services = Box::new(MockFrameworkServiceHost::new());
    let bind_calls = framework_services.bind_calls.clone();
    let test = RoutingTest::new("a", components, framework_services);
    test.check_use_realm(vec!["b"].into(), bind_calls).await;
}

///   a
///    \
///     b
///
/// a: offers directory /data/foo from self as /data/bar
/// a: offers service /svc/foo from self as /svc/bar
/// a: offers service /svc/file from self as /svc/device
/// b: uses directory /data/bar as /data/hippo
/// b: uses service /svc/bar as /svc/hippo
/// b: uses service /svc/device
///
/// The test related to `/svc/file` is used to verify that services that require
/// extended flags, like `OPEN_FLAG_DESCRIBE`, work correctly. This often
/// happens for fuchsia.hardware protocols that compose fuchsia.io protocols,
/// and expect that `fdio_open` should operate correctly.
#[fuchsia_async::run_singlethreaded(test)]
async fn use_from_parent() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                offers: vec![
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferDirectorySource::Self_,
                        source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                        target_path: CapabilityPath::try_from("/data/bar").unwrap(),
                        target: OfferTarget::Child("b".to_string()),
                    }),
                    OfferDecl::LegacyService(OfferLegacyServiceDecl {
                        source: OfferServiceSource::Self_,
                        source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                        target: OfferTarget::Child("b".to_string()),
                    }),
                    OfferDecl::LegacyService(OfferLegacyServiceDecl {
                        source: OfferServiceSource::Self_,
                        source_path: CapabilityPath::try_from("/svc/file").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/device").unwrap(),
                        target: OfferTarget::Child("b".to_string()),
                    }),
                ],
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                uses: vec![
                    UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/data/bar").unwrap(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    }),
                    UseDecl::LegacyService(UseLegacyServiceDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }),
                    UseDecl::LegacyService(UseLegacyServiceDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/device").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/device").unwrap(),
                    }),
                ],
                ..default_component_decl()
            },
        ),
    ];
    let framework_services = Box::new(MockFrameworkServiceHost::new());
    let test = RoutingTest::new("a", components, framework_services);
    test.check_use(
        vec!["b"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: true },
    ).await;
    test.check_use(
        vec!["b"].into(),
        CheckUse::LegacyService { path: default_service_capability(), should_succeed: true },
    ).await;
    test.check_open_file(vec!["b"].into(), "/svc/device".try_into().unwrap()).await
}

///   a
///    \
///     b
///      \
///       c
///
/// a: offers directory /data/foo from self as /data/bar
/// a: offers service /svc/foo from self as /svc/bar
/// b: offers directory /data/bar from realm as /data/baz
/// b: offers service /svc/bar from realm as /svc/baz
/// c: uses /data/baz as /data/hippo
/// c: uses /svc/baz as /svc/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_from_grandparent() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                offers: vec![
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferDirectorySource::Self_,
                        source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                        target_path: CapabilityPath::try_from("/data/bar").unwrap(),
                        target: OfferTarget::Child("b".to_string()),
                    }),
                    OfferDecl::LegacyService(OfferLegacyServiceDecl {
                        source: OfferServiceSource::Self_,
                        source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                        target: OfferTarget::Child("b".to_string()),
                    }),
                ],
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                offers: vec![
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferDirectorySource::Realm,
                        source_path: CapabilityPath::try_from("/data/bar").unwrap(),
                        target_path: CapabilityPath::try_from("/data/baz").unwrap(),
                        target: OfferTarget::Child("c".to_string()),
                    }),
                    OfferDecl::LegacyService(OfferLegacyServiceDecl {
                        source: OfferServiceSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                        target: OfferTarget::Child("c".to_string()),
                    }),
                ],
                children: vec![ChildDecl {
                    name: "c".to_string(),
                    url: "test:///c".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "c",
            ComponentDecl {
                uses: vec![
                    UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/data/baz").unwrap(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    }),
                    UseDecl::LegacyService(UseLegacyServiceDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }),
                ],
                ..default_component_decl()
            },
        ),
    ];
    let framework_services = Box::new(MockFrameworkServiceHost::new());
    let test = RoutingTest::new("a", components, framework_services);
    test.check_use(
        vec!["b", "c"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: true },
    ).await;
    test.check_use(
        vec!["b", "c"].into(),
        CheckUse::LegacyService { path: default_service_capability(), should_succeed: true },
    ).await;
}

///     a
///    /
///   b
///  / \
/// d   c
///
/// d: exposes directory /data/foo from self as /data/bar
/// b: offers directory /data/bar from d as /data/foobar to c
/// c: uses /data/foobar as /data/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_from_sibling_no_root() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                offers: vec![
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferDirectorySource::Child("d".to_string()),
                        source_path: CapabilityPath::try_from("/data/bar").unwrap(),
                        target_path: CapabilityPath::try_from("/data/foobar").unwrap(),
                        target: OfferTarget::Child("c".to_string()),
                    }),
                    OfferDecl::LegacyService(OfferLegacyServiceDecl {
                        source: OfferServiceSource::Child("d".to_string()),
                        source_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/foobar").unwrap(),
                        target: OfferTarget::Child("c".to_string()),
                    }),
                ],
                children: vec![
                    ChildDecl {
                        name: "c".to_string(),
                        url: "test:///c".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },
                    ChildDecl {
                        name: "d".to_string(),
                        url: "test:///d".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },
                ],
                ..default_component_decl()
            },
        ),
        (
            "c",
            ComponentDecl {
                uses: vec![
                    UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/data/foobar").unwrap(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    }),
                    UseDecl::LegacyService(UseLegacyServiceDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/foobar").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }),
                ],
                ..default_component_decl()
            },
        ),
        (
            "d",
            ComponentDecl {
                exposes: vec![
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Self_,
                        source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                        target_path: CapabilityPath::try_from("/data/bar").unwrap(),
                    }),
                    ExposeDecl::LegacyService(ExposeLegacyServiceDecl {
                        source: ExposeSource::Self_,
                        source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                    }),
                ],
                ..default_component_decl()
            },
        ),
    ];
    let framework_services = Box::new(MockFrameworkServiceHost::new());
    let test = RoutingTest::new("a", components, framework_services);
    test.check_use(
        vec!["b", "c"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: true },
    ).await;
    test.check_use(
        vec!["b", "c"].into(),
        CheckUse::LegacyService { path: default_service_capability(), should_succeed: true },
    ).await;
}

///   a
///  / \
/// b   c
///
/// b: exposes directory /data/foo from self as /data/bar
/// a: offers directory /data/bar from b as /data/baz to c
/// c: uses /data/baz as /data/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_from_sibling_root() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                offers: vec![
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferDirectorySource::Child("b".to_string()),
                        source_path: CapabilityPath::try_from("/data/bar").unwrap(),
                        target_path: CapabilityPath::try_from("/data/baz").unwrap(),
                        target: OfferTarget::Child("c".to_string()),
                    }),
                    OfferDecl::LegacyService(OfferLegacyServiceDecl {
                        source: OfferServiceSource::Child("b".to_string()),
                        source_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                        target: OfferTarget::Child("c".to_string()),
                    }),
                ],
                children: vec![
                    ChildDecl {
                        name: "b".to_string(),
                        url: "test:///b".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },
                    ChildDecl {
                        name: "c".to_string(),
                        url: "test:///c".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },
                ],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                exposes: vec![
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Self_,
                        source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                        target_path: CapabilityPath::try_from("/data/bar").unwrap(),
                    }),
                    ExposeDecl::LegacyService(ExposeLegacyServiceDecl {
                        source: ExposeSource::Self_,
                        source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                    }),
                ],
                ..default_component_decl()
            },
        ),
        (
            "c",
            ComponentDecl {
                uses: vec![
                    UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/data/baz").unwrap(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    }),
                    UseDecl::LegacyService(UseLegacyServiceDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }),
                ],
                ..default_component_decl()
            },
        ),
    ];
    let framework_services = Box::new(MockFrameworkServiceHost::new());
    let test = RoutingTest::new("a", components, framework_services);
    test.check_use(
        vec!["c"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: true },
    ).await;
    test.check_use(
        vec!["c"].into(),
        CheckUse::LegacyService { path: default_service_capability(), should_succeed: true },
    ).await;
}

///     a
///    / \
///   b   c
///  /
/// d
///
/// d: exposes directory /data/foo from self as /data/bar
/// b: exposes directory /data/bar from d as /data/baz
/// a: offers directory /data/baz from b as /data/foobar to c
/// c: uses /data/foobar as /data/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_from_niece() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                offers: vec![
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferDirectorySource::Child("b".to_string()),
                        source_path: CapabilityPath::try_from("/data/baz").unwrap(),
                        target_path: CapabilityPath::try_from("/data/foobar").unwrap(),
                        target: OfferTarget::Child("c".to_string()),
                    }),
                    OfferDecl::LegacyService(OfferLegacyServiceDecl {
                        source: OfferServiceSource::Child("b".to_string()),
                        source_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/foobar").unwrap(),
                        target: OfferTarget::Child("c".to_string()),
                    }),
                ],
                children: vec![
                    ChildDecl {
                        name: "b".to_string(),
                        url: "test:///b".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },
                    ChildDecl {
                        name: "c".to_string(),
                        url: "test:///c".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },
                ],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                exposes: vec![
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Child("d".to_string()),
                        source_path: CapabilityPath::try_from("/data/bar").unwrap(),
                        target_path: CapabilityPath::try_from("/data/baz").unwrap(),
                    }),
                    ExposeDecl::LegacyService(ExposeLegacyServiceDecl {
                        source: ExposeSource::Child("d".to_string()),
                        source_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                    }),
                ],
                children: vec![ChildDecl {
                    name: "d".to_string(),
                    url: "test:///d".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "c",
            ComponentDecl {
                uses: vec![
                    UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/data/foobar").unwrap(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    }),
                    UseDecl::LegacyService(UseLegacyServiceDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/foobar").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }),
                ],
                ..default_component_decl()
            },
        ),
        (
            "d",
            ComponentDecl {
                exposes: vec![
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Self_,
                        source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                        target_path: CapabilityPath::try_from("/data/bar").unwrap(),
                    }),
                    ExposeDecl::LegacyService(ExposeLegacyServiceDecl {
                        source: ExposeSource::Self_,
                        source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                    }),
                ],
                ..default_component_decl()
            },
        ),
    ];
    let framework_services = Box::new(MockFrameworkServiceHost::new());
    let test = RoutingTest::new("a", components, framework_services);
    test.check_use(
        vec!["c"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: true },
    ).await;
    test.check_use(
        vec!["c"].into(),
        CheckUse::LegacyService { path: default_service_capability(), should_succeed: true },
    ).await;
}

///      a
///     / \
///    /   \
///   b     c
///  / \   / \
/// d   e f   g
///            \
///             h
///
/// a,d,h: hosts /svc/foo and /data/foo
/// e: uses /svc/foo as /svc/hippo from a, uses /data/foo as /data/hippo from d
/// f: uses /data/foo from d as /data/hippo, uses /svc/foo from h as /svc/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_kitchen_sink() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                offers: vec![
                    OfferDecl::LegacyService(OfferLegacyServiceDecl {
                        source: OfferServiceSource::Self_,
                        source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/foo_from_a").unwrap(),
                        target: OfferTarget::Child("b".to_string()),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferDirectorySource::Child("b".to_string()),
                        source_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                        target_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                        target: OfferTarget::Child("c".to_string()),
                    }),
                ],
                children: vec![
                    ChildDecl {
                        name: "b".to_string(),
                        url: "test:///b".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },
                    ChildDecl {
                        name: "c".to_string(),
                        url: "test:///c".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },
                ],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                program: None,
                offers: vec![
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferDirectorySource::Child("d".to_string()),
                        source_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                        target_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                        target: OfferTarget::Child("e".to_string()),
                    }),
                    OfferDecl::LegacyService(OfferLegacyServiceDecl {
                        source: OfferServiceSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/foo_from_a").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/foo_from_a").unwrap(),
                        target: OfferTarget::Child("e".to_string()),
                    }),
                ],
                exposes: vec![ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Child("d".to_string()),
                    source_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                    target_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                })],
                children: vec![
                    ChildDecl {
                        name: "d".to_string(),
                        url: "test:///d".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },
                    ChildDecl {
                        name: "e".to_string(),
                        url: "test:///e".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },
                ],
                ..default_component_decl()
            },
        ),
        (
            "c",
            ComponentDecl {
                program: None,
                offers: vec![
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferDirectorySource::Realm,
                        source_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                        target_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                        target: OfferTarget::Child("f".to_string()),
                    }),
                    OfferDecl::LegacyService(OfferLegacyServiceDecl {
                        source: OfferServiceSource::Child("g".to_string()),
                        source_path: CapabilityPath::try_from("/svc/foo_from_h").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/foo_from_h").unwrap(),
                        target: OfferTarget::Child("f".to_string()),
                    }),
                ],
                children: vec![
                    ChildDecl {
                        name: "f".to_string(),
                        url: "test:///f".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },
                    ChildDecl {
                        name: "g".to_string(),
                        url: "test:///g".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },
                ],
                ..default_component_decl()
            },
        ),
        (
            "d",
            ComponentDecl {
                exposes: vec![ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                })],
                ..default_component_decl()
            },
        ),
        (
            "e",
            ComponentDecl {
                uses: vec![
                    UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    }),
                    UseDecl::LegacyService(UseLegacyServiceDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/foo_from_a").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }),
                ],
                ..default_component_decl()
            },
        ),
        (
            "f",
            ComponentDecl {
                uses: vec![
                    UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    }),
                    UseDecl::LegacyService(UseLegacyServiceDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/foo_from_h").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }),
                ],
                ..default_component_decl()
            },
        ),
        (
            "g",
            ComponentDecl {
                program: None,
                exposes: vec![ExposeDecl::LegacyService(ExposeLegacyServiceDecl {
                    source: ExposeSource::Child("h".to_string()),
                    source_path: CapabilityPath::try_from("/svc/foo_from_h").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/foo_from_h").unwrap(),
                })],
                children: vec![ChildDecl {
                    name: "h".to_string(),
                    url: "test:///h".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "h",
            ComponentDecl {
                exposes: vec![ExposeDecl::LegacyService(ExposeLegacyServiceDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/foo_from_h").unwrap(),
                })],
                ..default_component_decl()
            },
        ),
    ];
    let framework_services = Box::new(MockFrameworkServiceHost::new());
    let test = RoutingTest::new("a", components, framework_services);
    test.check_use(
        vec!["b", "e"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: true },
    ).await;
    test.check_use(
        vec!["b", "e"].into(),
        CheckUse::LegacyService { path: default_service_capability(), should_succeed: true },
    ).await;
    test.check_use(
        vec!["c", "f"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: true },
    ).await;
    test.check_use(
        vec!["c", "f"].into(),
        CheckUse::LegacyService { path: default_service_capability(), should_succeed: true },
    ).await;
}

///  component manager's namespace
///   |
///   a
///    \
///     b
///
/// a: offers directory /hippo/data/foo from realm as /foo
/// a: offers service /hippo/svc/foo from realm as /echo/echo
/// b: uses directory /foo as /data/hippo
/// b: uses service /echo/echo as /svc/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_from_component_manager_namespace() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                offers: vec![
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferDirectorySource::Realm,
                        source_path: CapabilityPath::try_from("/hippo/data/foo").unwrap(),
                        target_path: CapabilityPath::try_from("/foo").unwrap(),
                        target: OfferTarget::Child("b".to_string()),
                    }),
                    OfferDecl::LegacyService(OfferLegacyServiceDecl {
                        source: OfferServiceSource::Realm,
                        source_path: CapabilityPath::try_from("/hippo/svc/foo").unwrap(),
                        target_path: CapabilityPath::try_from("/echo/echo").unwrap(),
                        target: OfferTarget::Child("b".to_string()),
                    }),
                ],
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                uses: vec![
                    UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/foo").unwrap(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    }),
                    UseDecl::LegacyService(UseLegacyServiceDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/echo/echo").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }),
                ],
                ..default_component_decl()
            },
        ),
    ];
    let framework_services = Box::new(MockFrameworkServiceHost::new());
    let test = RoutingTest::new("a", components, framework_services);
    test.install_hippo_dir();
    test.check_use(
        vec!["b"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: true },
    ).await;
    test.check_use(
        vec!["b"].into(),
        CheckUse::LegacyService { path: default_service_capability(), should_succeed: true },
    ).await;
}

///   a
///    \
///     b
///
/// b: uses directory /data/hippo as /data/hippo, but it's not in its realm
/// b: uses service /svc/hippo as /svc/hippo, but it's not in its realm
#[fuchsia_async::run_singlethreaded(test)]
async fn use_not_offered() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                program: None,
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                uses: vec![
                    UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    }),
                    UseDecl::LegacyService(UseLegacyServiceDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }),
                ],
                ..default_component_decl()
            },
        ),
    ];
    let framework_services = Box::new(MockFrameworkServiceHost::new());
    let test = RoutingTest::new("a", components, framework_services);
    test.check_use(
        vec!["b"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: false },
    ).await;
    test.check_use(
        vec!["b"].into(),
        CheckUse::LegacyService { path: default_service_capability(), should_succeed: false },
    ).await;
}

///   a
///  / \
/// b   c
///
/// a: offers directory /data/hippo from b as /data/hippo, but it's not exposed by b
/// a: offers service /svc/hippo from b as /svc/hippo, but it's not exposed by b
/// c: uses directory /data/hippo as /data/hippo
/// c: uses service /svc/hippo as /svc/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_offer_source_not_exposed() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                program: None,
                offers: vec![
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        source: OfferDirectorySource::Child("b".to_string()),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        target: OfferTarget::Child("c".to_string()),
                    }),
                    OfferDecl::LegacyService(OfferLegacyServiceDecl {
                        source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        source: OfferServiceSource::Child("b".to_string()),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        target: OfferTarget::Child("c".to_string()),
                    }),
                ],
                children: vec![
                    ChildDecl {
                        name: "b".to_string(),
                        url: "test:///b".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },
                    ChildDecl {
                        name: "c".to_string(),
                        url: "test:///c".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },
                ],
                ..default_component_decl()
            },
        ),
        ("b", default_component_decl()),
        (
            "c",
            ComponentDecl {
                uses: vec![
                    UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    }),
                    UseDecl::LegacyService(UseLegacyServiceDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }),
                ],
                ..default_component_decl()
            },
        ),
    ];
    let framework_services = Box::new(MockFrameworkServiceHost::new());
    let test = RoutingTest::new("a", components, framework_services);
    test.check_use(
        vec!["c"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: false },
    ).await;
    test.check_use(
        vec!["c"].into(),
        CheckUse::LegacyService { path: default_service_capability(), should_succeed: false },
    ).await;
}

///   a
///    \
///     b
///      \
///       c
///
/// b: offers directory /data/hippo from its realm as /data/hippo, but it's not offered by a
/// b: offers service /svc/hippo from its realm as /svc/hippo, but it's not offfered by a
/// c: uses directory /data/hippo as /data/hippo
/// c: uses service /svc/hippo as /svc/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_offer_source_not_offered() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                program: None,
                offers: vec![
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        source: OfferDirectorySource::Realm,
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        target: OfferTarget::Child("c".to_string()),
                    }),
                    OfferDecl::LegacyService(OfferLegacyServiceDecl {
                        source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        source: OfferServiceSource::Realm,
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        target: OfferTarget::Child("c".to_string()),
                    }),
                ],
                children: vec![ChildDecl {
                    name: "c".to_string(),
                    url: "test:///c".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "c",
            ComponentDecl {
                uses: vec![
                    UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    }),
                    UseDecl::LegacyService(UseLegacyServiceDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }),
                ],
                ..default_component_decl()
            },
        ),
    ];
    let framework_services = Box::new(MockFrameworkServiceHost::new());
    let test = RoutingTest::new("a", components, framework_services);
    test.check_use(
        vec!["b", "c"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: false },
    ).await;
    test.check_use(
        vec!["b", "c"].into(),
        CheckUse::LegacyService { path: default_service_capability(), should_succeed: false },
    ).await;
}

///   a
///    \
///     b
///      \
///       c
///
/// b: uses directory /data/hippo as /data/hippo, but it's exposed to it, not offered
/// b: uses service /svc/hippo as /svc/hippo, but it's exposed to it, not offered
/// c: exposes /data/hippo
/// c: exposes /svc/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_from_expose() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                program: None,
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                uses: vec![
                    UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    }),
                    UseDecl::LegacyService(UseLegacyServiceDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }),
                ],
                children: vec![ChildDecl {
                    name: "c".to_string(),
                    url: "test:///c".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "c",
            ComponentDecl {
                exposes: vec![
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        source: ExposeSource::Self_,
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    }),
                    ExposeDecl::LegacyService(ExposeLegacyServiceDecl {
                        source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        source: ExposeSource::Self_,
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }),
                ],
                ..default_component_decl()
            },
        ),
    ];
    let framework_services = Box::new(MockFrameworkServiceHost::new());
    let test = RoutingTest::new("a", components, framework_services);
    test.check_use(
        vec!["b"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: false },
    ).await;
    test.check_use(
        vec!["b"].into(),
        CheckUse::LegacyService { path: default_service_capability(), should_succeed: false },
    ).await;
}

///   a
///    \
///     b
///
/// a: offers directory /data/hippo to b, but a is not executable
/// a: offers service /svc/hippo to b, but a is not executable
/// b: uses directory /data/hippo as /data/hippo, but it's not in its realm
/// b: uses service /svc/hippo as /svc/hippo, but it's not in its realm
#[fuchsia_async::run_singlethreaded(test)]
async fn offer_from_non_executable() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                program: None,
                offers: vec![
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        source: OfferDirectorySource::Self_,
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        target: OfferTarget::Child("b".to_string()),
                    }),
                    OfferDecl::LegacyService(OfferLegacyServiceDecl {
                        source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        source: OfferServiceSource::Self_,
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        target: OfferTarget::Child("b".to_string()),
                    }),
                ],
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                uses: vec![
                    UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    }),
                    UseDecl::LegacyService(UseLegacyServiceDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }),
                ],
                ..default_component_decl()
            },
        ),
    ];
    let framework_services = Box::new(MockFrameworkServiceHost::new());
    let test = RoutingTest::new("a", components, framework_services);
    test.check_use(
        vec!["b"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: false },
    ).await;
    test.check_use(
        vec!["b"].into(),
        CheckUse::LegacyService { path: default_service_capability(), should_succeed: false },
    ).await;
}

///   a
///    \
///     b
///    / \
///  [c] [d]
/// a: offers service /svc/hippo to b
/// b: offers service /svc/hippo to collection, creates [c]
/// [c]: instance in collection uses service /svc/hippo
/// [d]: ditto, but with /data/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_in_collection() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                offers: vec![
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                        source: OfferDirectorySource::Self_,
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        target: OfferTarget::Child("b".to_string()),
                    }),
                    OfferDecl::LegacyService(OfferLegacyServiceDecl {
                        source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                        source: OfferServiceSource::Self_,
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        target: OfferTarget::Child("b".to_string()),
                    }),
                ],
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                uses: vec![UseDecl::LegacyService(UseLegacyServiceDecl {
                    source: UseSource::Framework,
                    source_path: CapabilityPath::try_from("/svc/fuchsia.sys2.Realm").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.Realm").unwrap(),
                })],
                offers: vec![
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        source: OfferDirectorySource::Realm,
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        target: OfferTarget::Collection("coll".to_string()),
                    }),
                    OfferDecl::LegacyService(OfferLegacyServiceDecl {
                        source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        source: OfferServiceSource::Realm,
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        target: OfferTarget::Collection("coll".to_string()),
                    }),
                ],
                collections: vec![CollectionDecl {
                    name: "coll".to_string(),
                    durability: fsys::Durability::Transient,
                }],
                ..default_component_decl()
            },
        ),
        (
            "c",
            ComponentDecl {
                uses: vec![UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                })],
                ..default_component_decl()
            },
        ),
        (
            "d",
            ComponentDecl {
                uses: vec![UseDecl::LegacyService(UseLegacyServiceDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                })],
                ..default_component_decl()
            },
        ),
    ];
    // `RealFrameworkServiceHost` is needed to create dynamic children.
    let framework_services = Box::new(RealFrameworkServiceHost::new());
    let test = RoutingTest::new("a", components, framework_services);
    test.create_dynamic_child(
        vec!["b"].into(),
        "coll",
        ChildDecl {
            name: "c".to_string(),
            url: "test:///c".to_string(),
            startup: fsys::StartupMode::Lazy,
        },
    ).await;
    test.create_dynamic_child(
        vec!["b"].into(),
        "coll",
        ChildDecl {
            name: "d".to_string(),
            url: "test:///d".to_string(),
            startup: fsys::StartupMode::Lazy,
        },
    ).await;
    test.check_use(
        vec!["b", "coll:c"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: true },
    ).await;
    test.check_use(
        vec!["b", "coll:d"].into(),
        CheckUse::LegacyService { path: default_service_capability(), should_succeed: true },
    ).await;
}

///   a
///    \
///     b
///      \
///      [c]
/// a: offers service /svc/hippo to b
/// b: creates [c]
/// [c]: tries to use /svc/hippo, but can't because service was not offered to its collection
#[fuchsia_async::run_singlethreaded(test)]
async fn use_in_collection_not_offered() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                offers: vec![
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                        source: OfferDirectorySource::Self_,
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        target: OfferTarget::Child("b".to_string()),
                    }),
                    OfferDecl::LegacyService(OfferLegacyServiceDecl {
                        source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                        source: OfferServiceSource::Self_,
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        target: OfferTarget::Child("b".to_string()),
                    }),
                ],
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                uses: vec![UseDecl::LegacyService(UseLegacyServiceDecl {
                    source: UseSource::Framework,
                    source_path: CapabilityPath::try_from("/svc/fuchsia.sys2.Realm").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.Realm").unwrap(),
                })],
                collections: vec![CollectionDecl {
                    name: "coll".to_string(),
                    durability: fsys::Durability::Transient,
                }],
                ..default_component_decl()
            },
        ),
        (
            "c",
            ComponentDecl {
                uses: vec![
                    UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    }),
                    UseDecl::LegacyService(UseLegacyServiceDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }),
                ],
                ..default_component_decl()
            },
        ),
    ];
    // `RealFrameworkServiceHost` is needed to create dynamic children.
    let framework_services = Box::new(RealFrameworkServiceHost::new());
    let test = RoutingTest::new("a", components, framework_services);
    test.create_dynamic_child(
        vec!["b"].into(),
        "coll",
        ChildDecl {
            name: "c".to_string(),
            url: "test:///c".to_string(),
            startup: fsys::StartupMode::Lazy,
        },
    ).await;
    test.check_use(
        vec!["b", "coll:c"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: false },
    ).await;
    test.check_use(
        vec!["b", "coll:c"].into(),
        CheckUse::LegacyService { path: default_service_capability(), should_succeed: false },
    ).await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn expose_from_self_and_child() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                exposes: vec![
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Child("c".to_string()),
                        source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        target_path: CapabilityPath::try_from("/data/bar/hippo").unwrap(),
                    }),
                    ExposeDecl::LegacyService(ExposeLegacyServiceDecl {
                        source: ExposeSource::Child("c".to_string()),
                        source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/bar/hippo").unwrap(),
                    }),
                ],
                children: vec![ChildDecl {
                    name: "c".to_string(),
                    url: "test:///c".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "c",
            ComponentDecl {
                exposes: vec![
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Self_,
                        source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    }),
                    ExposeDecl::LegacyService(ExposeLegacyServiceDecl {
                        source: ExposeSource::Self_,
                        source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }),
                ],
                ..default_component_decl()
            },
        ),
    ];
    let framework_services = Box::new(MockFrameworkServiceHost::new());
    let test = RoutingTest::new("a", components, framework_services);
    test.check_use_exposed_dir(
        vec!["b"].into(),
        CheckUse::Directory { path: "/data/bar/hippo".try_into().unwrap(), should_succeed: true },
    ).await;
    test.check_use_exposed_dir(
        vec!["b"].into(),
        CheckUse::LegacyService {
            path: "/svc/bar/hippo".try_into().unwrap(),
            should_succeed: true,
        },
    ).await;
    test.check_use_exposed_dir(
        vec!["b", "c"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: true },
    ).await;
    test.check_use_exposed_dir(
        vec!["b", "c"].into(),
        CheckUse::LegacyService { path: default_service_capability(), should_succeed: true },
    ).await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn use_not_exposed() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                children: vec![ChildDecl {
                    name: "c".to_string(),
                    url: "test:///c".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "c",
            ComponentDecl {
                exposes: vec![
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Self_,
                        source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    }),
                    ExposeDecl::LegacyService(ExposeLegacyServiceDecl {
                        source: ExposeSource::Self_,
                        source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }),
                ],
                ..default_component_decl()
            },
        ),
    ];
    let framework_services = Box::new(MockFrameworkServiceHost::new());
    let test = RoutingTest::new("a", components, framework_services);
    // Capability is only exposed from "c", so it only be usable from there.
    test.check_use_exposed_dir(
        vec!["b"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: false },
    ).await;
    test.check_use_exposed_dir(
        vec!["b"].into(),
        CheckUse::LegacyService { path: default_service_capability(), should_succeed: false },
    ).await;
    test.check_use_exposed_dir(
        vec!["b", "c"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: true },
    ).await;
    test.check_use_exposed_dir(
        vec!["b", "c"].into(),
        CheckUse::LegacyService { path: default_service_capability(), should_succeed: true },
    ).await;
}
