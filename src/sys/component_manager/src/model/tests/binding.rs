// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::tests::mocks::*,
    crate::model::tests::routing_test_helpers::*,
    crate::model::*,
    cm_rust::{self, ChildDecl, ComponentDecl},
    fidl_fuchsia_sys2 as fsys,
    std::{collections::HashSet, sync::Arc},
};

async fn get_children(realm: &Realm) -> HashSet<ChildMoniker> {
    await!(realm.instance.state.lock()).child_realms.as_ref().unwrap().keys().cloned().collect()
}

async fn get_child_realm<'a>(realm: &'a Realm, child: &'a str) -> Arc<Realm> {
    await!(realm.instance.state.lock()).child_realms.as_ref().unwrap()[&child.into()].clone()
}

#[fuchsia_async::run_singlethreaded(test)]
async fn bind_instance_root() {
    let mut resolver = ResolverRegistry::new();
    let runner = MockRunner::new();
    let urls_run = runner.urls_run.clone();
    let mut mock_resolver = MockResolver::new();
    mock_resolver.add_component("root", default_component_decl());
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        ambient: Box::new(MockAmbientEnvironment::new()),
        root_component_url: "test:///root".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });
    let res = await!(model.look_up_and_bind_instance(AbsoluteMoniker::root()));
    let expected_res: Result<(), ModelError> = Ok(());
    assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
    let actual_urls = await!(urls_run.lock());
    let expected_urls = vec!["test:///root_resolved".to_string()];
    assert_eq!(*actual_urls, expected_urls);
    let actual_children = await!(get_children(&model.root_realm));
    assert!(actual_children.is_empty());
}

