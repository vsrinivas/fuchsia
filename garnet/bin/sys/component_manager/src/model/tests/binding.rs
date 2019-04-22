// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::tests::mocks::*,
    crate::model::tests::routing_test_helpers::*,
    crate::model::*,
    cm_rust::{self, ChildDecl, ComponentDecl},
    fidl_fuchsia_sys2 as fsys,
    futures::lock::Mutex,
    std::{collections::HashSet, sync::Arc},
};

fn get_children(realm: &Realm) -> HashSet<ChildMoniker> {
    realm.instance.child_realms.as_ref().unwrap().keys().map(|m| m.clone()).collect()
}

fn get_child_realm(realm: &Realm, child: &str) -> Arc<Mutex<Realm>> {
    realm.instance.child_realms.as_ref().unwrap()[&ChildMoniker::new(child.to_string())].clone()
}

#[fuchsia_async::run_singlethreaded(test)]
async fn bind_instance_root() {
    let mut resolver = ResolverRegistry::new();
    let runner = MockRunner::new();
    let uris_run = runner.uris_run.clone();
    let mut mock_resolver = MockResolver::new();
    mock_resolver.add_component("root", default_component_decl());
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        root_component_uri: "test:///root".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });
    let res = await!(model.look_up_and_bind_instance(AbsoluteMoniker::root()));
    let expected_res: Result<(), ModelError> = Ok(());
    assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
    let actual_uris = await!(uris_run.lock());
    let expected_uris = vec!["test:///root_resolved".to_string()];
    assert_eq!(*actual_uris, expected_uris);
    let root_realm = await!(model.root_realm.lock());
    let actual_children = get_children(&root_realm);
    assert!(actual_children.is_empty());
}

#[fuchsia_async::run_singlethreaded(test)]
async fn bind_instance_root_non_existent() {
    let mut resolver = ResolverRegistry::new();
    let runner = MockRunner::new();
    let uris_run = runner.uris_run.clone();
    let mut mock_resolver = MockResolver::new();
    mock_resolver.add_component("root", default_component_decl());
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        root_component_uri: "test:///root".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });
    let res =
        await!(model.look_up_and_bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new(
            "no-such-instance".to_string()
        )])));
    let expected_res: Result<(), ModelError> = Err(ModelError::instance_not_found(
        AbsoluteMoniker::new(vec![ChildMoniker::new("no-such-instance".to_string())]),
    ));
    assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
    let actual_uris = await!(uris_run.lock());
    let expected_uris: Vec<String> = vec![];
    assert_eq!(*actual_uris, expected_uris);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn bind_instance_child() {
    let mut resolver = ResolverRegistry::new();
    let runner = MockRunner::new();
    let uris_run = runner.uris_run.clone();
    let mut mock_resolver = MockResolver::new();
    mock_resolver.add_component(
        "root",
        ComponentDecl {
            children: vec![
                ChildDecl {
                    name: "system".to_string(),
                    uri: "test:///system".to_string(),
                    startup: fsys::StartupMode::Lazy,
                },
                ChildDecl {
                    name: "echo".to_string(),
                    uri: "test:///echo".to_string(),
                    startup: fsys::StartupMode::Lazy,
                },
            ],
            ..default_component_decl()
        },
    );
    mock_resolver.add_component("system", default_component_decl());
    mock_resolver.add_component("echo", default_component_decl());
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        root_component_uri: "test:///root".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });
    // bind to system
    assert!(await!(model.look_up_and_bind_instance(AbsoluteMoniker::new(vec!["system"]))).is_ok());
    let expected_uris = vec!["test:///system_resolved".to_string()];
    assert_eq!(*await!(uris_run.lock()), expected_uris);

    // Validate children. system is resolved, but not echo.
    let actual_children = get_children(&*await!(model.root_realm.lock()));
    let mut expected_children: HashSet<ChildMoniker> = HashSet::new();
    expected_children.insert(ChildMoniker::new("system".to_string()));
    expected_children.insert(ChildMoniker::new("echo".to_string()));
    assert_eq!(actual_children, expected_children);

    let system_realm = get_child_realm(&*await!(model.root_realm.lock()), "system");
    let echo_realm = get_child_realm(&*await!(model.root_realm.lock()), "echo");
    let actual_children = get_children(&*await!(system_realm.lock()));
    assert!(actual_children.is_empty());
    assert!(await!(echo_realm.lock()).instance.child_realms.is_none());
    // bind to echo
    assert!(await!(model.look_up_and_bind_instance(AbsoluteMoniker::new(vec!["echo"]))).is_ok());
    let expected_uris =
        vec!["test:///system_resolved".to_string(), "test:///echo_resolved".to_string()];
    assert_eq!(*await!(uris_run.lock()), expected_uris);

    // Validate children. Now echo is resolved.
    let echo_realm = get_child_realm(&*await!(model.root_realm.lock()), "echo");
    let actual_children = get_children(&*await!(echo_realm.lock()));
    assert!(actual_children.is_empty());
}

