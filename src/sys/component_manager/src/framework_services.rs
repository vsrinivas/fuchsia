// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*, cm_fidl_validator, cm_rust::FidlIntoNative, failure::Error,
    fidl::endpoints::ServerEnd, fidl_fuchsia_io::DirectoryMarker, fidl_fuchsia_sys2 as fsys,
    fuchsia_async as fasync, futures::future::FutureObj, futures::prelude::*, log::*, std::cmp,
    std::sync::Arc,
};

/// Provides the implementation of `FrameworkServiceHost` which is used in production.
pub struct RealFrameworkServiceHost {}

impl FrameworkServiceHost for RealFrameworkServiceHost {
    fn serve_realm_service(
        &self,
        model: Model,
        realm: Arc<Realm>,
        stream: fsys::RealmRequestStream,
    ) -> FutureObj<'static, Result<(), FrameworkServiceError>> {
        FutureObj::new(Box::new(async move {
            await!(Self::do_serve_realm_service(model, realm, stream))
                .map_err(|e| FrameworkServiceError::service_error(REALM_SERVICE.to_string(), e))
        }))
    }
}

impl RealFrameworkServiceHost {
    pub fn new() -> Self {
        RealFrameworkServiceHost {}
    }

    async fn do_serve_realm_service(
        model: Model,
        realm: Arc<Realm>,
        mut stream: fsys::RealmRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = await!(stream.try_next())? {
            match request {
                fsys::RealmRequest::CreateChild { responder, collection, decl } => {
                    let mut res =
                        await!(Self::create_child(model.clone(), realm.clone(), collection, decl));
                    responder.send(&mut res)?;
                }
                fsys::RealmRequest::BindChild { responder, child, exposed_dir } => {
                    let mut res =
                        await!(Self::bind_child(model.clone(), realm.clone(), child, exposed_dir));
                    responder.send(&mut res)?;
                }
                fsys::RealmRequest::DestroyChild { responder, .. } => {
                    info!("{} destroying child!", realm.abs_moniker);
                    responder.send(&mut Ok(()))?;
                }
                fsys::RealmRequest::ListChildren { responder, collection, iter } => {
                    let mut res =
                        await!(Self::list_children(model.clone(), realm.clone(), collection, iter));
                    responder.send(&mut res)?;
                }
            }
        }
        Ok(())
    }

    async fn create_child(
        model: Model,
        realm: Arc<Realm>,
        collection: fsys::CollectionRef,
        child_decl: fsys::ChildDecl,
    ) -> Result<(), fsys::Error> {
        let collection_name = collection.name.ok_or(fsys::Error::InvalidArguments)?;
        cm_fidl_validator::validate_child(&child_decl)
            .map_err(|_| fsys::Error::InvalidArguments)?;
        let child_decl = child_decl.fidl_into_native();
        await!(realm.add_dynamic_child(collection_name, &child_decl, &model.hooks)).map_err(
            |e| match e {
                ModelError::InstanceAlreadyExists { .. } => fsys::Error::InstanceAlreadyExists,
                ModelError::CollectionNotFound { .. } => fsys::Error::CollectionNotFound,
                ModelError::Unsupported { .. } => fsys::Error::Unsupported,
                e => {
                    error!("add_dynamic_child() failed: {}", e);
                    fsys::Error::Internal
                }
            },
        )?;
        Ok(())
    }

