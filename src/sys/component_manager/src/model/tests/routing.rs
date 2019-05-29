// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::tests::routing_test_helpers::*,
    crate::model::*,
    cm_rust::{
        self, CapabilityPath, ChildDecl, ComponentDecl, ExposeDecl, ExposeDirectoryDecl,
        ExposeServiceDecl, ExposeSource, OfferDecl, OfferDest, OfferDirectoryDecl,
        OfferServiceDecl, OfferSource, OfferTarget, UseDecl, UseDirectoryDecl, UseServiceDecl,
    },
    fidl_fuchsia_sys2 as fsys,
    std::convert::TryFrom,
};

///   a
///    \
///     b
///
/// b: uses ambient service /svc/fuchsia.sys2.Realm
#[fuchsia_async::run_singlethreaded(test)]
async fn use_ambient() {
    await!(run_routing_test(TestInputs {
        root_component: "a",
        users_to_check: vec![(AbsoluteMoniker::new(vec!["b"]), new_ambient_capability(), true),],
        components: vec![
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
                    uses: vec![UseDecl::Service(UseServiceDecl {
                        source_path: CapabilityPath::try_from("/svc/fuchsia.sys2.Realm").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.Realm").unwrap(),
                    }),],
                    ..default_component_decl()
                },
            ),
        ],
    }));
}