#[fuchsia_async::run_singlethreaded(test)]
async fn bind_instance_child_non_existent() {
    let mut resolver = ResolverRegistry::new();
    let runner = MockRunner::new();
    let uris_run = runner.uris_run.clone();
    let mut mock_resolver = MockResolver::new();
    mock_resolver.add_component(
        "root",
        ComponentDecl {
            children: vec![ChildDecl {
                name: "system".to_string(),
                uri: "test:///system".to_string(),
                startup: fsys::StartupMode::Lazy,
            }],
            ..default_component_decl()
        },
    );
    mock_resolver.add_component("system", default_component_decl());
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        root_component_uri: "test:///root".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });
    // bind to system
    assert!(await!(model.look_up_and_bind_instance(AbsoluteMoniker::new(vec!["system"]))).is_ok());
    let expected_uris = vec!["test:///system_resolved".to_string()];
    assert_eq!(*await!(uris_run.lock()), expected_uris);

    // can't bind to logger: it does not exist
    let moniker = AbsoluteMoniker::new(vec!["system", "logger"]);
    let res = await!(model.look_up_and_bind_instance(moniker.clone()));
    let expected_res: Result<(), ModelError> = Err(ModelError::instance_not_found(moniker));
    assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
    let actual_uris = await!(uris_run.lock());
    let expected_uris = vec!["test:///system_resolved".to_string()];
    assert_eq!(*actual_uris, expected_uris);
}

