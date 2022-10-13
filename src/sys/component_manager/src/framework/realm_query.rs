// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource, PERMITTED_FLAGS},
        model::{
            error::ModelError,
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            model::Model,
        },
    },
    async_trait::async_trait,
    cm_rust::CapabilityName,
    cm_task_scope::TaskScope,
    cm_util::channel,
    fidl::{endpoints::ServerEnd, prelude::*},
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys, fuchsia_zircon as zx,
    futures::lock::Mutex,
    futures::StreamExt,
    lazy_static::lazy_static,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, RelativeMoniker},
    std::{
        convert::TryFrom,
        path::PathBuf,
        sync::{Arc, Weak},
    },
    tracing::warn,
};

lazy_static! {
    pub static ref REALM_QUERY_CAPABILITY_NAME: CapabilityName =
        fsys::RealmQueryMarker::PROTOCOL_NAME.into();
}

// Serves the fuchsia.sys2.RealmQuery protocol.
pub struct RealmQuery {
    model: Arc<Model>,
}

impl RealmQuery {
    pub fn new(model: Arc<Model>) -> Self {
        Self { model }
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "RealmQuery",
            vec![EventType::CapabilityRouted],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    /// Given a `CapabilitySource`, determine if it is a framework-provided
    /// RealmQuery capability. If so, serve the capability.
    async fn on_capability_routed_async(
        self: Arc<Self>,
        source: CapabilitySource,
        capability_provider: Arc<Mutex<Option<Box<dyn CapabilityProvider>>>>,
    ) -> Result<(), ModelError> {
        // If this is a scoped framework directory capability, then check the source path
        if let CapabilitySource::Framework { capability, component } = source {
            if capability.matches_protocol(&REALM_QUERY_CAPABILITY_NAME) {
                // Set the capability provider, if not already set.
                let mut capability_provider = capability_provider.lock().await;
                if capability_provider.is_none() {
                    *capability_provider = Some(Box::new(RealmQueryCapabilityProvider::query(
                        self,
                        component.abs_moniker.clone(),
                    )));
                }
            }
        }
        Ok(())
    }

    /// Create the instance info and state matching the given moniker string in this scope
    async fn get_instance_info_and_resolved_state(
        self: &Arc<Self>,
        scope_moniker: &AbsoluteMoniker,
        moniker_str: &str,
    ) -> Result<(fsys::InstanceInfo, Option<Box<fsys::ResolvedState>>), fsys::RealmQueryError> {
        // Construct the complete moniker using the scope moniker and the relative moniker string.
        let relative_moniker = RelativeMoniker::try_from(moniker_str)
            .map_err(|_| fsys::RealmQueryError::BadMoniker)?;
        let moniker = scope_moniker.descendant(&relative_moniker);

        // TODO(https://fxbug.dev/108532): Close the connection if the scope root cannot be found.
        let instance =
            self.model.find(&moniker).await.ok_or(fsys::RealmQueryError::InstanceNotFound)?;

        let resolved = instance.create_fidl_resolved_state().await;

        let instance_id = self.model.component_id_index().look_up_moniker(&moniker).cloned();

        let state = match &resolved {
            Some(r) => {
                if r.execution.is_some() {
                    fsys::InstanceState::Started
                } else {
                    fsys::InstanceState::Resolved
                }
            }
            None => fsys::InstanceState::Unresolved,
        };

        let info = fsys::InstanceInfo {
            moniker: relative_moniker.to_string(),
            url: instance.component_url.clone(),
            instance_id,
            state,
        };

        Ok((info, resolved))
    }

    async fn get_instance_directories(
        self: &Arc<Self>,
        scope_moniker: &AbsoluteMoniker,
        moniker_str: &str,
    ) -> Result<Option<Box<fsys::ResolvedDirectories>>, fsys::RealmQueryError> {
        // Construct the complete moniker using the scope moniker and the relative moniker string.
        let relative_moniker = RelativeMoniker::try_from(moniker_str)
            .map_err(|_| fsys::RealmQueryError::BadMoniker)?;
        let moniker = scope_moniker.descendant(&relative_moniker);

        let instance =
            self.model.find(&moniker).await.ok_or(fsys::RealmQueryError::InstanceNotFound)?;

        let resolved_dirs = instance.create_fidl_resolved_directories().await;

        Ok(resolved_dirs)
    }

    /// Serve the fuchsia.sys2.RealmQuery protocol for a given scope on a given stream
    pub async fn serve(
        self: Arc<Self>,
        scope_moniker: AbsoluteMoniker,
        mut stream: fsys::RealmQueryRequestStream,
    ) {
        loop {
            let request = match stream.next().await {
                Some(Ok(request)) => request,
                Some(Err(error)) => {
                    warn!(?error, "Could not get next RealmQuery request");
                    break;
                }
                None => break,
            };
            let result = match request {
                fsys::RealmQueryRequest::GetInstanceInfo { moniker, responder } => {
                    let mut result =
                        self.get_instance_info_and_resolved_state(&scope_moniker, &moniker).await;
                    responder.send(&mut result)
                }
                fsys::RealmQueryRequest::GetInstanceDirectories { moniker, responder } => {
                    let mut result = self.get_instance_directories(&scope_moniker, &moniker).await;
                    responder.send(&mut result)
                }
            };
            if let Err(error) = result {
                warn!(?error, "Could not respond to RealmQuery request");
                break;
            }
        }
    }
}

#[async_trait]
impl Hook for RealmQuery {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        match &event.result {
            Ok(EventPayload::CapabilityRouted { source, capability_provider }) => {
                self.on_capability_routed_async(source.clone(), capability_provider.clone())
                    .await?;
            }
            _ => {}
        }
        Ok(())
    }
}

