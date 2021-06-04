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
    cm_rust::ResolverRegistration,
    fidl_fuchsia_io as fio, fidl_fuchsia_mem as fmem, fidl_fuchsia_sys2 as fsys,
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
    pub decl: fsys::ComponentDecl,
    pub package: Option<fsys::Package>,
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
        Ok(ResolvedComponent {
            resolved_url: component.resolved_url.ok_or(ResolverError::RemoteInvalidData)?,
            decl: read_and_validate_manifest(decl_buffer).await?,
            package: component.package,
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
    data: fmem::Data,
) -> Result<fsys::ComponentDecl, ResolverError> {
    let bytes = match data {
        fmem::Data::Bytes(bytes) => bytes,
        fmem::Data::Buffer(buffer) => {
            let mut contents = Vec::<u8>::new();
            contents.resize(buffer.size as usize, 0);
            buffer.vmo.read(&mut contents, 0).map_err(ResolverError::ManifestIo)?;
            contents
        }
        _ => return Err(ResolverError::RemoteInvalidData),
    };
    let component_decl: fsys::ComponentDecl = fidl::encoding::decode_persistent(&bytes)
        .map_err(|err| ResolverError::manifest_invalid(err))?;
    cm_fidl_validator::validate(&component_decl).map_err(|e| ResolverError::manifest_invalid(e))?;
    Ok(component_decl)
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
    #[error("failed to read manifest: {0}")]
    ManifestIo(Status),
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
        }
    }
}

#[derive(Error, Clone, Debug)]
#[error("remote resolver responded with {0:?}")]
struct RemoteError(fsys::ResolverError);

#[cfg(test)]
mod tests {
    use crate::model::component::ComponentInstance;
    use crate::model::environment::Environment;
    use std::sync::Weak;
    use {super::*, anyhow::format_err};

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
                decl: fsys::ComponentDecl {
                    program: None,
                    uses: None,
                    exposes: None,
                    offers: None,
                    facets: None,
                    capabilities: None,
                    children: None,
                    collections: None,
                    environments: None,
                    ..fsys::ComponentDecl::EMPTY
                },
                package: None,
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
}