/// Create a hierarchy of children:
///
///   a
///  / \
/// b   c
///      \
///       d
///        \
///         e
///
/// `b`, `c`, and `d` are started eagerly. `a` and `e` are lazy.
#[fuchsia_async::run_singlethreaded(test)]
async fn bind_instance_eager_children() {
    let mut resolver = ResolverRegistry::new();
    let runner = MockRunner::new();
    let uris_run = runner.uris_run.clone();
    let mut mock_resolver = MockResolver::new();
    mock_resolver.add_component(
        "root",
        ComponentDecl {
            children: vec![ChildDecl {
                name: "a".to_string(),
                uri: "test:///a".to_string(),
                startup: fsys::StartupMode::Lazy,
            }],
            ..default_component_decl()
        },
    );
    mock_resolver.add_component(
        "a",
        ComponentDecl {
            children: vec![
                ChildDecl {
                    name: "b".to_string(),
                    uri: "test:///b".to_string(),
                    startup: fsys::StartupMode::Eager,
                },
                ChildDecl {
                    name: "c".to_string(),
                    uri: "test:///c".to_string(),
                    startup: fsys::StartupMode::Eager,
                },
            ],
            ..default_component_decl()
        },
    );
    mock_resolver.add_component("b", default_component_decl());
    mock_resolver.add_component(
        "c",
        ComponentDecl {
            children: vec![ChildDecl {
                name: "d".to_string(),
                uri: "test:///d".to_string(),
                startup: fsys::StartupMode::Eager,
            }],
            ..default_component_decl()
        },
    );
    mock_resolver.add_component(
        "d",
        ComponentDecl {
            children: vec![ChildDecl {
                name: "e".to_string(),
                uri: "test:///e".to_string(),
                startup: fsys::StartupMode::Lazy,
            }],
            ..default_component_decl()
        },
    );
    mock_resolver.add_component("e", default_component_decl());
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        root_component_uri: "test:///root".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });

    // Bind to the top component, and check that it and the eager components were started.
    {
        let res =
            await!(model.look_up_and_bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new(
                "a".to_string()
            ),])));
        let expected_res: Result<(), ModelError> = Ok(());
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
        let actual_uris = await!(uris_run.lock());
        // Execution order of `b` and `c` is non-deterministic.
        let expected_uris1 = vec![
            "test:///a_resolved".to_string(),
            "test:///b_resolved".to_string(),
            "test:///c_resolved".to_string(),
            "test:///d_resolved".to_string(),
        ];
        let expected_uris2 = vec![
            "test:///a_resolved".to_string(),
            "test:///c_resolved".to_string(),
            "test:///b_resolved".to_string(),
            "test:///d_resolved".to_string(),
        ];
        assert!(
            *actual_uris == expected_uris1 || *actual_uris == expected_uris2,
            "uris_run failed to match: {:?}",
            *actual_uris
        );
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn bind_instance_no_execute() {
    // Create a non-executable component with an eagerly-started child.
    let mut resolver = ResolverRegistry::new();
    let runner = MockRunner::new();
    let uris_run = runner.uris_run.clone();
    let mut mock_resolver = MockResolver::new();
    mock_resolver.add_component(
        "root",
        ComponentDecl {
            children: vec![ChildDecl {
                name: "a".to_string(),
                uri: "test:///a".to_string(),
                startup: fsys::StartupMode::Lazy,
            }],
            ..default_component_decl()
        },
    );
    mock_resolver.add_component(
        "a",
        ComponentDecl {
            program: None,
            children: vec![ChildDecl {
                name: "b".to_string(),
                uri: "test:///b".to_string(),
                startup: fsys::StartupMode::Eager,
            }],
            ..default_component_decl()
        },
    );
    mock_resolver.add_component("b", default_component_decl());
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        root_component_uri: "test:///root".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });

    // Bind to the parent component. The child should be started. However, the parent component
    // is non-executable so it is not run.
    assert!(await!(model.look_up_and_bind_instance(AbsoluteMoniker::new(vec!["a"]))).is_ok());
    let actual_uris = await!(uris_run.lock());
    let expected_uris = vec!["test:///b_resolved".to_string()];
    assert_eq!(*actual_uris, expected_uris);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn bind_instance_recursive_child() {
    let mut resolver = ResolverRegistry::new();
    let runner = MockRunner::new();
    let uris_run = runner.uris_run.clone();
    let mut mock_resolver = MockResolver::new();
    mock_resolver.add_component(
        "root",
        ComponentDecl {
            children: vec![ChildDecl {
                name: "system".to_string(),
                uri: "test:///system".to_string(),
                startup: fsys::StartupMode::Lazy,
            }],
            ..default_component_decl()
        },
    );
    mock_resolver.add_component(
        "system",
        ComponentDecl {
            children: vec![
                ChildDecl {
                    name: "logger".to_string(),
                    uri: "test:///logger".to_string(),
                    startup: fsys::StartupMode::Lazy,
                },
                ChildDecl {
                    name: "netstack".to_string(),
                    uri: "test:///netstack".to_string(),
                    startup: fsys::StartupMode::Lazy,
                },
            ],
            ..default_component_decl()
        },
    );
    mock_resolver.add_component("logger", default_component_decl());
    mock_resolver.add_component("netstack", default_component_decl());
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        root_component_uri: "test:///root".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });

    // bind to logger (before ever binding to system)
    assert!(
        await!(model.look_up_and_bind_instance(AbsoluteMoniker::new(vec!["system", "logger"])))
            .is_ok()
    );
    let expected_uris = vec!["test:///logger_resolved".to_string()];
    assert_eq!(*await!(uris_run.lock()), expected_uris);

    // bind to netstack
    assert!(await!(
        model.look_up_and_bind_instance(AbsoluteMoniker::new(vec!["system", "netstack"]))
    )
    .is_ok());
    let expected_uris =
        vec!["test:///logger_resolved".to_string(), "test:///netstack_resolved".to_string()];
    assert_eq!(*await!(uris_run.lock()), expected_uris);

    // finally, bind to system
    assert!(await!(model.look_up_and_bind_instance(AbsoluteMoniker::new(vec!["system"]))).is_ok());
    let expected_uris = vec![
        "test:///logger_resolved".to_string(),
        "test:///netstack_resolved".to_string(),
        "test:///system_resolved".to_string(),
    ];
    assert_eq!(*await!(uris_run.lock()), expected_uris);

    // validate children
    let actual_children = get_children(&*await!(model.root_realm.lock()));
    let mut expected_children: HashSet<ChildMoniker> = HashSet::new();
    expected_children.insert(ChildMoniker::new("system".to_string()));
    assert_eq!(actual_children, expected_children);

    let system_realm = get_child_realm(&*await!(model.root_realm.lock()), "system");

    let actual_children = get_children(&*await!(system_realm.lock()));
    let mut expected_children: HashSet<ChildMoniker> = HashSet::new();
    expected_children.insert(ChildMoniker::new("logger".to_string()));
    expected_children.insert(ChildMoniker::new("netstack".to_string()));
    assert_eq!(actual_children, expected_children);

    let logger_realm = get_child_realm(&*await!(system_realm.lock()), "logger");
    let actual_children = get_children(&*await!(logger_realm.lock()));
    assert!(actual_children.is_empty());

    let netstack_realm = get_child_realm(&*await!(system_realm.lock()), "netstack");
    let actual_children = get_children(&*await!(netstack_realm.lock()));
    assert!(actual_children.is_empty());
}