pub struct RealmQueryCapabilityProvider {
    query: Arc<RealmQuery>,
    scope_moniker: AbsoluteMoniker,
}

impl RealmQueryCapabilityProvider {
    pub fn query(query: Arc<RealmQuery>, scope_moniker: AbsoluteMoniker) -> Self {
        Self { query, scope_moniker }
    }
}

#[async_trait]
impl CapabilityProvider for RealmQueryCapabilityProvider {
    async fn open(
        self: Box<Self>,
        task_scope: TaskScope,
        flags: fio::OpenFlags,
        _open_mode: u32,
        relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let forbidden = flags - PERMITTED_FLAGS;
        if !forbidden.is_empty() {
            warn!(?forbidden, "RealmQuery capability");
            return Ok(());
        }

        if relative_path.components().count() != 0 {
            warn!(
                path=%relative_path.display(),
                "RealmQuery capability got open request with non-empty",
            );
            return Ok(());
        }

        let server_end = channel::take_channel(server_end);

        let server_end = ServerEnd::<fsys::RealmQueryMarker>::new(server_end);
        let stream: fsys::RealmQueryRequestStream =
            server_end.into_stream().map_err(ModelError::stream_creation_error)?;
        task_scope
            .add_task(async move {
                self.query.serve(self.scope_moniker, stream).await;
            })
            .await;

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::component::StartReason,
        crate::model::testing::test_helpers::{TestEnvironmentBuilder, TestModelResult},
        assert_matches::assert_matches,
        cm_rust::*,
        cm_rust_testing::ComponentDeclBuilder,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_config as fconfig,
        fidl_fuchsia_component_decl as fdecl, fuchsia_async as fasync,
        moniker::*,
        routing_test_helpers::component_id_index::make_index_file,
    };