///   a
///    \
///     b
///
/// a: offers directory /data/foo from self as /data/bar
/// a: offers service /svc/foo from self as /svc/bar
/// b: uses directory /data/bar as /data/hippo
/// b: uses service /svc/bar as /svc/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_from_parent() {
    await!(run_routing_test(TestInputs {
        root_component: "a",
        users_to_check: vec![
            (AbsoluteMoniker::new(vec!["b"]), new_directory_capability(), true),
            (AbsoluteMoniker::new(vec!["b"]), new_service_capability(), true),
        ],
        components: vec![
            (
                "a",
                ComponentDecl {
                    offers: vec![
                        OfferDecl::Directory(OfferDirectoryDecl {
                            source: OfferSource::Myself,
                            source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/data/bar").unwrap(),
                                dest: OfferDest::Child("b".to_string()),
                            }],
                        }),
                        OfferDecl::Service(OfferServiceDecl {
                            source: OfferSource::Myself,
                            source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                                dest: OfferDest::Child("b".to_string()),
                            }],
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
                            source_path: CapabilityPath::try_from("/data/bar").unwrap(),
                            target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        }),
                        UseDecl::Service(UseServiceDecl {
                            source_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                            target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        }),
                    ],
                    ..default_component_decl()
                },
            ),
        ],
    }));
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
    await!(run_routing_test(TestInputs {
        root_component: "a",
        users_to_check: vec![
            (AbsoluteMoniker::new(vec!["b", "c"]), new_directory_capability(), true),
            (AbsoluteMoniker::new(vec!["b", "c"]), new_service_capability(), true),
        ],
        components: vec![
            (
                "a",
                ComponentDecl {
                    offers: vec![
                        OfferDecl::Directory(OfferDirectoryDecl {
                            source: OfferSource::Myself,
                            source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/data/bar").unwrap(),
                                dest: OfferDest::Child("b".to_string()),
                            }],
                        }),
                        OfferDecl::Service(OfferServiceDecl {
                            source: OfferSource::Myself,
                            source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                                dest: OfferDest::Child("b".to_string()),
                            }],
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
                            source: OfferSource::Realm,
                            source_path: CapabilityPath::try_from("/data/bar").unwrap(),
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/data/baz").unwrap(),
                                dest: OfferDest::Child("c".to_string()),
                            }],
                        }),
                        OfferDecl::Service(OfferServiceDecl {
                            source: OfferSource::Realm,
                            source_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                                dest: OfferDest::Child("c".to_string()),
                            }],
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
                            source_path: CapabilityPath::try_from("/data/baz").unwrap(),
                            target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        }),
                        UseDecl::Service(UseServiceDecl {
                            source_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                            target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        }),
                    ],
                    ..default_component_decl()
                },
            ),
        ],
    }));
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
    await!(run_routing_test(TestInputs {
        root_component: "a",
        users_to_check: vec![
            (AbsoluteMoniker::new(vec!["b", "c"]), new_directory_capability(), true),
            (AbsoluteMoniker::new(vec!["b", "c"]), new_service_capability(), true),
        ],
        components: vec![
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
                            source: OfferSource::Child("d".to_string()),
                            source_path: CapabilityPath::try_from("/data/bar").unwrap(),
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/data/foobar").unwrap(),
                                dest: OfferDest::Child("c".to_string()),
                            }],
                        }),
                        OfferDecl::Service(OfferServiceDecl {
                            source: OfferSource::Child("d".to_string()),
                            source_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/svc/foobar").unwrap(),
                                dest: OfferDest::Child("c".to_string()),
                            }],
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
                            source_path: CapabilityPath::try_from("/data/foobar").unwrap(),
                            target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        }),
                        UseDecl::Service(UseServiceDecl {
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
                            source: ExposeSource::Myself,
                            source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                            target_path: CapabilityPath::try_from("/data/bar").unwrap(),
                        }),
                        ExposeDecl::Service(ExposeServiceDecl {
                            source: ExposeSource::Myself,
                            source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                            target_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                        }),
                    ],
                    ..default_component_decl()
                },
            ),
        ],
    }));
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
    await!(run_routing_test(TestInputs {
        root_component: "a",
        users_to_check: vec![
            (AbsoluteMoniker::new(vec!["c"]), new_directory_capability(), true),
            (AbsoluteMoniker::new(vec!["c"]), new_service_capability(), true),
        ],
        components: vec![
            (
                "a",
                ComponentDecl {
                    offers: vec![
                        OfferDecl::Directory(OfferDirectoryDecl {
                            source: OfferSource::Child("b".to_string()),
                            source_path: CapabilityPath::try_from("/data/bar").unwrap(),
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/data/baz").unwrap(),
                                dest: OfferDest::Child("c".to_string()),
                            }],
                        }),
                        OfferDecl::Service(OfferServiceDecl {
                            source: OfferSource::Child("b".to_string()),
                            source_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                                dest: OfferDest::Child("c".to_string()),
                            }],
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
                            source: ExposeSource::Myself,
                            source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                            target_path: CapabilityPath::try_from("/data/bar").unwrap(),
                        }),
                        ExposeDecl::Service(ExposeServiceDecl {
                            source: ExposeSource::Myself,
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
                            source_path: CapabilityPath::try_from("/data/baz").unwrap(),
                            target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        }),
                        UseDecl::Service(UseServiceDecl {
                            source_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                            target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        }),
                    ],
                    ..default_component_decl()
                },
            ),
        ],
    }));
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
    await!(run_routing_test(TestInputs {
        root_component: "a",
        users_to_check: vec![
            (AbsoluteMoniker::new(vec!["c"]), new_directory_capability(), true),
            (AbsoluteMoniker::new(vec!["c"]), new_service_capability(), true),
        ],
        components: vec![
            (
                "a",
                ComponentDecl {
                    offers: vec![
                        OfferDecl::Directory(OfferDirectoryDecl {
                            source: OfferSource::Child("b".to_string()),
                            source_path: CapabilityPath::try_from("/data/baz").unwrap(),
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/data/foobar").unwrap(),
                                dest: OfferDest::Child("c".to_string()),
                            }],
                        }),
                        OfferDecl::Service(OfferServiceDecl {
                            source: OfferSource::Child("b".to_string()),
                            source_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/svc/foobar").unwrap(),
                                dest: OfferDest::Child("c".to_string()),
                            }],
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
                        ExposeDecl::Service(ExposeServiceDecl {
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
                            source_path: CapabilityPath::try_from("/data/foobar").unwrap(),
                            target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        }),
                        UseDecl::Service(UseServiceDecl {
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
                            source: ExposeSource::Myself,
                            source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                            target_path: CapabilityPath::try_from("/data/bar").unwrap(),
                        }),
                        ExposeDecl::Service(ExposeServiceDecl {
                            source: ExposeSource::Myself,
                            source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                            target_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                        }),
                    ],
                    ..default_component_decl()
                },
            ),
        ],
    }));
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
    await!(run_routing_test(TestInputs {
        root_component: "a",
        users_to_check: vec![
            (AbsoluteMoniker::new(vec!["b", "e"]), new_directory_capability(), true),
            (AbsoluteMoniker::new(vec!["b", "e"]), new_service_capability(), true),
            (AbsoluteMoniker::new(vec!["c", "f"]), new_directory_capability(), true),
            (AbsoluteMoniker::new(vec!["c", "f"]), new_service_capability(), true),
        ],
        components: vec![
            (
                "a",
                ComponentDecl {
                    offers: vec![
                        OfferDecl::Service(OfferServiceDecl {
                            source: OfferSource::Myself,
                            source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/svc/foo_from_a").unwrap(),
                                dest: OfferDest::Child("b".to_string()),
                            }],
                        }),
                        OfferDecl::Directory(OfferDirectoryDecl {
                            source: OfferSource::Child("b".to_string()),
                            source_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                                dest: OfferDest::Child("c".to_string()),
                            }],
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
                            source: OfferSource::Child("d".to_string()),
                            source_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                                dest: OfferDest::Child("e".to_string()),
                            }],
                        }),
                        OfferDecl::Service(OfferServiceDecl {
                            source: OfferSource::Realm,
                            source_path: CapabilityPath::try_from("/svc/foo_from_a").unwrap(),
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/svc/foo_from_a").unwrap(),
                                dest: OfferDest::Child("e".to_string()),
                            }],
                        }),
                    ],
                    exposes: vec![ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Child("d".to_string()),
                        source_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                        target_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                    }),],
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
                            source: OfferSource::Realm,
                            source_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                                dest: OfferDest::Child("f".to_string()),
                            }],
                        }),
                        OfferDecl::Service(OfferServiceDecl {
                            source: OfferSource::Child("g".to_string()),
                            source_path: CapabilityPath::try_from("/svc/foo_from_h").unwrap(),
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/svc/foo_from_h").unwrap(),
                                dest: OfferDest::Child("f".to_string()),
                            }],
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
                        source: ExposeSource::Myself,
                        source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                        target_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                    }),],
                    ..default_component_decl()
                },
            ),
            (
                "e",
                ComponentDecl {
                    uses: vec![
                        UseDecl::Directory(UseDirectoryDecl {
                            source_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                            target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        }),
                        UseDecl::Service(UseServiceDecl {
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
                            source_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                            target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        }),
                        UseDecl::Service(UseServiceDecl {
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
                    exposes: vec![ExposeDecl::Service(ExposeServiceDecl {
                        source: ExposeSource::Child("h".to_string()),
                        source_path: CapabilityPath::try_from("/svc/foo_from_h").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/foo_from_h").unwrap(),
                    }),],
                    children: vec![ChildDecl {
                        name: "h".to_string(),
                        url: "test:///h".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },],
                    ..default_component_decl()
                },
            ),
            (
                "h",
                ComponentDecl {
                    exposes: vec![ExposeDecl::Service(ExposeServiceDecl {
                        source: ExposeSource::Myself,
                        source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/foo_from_h").unwrap(),
                    }),],
                    ..default_component_decl()
                },
            ),
        ],
    }));
}

