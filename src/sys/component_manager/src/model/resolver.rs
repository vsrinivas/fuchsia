// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        component::{ComponentInstance, WeakComponentInstance},
        error::ModelError,
        routing::{route_and_open_capability, OpenOptions, OpenResolverOptions, RouteRequest},
    },
    ::routing::component_instance::ComponentInstanceInterface,
    anyhow::Error,
    async_trait::async_trait,
    clonable_error::ClonableError,
    cm_rust::{FidlIntoNative, ResolverRegistration},
    fidl_fuchsia_component_decl as fdecl, fidl_fuchsia_io as fio, fidl_fuchsia_mem as fmem,
    fidl_fuchsia_sys2 as fsys,
    fuchsia_zircon::Status,
    std::{collections::HashMap, sync::Arc},
    thiserror::Error,
    url::Url,
};

/// Resolves a component URL to its content.
/// TODO: Consider defining an internal representation for `fsys::Component` so as to
/// further isolate the `Model` from FIDL interfacting concerns.
#[async_trait]
pub trait Resolver {
    /// Resolves a component URL to its content. This function takes in the `component_url` to
    /// resolve and the `target` component that is trying to be resolved.
    async fn resolve(
        &self,
        component_url: &str,
        target: &Arc<ComponentInstance>,
    ) -> Result<ResolvedComponent, ResolverError>;
}

/// The response returned from a Resolver. This struct is derived from the FIDL
/// [`fuchsia.sys2.Component`][fidl_fuchsia_sys2::Component] table, except that
/// the opaque binary ComponentDecl has been deserialized and validated.
#[derive(Debug)]
pub struct ResolvedComponent {
    pub resolved_url: String,
    pub decl: cm_rust::ComponentDecl,
    pub package: Option<fsys::Package>,
    pub config_values: Option<cm_rust::ValuesData>,
}

/// Resolves a component URL using a resolver selected based on the URL's scheme.
#[derive(Default)]
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
        component_url: &str,
        target: &Arc<ComponentInstance>,
    ) -> Result<ResolvedComponent, ResolverError> {
        match Url::parse(component_url) {
            Ok(parsed_url) => {
                if let Some(resolver) = self.resolvers.get(parsed_url.scheme()) {
                    resolver.resolve(component_url, target).await
                } else {
                    Err(ResolverError::SchemeNotRegistered)
                }
            }
            Err(e) => {
                if e == url::ParseError::RelativeUrlWithoutBase {
                    if let Some(resolver) = self.resolvers.get("") {
                        resolver.resolve(component_url, target).await
                    } else {
                        Err(ResolverError::SchemeNotRegistered)
                    }
                } else {
                    Err(ResolverError::malformed_url(e))
                }
            }
        }
    }
}

/// A resolver whose implementation lives in an external component. The source
/// of the resolver is determined through capability routing.
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
        component_url: &str,
        _target: &Arc<ComponentInstance>,
    ) -> Result<ResolvedComponent, ResolverError> {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<fsys::ComponentResolverMarker>()
            .map_err(ResolverError::internal)?;
        let component = self.component.upgrade().map_err(ResolverError::routing_error)?;
        let open_options = OpenResolverOptions {
            flags: fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
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
        let component = proxy.resolve(component_url).await.map_err(ResolverError::fidl_error)??;
        let decl_buffer: fmem::Data = component.decl.ok_or(ResolverError::RemoteInvalidData)?;
        let decl = read_and_validate_manifest(&decl_buffer).await?;
        let config_values = if decl.config.is_some() {
            Some(read_and_validate_config_values(
                &component.config_values.ok_or(ResolverError::RemoteInvalidData)?,
            )?)
        } else {
            None
        };
        Ok(ResolvedComponent {
            resolved_url: component.resolved_url.ok_or(ResolverError::RemoteInvalidData)?,
            decl,
            package: component.package,
            config_values,
        })
    }
}