    #[fuchsia::test]
    async fn read_all_properties() {
        // Create index.
        let iid = format!("1234{}", "5".repeat(60));
        let index_file = make_index_file(component_id_index::Index {
            instances: vec![component_id_index::InstanceIdEntry {
                instance_id: Some(iid.clone()),
                appmgr_moniker: None,
                moniker: Some(AbsoluteMoniker::parse_str("/").unwrap()),
            }],
            ..component_id_index::Index::default()
        })
        .unwrap();

        let use_decl = UseDecl::Protocol(UseProtocolDecl {
            source: UseSource::Framework,
            source_name: "foo".into(),
            target_path: CapabilityPath::try_from("/svc/foo").unwrap(),
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });

        let expose_decl = ExposeDecl::Protocol(ExposeProtocolDecl {
            source: ExposeSource::Self_,
            source_name: "bar".into(),
            target: ExposeTarget::Parent,
            target_name: "bar".into(),
        });

        let checksum = ConfigChecksum::Sha256([
            0x07, 0xA8, 0xE6, 0x85, 0xC8, 0x79, 0xA9, 0x79, 0xC3, 0x26, 0x17, 0xDC, 0x4E, 0x74,
            0x65, 0x7F, 0xF1, 0xF7, 0x73, 0xE7, 0x12, 0xEE, 0x51, 0xFD, 0xF6, 0x57, 0x43, 0x07,
            0xA7, 0xAF, 0x2E, 0x64,
        ]);

        let config = ConfigDecl {
            fields: vec![ConfigField { key: "my_field".to_string(), type_: ConfigValueType::Bool }],
            checksum: checksum.clone(),
            value_source: ConfigValueSource::PackagePath("meta/root.cvf".into()),
        };

        let config_values = ValuesData {
            values: vec![ValueSpec { value: Value::Single(SingleValue::Bool(true)) }],
            checksum: checksum.clone(),
        };

        let components = vec![(
            "root",
            ComponentDeclBuilder::new()
                .add_config(config)
                .use_(use_decl.clone())
                .expose(expose_decl.clone())
                .build(),
        )];

        let TestModelResult { model, builtin_environment, .. } = TestEnvironmentBuilder::new()
            .set_components(components)
            .set_component_id_index_path(index_file.path().to_str().map(str::to_string))
            .set_config_values(vec![("meta/root.cvf", config_values)])
            .build()
            .await;

        let realm_query = {
            let env = builtin_environment.lock().await;
            env.realm_query.clone().unwrap()
        };

        let (query, query_request_stream) =
            create_proxy_and_stream::<fsys::RealmQueryMarker>().unwrap();

        let _query_task = fasync::Task::local(async move {
            realm_query.serve(AbsoluteMoniker::root(), query_request_stream).await
        });

        model.start().await;

        let (info, resolved) = query.get_instance_info("./").await.unwrap().unwrap();
        assert_eq!(info.moniker, ".");
        assert_eq!(info.url, "test:///root");
        assert_eq!(info.state, fsys::InstanceState::Started);
        assert_eq!(info.instance_id.clone().unwrap(), iid);

        let resolved = resolved.unwrap();
        let execution = resolved.execution.unwrap();

        // Component should have one config field with right value
        let config = resolved.config.unwrap();
        assert_eq!(config.fields.len(), 1);
        let field = &config.fields[0];
        assert_eq!(field.key, "my_field");
        assert_matches!(field.value, fconfig::Value::Single(fconfig::SingleValue::Bool(true)));
        assert_eq!(config.checksum, checksum.native_into_fidl());

        // Component should have one use and one expose decl
        assert_eq!(resolved.uses.len(), 1);
        assert_eq!(resolved.uses[0], use_decl.native_into_fidl());
        assert_eq!(resolved.exposes.len(), 1);
        assert_eq!(resolved.exposes[0], expose_decl.native_into_fidl());

        // Test resolvers provide a pkg dir with a fake file
        let pkg_dir = resolved.pkg_dir.unwrap();
        let pkg_dir = pkg_dir.into_proxy().unwrap();
        let entries = fuchsia_fs::directory::readdir(&pkg_dir).await.unwrap();
        assert_eq!(
            entries,
            vec![fuchsia_fs::directory::DirEntry {
                name: "fake_file".to_string(),
                kind: fuchsia_fs::directory::DirentKind::File
            }]
        );

        // Component Manager serves the exposed dir with the `bar` protocol
        let exposed_dir = resolved.exposed_dir.into_proxy().unwrap();
        let entries = fuchsia_fs::directory::readdir(&exposed_dir).await.unwrap();
        assert_eq!(
            entries,
            vec![fuchsia_fs::directory::DirEntry {
                name: "bar".to_string(),
                kind: fuchsia_fs::directory::DirentKind::Unknown
            }]
        );

        // Component Manager serves the namespace dir with the `foo` protocol.
        let ns_dir = resolved.ns_dir.into_proxy().unwrap();
        let entries = fuchsia_fs::directory::readdir(&ns_dir).await.unwrap();
        assert_eq!(
            entries,
            vec![
                fuchsia_fs::directory::DirEntry {
                    name: "pkg".to_string(),
                    kind: fuchsia_fs::directory::DirentKind::Directory
                },
                fuchsia_fs::directory::DirEntry {
                    name: "svc".to_string(),
                    kind: fuchsia_fs::directory::DirentKind::Directory
                },
            ]
        );
        let svc_dir =
            fuchsia_fs::directory::open_directory(&ns_dir, "svc", fio::OpenFlags::RIGHT_READABLE)
                .await
                .unwrap();
        let entries = fuchsia_fs::directory::readdir(&svc_dir).await.unwrap();
        assert_eq!(
            entries,
            vec![fuchsia_fs::directory::DirEntry {
                name: "foo".to_string(),
                kind: fuchsia_fs::directory::DirentKind::Unknown
            }]
        );

        // Test runners don't provide an out dir or a runtime dir
        assert!(execution.out_dir.is_none());
        assert!(execution.runtime_dir.is_none());
    }