///  component manager's namespace
///   |
///   a
///    \
///     b
///
/// a: offers directory /hippo/data/foo from self as /foo
/// a: offers service /svc/fidl.examples.echo.Echo from self as /echo/echo
/// b: uses directory /foo as /data/hippo
/// b: uses service /echo/echo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_from_component_manager_namespace() {
    install_hippo_dir();
    await!(run_routing_test(TestInputs {
        root_component: "a",
        users_to_check: vec![
            (AbsoluteMoniker::new(vec!["b"]), new_directory_capability(), true),
            (AbsoluteMoniker::new(vec!["b"]), new_service_capability(), true),
        ],
        components: vec![
            (
                "a",
                ComponentDecl {
                    offers: vec![
                        OfferDecl::Directory(OfferDirectoryDecl {
                            source: OfferSource::Realm,
                            source_path: CapabilityPath::try_from("/hippo/data/foo").unwrap(),
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/foo").unwrap(),
                                dest: OfferDest::Child("b".to_string()),
                            }],
                        }),
                        OfferDecl::Service(OfferServiceDecl {
                            source: OfferSource::Realm,
                            source_path: CapabilityPath::try_from("/svc/fidl.examples.echo.Echo")
                                .unwrap(),
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/echo/echo").unwrap(),
                                dest: OfferDest::Child("b".to_string()),
                            }],
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
                            source_path: CapabilityPath::try_from("/foo").unwrap(),
                            target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        }),
                        UseDecl::Service(UseServiceDecl {
                            source_path: CapabilityPath::try_from("/echo/echo").unwrap(),
                            target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        }),
                    ],
                    ..default_component_decl()
                },
            ),
        ],
    }));
}

