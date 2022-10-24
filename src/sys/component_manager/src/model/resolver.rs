// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        component::{ComponentInstance, WeakComponentInstance},
        routing::{route_and_open_capability, OpenOptions, OpenResolverOptions, RouteRequest},
    },
    ::routing::component_instance::ComponentInstanceInterface,
    ::routing::resolving::{ComponentAddress, ResolvedComponent, ResolverError},
    async_trait::async_trait,
    cm_rust::{FidlIntoNative, RegistrationSource, ResolverRegistration},
    fidl_fuchsia_component_decl as fdecl, fidl_fuchsia_component_resolution as fresolution,
    fidl_fuchsia_io as fio, fidl_fuchsia_mem as fmem,
    std::{collections::HashMap, convert::TryInto, sync::Arc},
    tracing::error,
};

/// Resolves a component URL to its content.
#[async_trait]
pub trait Resolver: std::fmt::Debug {
    /// Resolves a component URL to its content. This function takes in the
    /// `component_address` (from an absolute or relative URL), and the `target`
    /// component that is trying to be resolved.
    async fn resolve(
        &self,
        component_address: &ComponentAddress,
        target: &Arc<ComponentInstance>,
    ) -> Result<ResolvedComponent, ResolverError>;
}

/// Resolves a component URL using a resolver selected based on the URL's scheme.
#[derive(Debug, Default)]
pub struct ResolverRegistry {
    resolvers: HashMap<String, Box<dyn Resolver + Send + Sync + 'static>>,
}

impl ResolverRegistry {
    pub fn new() -> ResolverRegistry {
        Default::default()
    }

    pub fn register(
        &mut self,
        scheme: String,
        resolver: Box<dyn Resolver + Send + Sync + 'static>,
    ) {
        // ComponentDecl validation checks that there aren't any duplicate schemes.
        assert!(
            self.resolvers.insert(scheme, resolver).is_none(),
            "Found duplicate scheme in ComponentDecl"
        );
    }

    /// Creates and populates a `ResolverRegistry` with `RemoteResolvers` that
    /// have been registered with an environment.
    pub fn from_decl(decl: &[ResolverRegistration], parent: &Arc<ComponentInstance>) -> Self {
        let mut registry = ResolverRegistry::new();
        for resolver in decl {
            registry.register(
                resolver.scheme.clone().into(),
                Box::new(RemoteResolver::new(resolver.clone(), parent.as_weak())),
            );
        }
        registry
    }
}

#[async_trait]
impl Resolver for ResolverRegistry {
    async fn resolve(
        &self,
        component_address: &ComponentAddress,
        target: &Arc<ComponentInstance>,
    ) -> Result<ResolvedComponent, ResolverError> {
        if let Some(resolver) = self.resolvers.get(component_address.scheme()) {
            resolver.resolve(component_address, target).await
        } else {
            Err(ResolverError::SchemeNotRegistered)
        }
    }
}

/// A resolver whose implementation lives in an external component. The source
/// of the resolver is determined through capability routing.
#[derive(Debug)]
pub struct RemoteResolver {
    registration: ResolverRegistration,
    component: WeakComponentInstance,
}

impl RemoteResolver {
    pub fn new(registration: ResolverRegistration, component: WeakComponentInstance) -> Self {
        RemoteResolver { registration, component }
    }
}

// TODO(61288): Implement some sort of caching of the routed capability. Multiple
// component URL resolutions should be possible on a single channel.
#[async_trait]
impl Resolver for RemoteResolver {
    async fn resolve(
        &self,
        component_address: &ComponentAddress,
        _target: &Arc<ComponentInstance>,
    ) -> Result<ResolvedComponent, ResolverError> {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<fresolution::ResolverMarker>()
            .map_err(ResolverError::internal)?;
        let component = self.component.upgrade().map_err(ResolverError::routing_error)?;
        let open_options = OpenResolverOptions {
            flags: fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            open_mode: fio::MODE_TYPE_SERVICE,
            server_chan: &mut server_end.into_channel(),
        };
        route_and_open_capability(
            RouteRequest::Resolver(self.registration.clone()),
            &component,
            OpenOptions::Resolver(open_options),
        )
        .await
        .map_err(ResolverError::routing_error)?;
        let (component_url, some_context) = component_address.to_url_and_context();
        let component = if component_address.is_relative_path() {
            let context = some_context.ok_or_else(|| {
                error!(url=%component_url, "calling resolve_with_context() with absolute");
                ResolverError::RelativeUrlMissingContext(component_url.to_string())
            })?;
            proxy
                .resolve_with_context(component_url, &mut context.into())
                .await
                .map_err(ResolverError::fidl_error)??
        } else {
            proxy.resolve(component_url).await.map_err(ResolverError::fidl_error)??
        };
        let decl_buffer: fmem::Data = component.decl.ok_or(ResolverError::RemoteInvalidData)?;
        let decl = read_and_validate_manifest(&decl_buffer).await?;
        let config_values = if decl.config.is_some() {
            Some(read_and_validate_config_values(
                &component.config_values.ok_or(ResolverError::RemoteInvalidData)?,
            )?)
        } else {
            None
        };
        let resolved_by = format!(
            "RemoteResolver::{}{}",
            if let RegistrationSource::Child(ref name) = self.registration.source {
                name.to_string() + "/"
            } else {
                "".into()
            },
            self.registration.resolver.str()
        );
        let resolved_url = component.url.ok_or(ResolverError::RemoteInvalidData)?;
        let context_to_resolve_children = component.resolution_context.map(Into::into);
        Ok(ResolvedComponent {
            resolved_by,
            resolved_url,
            context_to_resolve_children,
            decl,
            package: component.package.map(TryInto::try_into).transpose()?,
            config_values,
        })
    }
}