    #[fuchsia::test]
    async fn observe_dynamic_lifecycle() {
        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_collection(CollectionDecl {
                        name: "my_coll".to_string(),
                        durability: fdecl::Durability::Transient,
                        environment: None,
                        allowed_offers: cm_types::AllowedOffers::StaticOnly,
                        allow_long_names: false,
                        persistent_storage: None,
                    })
                    .build(),
            ),
            ("a", ComponentDeclBuilder::new().build()),
        ];

        let TestModelResult { model, builtin_environment, .. } =
            TestEnvironmentBuilder::new().set_components(components).build().await;

        let realm_query = {
            let env = builtin_environment.lock().await;
            env.realm_query.clone().unwrap()
        };

        let (query, query_request_stream) =
            create_proxy_and_stream::<fsys::RealmQueryMarker>().unwrap();

        let _query_task = fasync::Task::local(async move {
            realm_query.serve(AbsoluteMoniker::root(), query_request_stream).await
        });

        model.start().await;

        let component_root = model.look_up(&AbsoluteMoniker::root()).await.unwrap();
        component_root
            .add_dynamic_child(
                "my_coll".to_string(),
                &ChildDecl {
                    name: "a".to_string(),
                    url: "test:///a".to_string(),
                    startup: fdecl::StartupMode::Lazy,
                    on_terminate: None,
                    environment: None,
                },
                fcomponent::CreateChildArgs::EMPTY,
            )
            .await
            .unwrap();

        // `a` should be unresolved
        let (info, resolved) = query.get_instance_info("./my_coll:a").await.unwrap().unwrap();
        assert_eq!(info.moniker, "./my_coll:a");
        assert_eq!(info.url, "test:///a");
        assert_eq!(info.state, fsys::InstanceState::Unresolved);
        assert!(info.instance_id.is_none());
        assert!(resolved.is_none());

        let moniker_a = AbsoluteMoniker::parse_str("/my_coll:a").unwrap();
        let component_a = model.look_up(&moniker_a).await.unwrap();

        // `a` should be resolved
        let (info, resolved) = query.get_instance_info("./my_coll:a").await.unwrap().unwrap();
        assert_eq!(info.state, fsys::InstanceState::Resolved);

        let resolved = resolved.unwrap();
        assert!(resolved.config.is_none());
        assert!(resolved.uses.is_empty());
        assert!(resolved.exposes.is_empty());
        assert!(resolved.pkg_dir.is_some());
        assert!(resolved.execution.is_none());

        let result = component_a.start(&StartReason::Debug).await.unwrap();
        assert_eq!(result, fsys::StartResult::Started);

        // `a` should be started
        let (info, resolved) = query.get_instance_info("./my_coll:a").await.unwrap().unwrap();
        assert_eq!(info.state, fsys::InstanceState::Started);

        let resolved = resolved.unwrap();
        assert!(resolved.config.is_none());
        assert!(resolved.uses.is_empty());
        assert!(resolved.exposes.is_empty());
        assert!(resolved.pkg_dir.is_some());

        let execution = resolved.execution.unwrap();
        assert!(execution.out_dir.is_none());
        assert!(execution.runtime_dir.is_none());

        component_a.stop_instance(false, false).await.unwrap();

        // `a` should be stopped
        let (info, resolved) = query.get_instance_info("./my_coll:a").await.unwrap().unwrap();
        assert_eq!(info.state, fsys::InstanceState::Resolved);

        let resolved = resolved.unwrap();
        assert!(resolved.config.is_none());
        assert!(resolved.uses.is_empty());
        assert!(resolved.exposes.is_empty());
        assert!(resolved.pkg_dir.is_some());

        assert!(resolved.execution.is_none());

        let child_moniker = ChildMoniker::parse("my_coll:a").unwrap();
        component_root.remove_dynamic_child(&child_moniker).await.unwrap();

        // `a` should be destroyed after purge
        let err = query.get_instance_info("./my_coll:a").await.unwrap().unwrap_err();
        assert_eq!(err, fsys::RealmQueryError::InstanceNotFound);
    }

    #[fuchsia::test]
    async fn scoped_to_child() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().build()),
        ];

        let TestModelResult { model, builtin_environment, .. } =
            TestEnvironmentBuilder::new().set_components(components).build().await;

        let realm_query = {
            let env = builtin_environment.lock().await;
            env.realm_query.clone().unwrap()
        };

        let (query, query_request_stream) =
            create_proxy_and_stream::<fsys::RealmQueryMarker>().unwrap();

        let moniker_a = AbsoluteMoniker::parse_str("/a").unwrap();

        let _query_task =
            fasync::Task::local(
                async move { realm_query.serve(moniker_a, query_request_stream).await },
            );

        model.start().await;

        // `a` should be unresolved
        let (info, resolved) = query.get_instance_info(".").await.unwrap().unwrap();
        assert_eq!(info.moniker, ".");
        assert_eq!(info.url, "test:///a");
        assert_eq!(info.state, fsys::InstanceState::Unresolved);
        assert!(info.instance_id.is_none());
        assert!(resolved.is_none());

        let moniker_a = AbsoluteMoniker::parse_str("/a").unwrap();
        let component_a = model.look_up(&moniker_a).await.unwrap();

        // `a` should be resolved
        let (info, resolved) = query.get_instance_info(".").await.unwrap().unwrap();
        assert_eq!(info.state, fsys::InstanceState::Resolved);

        let resolved = resolved.unwrap();
        assert!(resolved.config.is_none());
        assert!(resolved.uses.is_empty());
        assert!(resolved.exposes.is_empty());
        assert!(resolved.pkg_dir.is_some());

        assert!(resolved.execution.is_none());

        let result = component_a.start(&StartReason::Debug).await.unwrap();
        assert_eq!(result, fsys::StartResult::Started);

        // `a` should be started
        let (info, resolved) = query.get_instance_info(".").await.unwrap().unwrap();
        assert_eq!(info.state, fsys::InstanceState::Started);

        let resolved = resolved.unwrap();
        assert!(resolved.config.is_none());
        assert!(resolved.uses.is_empty());
        assert!(resolved.exposes.is_empty());
        assert!(resolved.pkg_dir.is_some());

        let execution = resolved.execution.unwrap();
        assert!(execution.out_dir.is_none());
        assert!(execution.runtime_dir.is_none());

        component_a.stop_instance(false, false).await.unwrap();

        // `a` should be stopped
        let (info, resolved) = query.get_instance_info(".").await.unwrap().unwrap();
        assert_eq!(info.state, fsys::InstanceState::Resolved);

        let resolved = resolved.unwrap();
        assert!(resolved.config.is_none());
        assert!(resolved.uses.is_empty());
        assert!(resolved.exposes.is_empty());
        assert!(resolved.pkg_dir.is_some());

        assert!(resolved.execution.is_none());
    }

    #[fuchsia::test]
    async fn get_instance_directories() {
        let use_decl = UseDecl::Protocol(UseProtocolDecl {
            source: UseSource::Framework,
            source_name: "foo".into(),
            target_path: CapabilityPath::try_from("/svc/foo").unwrap(),
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        });

        let components = vec![("root", ComponentDeclBuilder::new().use_(use_decl.clone()).build())];

        let TestModelResult { model, builtin_environment, .. } =
            TestEnvironmentBuilder::new().set_components(components).build().await;

        let realm_query = {
            let env = builtin_environment.lock().await;
            env.realm_query.clone().unwrap()
        };

        let (query, query_request_stream) =
            create_proxy_and_stream::<fsys::RealmQueryMarker>().unwrap();

        let _query_task = fasync::Task::local(async move {
            realm_query.serve(AbsoluteMoniker::root(), query_request_stream).await
        });

        model.start().await;

        let resolved_dirs = query.get_instance_directories("./").await.unwrap().unwrap().unwrap();

        let ns_entries = resolved_dirs.ns_entries;
        assert_eq!(ns_entries.len(), 2);

        for entry in ns_entries {
            let path = entry.path.unwrap();
            let dir = entry.directory.unwrap().into_proxy().unwrap();
            match path.as_str() {
                "/svc" => {
                    let entries = fuchsia_fs::directory::readdir(&dir).await.unwrap();
                    assert_eq!(
                        entries,
                        vec![fuchsia_fs::directory::DirEntry {
                            name: "foo".to_string(),
                            kind: fuchsia_fs::directory::DirentKind::Unknown
                        }]
                    );
                }
                "/pkg" => {}
                path => panic!("unexpected directory: {}", path),
            }
        }

        assert!(resolved_dirs.pkg_dir.is_some());

        let execution_dirs = resolved_dirs.execution_dirs.unwrap();
        assert!(execution_dirs.out_dir.is_none());
        assert!(execution_dirs.runtime_dir.is_none());
    }
}