///   a
///    \
///     b
///
/// b: uses directory /data/hippo as /data/hippo, but it's not in its realm
/// b: uses service /svc/hippo as /svc/hippo, but it's not in its realm
#[fuchsia_async::run_singlethreaded(test)]
async fn use_not_offered() {
    await!(run_routing_test(TestInputs {
        root_component: "a",
        users_to_check: vec![
            (AbsoluteMoniker::new(vec!["b"]), new_directory_capability(), false),
            (AbsoluteMoniker::new(vec!["b"]), new_service_capability(), false),
        ],
        components: vec![
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
                            source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                            target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        }),
                        UseDecl::Service(UseServiceDecl {
                            source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                            target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        }),
                    ],
                    ..default_component_decl()
                },
            ),
        ],
    }));
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
    await!(run_routing_test(TestInputs {
        root_component: "a",
        users_to_check: vec![
            (AbsoluteMoniker::new(vec!["c"]), new_directory_capability(), false),
            (AbsoluteMoniker::new(vec!["c"]), new_service_capability(), false),
        ],
        components: vec![
            (
                "a",
                ComponentDecl {
                    program: None,
                    offers: vec![
                        OfferDecl::Directory(OfferDirectoryDecl {
                            source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                            source: OfferSource::Child("b".to_string()),
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                                dest: OfferDest::Child("c".to_string()),
                            }],
                        }),
                        OfferDecl::Service(OfferServiceDecl {
                            source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                            source: OfferSource::Child("b".to_string()),
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                                dest: OfferDest::Child("c".to_string()),
                            }],
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
                            source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                            target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        }),
                        UseDecl::Service(UseServiceDecl {
                            source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                            target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        }),
                    ],
                    ..default_component_decl()
                },
            ),
        ],
    }));
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
    await!(run_routing_test(TestInputs {
        root_component: "a",
        users_to_check: vec![
            (AbsoluteMoniker::new(vec!["b", "c"]), new_directory_capability(), false),
            (AbsoluteMoniker::new(vec!["b", "c"]), new_service_capability(), false),
        ],
        components: vec![
            (
                "a",
                ComponentDecl {
                    children: vec![ChildDecl {
                        name: "b".to_string(),
                        url: "test:///b".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },],
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
                            source: OfferSource::Realm,
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                                dest: OfferDest::Child("c".to_string()),
                            }],
                        }),
                        OfferDecl::Service(OfferServiceDecl {
                            source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                            source: OfferSource::Realm,
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                                dest: OfferDest::Child("c".to_string()),
                            }],
                        }),
                    ],
                    children: vec![ChildDecl {
                        name: "c".to_string(),
                        url: "test:///c".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },],
                    ..default_component_decl()
                },
            ),
            (
                "c",
                ComponentDecl {
                    uses: vec![
                        UseDecl::Directory(UseDirectoryDecl {
                            source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                            target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        }),
                        UseDecl::Service(UseServiceDecl {
                            source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                            target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        }),
                    ],
                    ..default_component_decl()
                },
            ),
        ],
    }));
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
    await!(run_routing_test(TestInputs {
        root_component: "a",
        users_to_check: vec![
            (AbsoluteMoniker::new(vec!["b"]), new_directory_capability(), false),
            (AbsoluteMoniker::new(vec!["b"]), new_service_capability(), false),
        ],
        components: vec![
            (
                "a",
                ComponentDecl {
                    program: None,
                    children: vec![ChildDecl {
                        name: "b".to_string(),
                        url: "test:///b".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },],
                    ..default_component_decl()
                },
            ),
            (
                "b",
                ComponentDecl {
                    uses: vec![
                        UseDecl::Directory(UseDirectoryDecl {
                            source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                            target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        }),
                        UseDecl::Service(UseServiceDecl {
                            source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                            target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        }),
                    ],
                    children: vec![ChildDecl {
                        name: "c".to_string(),
                        url: "test:///c".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },],
                    ..default_component_decl()
                },
            ),
            (
                "c",
                ComponentDecl {
                    exposes: vec![
                        ExposeDecl::Directory(ExposeDirectoryDecl {
                            source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                            source: ExposeSource::Myself,
                            target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        }),
                        ExposeDecl::Service(ExposeServiceDecl {
                            source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                            source: ExposeSource::Myself,
                            target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        }),
                    ],
                    ..default_component_decl()
                },
            ),
        ],
    }));
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
    await!(run_routing_test(TestInputs {
        root_component: "a",
        users_to_check: vec![
            (AbsoluteMoniker::new(vec!["b"]), new_directory_capability(), false),
            (AbsoluteMoniker::new(vec!["b"]), new_service_capability(), false),
        ],
        components: vec![
            (
                "a",
                ComponentDecl {
                    program: None,
                    offers: vec![
                        OfferDecl::Directory(OfferDirectoryDecl {
                            source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                            source: OfferSource::Myself,
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                                dest: OfferDest::Child("b".to_string()),
                            }],
                        }),
                        OfferDecl::Service(OfferServiceDecl {
                            source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                            source: OfferSource::Myself,
                            targets: vec![OfferTarget {
                                target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                                dest: OfferDest::Child("b".to_string()),
                            }],
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
                            source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                            target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        }),
                        UseDecl::Service(UseServiceDecl {
                            source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                            target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        }),
                    ],
                    ..default_component_decl()
                },
            ),
        ],
    }));
}