#[derive(Debug)]
pub struct BuiltinResolver(pub Arc<dyn Resolver + Send + Sync + 'static>);

#[async_trait]
impl Resolver for BuiltinResolver {
    async fn resolve(
        &self,
        component_address: &ComponentAddress,
        target: &Arc<ComponentInstance>,
    ) -> Result<ResolvedComponent, ResolverError> {
        self.0.resolve(component_address, target).await
    }
}

pub async fn read_and_validate_manifest(
    data: &fmem::Data,
) -> Result<cm_rust::ComponentDecl, ResolverError> {
    let bytes = mem_util::bytes_from_data(data).map_err(ResolverError::manifest_invalid)?;
    let component_decl: fdecl::Component =
        fidl::encoding::decode_persistent(&bytes).map_err(ResolverError::manifest_invalid)?;
    cm_fidl_validator::validate(&component_decl).map_err(ResolverError::manifest_invalid)?;
    Ok(component_decl.fidl_into_native())
}

pub fn read_and_validate_config_values(
    data: &fmem::Data,
) -> Result<cm_rust::ValuesData, ResolverError> {
    let bytes = mem_util::bytes_from_data(&data).map_err(ResolverError::config_values_invalid)?;
    let values = fidl::encoding::decode_persistent(&bytes).map_err(ResolverError::fidl_error)?;
    cm_fidl_validator::validate_values_data(&values)
        .map_err(|e| ResolverError::config_values_invalid(e))?;
    Ok(values.fidl_into_native())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::{
            component::{ComponentInstance, ComponentManagerInstance, WeakExtendedInstance},
            context::WeakModelContext,
            environment::Environment,
            hooks::Hooks,
        },
        anyhow::{format_err, Error},
        assert_matches::assert_matches,
        async_trait::async_trait,
        cm_moniker::InstancedAbsoluteMoniker,
        cm_rust::NativeIntoFidl,
        cm_rust_testing::new_decl_from_json,
        fidl_fuchsia_component_config as fconfig, fidl_fuchsia_component_decl as fdecl,
        lazy_static::lazy_static,
        moniker::AbsoluteMonikerBase,
        routing::environment::{DebugRegistry, RunnerRegistry},
        routing::resolving::{ComponentAddressKind, ComponentResolutionContext},
        serde_json::json,
        std::sync::{Mutex, Weak},
    };

    #[derive(Debug)]
    struct MockOkResolver {
        pub expected_url: String,
        pub resolved_url: String,
    }

    #[async_trait]
    impl Resolver for MockOkResolver {
        async fn resolve(
            &self,
            component_address: &ComponentAddress,
            _target: &Arc<ComponentInstance>,
        ) -> Result<ResolvedComponent, ResolverError> {
            assert_eq!(Some(self.expected_url.as_str()), component_address.original_url());
            assert_eq!(self.expected_url.as_str(), component_address.url());
            Ok(ResolvedComponent {
                resolved_by: "resolver::MockOkResolver".into(),
                resolved_url: self.resolved_url.clone(),
                // MockOkResolver only resolves one component, so it does not
                // need to provide a context for resolving children.
                context_to_resolve_children: None,
                decl: cm_rust::ComponentDecl::default(),
                package: None,
                config_values: None,
            })
        }
    }

    struct MockErrorResolver {
        pub expected_url: String,
        pub error: Box<dyn Fn(&str) -> ResolverError + Send + Sync + 'static>,
    }

    impl core::fmt::Debug for MockErrorResolver {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            f.debug_struct("MockErrorResolver").finish()
        }
    }

    #[async_trait]
    impl Resolver for MockErrorResolver {
        async fn resolve(
            &self,
            component_address: &ComponentAddress,
            _target: &Arc<ComponentInstance>,
        ) -> Result<ResolvedComponent, ResolverError> {
            assert_eq!(Some(self.expected_url.as_str()), component_address.original_url());
            assert_eq!(self.expected_url, component_address.url());
            Err((self.error)(component_address.url()))
        }
    }

    #[derive(Debug, Clone)]
    struct ResolveState {
        pub expected_url: String,
        pub resolved_url: String,
        pub expected_context: Option<ComponentResolutionContext>,
        pub context_to_resolve_children: Option<ComponentResolutionContext>,
    }

    impl ResolveState {
        fn new(
            url: &str,
            expected_context: Option<ComponentResolutionContext>,
            context_to_resolve_children: Option<ComponentResolutionContext>,
        ) -> Self {
            Self {
                expected_url: url.to_string(),
                resolved_url: url.to_string(),
                expected_context,
                context_to_resolve_children,
            }
        }
    }

    #[derive(Debug)]
    struct MockMultipleOkResolver {
        pub resolve_states: Arc<Mutex<Vec<ResolveState>>>,
    }

    impl MockMultipleOkResolver {
        fn new(resolve_states: Vec<ResolveState>) -> Self {
            Self { resolve_states: Arc::new(Mutex::new(resolve_states)) }
        }
    }

    #[async_trait]
    impl Resolver for MockMultipleOkResolver {
        async fn resolve(
            &self,
            component_address: &ComponentAddress,
            _target: &Arc<ComponentInstance>,
        ) -> Result<ResolvedComponent, ResolverError> {
            let ResolveState {
                expected_url,
                resolved_url,
                expected_context,
                context_to_resolve_children,
            } = self.resolve_states.lock().unwrap().remove(0);
            let (component_url, some_context) = component_address.to_url_and_context();
            assert_eq!(expected_url, component_url);
            assert_eq!(expected_context.as_ref(), some_context, "resolving {}", component_url);
            Ok(ResolvedComponent {
                resolved_by: "resolver::MockMultipleOkResolver".into(),
                resolved_url,
                context_to_resolve_children,

                // We don't actually need to return a valid component here as these unit tests only
                // cover the process of going from relative -> full URL.
                decl: cm_rust::ComponentDecl::default(),
                package: None,
                config_values: None,
            })
        }
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn register_and_resolve() {
        let mut registry = ResolverRegistry::new();
        registry.register(
            "foo".to_string(),
            Box::new(MockOkResolver {
                expected_url: "foo://url".to_string(),
                resolved_url: "foo://resolved".to_string(),
            }),
        );
        registry.register(
            "bar".to_string(),
            Box::new(MockErrorResolver {
                expected_url: "bar://url".to_string(),
                error: Box::new(|_| {
                    ResolverError::manifest_not_found(format_err!("not available"))
                }),
            }),
        );

        let root = ComponentInstance::new_root(
            Environment::empty(),
            Weak::new(),
            Weak::new(),
            "fuchsia-boot:///#meta/root.cm".to_string(),
        );

        // Resolve known scheme that returns success.
        let component = registry
            .resolve(&ComponentAddress::from_absolute_url("foo://url").unwrap(), &root)
            .await
            .unwrap();
        assert_eq!("foo://resolved", component.resolved_url);

        // Resolve a different scheme that produces an error.
        let expected_res: Result<ResolvedComponent, ResolverError> =
            Err(ResolverError::manifest_not_found(format_err!("not available")));
        assert_eq!(
            format!("{:?}", expected_res),
            format!(
                "{:?}",
                registry
                    .resolve(&ComponentAddress::from_absolute_url("bar://url").unwrap(), &root)
                    .await
            )
        );

        // Resolve an unknown scheme
        let expected_res: Result<ResolvedComponent, ResolverError> =
            Err(ResolverError::SchemeNotRegistered);
        assert_eq!(
            format!("{:?}", expected_res),
            format!(
                "{:?}",
                registry
                    .resolve(&ComponentAddress::from_absolute_url("unknown://url").unwrap(), &root)
                    .await
            ),
        );

        // Resolve a possible relative path (e.g., subpackage) URL lacking a
        // resolvable parent causes a SchemeNotRegistered.
        assert_matches!(
            ComponentAddress::from("xxx#meta/comp.cm", &root).await,
            Err(ResolverError::NoParentContext(_))
        );
    }

    #[fuchsia::test]
    #[should_panic(expected = "Found duplicate scheme in ComponentDecl")]
    fn test_duplicate_registration() {
        let mut registry = ResolverRegistry::new();
        let resolver_a =
            MockOkResolver { expected_url: "".to_string(), resolved_url: "".to_string() };
        let resolver_b =
            MockOkResolver { expected_url: "".to_string(), resolved_url: "".to_string() };
        registry.register("fuchsia-pkg".to_string(), Box::new(resolver_a));
        registry.register("fuchsia-pkg".to_string(), Box::new(resolver_b));
    }

    #[fuchsia::test]
    fn test_multiple_scheme_registration() {
        let mut registry = ResolverRegistry::new();
        let resolver_a =
            MockOkResolver { expected_url: "".to_string(), resolved_url: "".to_string() };
        let resolver_b =
            MockOkResolver { expected_url: "".to_string(), resolved_url: "".to_string() };
        registry.register("fuchsia-pkg".to_string(), Box::new(resolver_a));
        registry.register("fuchsia-boot".to_string(), Box::new(resolver_b));
    }

    lazy_static! {
        static ref COMPONENT_DECL: cm_rust::ComponentDecl = new_decl_from_json(json!(
        {
            "include": [ "syslog/client.shard.cml" ],
            "program": {
                "runner": "elf",
                "binary": "bin/example",
            },
            "children": [
                {
                    "name": "logger",
                    "url": "fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm",
                    "environment": "#env_one",
                },
            ],
            "collections": [
                {
                    "name": "modular",
                    "durability": "transient",
                },
            ],
            "capabilities": [
                {
                    "protocol": "fuchsia.logger.Log2",
                    "path": "/svc/fuchsia.logger.Log2",
                },
            ],
            "use": [
                {
                    "protocol": "fuchsia.fonts.LegacyProvider",
                },
            ],
            "environments": [
                {
                    "name": "env_one",
                    "extends": "none",
                    "__stop_timeout_ms": 1337,
                },
            ],
            "facets": {
                "author": "Fuchsia",
            }}))
        .expect("failed to construct manifest");
    }

    #[fuchsia::test]
    async fn test_read_and_validate_manifest() {
        let manifest = fmem::Data::Bytes(
            fidl::encoding::encode_persistent::<fdecl::Component>(
                &mut (COMPONENT_DECL.clone()).native_into_fidl(),
            )
            .expect("failed to encode manifest"),
        );
        let actual =
            read_and_validate_manifest(&manifest).await.expect("failed to decode manifest");
        assert_eq!(actual, COMPONENT_DECL.clone());
    }

    #[fuchsia::test]
    async fn test_read_and_validate_config_values() {
        let mut fidl_config_values = fconfig::ValuesData {
            values: Some(vec![
                fconfig::ValueSpec {
                    value: Some(fconfig::Value::Single(fconfig::SingleValue::Bool(false))),
                    ..fconfig::ValueSpec::EMPTY
                },
                fconfig::ValueSpec {
                    value: Some(fconfig::Value::Single(fconfig::SingleValue::Uint8(5))),
                    ..fconfig::ValueSpec::EMPTY
                },
                fconfig::ValueSpec {
                    value: Some(fconfig::Value::Single(fconfig::SingleValue::String(
                        "hello!".to_string(),
                    ))),
                    ..fconfig::ValueSpec::EMPTY
                },
                fconfig::ValueSpec {
                    value: Some(fconfig::Value::Vector(fconfig::VectorValue::BoolVector(vec![
                        true, false,
                    ]))),
                    ..fconfig::ValueSpec::EMPTY
                },
                fconfig::ValueSpec {
                    value: Some(fconfig::Value::Vector(fconfig::VectorValue::StringVector(vec![
                        "hello!".to_string(),
                        "world!".to_string(),
                    ]))),
                    ..fconfig::ValueSpec::EMPTY
                },
            ]),
            checksum: Some(fdecl::ConfigChecksum::Sha256([0; 32])),
            ..fconfig::ValuesData::EMPTY
        };
        let config_values = cm_rust::ValuesData {
            values: vec![
                cm_rust::ValueSpec {
                    value: cm_rust::Value::Single(cm_rust::SingleValue::Bool(false)),
                },
                cm_rust::ValueSpec {
                    value: cm_rust::Value::Single(cm_rust::SingleValue::Uint8(5)),
                },
                cm_rust::ValueSpec {
                    value: cm_rust::Value::Single(cm_rust::SingleValue::String(
                        "hello!".to_string(),
                    )),
                },
                cm_rust::ValueSpec {
                    value: cm_rust::Value::Vector(cm_rust::VectorValue::BoolVector(vec![
                        true, false,
                    ])),
                },
                cm_rust::ValueSpec {
                    value: cm_rust::Value::Vector(cm_rust::VectorValue::StringVector(vec![
                        "hello!".to_string(),
                        "world!".to_string(),
                    ])),
                },
            ],
            checksum: cm_rust::ConfigChecksum::Sha256([0; 32]),
        };
        let data = fmem::Data::Bytes(
            fidl::encoding::encode_persistent(&mut fidl_config_values)
                .expect("failed to encode config values"),
        );
        let actual =
            read_and_validate_config_values(&data).expect("failed to decode config values");
        assert_eq!(actual, config_values);
    }

    #[fuchsia::test]
    async fn test_from_absolute_component_url_with_component_instance() -> Result<(), Error> {
        let top_instance = Arc::new(ComponentManagerInstance::new(vec![], vec![]));
        let environment = Environment::new_root(
            &top_instance,
            RunnerRegistry::default(),
            ResolverRegistry::new(),
            DebugRegistry::default(),
        );
        let root = ComponentInstance::new_root(
            environment,
            Weak::new(),
            Weak::new(),
            "fuchsia-pkg://fuchsia.com/package#meta/comp.cm".to_string(),
        );

        let abs =
            ComponentAddress::from("fuchsia-pkg://fuchsia.com/package#meta/comp.cm", &root).await?;
        assert_matches!(abs.kind(), ComponentAddressKind::Absolute { .. });
        assert_eq!(abs.authority_or_empty_str(), "fuchsia.com");
        assert_eq!(abs.scheme(), "fuchsia-pkg");
        assert_eq!(abs.path(), "/package");
        assert_eq!(abs.resource(), Some("meta/comp.cm"));
        Ok(())
    }

    #[fuchsia::test]
    async fn test_from_relative_path_component_url_with_component_instance() -> Result<(), Error> {
        let expected_urls_and_contexts = vec![
            ResolveState::new(
                "fuchsia-pkg://fuchsia.com/package#meta/comp.cm",
                None,
                Some(ComponentResolutionContext::new("package_context".as_bytes().to_vec())),
            ),
            ResolveState::new(
                "subpackage#meta/subcomp.cm",
                Some(ComponentResolutionContext::new("package_context".as_bytes().to_vec())),
                Some(ComponentResolutionContext::new("subpackage_context".as_bytes().to_vec())),
            ),
        ];
        let mut resolver = ResolverRegistry::new();

        resolver.register(
            "fuchsia-pkg".to_string(),
            Box::new(MockMultipleOkResolver::new(expected_urls_and_contexts.clone())),
        );

        let top_instance = Arc::new(ComponentManagerInstance::new(vec![], vec![]));
        let environment = Environment::new_root(
            &top_instance,
            RunnerRegistry::default(),
            resolver,
            DebugRegistry::default(),
        );
        let root = ComponentInstance::new_root(
            environment,
            Weak::new(),
            Weak::new(),
            "fuchsia-pkg://fuchsia.com/package#meta/comp.cm".to_string(),
        );
        let child = ComponentInstance::new(
            root.environment.clone(),
            InstancedAbsoluteMoniker::parse_str("/root:0/child:0")?,
            "subpackage#meta/subcomp.cm".to_string(),
            fdecl::StartupMode::Lazy,
            fdecl::OnTerminate::None,
            WeakModelContext::new(Weak::new()),
            WeakExtendedInstance::Component(WeakComponentInstance::from(&root)),
            Arc::new(Hooks::new()),
            None,
            false,
        );

        let relpath = ComponentAddress::from("subpackage#meta/subcomp.cm", &child).await?;
        assert_matches!(relpath.kind(), ComponentAddressKind::RelativePath { .. });
        assert_eq!(relpath.path(), "subpackage");
        assert_eq!(relpath.resource(), Some("meta/subcomp.cm"));
        assert_eq!(
            relpath.context(),
            &ComponentResolutionContext::new("package_context".as_bytes().to_vec())
        );
        Ok(())
    }

    #[fuchsia::test]
    async fn relative_to_fuchsia_pkg() -> Result<(), Error> {
        let expected_urls_and_contexts = vec![
            ResolveState::new(
                "fuchsia-pkg://fuchsia.com/my-package#meta/my-root.cm",
                None,
                Some(ComponentResolutionContext::new("package_context".as_bytes().to_vec())),
            ),
            ResolveState::new(
                "fuchsia-pkg://fuchsia.com/my-package#meta/my-child.cm",
                None,
                Some(ComponentResolutionContext::new("package_context".as_bytes().to_vec())),
            ),
        ];
        let mut resolver = ResolverRegistry::new();

        resolver.register(
            "fuchsia-pkg".to_string(),
            Box::new(MockMultipleOkResolver::new(expected_urls_and_contexts.clone())),
        );

        let top_instance = Arc::new(ComponentManagerInstance::new(vec![], vec![]));
        let environment = Environment::new_root(
            &top_instance,
            RunnerRegistry::default(),
            resolver,
            DebugRegistry::default(),
        );
        let root = ComponentInstance::new_root(
            environment,
            Weak::new(),
            Weak::new(),
            "fuchsia-pkg://fuchsia.com/my-package#meta/my-root.cm".to_string(),
        );

        let child = ComponentInstance::new(
            root.environment.clone(),
            InstancedAbsoluteMoniker::parse_str("/root:0/child:0")?,
            "#meta/my-child.cm".to_string(),
            fdecl::StartupMode::Lazy,
            fdecl::OnTerminate::None,
            WeakModelContext::new(Weak::new()),
            WeakExtendedInstance::Component(WeakComponentInstance::from(&root)),
            Arc::new(Hooks::new()),
            None,
            false,
        );

        let resolved = child
            .environment
            .resolve(&ComponentAddress::from(&child.component_url, &child).await?, &child)
            .await?;
        let expected = expected_urls_and_contexts.as_slice().last().unwrap();
        assert_eq!(&resolved.resolved_url, &expected.resolved_url);
        assert_eq!(&resolved.context_to_resolve_children, &expected.context_to_resolve_children);

        Ok(())
    }

    #[fuchsia::test]
    async fn two_relative_to_fuchsia_pkg() -> Result<(), Error> {
        let expected_urls_and_contexts = vec![
            ResolveState::new(
                "fuchsia-pkg://fuchsia.com/my-package#meta/my-root.cm",
                None,
                Some(ComponentResolutionContext::new("package_context".as_bytes().to_vec())),
            ),
            ResolveState::new(
                "fuchsia-pkg://fuchsia.com/my-package#meta/my-child.cm",
                None,
                Some(ComponentResolutionContext::new("package_context".as_bytes().to_vec())),
            ),
            ResolveState::new(
                "fuchsia-pkg://fuchsia.com/my-package#meta/my-child2.cm",
                None,
                Some(ComponentResolutionContext::new("package_context".as_bytes().to_vec())),
            ),
        ];
        let mut resolver = ResolverRegistry::new();

        resolver.register(
            "fuchsia-pkg".to_string(),
            Box::new(MockMultipleOkResolver::new(expected_urls_and_contexts.clone())),
        );

        let top_instance = Arc::new(ComponentManagerInstance::new(vec![], vec![]));
        let environment = Environment::new_root(
            &top_instance,
            RunnerRegistry::default(),
            resolver,
            DebugRegistry::default(),
        );
        let root = ComponentInstance::new_root(
            environment,
            Weak::new(),
            Weak::new(),
            "fuchsia-pkg://fuchsia.com/my-package#meta/my-root.cm".to_string(),
        );

        let child_one = ComponentInstance::new(
            root.environment.clone(),
            InstancedAbsoluteMoniker::parse_str("/root:0/child:0")?,
            "#meta/my-child.cm".to_string(),
            fdecl::StartupMode::Lazy,
            fdecl::OnTerminate::None,
            WeakModelContext::new(Weak::new()),
            WeakExtendedInstance::Component(WeakComponentInstance::from(&root)),
            Arc::new(Hooks::new()),
            None,
            false,
        );

        let child_two = ComponentInstance::new(
            root.environment.clone(),
            InstancedAbsoluteMoniker::parse_str("/root:0/child:0")?,
            "#meta/my-child2.cm".to_string(),
            fdecl::StartupMode::Lazy,
            fdecl::OnTerminate::None,
            WeakModelContext::new(Weak::new()),
            WeakExtendedInstance::Component(WeakComponentInstance::from(&child_one)),
            Arc::new(Hooks::new()),
            None,
            false,
        );

        let resolved = child_two
            .environment
            .resolve(
                &ComponentAddress::from(&child_two.component_url, &child_two).await?,
                &child_two,
            )
            .await?;
        let expected = expected_urls_and_contexts.as_slice().last().unwrap();
        assert_eq!(&resolved.resolved_url, &expected.resolved_url);
        assert_eq!(&resolved.context_to_resolve_children, &expected.context_to_resolve_children);
        Ok(())
    }

    #[fuchsia::test]
    async fn relative_to_fuchsia_boot() -> Result<(), Error> {
        let expected_urls_and_contexts = vec![
            ResolveState::new(
                "fuchsia-boot:///#meta/my-root.cm",
                None,
                Some(ComponentResolutionContext::new("package_context".as_bytes().to_vec())),
            ),
            ResolveState::new(
                "fuchsia-boot:///#meta/my-child.cm",
                None,
                Some(ComponentResolutionContext::new("package_context".as_bytes().to_vec())),
            ),
        ];
        let mut resolver = ResolverRegistry::new();

        resolver.register(
            "fuchsia-boot".to_string(),
            Box::new(MockMultipleOkResolver::new(expected_urls_and_contexts.clone())),
        );

        let top_instance = Arc::new(ComponentManagerInstance::new(vec![], vec![]));
        let environment = Environment::new_root(
            &top_instance,
            RunnerRegistry::default(),
            resolver,
            DebugRegistry::default(),
        );
        let root = ComponentInstance::new_root(
            environment,
            Weak::new(),
            Weak::new(),
            "fuchsia-boot:///#meta/my-root.cm".to_string(),
        );

        let child = ComponentInstance::new(
            root.environment.clone(),
            InstancedAbsoluteMoniker::parse_str("/root:0/child:0")?,
            "#meta/my-child.cm".to_string(),
            fdecl::StartupMode::Lazy,
            fdecl::OnTerminate::None,
            WeakModelContext::new(Weak::new()),
            WeakExtendedInstance::Component(WeakComponentInstance::from(&root)),
            Arc::new(Hooks::new()),
            None,
            false,
        );

        let resolved = child
            .environment
            .resolve(&ComponentAddress::from(&child.component_url, &child).await?, &child)
            .await?;
        let expected = expected_urls_and_contexts.as_slice().last().unwrap();
        assert_eq!(&resolved.resolved_url, &expected.resolved_url);
        assert_eq!(&resolved.context_to_resolve_children, &expected.context_to_resolve_children);
        Ok(())
    }

    #[fuchsia::test]
    async fn resolve_above_root_error() -> Result<(), Error> {
        let resolver = ResolverRegistry::new();

        let top_instance = Arc::new(ComponentManagerInstance::new(vec![], vec![]));
        let environment = Environment::new_root(
            &top_instance,
            RunnerRegistry::default(),
            resolver,
            DebugRegistry::default(),
        );
        let root = ComponentInstance::new_root(
            environment,
            Weak::new(),
            Weak::new(),
            "#meta/my-root.cm".to_string(),
        );

        let child = ComponentInstance::new(
            root.environment.clone(),
            InstancedAbsoluteMoniker::parse_str("/root:0/child:0")?,
            "#meta/my-child.cm".to_string(),
            fdecl::StartupMode::Lazy,
            fdecl::OnTerminate::None,
            WeakModelContext::new(Weak::new()),
            WeakExtendedInstance::Component(WeakComponentInstance::from(&root)),
            Arc::new(Hooks::new()),
            None,
            false,
        );

        let result = ComponentAddress::from(&child.component_url, &child).await;
        assert_matches!(result, Err(ResolverError::Internal(..)));
        Ok(())
    }

    #[fuchsia::test]
    async fn relative_resource_and_path_to_fuchsia_pkg() -> Result<(), Error> {
        let expected_urls_and_contexts = vec![
            ResolveState::new(
                "fuchsia-pkg://fuchsia.com/my-package#meta/my-root.cm",
                None,
                Some(ComponentResolutionContext::new("fuchsia.com...".as_bytes().to_vec())),
            ),
            ResolveState::new(
                "my-subpackage#meta/my-child.cm",
                Some(ComponentResolutionContext::new("fuchsia.com...".as_bytes().to_vec())),
                Some(ComponentResolutionContext::new("my-subpackage...".as_bytes().to_vec())),
            ),
            ResolveState::new(
                "my-subpackage#meta/my-child2.cm",
                Some(ComponentResolutionContext::new("fuchsia.com...".as_bytes().to_vec())),
                Some(ComponentResolutionContext::new("my-subpackage...".as_bytes().to_vec())),
            ),
        ];
        let mut resolver = ResolverRegistry::new();

        resolver.register(
            "fuchsia-pkg".to_string(),
            Box::new(MockMultipleOkResolver::new(expected_urls_and_contexts.clone())),
        );

        let top_instance = Arc::new(ComponentManagerInstance::new(vec![], vec![]));
        let environment = Environment::new_root(
            &top_instance,
            RunnerRegistry::default(),
            resolver,
            DebugRegistry::default(),
        );
        let root = ComponentInstance::new_root(
            environment,
            Weak::new(),
            Weak::new(),
            "fuchsia-pkg://fuchsia.com/my-package#meta/my-root.cm".to_string(),
        );

        let child_one = ComponentInstance::new(
            root.environment.clone(),
            InstancedAbsoluteMoniker::parse_str("/root:0/child:0")?,
            "my-subpackage#meta/my-child.cm".to_string(),
            fdecl::StartupMode::Lazy,
            fdecl::OnTerminate::None,
            WeakModelContext::new(Weak::new()),
            WeakExtendedInstance::Component(WeakComponentInstance::from(&root)),
            Arc::new(Hooks::new()),
            None,
            false,
        );

        let child_two = ComponentInstance::new(
            root.environment.clone(),
            InstancedAbsoluteMoniker::parse_str("/root:0/child:0/child2:0")?,
            "#meta/my-child2.cm".to_string(),
            fdecl::StartupMode::Lazy,
            fdecl::OnTerminate::None,
            WeakModelContext::new(Weak::new()),
            WeakExtendedInstance::Component(WeakComponentInstance::from(&child_one)),
            Arc::new(Hooks::new()),
            None,
            false,
        );

        let resolved = child_two
            .environment
            .resolve(
                &ComponentAddress::from(&child_two.component_url, &child_two).await?,
                &child_two,
            )
            .await?;
        let expected = expected_urls_and_contexts.as_slice().last().unwrap();
        assert_eq!(&resolved.resolved_url, &expected.resolved_url);
        assert_eq!(&resolved.context_to_resolve_children, &expected.context_to_resolve_children);
        Ok(())
    }

    #[fuchsia::test]
    async fn two_relative_resources_and_path_to_fuchsia_pkg() -> Result<(), Error> {
        let expected_urls_and_contexts = vec![
            ResolveState::new(
                "fuchsia-pkg://fuchsia.com/my-package#meta/my-root.cm",
                None,
                Some(ComponentResolutionContext::new("fuchsia.com...".as_bytes().to_vec())),
            ),
            ResolveState::new(
                "my-subpackage#meta/my-child.cm",
                Some(ComponentResolutionContext::new("fuchsia.com...".as_bytes().to_vec())),
                Some(ComponentResolutionContext::new("my-subpackage...".as_bytes().to_vec())),
            ),
            ResolveState::new(
                "my-subpackage#meta/my-child2.cm",
                Some(ComponentResolutionContext::new("fuchsia.com...".as_bytes().to_vec())),
                Some(ComponentResolutionContext::new("my-subpackage...".as_bytes().to_vec())),
            ),
            ResolveState::new(
                "my-subpackage#meta/my-child3.cm",
                Some(ComponentResolutionContext::new("fuchsia.com...".as_bytes().to_vec())),
                Some(ComponentResolutionContext::new("my-subpackage...".as_bytes().to_vec())),
            ),
        ];
        let mut resolver = ResolverRegistry::new();

        resolver.register(
            "fuchsia-pkg".to_string(),
            Box::new(MockMultipleOkResolver::new(expected_urls_and_contexts.clone())),
        );

        let top_instance = Arc::new(ComponentManagerInstance::new(vec![], vec![]));
        let environment = Environment::new_root(
            &top_instance,
            RunnerRegistry::default(),
            resolver,
            DebugRegistry::default(),
        );
        let root = ComponentInstance::new_root(
            environment,
            Weak::new(),
            Weak::new(),
            "fuchsia-pkg://fuchsia.com/my-package#meta/my-root.cm".to_string(),
        );

        let child_one = ComponentInstance::new(
            root.environment.clone(),
            InstancedAbsoluteMoniker::parse_str("/root:0/child:0")?,
            "my-subpackage#meta/my-child.cm".to_string(),
            fdecl::StartupMode::Lazy,
            fdecl::OnTerminate::None,
            WeakModelContext::new(Weak::new()),
            WeakExtendedInstance::Component(WeakComponentInstance::from(&root)),
            Arc::new(Hooks::new()),
            None,
            false,
        );

        let child_two = ComponentInstance::new(
            root.environment.clone(),
            InstancedAbsoluteMoniker::parse_str("/root:0/child:0/child2:0")?,
            "#meta/my-child2.cm".to_string(),
            fdecl::StartupMode::Lazy,
            fdecl::OnTerminate::None,
            WeakModelContext::new(Weak::new()),
            WeakExtendedInstance::Component(WeakComponentInstance::from(&child_one)),
            Arc::new(Hooks::new()),
            None,
            false,
        );

        let child_three = ComponentInstance::new(
            root.environment.clone(),
            InstancedAbsoluteMoniker::parse_str("/root:0/child:0/child2:0/child3:0")?,
            "#meta/my-child3.cm".to_string(),
            fdecl::StartupMode::Lazy,
            fdecl::OnTerminate::None,
            WeakModelContext::new(Weak::new()),
            WeakExtendedInstance::Component(WeakComponentInstance::from(&child_two)),
            Arc::new(Hooks::new()),
            None,
            false,
        );

        let resolved = child_three
            .environment
            .resolve(
                &ComponentAddress::from(&child_three.component_url, &child_three).await?,
                &child_three,
            )
            .await?;
        let expected = expected_urls_and_contexts.as_slice().last().unwrap();
        assert_eq!(&resolved.resolved_url, &expected.resolved_url);
        assert_eq!(&resolved.context_to_resolve_children, &expected.context_to_resolve_children);
        Ok(())
    }

    #[fuchsia::test]
    async fn relative_resources_and_paths_to_realm_builder() -> Result<(), Error> {
        let expected_urls_and_contexts = vec![
            ResolveState::new(
                "fuchsia-pkg://fuchsia.com/my-package#meta/my-root.cm",
                None,
                Some(ComponentResolutionContext::new("fuchsia.com...".as_bytes().to_vec())),
            ),
            ResolveState::new(
                "my-subpackage1#meta/sub1.cm",
                Some(ComponentResolutionContext::new("fuchsia.com...".as_bytes().to_vec())),
                Some(ComponentResolutionContext::new("my-subpackage1...".as_bytes().to_vec())),
            ),
            ResolveState::new(
                "my-subpackage1#meta/sub1-child.cm",
                Some(ComponentResolutionContext::new("fuchsia.com...".as_bytes().to_vec())),
                Some(ComponentResolutionContext::new("my-subpackage1...".as_bytes().to_vec())),
            ),
            ResolveState::new(
                "my-subpackage2#meta/sub2.cm",
                Some(ComponentResolutionContext::new("my-subpackage1...".as_bytes().to_vec())),
                Some(ComponentResolutionContext::new("my-subpackage2...".as_bytes().to_vec())),
            ),
            ResolveState::new(
                "my-subpackage2#meta/sub2-child.cm",
                Some(ComponentResolutionContext::new("my-subpackage1...".as_bytes().to_vec())),
                Some(ComponentResolutionContext::new("my-subpackage2...".as_bytes().to_vec())),
            ),
        ];
        let mut resolver = ResolverRegistry::new();

        resolver.register(
            "fuchsia-pkg".to_string(),
            Box::new(MockMultipleOkResolver::new(expected_urls_and_contexts.clone())),
        );
        resolver.register(
            "realm-builder".to_string(),
            Box::new(MockOkResolver {
                expected_url: "realm-builder://0/my-realm".to_string(),
                resolved_url: "realm-builder://0/my-realm".to_string(),
            }),
        );

        let top_instance = Arc::new(ComponentManagerInstance::new(vec![], vec![]));
        let environment = Environment::new_root(
            &top_instance,
            RunnerRegistry::default(),
            resolver,
            DebugRegistry::default(),
        );

        let root = ComponentInstance::new_root(
            environment,
            Weak::new(),
            Weak::new(),
            "fuchsia-pkg://fuchsia.com/my-package#meta/my-root.cm".to_string(),
        );

        let realm = ComponentInstance::new(
            root.environment.clone(),
            InstancedAbsoluteMoniker::parse_str("/root:0/realm:0/child:0")?,
            "realm-builder://0/my-realm".to_string(),
            fdecl::StartupMode::Lazy,
            fdecl::OnTerminate::None,
            WeakModelContext::new(Weak::new()),
            WeakExtendedInstance::Component(WeakComponentInstance::from(&root)),
            Arc::new(Hooks::new()),
            None,
            false,
        );

        let child_one = ComponentInstance::new(
            root.environment.clone(),
            InstancedAbsoluteMoniker::parse_str("/root:0/realm:0/child:0")?,
            "my-subpackage1#meta/sub1.cm".to_string(),
            fdecl::StartupMode::Lazy,
            fdecl::OnTerminate::None,
            WeakModelContext::new(Weak::new()),
            WeakExtendedInstance::Component(WeakComponentInstance::from(&realm)),
            Arc::new(Hooks::new()),
            None,
            false,
        );

        let child_two = ComponentInstance::new(
            root.environment.clone(),
            InstancedAbsoluteMoniker::parse_str("/root:0/realm:0/child:0/child2:0")?,
            "#meta/sub1-child.cm".to_string(),
            fdecl::StartupMode::Lazy,
            fdecl::OnTerminate::None,
            WeakModelContext::new(Weak::new()),
            WeakExtendedInstance::Component(WeakComponentInstance::from(&child_one)),
            Arc::new(Hooks::new()),
            None,
            false,
        );

        let child_three = ComponentInstance::new(
            root.environment.clone(),
            InstancedAbsoluteMoniker::parse_str("/root:0/realm:0/child:0/child2:0/child3:0")?,
            "my-subpackage2#meta/sub2.cm".to_string(),
            fdecl::StartupMode::Lazy,
            fdecl::OnTerminate::None,
            WeakModelContext::new(Weak::new()),
            WeakExtendedInstance::Component(WeakComponentInstance::from(&child_two)),
            Arc::new(Hooks::new()),
            None,
            false,
        );

        let child_four = ComponentInstance::new(
            root.environment.clone(),
            InstancedAbsoluteMoniker::parse_str(
                "/root:0/realm:0/child:0/child2:0/child3:0/child4:0",
            )?,
            "#meta/sub2-child.cm".to_string(),
            fdecl::StartupMode::Lazy,
            fdecl::OnTerminate::None,
            WeakModelContext::new(Weak::new()),
            WeakExtendedInstance::Component(WeakComponentInstance::from(&child_three)),
            Arc::new(Hooks::new()),
            None,
            false,
        );

        let resolved = child_four
            .environment
            .resolve(
                &ComponentAddress::from(&child_four.component_url, &child_four).await?,
                &child_four,
            )
            .await?;
        let expected = expected_urls_and_contexts.as_slice().last().unwrap();
        assert_eq!(&resolved.resolved_url, &expected.resolved_url);
        assert_eq!(&resolved.context_to_resolve_children, &expected.context_to_resolve_children);
        Ok(())
    }
}