    async fn bind_child(
        model: Model,
        realm: Arc<Realm>,
        child: fsys::ChildRef,
        exposed_dir: ServerEnd<DirectoryMarker>,
    ) -> Result<(), fsys::Error> {
        let child_name = child.name.ok_or(fsys::Error::InvalidArguments)?;
        let collection = child.collection;
        let child_moniker = ChildMoniker::new(child_name, collection);
        await!(realm.resolve_decl()).map_err(|e| match e {
            ModelError::ResolverError { err } => {
                debug!("failed to resolve: {:?}", err);
                fsys::Error::InstanceCannotResolve
            }
            e => {
                error!("resolve_decl() failed: {}", e);
                fsys::Error::Internal
            }
        })?;
        let realm_state = await!(realm.state.lock());
        if let Some(child_realm) = realm_state.child_realms.as_ref().unwrap().get(&child_moniker) {
            await!(
                model.bind_instance_open_exposed(child_realm.clone(), exposed_dir.into_channel())
            )
            .map_err(|e| match e {
                ModelError::ResolverError { err } => {
                    debug!("failed to resolve child: {:?}", err);
                    fsys::Error::InstanceCannotResolve
                }
                ModelError::RunnerError { err } => {
                    debug!("failed to start child: {:?}", err);
                    fsys::Error::InstanceCannotStart
                }
                e => {
                    error!("bind_instance_open_exposed() failed: {}", e);
                    fsys::Error::Internal
                }
            })?;
        } else {
            return Err(fsys::Error::InstanceNotFound);
        }
        Ok(())
    }

    async fn list_children(
        model: Model,
        realm: Arc<Realm>,
        collection: fsys::CollectionRef,
        iter: ServerEnd<fsys::ChildIteratorMarker>,
    ) -> Result<(), fsys::Error> {
        let collection_name = collection.name.ok_or(fsys::Error::InvalidArguments)?;
        await!(realm.resolve_decl()).map_err(|e| {
            error!("resolve_decl() failed: {}", e);
            fsys::Error::Internal
        })?;
        let state = await!(realm.state.lock());
        let decl = state.decl.as_ref().unwrap();
        let _ = decl
            .find_collection(&collection_name)
            .ok_or_else(|| fsys::Error::CollectionNotFound)?;
        let mut children: Vec<_> = state
            .child_realms
            .as_ref()
            .unwrap()
            .keys()
            .filter_map(|m| match m.collection() {
                Some(c) => {
                    if c == collection_name {
                        Some(fsys::ChildRef {
                            name: Some(m.name().to_string()),
                            collection: m.collection().map(|s| s.to_string()),
                        })
                    } else {
                        None
                    }
                }
                _ => None,
            })
            .collect();
        children.sort_unstable_by(|a, b| {
            let a = a.name.as_ref().unwrap();
            let b = b.name.as_ref().unwrap();
            if a == b {
                cmp::Ordering::Equal
            } else if a < b {
                cmp::Ordering::Less
            } else {
                cmp::Ordering::Greater
            }
        });
        let stream = iter.into_stream().expect("could not convert iterator channel into stream");
        let batch_size = model.config.list_children_batch_size;
        fasync::spawn(async move {
            if let Err(e) = await!(Self::serve_child_iterator(children, stream, batch_size)) {
                // TODO: Set an epitaph to indicate this was an unexpected error.
                warn!("serve_child_iterator failed: {}", e);
            }
        });
        Ok(())
    }

