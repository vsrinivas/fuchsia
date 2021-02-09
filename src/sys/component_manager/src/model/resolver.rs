// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        component::{ComponentInstance, WeakComponentInstance},
        error::ModelError,
        routing,
    },
    anyhow::Error,
    clonable_error::ClonableError,
    cm_rust::ResolverRegistration,
    fidl_fuchsia_io as fio, fidl_fuchsia_mem as fmem, fidl_fuchsia_sys2 as fsys,
    fuchsia_zircon::Status,
    futures::future::{self, BoxFuture},
    std::{collections::HashMap, path::PathBuf, sync::Arc},
    thiserror::Error,
    url::Url,
};

/// Resolves a component URL to its content.
/// TODO: Consider defining an internal representation for `fsys::Component` so as to
/// further isolate the `Model` from FIDL interfacting concerns.
pub trait Resolver {
    fn resolve<'a>(&'a self, component_url: &'a str) -> ResolverFut<'a>;
}

pub type ResolverFut<'a> = BoxFuture<'a, Result<ResolvedComponent, ResolverError>>;

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

impl Resolver for ResolverRegistry {
    fn resolve<'a>(&'a self, component_url: &'a str) -> ResolverFut<'a> {
        match Url::parse(component_url) {
            Ok(parsed_url) => {
                if let Some(ref resolver) = self.resolvers.get(parsed_url.scheme()) {
                    resolver.resolve(component_url)
                } else {
                    Box::pin(future::err(ResolverError::SchemeNotRegistered))
                }
            }
            Err(e) => Box::pin(future::err(ResolverError::url_parse_error(component_url, e))),
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
impl Resolver for RemoteResolver {
    fn resolve<'a>(&'a self, component_url: &'a str) -> ResolverFut<'a> {
        Box::pin(async move {
            let (proxy, server_end) =
                fidl::endpoints::create_proxy::<fsys::ComponentResolverMarker>()
                    .map_err(ResolverError::unknown_resolver_error)?;
            let component = self.component.upgrade().map_err(ResolverError::routing_error)?;
            let capability_source = routing::route_resolver(self.registration.clone(), &component)
                .await
                .map_err(ResolverError::routing_error)?;
            routing::open_capability_at_source(
                fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
                fio::MODE_TYPE_SERVICE,
                PathBuf::new(),
                capability_source,
                &component,
                &mut server_end.into_channel(),
            )
            .await
            .map_err(ResolverError::routing_error)?;
            let (status, component) =
                proxy.resolve(component_url).await.map_err(ResolverError::fidl_error)?;
            let status = Status::from_raw(status);
            match status {
                Status::OK => {
                    let decl_buffer: fmem::Data =
                        component.decl.ok_or(ResolverError::RemoteInvalidData)?;
                    Ok(ResolvedComponent {
                        resolved_url: component
                            .resolved_url
                            .ok_or(ResolverError::RemoteInvalidData)?,
                        decl: read_and_validate_manifest(component_url, decl_buffer).await?,
                        package: component.package,
                    })
                }
                Status::INVALID_ARGS => {
                    Err(ResolverError::url_parse_error(component_url, RemoteError(status)))
                }
                Status::NOT_FOUND => {
                    Err(ResolverError::component_not_available(component_url, RemoteError(status)))
                }
                Status::UNAVAILABLE => {
                    Err(ResolverError::manifest_invalid(component_url, RemoteError(status)))
                }
                _ => Err(ResolverError::unknown_resolver_error(RemoteError(status))),
            }
        })
    }
}

async fn read_and_validate_manifest(
    component_url: &str,
    data: fmem::Data,
) -> Result<fsys::ComponentDecl, ResolverError> {
    let bytes = match data {
        fmem::Data::Bytes(bytes) => bytes,
        fmem::Data::Buffer(buffer) => {
            let mut contents = Vec::<u8>::new();
            contents.resize(buffer.size as usize, 0);
            buffer.vmo.read(&mut contents, 0).map_err(|status| ResolverError::ManifestIO {
                url: component_url.to_string(),
                status,
            })?;
            contents
        }
        _ => return Err(ResolverError::RemoteInvalidData),
    };
    let component_decl: fsys::ComponentDecl = fidl::encoding::decode_persistent(&bytes)
        .map_err(|err| ResolverError::manifest_invalid(component_url.to_string(), err))?;
    cm_fidl_validator::validate(&component_decl)
        .map_err(|e| ResolverError::manifest_invalid(component_url, e))?;
    Ok(component_decl)
}

/// Errors produced by `Resolver`.
#[derive(Debug, Error, Clone)]
pub enum ResolverError {
    #[error("resolver is already registered for scheme \"{}\"", scheme)]
    DuplicateResolverError { scheme: String },
    #[error("component not available with url \"{}\": {}", url, err)]
    ComponentNotAvailable {
        url: String,
        #[source]
        err: ClonableError,
    },
    #[error("component manifest not available for url \"{}\": {}", url, err)]
    ManifestNotAvailable {
        url: String,
        #[source]
        err: ClonableError,
    },
    #[error("component manifest invalid for url \"{}\": {}", url, err)]
    ManifestInvalid {
        url: String,
        #[source]
        err: ClonableError,
    },
    #[error("failed to read manifest for url \"{}\": {}", url, status)]
    ManifestIO { url: String, status: Status },
    #[error("Model not available.")]
    ModelAccessError,
    #[error("scheme not registered")]
    SchemeNotRegistered,
    #[error("failed to parse url \"{}\": {}", url, err)]
    UrlParseError {
        url: String,
        #[source]
        err: ClonableError,
    },
    #[error("url missing resource \"{}\"", url)]
    UrlMissingResourceError { url: String },
    #[error("failed to route resolver capability: {}", .0)]
    RoutingError(#[source] Box<ModelError>),
    #[error("the remote resolver returned invalid data")]
    RemoteInvalidData,
    #[error("an unknown error ocurred with the resolver: {}", .0)]
    UnknownResolverError(#[source] ClonableError),
    #[error("an error occurred sending a FIDL request to the remote resolver: {}", .0)]
    FidlError(#[source] ClonableError),
}

impl ResolverError {
    pub fn component_not_available(url: impl Into<String>, err: impl Into<Error>) -> ResolverError {
        ResolverError::ComponentNotAvailable { url: url.into(), err: err.into().into() }
    }

    pub fn manifest_not_available(url: impl Into<String>, err: impl Into<Error>) -> ResolverError {
        ResolverError::ManifestNotAvailable { url: url.into(), err: err.into().into() }
    }

    pub fn manifest_invalid(url: impl Into<String>, err: impl Into<Error>) -> ResolverError {
        ResolverError::ManifestInvalid { url: url.into(), err: err.into().into() }
    }

    pub fn model_not_available() -> ResolverError {
        ResolverError::ModelAccessError
    }

    pub fn url_parse_error(url: impl Into<String>, err: impl Into<Error>) -> ResolverError {
        ResolverError::UrlParseError { url: url.into(), err: err.into().into() }
    }

    pub fn url_missing_resource_error(url: impl Into<String>) -> ResolverError {
        ResolverError::UrlMissingResourceError { url: url.into() }
    }

    pub fn routing_error(err: ModelError) -> ResolverError {
        ResolverError::RoutingError(Box::new(err))
    }

    pub fn unknown_resolver_error(err: impl Into<Error>) -> ResolverError {
        ResolverError::UnknownResolverError(err.into().into())
    }

    pub fn fidl_error(err: impl Into<Error>) -> ResolverError {
        ResolverError::FidlError(err.into().into())
    }
}

#[derive(Error, Clone, Debug)]
#[error("remote resolver returned status {}", .0)]
struct RemoteError(Status);

#[cfg(test)]
mod tests {
    use {super::*, anyhow::format_err};

    struct MockOkResolver {
        pub expected_url: String,
        pub resolved_url: String,
    }

    impl Resolver for MockOkResolver {
        fn resolve<'a>(&'a self, component_url: &'a str) -> ResolverFut<'a> {
            assert_eq!(self.expected_url, component_url);
            Box::pin(future::ok(ResolvedComponent {
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
            }))
        }
    }

    struct MockErrorResolver {
        pub expected_url: String,
        pub error: Box<dyn Fn(&str) -> ResolverError + Send + Sync + 'static>,
    }

    impl Resolver for MockErrorResolver {
        fn resolve<'a>(&'a self, component_url: &'a str) -> ResolverFut<'a> {
            assert_eq!(self.expected_url, component_url);
            Box::pin(future::err((self.error)(component_url)))
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
                error: Box::new(|url| {
                    ResolverError::component_not_available(url, format_err!("not available"))
                }),
            }),
        );

        // Resolve known scheme that returns success.
        let component = registry.resolve("foo://url").await.unwrap();
        assert_eq!("foo://resolved", component.resolved_url);

        // Resolve a different scheme that produces an error.
        let expected_res: Result<ResolvedComponent, ResolverError> =
            Err(ResolverError::component_not_available("bar://url", format_err!("not available")));
        assert_eq!(
            format!("{:?}", expected_res),
            format!("{:?}", registry.resolve("bar://url").await)
        );

        // Resolve an unknown scheme
        let expected_res: Result<ResolvedComponent, ResolverError> =
            Err(ResolverError::SchemeNotRegistered);
        assert_eq!(
            format!("{:?}", expected_res),
            format!("{:?}", registry.resolve("unknown://url").await),
        );

        // Resolve an URL lacking a scheme.
        let expected_res: Result<ResolvedComponent, ResolverError> =
            Err(ResolverError::url_parse_error("xxx", url::ParseError::RelativeUrlWithoutBase));
        assert_eq!(format!("{:?}", expected_res), format!("{:?}", registry.resolve("xxx").await),);
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
