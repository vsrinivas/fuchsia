// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*, cm_fidl_validator, cm_rust::FidlIntoNative, failure::Error,
    fidl_fuchsia_sys2 as fsys, futures::future::FutureObj, futures::prelude::*, log::*,
    std::sync::Arc,
};

/// Provides the implementation of `AmbientEnvironment` which is used in production.
pub struct RealAmbientEnvironment {}

impl AmbientEnvironment for RealAmbientEnvironment {
    fn serve_realm_service(
        &self,
        realm: Arc<Realm>,
        hooks: Arc<Hooks>,
        stream: fsys::RealmRequestStream,
    ) -> FutureObj<'static, Result<(), AmbientError>> {
        FutureObj::new(Box::new(async move {
            await!(Self::do_serve_realm_service(realm, hooks, stream))
                .map_err(|e| AmbientError::service_error(REALM_SERVICE.to_string(), e))
        }))
    }
}

impl RealAmbientEnvironment {
    pub fn new() -> Self {
        RealAmbientEnvironment {}
    }

    async fn do_serve_realm_service(
        realm: Arc<Realm>,
        hooks: Arc<Hooks>,
        mut stream: fsys::RealmRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = await!(stream.try_next())? {
            match request {
                fsys::RealmRequest::CreateChild { responder, collection, decl } => {
                    let mut res =
                        await!(Self::create_child(realm.clone(), hooks.clone(), collection, decl));
                    responder.send(&mut res)?;
                }
                fsys::RealmRequest::BindChild { responder, .. } => {
                    info!("{} binding to child!", realm.abs_moniker);
                    responder.send(&mut Ok(()))?;
                }
                fsys::RealmRequest::DestroyChild { responder, .. } => {
                    info!("{} destroying child!", realm.abs_moniker);
                    responder.send(&mut Ok(()))?;
                }
                fsys::RealmRequest::ListChildren { responder, .. } => {
                    info!("{} listing children!", realm.abs_moniker);
                    responder.send(&mut Ok(()))?;
                }
            }
        }
        Ok(())
    }

    async fn create_child(
        realm: Arc<Realm>,
        hooks: Arc<Hooks>,
        collection: fsys::CollectionRef,
        child_decl: fsys::ChildDecl,
    ) -> Result<(), fsys::Error> {
        let collection_name = collection.name.ok_or(fsys::Error::InvalidArguments)?;
        cm_fidl_validator::validate_child(&child_decl)
            .map_err(|_| fsys::Error::InvalidArguments)?;
        let child_decl = child_decl.fidl_into_native();
        await!(realm.add_dynamic_child(collection_name, &child_decl, &hooks)).map_err(
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
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        crate::model::testing::mocks::*,
        crate::model::testing::routing_test_helpers::*,
        crate::model::testing::test_hook::*,
        cm_rust::{self, ChildDecl, CollectionDecl, ComponentDecl, NativeIntoFidl},
        fidl::endpoints,
        fuchsia_async as fasync,
        std::collections::HashSet,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn create_dynamic_child() {
        let mut resolver = ResolverRegistry::new();
        let runner = MockRunner::new();
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
                collections: vec![CollectionDecl {
                    name: "coll".to_string(),
                    durability: fsys::Durability::Transient,
                }],
                ..default_component_decl()
            },
        );
        resolver.register("test".to_string(), Box::new(mock_resolver));
        let hook = Arc::new(TestHook::new());
        let model = Model::new(ModelParams {
            ambient: Box::new(RealAmbientEnvironment::new()),
            root_component_url: "test:///root".to_string(),
            root_resolver_registry: resolver,
            root_default_runner: Box::new(runner),
            hooks: vec![hook.clone()],
        });

        // Host ambient service.
        assert!(await!(model.look_up_and_bind_instance(vec!["system"].into())).is_ok());
        let system_realm = await!(get_child_realm(&*model.root_realm, "system"));
        let (realm_proxy, stream) =
            endpoints::create_proxy_and_stream::<fsys::RealmMarker>().unwrap();
        {
            let system_realm = system_realm.clone();
            let model = model.clone();
            fasync::spawn(async move {
                await!(model.ambient.serve_realm_service(
                    system_realm,
                    model.hooks.clone(),
                    stream
                ))
                .expect("failed serving realm service");
            });
        }

        // Create children "a" and "b" in collection.
        let collection_ref = fsys::CollectionRef { name: Some("coll".to_string()) };
        let res = await!(realm_proxy.create_child(collection_ref, child_decl("a")));
        let _ = res.expect("failed to create child a").expect("failed to create child a");

        let collection_ref = fsys::CollectionRef { name: Some("coll".to_string()) };
        let res = await!(realm_proxy.create_child(collection_ref, child_decl("b")));
        let _ = res.expect("failed to create child b").expect("failed to create child b");

        // Verify that the component topology matches expectations.
        let actual_children = await!(get_children(&system_realm));
        let mut expected_children: HashSet<ChildMoniker> = HashSet::new();
        expected_children.insert("coll:a".into());
        expected_children.insert("coll:b".into());
        assert_eq!(actual_children, expected_children);
        assert_eq!("(system(coll:a,coll:b))", hook.print());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn create_dynamic_child_errors() {
        let mut resolver = ResolverRegistry::new();
        let runner = MockRunner::new();
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
        resolver.register("test".to_string(), Box::new(mock_resolver));
        let model = Model::new(ModelParams {
            ambient: Box::new(RealAmbientEnvironment::new()),
            root_component_url: "test:///root".to_string(),
            root_resolver_registry: resolver,
            root_default_runner: Box::new(runner),
            hooks: vec![],
        });

        // Host ambient service.
        assert!(await!(model.look_up_and_bind_instance(vec!["system"].into())).is_ok());
        let system_realm = await!(get_child_realm(&*model.root_realm, "system"));
        let (realm_proxy, stream) =
            endpoints::create_proxy_and_stream::<fsys::RealmMarker>().unwrap();
        let hooks = Arc::new(vec![]);
        {
            let system_realm = system_realm.clone();
            let model = model.clone();
            fasync::spawn(async move {
                await!(model.ambient.serve_realm_service(system_realm, hooks, stream))
                    .expect("failed serving realm service");
            });
        }

        // Invalid arguments.
        {
            let collection_ref = fsys::CollectionRef { name: None };
            let err = await!(realm_proxy.create_child(collection_ref, child_decl("a")))
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
            let err = await!(realm_proxy.create_child(collection_ref, child_decl))
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fsys::Error::InvalidArguments);
        }

        // Instance already exists.
        {
            let collection_ref = fsys::CollectionRef { name: Some("coll".to_string()) };
            let res = await!(realm_proxy.create_child(collection_ref, child_decl("a")));
            let _ = res.expect("failed to create child a");
            let collection_ref = fsys::CollectionRef { name: Some("coll".to_string()) };
            let err = await!(realm_proxy.create_child(collection_ref, child_decl("a")))
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fsys::Error::InstanceAlreadyExists);
        }

        // Collection not found.
        {
            let collection_ref = fsys::CollectionRef { name: Some("nonexistent".to_string()) };
            let err = await!(realm_proxy.create_child(collection_ref, child_decl("a")))
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fsys::Error::CollectionNotFound);
        }

        // Unsupported.
        {
            let collection_ref = fsys::CollectionRef { name: Some("pcoll".to_string()) };
            let err = await!(realm_proxy.create_child(collection_ref, child_decl("a")))
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
            let err = await!(realm_proxy.create_child(collection_ref, child_decl))
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fsys::Error::Unsupported);
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
}