#[fuchsia_async::run_singlethreaded(test)]
async fn bind_instance_root_non_existent() {
    let mut resolver = ResolverRegistry::new();
    let runner = MockRunner::new();
    let urls_run = runner.urls_run.clone();
    let mut mock_resolver = MockResolver::new();
    mock_resolver.add_component("root", default_component_decl());
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        ambient: Box::new(MockAmbientEnvironment::new()),
        root_component_url: "test:///root".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });
    let res = await!(model.look_up_and_bind_instance(vec!["no-such-instance"].into()));
    let expected_res: Result<(), ModelError> =
        Err(ModelError::instance_not_found(vec!["no-such-instance"].into()));
    assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
    let actual_urls = await!(urls_run.lock());
    let expected_urls: Vec<String> = vec![];
    assert_eq!(*actual_urls, expected_urls);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn bind_instance_child() {
    let mut resolver = ResolverRegistry::new();
    let runner = MockRunner::new();
    let urls_run = runner.urls_run.clone();
    let mut mock_resolver = MockResolver::new();
    mock_resolver.add_component(
        "root",
        ComponentDecl {
            children: vec![
                ChildDecl {
                    name: "system".to_string(),
                    url: "test:///system".to_string(),
                    startup: fsys::StartupMode::Lazy,
                },
                ChildDecl {
                    name: "echo".to_string(),
                    url: "test:///echo".to_string(),
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
        ambient: Box::new(MockAmbientEnvironment::new()),
        root_component_url: "test:///root".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });
    // bind to system
    assert!(await!(model.look_up_and_bind_instance(vec!["system"].into())).is_ok());
    let expected_urls = vec!["test:///system_resolved".to_string()];
    assert_eq!(*await!(urls_run.lock()), expected_urls);

    // Validate children. system is resolved, but not echo.
    let actual_children = await!(get_children(&*model.root_realm));
    let mut expected_children: HashSet<ChildMoniker> = HashSet::new();
    expected_children.insert("system".into());
    expected_children.insert("echo".into());
    assert_eq!(actual_children, expected_children);

    let system_realm = await!(get_child_realm(&*model.root_realm, "system"));
    let echo_realm = await!(get_child_realm(&*model.root_realm, "echo"));
    let actual_children = await!(get_children(&*system_realm));
    assert!(actual_children.is_empty());
    assert!(await!(echo_realm.instance.state.lock()).child_realms.is_none());
    // bind to echo
    assert!(await!(model.look_up_and_bind_instance(vec!["echo"].into())).is_ok());
    let expected_urls =
        vec!["test:///system_resolved".to_string(), "test:///echo_resolved".to_string()];
    assert_eq!(*await!(urls_run.lock()), expected_urls);

    // Validate children. Now echo is resolved.
    let echo_realm = await!(get_child_realm(&*model.root_realm, "echo"));
    let actual_children = await!(get_children(&*echo_realm));
    assert!(actual_children.is_empty());
}

#[fuchsia_async::run_singlethreaded(test)]
async fn bind_instance_child_non_existent() {
    let mut resolver = ResolverRegistry::new();
    let runner = MockRunner::new();
    let urls_run = runner.urls_run.clone();
    let mut mock_resolver = MockResolver::new();
    mock_resolver.add_component(
        "root",
        ComponentDecl {
            children: vec![ChildDecl {
                name: "system".to_string(),
                url: "test:///system".to_string(),
                startup: fsys::StartupMode::Lazy,
            }],
            ..default_component_decl()
        },
    );
    mock_resolver.add_component("system", default_component_decl());
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        ambient: Box::new(MockAmbientEnvironment::new()),
        root_component_url: "test:///root".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });
    // bind to system
    assert!(await!(model.look_up_and_bind_instance(vec!["system"].into())).is_ok());
    let expected_urls = vec!["test:///system_resolved".to_string()];
    assert_eq!(*await!(urls_run.lock()), expected_urls);

    // can't bind to logger: it does not exist
    let moniker: AbsoluteMoniker = vec!["system", "logger"].into();
    let res = await!(model.look_up_and_bind_instance(moniker.clone()));
    let expected_res: Result<(), ModelError> = Err(ModelError::instance_not_found(moniker));
    assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
    let actual_urls = await!(urls_run.lock());
    let expected_urls = vec!["test:///system_resolved".to_string()];
    assert_eq!(*actual_urls, expected_urls);
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
    let urls_run = runner.urls_run.clone();
    let mut mock_resolver = MockResolver::new();
    mock_resolver.add_component(
        "root",
        ComponentDecl {
            children: vec![ChildDecl {
                name: "a".to_string(),
                url: "test:///a".to_string(),
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
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Eager,
                },
                ChildDecl {
                    name: "c".to_string(),
                    url: "test:///c".to_string(),
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
                url: "test:///d".to_string(),
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
                url: "test:///e".to_string(),
                startup: fsys::StartupMode::Lazy,
            }],
            ..default_component_decl()
        },
    );
    mock_resolver.add_component("e", default_component_decl());
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        ambient: Box::new(MockAmbientEnvironment::new()),
        root_component_url: "test:///root".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });

    // Bind to the top component, and check that it and the eager components were started.
    {
        let res = await!(model.look_up_and_bind_instance(AbsoluteMoniker::new(vec!["a".into(),])));
        let expected_res: Result<(), ModelError> = Ok(());
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
        let actual_urls = await!(urls_run.lock());
        // Execution order of `b` and `c` is non-deterministic.
        let expected_urls1 = vec![
            "test:///a_resolved".to_string(),
            "test:///b_resolved".to_string(),
            "test:///c_resolved".to_string(),
            "test:///d_resolved".to_string(),
        ];
        let expected_urls2 = vec![
            "test:///a_resolved".to_string(),
            "test:///c_resolved".to_string(),
            "test:///b_resolved".to_string(),
            "test:///d_resolved".to_string(),
        ];
        assert!(
            *actual_urls == expected_urls1 || *actual_urls == expected_urls2,
            "urls_run failed to match: {:?}",
            *actual_urls
        );
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn bind_instance_no_execute() {
    // Create a non-executable component with an eagerly-started child.
    let mut resolver = ResolverRegistry::new();
    let runner = MockRunner::new();
    let urls_run = runner.urls_run.clone();
    let mut mock_resolver = MockResolver::new();
    mock_resolver.add_component(
        "root",
        ComponentDecl {
            children: vec![ChildDecl {
                name: "a".to_string(),
                url: "test:///a".to_string(),
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
                url: "test:///b".to_string(),
                startup: fsys::StartupMode::Eager,
            }],
            ..default_component_decl()
        },
    );
    mock_resolver.add_component("b", default_component_decl());
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        ambient: Box::new(MockAmbientEnvironment::new()),
        root_component_url: "test:///root".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });

    // Bind to the parent component. The child should be started. However, the parent component
    // is non-executable so it is not run.
    assert!(await!(model.look_up_and_bind_instance(vec!["a"].into())).is_ok());
    let actual_urls = await!(urls_run.lock());
    let expected_urls = vec!["test:///b_resolved".to_string()];
    assert_eq!(*actual_urls, expected_urls);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn bind_instance_recursive_child() {
    let mut resolver = ResolverRegistry::new();
    let runner = MockRunner::new();
    let urls_run = runner.urls_run.clone();
    let mut mock_resolver = MockResolver::new();
    mock_resolver.add_component(
        "root",
        ComponentDecl {
            children: vec![ChildDecl {
                name: "system".to_string(),
                url: "test:///system".to_string(),
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
                    url: "test:///logger".to_string(),
                    startup: fsys::StartupMode::Lazy,
                },
                ChildDecl {
                    name: "netstack".to_string(),
                    url: "test:///netstack".to_string(),
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
        ambient: Box::new(MockAmbientEnvironment::new()),
        root_component_url: "test:///root".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });

    // bind to logger (before ever binding to system)
    assert!(await!(model.look_up_and_bind_instance(vec!["system", "logger"].into())).is_ok());
    let expected_urls = vec!["test:///logger_resolved".to_string()];
    assert_eq!(*await!(urls_run.lock()), expected_urls);

    // bind to netstack
    assert!(await!(model.look_up_and_bind_instance(vec!["system", "netstack"].into())).is_ok());
    let expected_urls =
        vec!["test:///logger_resolved".to_string(), "test:///netstack_resolved".to_string()];
    assert_eq!(*await!(urls_run.lock()), expected_urls);

    // finally, bind to system
    assert!(await!(model.look_up_and_bind_instance(vec!["system"].into())).is_ok());
    let expected_urls = vec![
        "test:///logger_resolved".to_string(),
        "test:///netstack_resolved".to_string(),
        "test:///system_resolved".to_string(),
    ];
    assert_eq!(*await!(urls_run.lock()), expected_urls);

    // validate children
    let actual_children = await!(get_children(&*model.root_realm));
    let mut expected_children: HashSet<ChildMoniker> = HashSet::new();
    expected_children.insert("system".into());
    assert_eq!(actual_children, expected_children);

    let system_realm = await!(get_child_realm(&*model.root_realm, "system"));

    let actual_children = await!(get_children(&*system_realm));
    let mut expected_children: HashSet<ChildMoniker> = HashSet::new();
    expected_children.insert("logger".into());
    expected_children.insert("netstack".into());
    assert_eq!(actual_children, expected_children);

    let logger_realm = await!(get_child_realm(&*system_realm, "logger"));
    let actual_children = await!(get_children(&*logger_realm));
    assert!(actual_children.is_empty());

    let netstack_realm = await!(get_child_realm(&*system_realm, "netstack"));
    let actual_children = await!(get_children(&*netstack_realm));
    assert!(actual_children.is_empty());
}