    async fn serve_child_iterator(
        mut children: Vec<fsys::ChildRef>,
        mut stream: fsys::ChildIteratorRequestStream,
        batch_size: usize,
    ) -> Result<(), Error> {
        while let Some(request) = await!(stream.try_next())? {
            match request {
                fsys::ChildIteratorRequest::Next { responder } => {
                    let n_to_send = std::cmp::min(children.len(), batch_size);
                    let res: Vec<_> = children.drain(..n_to_send).collect();
                    let mut res = res.into_iter();
                    responder.send(&mut res)?;
                }
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        crate::model::testing::mocks::*,
        crate::model::testing::routing_test_helpers::*,
        crate::model::testing::test_hook::*,
        cm_rust::{
            self, CapabilityPath, ChildDecl, CollectionDecl, ComponentDecl, ExposeDecl,
            ExposeServiceDecl, ExposeSource, NativeIntoFidl,
        },
        fidl::endpoints,
        fidl_fidl_examples_echo as echo,
        fidl_fuchsia_io::MODE_TYPE_SERVICE,
        fuchsia_async as fasync,
        io_util::OPEN_RIGHT_READABLE,
        std::collections::HashSet,
        std::convert::TryFrom,
        std::path::PathBuf,
    };

    struct FrameworkServiceTest {
        hook: Arc<TestHook>,
        realm: Arc<Realm>,
        realm_proxy: fsys::RealmProxy,
    }

    impl FrameworkServiceTest {
        async fn new(
            mock_resolver: MockResolver,
            mock_runner: MockRunner,
            realm_moniker: AbsoluteMoniker,
        ) -> Self {
            // Init model.
            let mut resolver = ResolverRegistry::new();
            resolver.register("test".to_string(), Box::new(mock_resolver));
            let hook = Arc::new(TestHook::new());
            let mut config = ModelConfig::default();
            config.list_children_batch_size = 2;
            let model = Model::new(ModelParams {
                framework_services: Box::new(RealFrameworkServiceHost::new()),
                root_component_url: "test:///root".to_string(),
                root_resolver_registry: resolver,
                root_default_runner: Arc::new(mock_runner),
                hooks: vec![hook.clone()],
                config,
            });

            // Look up and bind to realm.
            let realm =
                await!(model.look_up_realm(&realm_moniker)).expect("failed to look up realm");
            await!(model.bind_instance(realm.clone())).expect("failed to bind to realm");

            // Host framework service.
            let (realm_proxy, stream) =
                endpoints::create_proxy_and_stream::<fsys::RealmMarker>().unwrap();
            {
                let realm = realm.clone();
                let model = model.clone();
                fasync::spawn(async move {
                    await!(model.framework_services.serve_realm_service(
                        model.clone(),
                        realm,
                        stream
                    ))
                    .expect("failed serving realm service");
                });
            }
            FrameworkServiceTest { hook, realm, realm_proxy }
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn create_dynamic_child() {
        // Set up model and realm service.
        let mut mock_resolver = MockResolver::new();
        let mock_runner = MockRunner::new();
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
                collections: vec![CollectionDecl {
                    name: "coll".to_string(),
                    durability: fsys::Durability::Transient,
                }],
                ..default_component_decl()
            },
        );
        let test =
            await!(FrameworkServiceTest::new(mock_resolver, mock_runner, vec!["system"].into()));

        // Create children "a" and "b" in collection.
        let collection_ref = fsys::CollectionRef { name: Some("coll".to_string()) };
        let res = await!(test.realm_proxy.create_child(collection_ref, child_decl("a")));
        let _ = res.expect("failed to create child a").expect("failed to create child a");

        let collection_ref = fsys::CollectionRef { name: Some("coll".to_string()) };
        let res = await!(test.realm_proxy.create_child(collection_ref, child_decl("b")));
        let _ = res.expect("failed to create child b").expect("failed to create child b");

        // Verify that the component topology matches expectations.
        let actual_children = await!(get_children(&test.realm));
        let mut expected_children: HashSet<ChildMoniker> = HashSet::new();
        expected_children.insert("coll:a".into());
        expected_children.insert("coll:b".into());
        assert_eq!(actual_children, expected_children);
        assert_eq!("(system(coll:a,coll:b))", test.hook.print());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn create_dynamic_child_errors() {
        let mut mock_resolver = MockResolver::new();
        let mock_runner = MockRunner::new();
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
                collections: vec![
                    CollectionDecl {
                        name: "coll".to_string(),
                        durability: fsys::Durability::Transient,
                    },
                    CollectionDecl {
                        name: "pcoll".to_string(),
                        durability: fsys::Durability::Persistent,
                    },
                ],
                ..default_component_decl()
            },
        );
        let test =
            await!(FrameworkServiceTest::new(mock_resolver, mock_runner, vec!["system"].into()));

        // Invalid arguments.
        {
            let collection_ref = fsys::CollectionRef { name: None };
            let err = await!(test.realm_proxy.create_child(collection_ref, child_decl("a")))
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fsys::Error::InvalidArguments);
        }
        {
            let collection_ref = fsys::CollectionRef { name: Some("coll".to_string()) };
            let child_decl = fsys::ChildDecl {
                name: Some("a".to_string()),
                url: None,
                startup: Some(fsys::StartupMode::Lazy),
            };
            let err = await!(test.realm_proxy.create_child(collection_ref, child_decl))
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fsys::Error::InvalidArguments);
        }