pub struct BuiltinResolver(pub Arc<dyn Resolver + Send + Sync + 'static>);

#[async_trait]
impl Resolver for BuiltinResolver {
    async fn resolve(
        &self,
        component_url: &str,
        target: &Arc<ComponentInstance>,
    ) -> Result<ResolvedComponent, ResolverError> {
        self.0.resolve(component_url, target).await
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

/// Errors produced by `Resolver`.
#[derive(Debug, Error, Clone)]
pub enum ResolverError {
    #[error("an unexpected error occurred: {0}")]
    Internal(#[source] ClonableError),
    #[error("an IO error occurred: {0}")]
    Io(#[source] ClonableError),
    #[error("component manifest not found: {0}")]
    ManifestNotFound(#[source] ClonableError),
    #[error("package not found: {0}")]
    PackageNotFound(#[source] ClonableError),
    #[error("component manifest invalid: {0}")]
    ManifestInvalid(#[source] ClonableError),
    #[error("config values file invalid: {0}")]
    ConfigValuesInvalid(#[source] ClonableError),
    #[error("failed to read manifest: {0}")]
    ManifestIo(Status),
    #[error("failed to read config values: {0}")]
    ConfigValuesIo(Status),
    #[error("Model not available")]
    ModelNotAvailable,
    #[error("scheme not registered")]
    SchemeNotRegistered,
    #[error("malformed url: {0}")]
    MalformedUrl(#[source] ClonableError),
    #[error("url missing resource")]
    UrlMissingResource,
    #[error("failed to route resolver capability: {0}")]
    RoutingError(#[source] Box<ModelError>),
    #[error("the remote resolver returned invalid data")]
    RemoteInvalidData,
    #[error("an error occurred sending a FIDL request to the remote resolver: {0}")]
    FidlError(#[source] ClonableError),
}

impl ResolverError {
    pub fn internal(err: impl Into<Error>) -> ResolverError {
        ResolverError::Internal(err.into().into())
    }

    pub fn io(err: impl Into<Error>) -> ResolverError {
        ResolverError::Io(err.into().into())
    }

    pub fn manifest_not_found(err: impl Into<Error>) -> ResolverError {
        ResolverError::ManifestNotFound(err.into().into())
    }

    pub fn package_not_found(err: impl Into<Error>) -> ResolverError {
        ResolverError::PackageNotFound(err.into().into())
    }

    pub fn manifest_invalid(err: impl Into<Error>) -> ResolverError {
        ResolverError::ManifestInvalid(err.into().into())
    }

    pub fn config_values_invalid(err: impl Into<Error>) -> ResolverError {
        ResolverError::ConfigValuesInvalid(err.into().into())
    }

    pub fn malformed_url(err: impl Into<Error>) -> ResolverError {
        ResolverError::MalformedUrl(err.into().into())
    }

    pub fn routing_error(err: impl Into<ModelError>) -> ResolverError {
        ResolverError::RoutingError(Box::new(err.into()))
    }

    pub fn fidl_error(err: impl Into<Error>) -> ResolverError {
        ResolverError::FidlError(err.into().into())
    }
}

impl From<fsys::ResolverError> for ResolverError {
    fn from(err: fsys::ResolverError) -> ResolverError {
        match err {
            fsys::ResolverError::Internal => ResolverError::internal(RemoteError(err)),
            fsys::ResolverError::Io => ResolverError::io(RemoteError(err)),
            fsys::ResolverError::PackageNotFound
            | fsys::ResolverError::NoSpace
            | fsys::ResolverError::ResourceUnavailable
            | fsys::ResolverError::NotSupported => {
                ResolverError::package_not_found(RemoteError(err))
            }
            fsys::ResolverError::ManifestNotFound => {
                ResolverError::manifest_not_found(RemoteError(err))
            }
            fsys::ResolverError::InvalidArgs => ResolverError::malformed_url(RemoteError(err)),
            fsys::ResolverError::InvalidManifest => {
                ResolverError::ManifestInvalid(anyhow::Error::from(RemoteError(err)).into())
            }
            fsys::ResolverError::ConfigValuesNotFound => {
                ResolverError::ConfigValuesIo(Status::NOT_FOUND)
            }
        }
    }
}

#[derive(Error, Clone, Debug)]
#[error("remote resolver responded with {0:?}")]
struct RemoteError(fsys::ResolverError);

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::{component::ComponentInstance, environment::Environment},
        anyhow::format_err,
        cm_rust::NativeIntoFidl,
        cm_rust_testing::new_decl_from_json,
        fidl_fuchsia_component_config as fconfig, fidl_fuchsia_component_decl as fdecl,
        lazy_static::lazy_static,
        serde_json::json,
        std::sync::Weak,
    };

    struct MockOkResolver {
        pub expected_url: String,
        pub resolved_url: String,
    }

    #[async_trait]
    impl Resolver for MockOkResolver {
        async fn resolve(
            &self,
            component_url: &str,
            _target: &Arc<ComponentInstance>,
        ) -> Result<ResolvedComponent, ResolverError> {
            assert_eq!(self.expected_url, component_url);
            Ok(ResolvedComponent {
                resolved_url: self.resolved_url.clone(),
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

    #[async_trait]
    impl Resolver for MockErrorResolver {
        async fn resolve(
            &self,
            component_url: &str,
            _target: &Arc<ComponentInstance>,
        ) -> Result<ResolvedComponent, ResolverError> {
            assert_eq!(self.expected_url, component_url);
            Err((self.error)(component_url))
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
        let component = registry.resolve("foo://url", &root).await.unwrap();
        assert_eq!("foo://resolved", component.resolved_url);

        // Resolve a different scheme that produces an error.
        let expected_res: Result<ResolvedComponent, ResolverError> =
            Err(ResolverError::manifest_not_found(format_err!("not available")));
        assert_eq!(
            format!("{:?}", expected_res),
            format!("{:?}", registry.resolve("bar://url", &root).await)
        );

        // Resolve an unknown scheme
        let expected_res: Result<ResolvedComponent, ResolverError> =
            Err(ResolverError::SchemeNotRegistered);
        assert_eq!(
            format!("{:?}", expected_res),
            format!("{:?}", registry.resolve("unknown://url", &root).await),
        );

        // Resolve an URL lacking a scheme.
        let expected_res: Result<ResolvedComponent, ResolverError> =
            Err(ResolverError::SchemeNotRegistered);
        assert_eq!(
            format!("{:?}", expected_res),
            format!("{:?}", registry.resolve("xxx", &root).await),
        );
    }

    #[test]
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

    #[test]
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
                    "durability": "persistent",
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
                    value: Some(fconfig::Value::Single(fconfig::SingleValue::Flag(false))),
                    ..fconfig::ValueSpec::EMPTY
                },
                fconfig::ValueSpec {
                    value: Some(fconfig::Value::Single(fconfig::SingleValue::Unsigned8(5))),
                    ..fconfig::ValueSpec::EMPTY
                },
                fconfig::ValueSpec {
                    value: Some(fconfig::Value::Single(fconfig::SingleValue::Text(
                        "hello!".to_string(),
                    ))),
                    ..fconfig::ValueSpec::EMPTY
                },
                fconfig::ValueSpec {
                    value: Some(fconfig::Value::List(fconfig::ListValue::FlagList(vec![
                        true, false,
                    ]))),
                    ..fconfig::ValueSpec::EMPTY
                },
                fconfig::ValueSpec {
                    value: Some(fconfig::Value::List(fconfig::ListValue::TextList(vec![
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
                    value: cm_rust::Value::Single(cm_rust::SingleValue::Flag(false)),
                },
                cm_rust::ValueSpec {
                    value: cm_rust::Value::Single(cm_rust::SingleValue::Unsigned8(5)),
                },
                cm_rust::ValueSpec {
                    value: cm_rust::Value::Single(cm_rust::SingleValue::Text("hello!".to_string())),
                },
                cm_rust::ValueSpec {
                    value: cm_rust::Value::List(cm_rust::ListValue::FlagList(vec![true, false])),
                },
                cm_rust::ValueSpec {
                    value: cm_rust::Value::List(cm_rust::ListValue::TextList(vec![
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
}