        // Instance already exists.
        {
            let collection_ref = fsys::CollectionRef { name: Some("coll".to_string()) };
            let res = await!(test.realm_proxy.create_child(collection_ref, child_decl("a")));
            let _ = res.expect("failed to create child a");
            let collection_ref = fsys::CollectionRef { name: Some("coll".to_string()) };
            let err = await!(test.realm_proxy.create_child(collection_ref, child_decl("a")))
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fsys::Error::InstanceAlreadyExists);
        }

        // Collection not found.
        {
            let collection_ref = fsys::CollectionRef { name: Some("nonexistent".to_string()) };
            let err = await!(test.realm_proxy.create_child(collection_ref, child_decl("a")))
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fsys::Error::CollectionNotFound);
        }

        // Unsupported.
        {
            let collection_ref = fsys::CollectionRef { name: Some("pcoll".to_string()) };
            let err = await!(test.realm_proxy.create_child(collection_ref, child_decl("a")))
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fsys::Error::Unsupported);
        }
        {
            let collection_ref = fsys::CollectionRef { name: Some("coll".to_string()) };
            let child_decl = fsys::ChildDecl {
                name: Some("b".to_string()),
                url: Some("test:///b".to_string()),
                startup: Some(fsys::StartupMode::Eager),
            };
            let err = await!(test.realm_proxy.create_child(collection_ref, child_decl))
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fsys::Error::Unsupported);
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn bind_static_child() {
        // Create a hierarchy of three components, the last with eager startup. The middle
        // component hosts and exposes the "/svc/hippo" service.
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
                exposes: vec![ExposeDecl::Service(ExposeServiceDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                })],
                children: vec![ChildDecl {
                    name: "eager".to_string(),
                    url: "test:///eager".to_string(),
                    startup: fsys::StartupMode::Eager,
                }],
                ..default_component_decl()
            },
        );
        mock_resolver.add_component("eager", ComponentDecl { ..default_component_decl() });
        let mut mock_runner = MockRunner::new();
        let mut out_dir = OutDir::new();
        out_dir.add_service();
        mock_runner.host_fns.insert("test:///system_resolved".to_string(), out_dir.host_fn());
        let urls_run = mock_runner.urls_run.clone();
        let test = await!(FrameworkServiceTest::new(mock_resolver, mock_runner, vec![].into()));

        // Bind to child and use exposed service.
        let child_ref = fsys::ChildRef { name: Some("system".to_string()), collection: None };
        let (dir_proxy, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
        let res = await!(test.realm_proxy.bind_child(child_ref, server_end));
        let _ = res.expect("failed to bind to system").expect("failed to bind to system");
        let node_proxy = io_util::open_node(
            &dir_proxy,
            &PathBuf::from("svc/hippo"),
            OPEN_RIGHT_READABLE,
            MODE_TYPE_SERVICE,
        )
        .expect("failed to open echo service");
        let echo_proxy = echo::EchoProxy::new(node_proxy.into_channel().unwrap());
        let res = await!(echo_proxy.echo_string(Some("hippos")));
        assert_eq!(res.expect("failed to use echo service"), Some("hippos".to_string()));

        // Verify that the bindings happened (including the eager binding) and the component
        // topology matches expectations.
        let expected_urls = vec![
            "test:///root_resolved".to_string(),
            "test:///system_resolved".to_string(),
            "test:///eager_resolved".to_string(),
        ];
        assert_eq!(*await!(urls_run.lock()), expected_urls);
        assert_eq!("(system(eager))", test.hook.print());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn bind_dynamic_child() {
        // Create a root component with a collection and define a component that exposes a service.
        let mut mock_resolver = MockResolver::new();
        mock_resolver.add_component(
            "root",
            ComponentDecl {
                collections: vec![CollectionDecl {
                    name: "coll".to_string(),
                    durability: fsys::Durability::Transient,
                }],
                ..default_component_decl()
            },
        );
        mock_resolver.add_component(
            "system",
            ComponentDecl {
                exposes: vec![ExposeDecl::Service(ExposeServiceDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                })],
                ..default_component_decl()
            },
        );
        let mut mock_runner = MockRunner::new();
        let mut out_dir = OutDir::new();
        out_dir.add_service();
        mock_runner.host_fns.insert("test:///system_resolved".to_string(), out_dir.host_fn());
        let urls_run = mock_runner.urls_run.clone();
        let test = await!(FrameworkServiceTest::new(mock_resolver, mock_runner, vec![].into()));

        // Add "system" to collection.
        let collection_ref = fsys::CollectionRef { name: Some("coll".to_string()) };
        let res = await!(test.realm_proxy.create_child(collection_ref, child_decl("system")));
        let _ = res.expect("failed to create child system").expect("failed to create child system");

        // Bind to child and use exposed service.
        let child_ref = fsys::ChildRef {
            name: Some("system".to_string()),
            collection: Some("coll".to_string()),
        };
        let (dir_proxy, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
        let res = await!(test.realm_proxy.bind_child(child_ref, server_end));
        let _ = res.expect("failed to bind to system").expect("failed to bind to system");
        let node_proxy = io_util::open_node(
            &dir_proxy,
            &PathBuf::from("svc/hippo"),
            OPEN_RIGHT_READABLE,
            MODE_TYPE_SERVICE,
        )
        .expect("failed to open echo service");
        let echo_proxy = echo::EchoProxy::new(node_proxy.into_channel().unwrap());
        let res = await!(echo_proxy.echo_string(Some("hippos")));
        assert_eq!(res.expect("failed to use echo service"), Some("hippos".to_string()));

        // Verify that the binding happened and the component topology matches expectations.
        let expected_urls =
            vec!["test:///root_resolved".to_string(), "test:///system_resolved".to_string()];
        assert_eq!(*await!(urls_run.lock()), expected_urls);
        assert_eq!("(coll:system)", test.hook.print());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn bind_child_errors() {
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
                        name: "unresolvable".to_string(),
                        url: "test:///unresolvable".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },
                    ChildDecl {
                        name: "unrunnable".to_string(),
                        url: "test:///unrunnable".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },
                ],
                ..default_component_decl()
            },
        );
        mock_resolver.add_component("system", ComponentDecl { ..default_component_decl() });
        mock_resolver.add_component("unrunnable", ComponentDecl { ..default_component_decl() });
        let mut mock_runner = MockRunner::new();
        mock_runner.cause_failure("unrunnable");
        let test = await!(FrameworkServiceTest::new(mock_resolver, mock_runner, vec![].into()));

        // Invalid arguments.
        {
            let child_ref = fsys::ChildRef { name: None, collection: None };
            let (_, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
            let err = await!(test.realm_proxy.bind_child(child_ref, server_end))
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fsys::Error::InvalidArguments);
        }

        // Instance not found.
        {
            let child_ref = fsys::ChildRef { name: Some("missing".to_string()), collection: None };
            let (_, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
            let err = await!(test.realm_proxy.bind_child(child_ref, server_end))
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fsys::Error::InstanceNotFound);
        }

        // Instance cannot start.
        {
            let child_ref =
                fsys::ChildRef { name: Some("unrunnable".to_string()), collection: None };
            let (_, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
            let err = await!(test.realm_proxy.bind_child(child_ref, server_end))
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fsys::Error::InstanceCannotStart);
        }

        // Instance cannot resolve.
        {
            let child_ref =
                fsys::ChildRef { name: Some("unresolvable".to_string()), collection: None };
            let (_, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
            let err = await!(test.realm_proxy.bind_child(child_ref, server_end))
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fsys::Error::InstanceCannotResolve);
        }
    }

    fn child_decl(name: &str) -> fsys::ChildDecl {
        ChildDecl {
            name: name.to_string(),
            url: format!("test:///{}", name),
            startup: fsys::StartupMode::Lazy,
        }
        .native_into_fidl()
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn list_children() {
        // Create a root component with collections and a static child.
        let mut mock_resolver = MockResolver::new();
        mock_resolver.add_component(
            "root",
            ComponentDecl {
                children: vec![ChildDecl {
                    name: "static".to_string(),
                    url: "test:///static".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                collections: vec![
                    CollectionDecl {
                        name: "coll".to_string(),
                        durability: fsys::Durability::Transient,
                    },
                    CollectionDecl {
                        name: "coll2".to_string(),
                        durability: fsys::Durability::Transient,
                    },
                ],
                ..default_component_decl()
            },
        );
        mock_resolver.add_component("static", default_component_decl());
        let mock_runner = MockRunner::new();
        let test = await!(FrameworkServiceTest::new(mock_resolver, mock_runner, vec![].into()));

        // Create children "a" and "b" in collection 1, "c" in collection 2.
        let collection_ref = fsys::CollectionRef { name: Some("coll".to_string()) };
        let res = await!(test.realm_proxy.create_child(collection_ref, child_decl("a")));
        let _ = res.expect("failed to create child a").expect("failed to create child a");

        let collection_ref = fsys::CollectionRef { name: Some("coll".to_string()) };
        let res = await!(test.realm_proxy.create_child(collection_ref, child_decl("b")));
        let _ = res.expect("failed to create child b").expect("failed to create child b");

        let collection_ref = fsys::CollectionRef { name: Some("coll".to_string()) };
        let res = await!(test.realm_proxy.create_child(collection_ref, child_decl("c")));
        let _ = res.expect("failed to create child c").expect("failed to create child c");

        let collection_ref = fsys::CollectionRef { name: Some("coll2".to_string()) };
        let res = await!(test.realm_proxy.create_child(collection_ref, child_decl("d")));
        let _ = res.expect("failed to create child d").expect("failed to create child d");

        // Verify that we see the expected children when listing the collection.
        let (iterator_proxy, server_end) = endpoints::create_proxy().unwrap();
        let collection_ref = fsys::CollectionRef { name: Some("coll".to_string()) };
        let res = await!(test.realm_proxy.list_children(collection_ref, server_end));
        let _ = res.expect("failed to list children").expect("failed to list children");

        let res = await!(iterator_proxy.next());
        let children = res.expect("failed to iterate over children");
        assert_eq!(
            children,
            vec![
                fsys::ChildRef {
                    name: Some("a".to_string()),
                    collection: Some("coll".to_string())
                },
                fsys::ChildRef {
                    name: Some("b".to_string()),
                    collection: Some("coll".to_string())
                },
            ]
        );

        let res = await!(iterator_proxy.next());
        let children = res.expect("failed to iterate over children");
        assert_eq!(
            children,
            vec![fsys::ChildRef {
                name: Some("c".to_string()),
                collection: Some("coll".to_string())
            },]
        );

        let res = await!(iterator_proxy.next());
        let children = res.expect("failed to iterate over children");
        assert_eq!(children, vec![]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn list_children_errors() {
        // Create a root component with a collection.
        let mut mock_resolver = MockResolver::new();
        mock_resolver.add_component(
            "root",
            ComponentDecl {
                collections: vec![CollectionDecl {
                    name: "coll".to_string(),
                    durability: fsys::Durability::Transient,
                }],
                ..default_component_decl()
            },
        );
        let mock_runner = MockRunner::new();
        let test = await!(FrameworkServiceTest::new(mock_resolver, mock_runner, vec![].into()));

        // Invalid arguments.
        {
            let collection_ref = fsys::CollectionRef { name: None };
            let (_, server_end) = endpoints::create_proxy().unwrap();
            let err = await!(test.realm_proxy.list_children(collection_ref, server_end))
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fsys::Error::InvalidArguments);
        }

        // Collection not found.
        {
            let collection_ref = fsys::CollectionRef { name: Some("nonexistent".to_string()) };
            let (_, server_end) = endpoints::create_proxy().unwrap();
            let err = await!(test.realm_proxy.list_children(collection_ref, server_end))
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fsys::Error::CollectionNotFound);
        }
    }
}
