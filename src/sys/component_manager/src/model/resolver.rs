// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::EnvironmentCapability,
        model::{
            error::ModelError,
            realm::{Realm, WeakRealm},
            routing,
        },
    },
    anyhow::Error,
    clonable_error::ClonableError,
    cm_rust::{CapabilityName, RegistrationSource, ResolverRegistration},
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
    fuchsia_zircon::Status,
    futures::future::{self, BoxFuture},
    std::{collections::HashMap, sync::Arc},
    thiserror::Error,
    url::Url,
};

/// Resolves a component URL to its content.
/// TODO: Consider defining an internal representation for `fsys::Component` so as to
/// further isolate the `Model` from FIDL interfacting concerns.
pub trait Resolver {
    fn resolve<'a>(&'a self, component_url: &'a str) -> ResolverFut<'a>;
}

pub type ResolverFut<'a> = BoxFuture<'a, Result<fsys::Component, ResolverError>>;

#[derive(Error, Debug, Clone)]
pub enum ResolverRegistrationError {
    #[error("a resolver is already registered with the URL scheme \"{}\"", 0)]
    SchemeAlreadyRegistered(String),
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
    ) -> Result<(), ResolverRegistrationError> {
        if self.resolvers.contains_key(&scheme) {
            Err(ResolverRegistrationError::SchemeAlreadyRegistered(scheme.clone()))
        } else {
            self.resolvers.insert(scheme, resolver);
            Ok(())
        }
    }

    /// Creates and populates a `ResolverRegistry` with `RemoteResolvers` that
    /// have been registered with an environment.
    pub fn from_decl(
        decl: &[ResolverRegistration],
        parent: &Arc<Realm>,
    ) -> Result<Self, ResolverRegistrationError> {
        let mut registry = ResolverRegistry::new();
        for resolver in decl {
            registry.register(
                resolver.scheme.clone().into(),
                Box::new(RemoteResolver::new(
                    resolver.resolver.clone(),
                    resolver.source.clone(),
                    parent.as_weak(),
                )),
            )?;
        }
        Ok(registry)
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
    capability_name: CapabilityName,
    source: RegistrationSource,
    realm: WeakRealm,
}

impl RemoteResolver {
    pub fn new(name: CapabilityName, source: RegistrationSource, realm: WeakRealm) -> Self {
        RemoteResolver { capability_name: name, source, realm }
    }
}

// TODO(61288): Implement some sort of caching of the routed capability. Multiple
// component URL resolutions should be possible on a single channel.
impl Resolver for RemoteResolver {
    fn resolve<'a>(&'a self, component_url: &'a str) -> ResolverFut<'a> {
        Box::pin(async move {
            let decl = EnvironmentCapability::Resolver {
                source_name: self.capability_name.clone(),
                source: self.source.clone(),
            };
            let flags = fio::OPEN_RIGHT_READABLE | fio::OPEN_FLAG_POSIX;
            let open_mode = fio::MODE_TYPE_SERVICE;
            let (proxy, server_end) =
                fidl::endpoints::create_proxy::<fsys::ComponentResolverMarker>()
                    .map_err(ResolverError::unknown_resolver_error)?;
            let realm = self.realm.upgrade().map_err(ResolverError::routing_error)?;
            routing::route_capability_from_environment(
                flags,
                open_mode,
                String::new(),
                decl,
                &realm,
                &mut server_end.into_channel(),
            )
            .await
            .map_err(ResolverError::routing_error)?;
            let (status, component) = proxy
                .resolve(component_url)
                .await
                .map_err(ResolverError::unknown_resolver_error)?;
            let status = Status::from_raw(status);
            match status {
                Status::OK => Ok(component),
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
    #[error("an unknown error ocurred with the resolver: {}", .0)]
    UnknownResolverError(#[source] ClonableError),
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
}

#[derive(Error, Clone, Debug)]
#[error("remote resolver returned status {}", .0)]
struct RemoteError(Status);

#[cfg(test)]
mod tests {
    use {super::*, anyhow::format_err, matches::assert_matches};

    struct MockOkResolver {
        pub expected_url: String,
        pub resolved_url: String,
    }

    impl Resolver for MockOkResolver {
        fn resolve<'a>(&'a self, component_url: &'a str) -> ResolverFut<'a> {
            assert_eq!(self.expected_url, component_url);
            Box::pin(future::ok(fsys::Component {
                resolved_url: Some(self.resolved_url.clone()),
                decl: Some(fsys::ComponentDecl {
                    program: None,
                    uses: None,
                    exposes: None,
                    offers: None,
                    facets: None,
                    capabilities: None,
                    children: None,
                    collections: None,
                    environments: None,
                }),
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
        registry
            .register(
                "foo".to_string(),
                Box::new(MockOkResolver {
                    expected_url: "foo://url".to_string(),
                    resolved_url: "foo://resolved".to_string(),
                }),
            )
            .unwrap();
        registry
            .register(
                "bar".to_string(),
                Box::new(MockErrorResolver {
                    expected_url: "bar://url".to_string(),
                    error: Box::new(|url| {
                        ResolverError::component_not_available(url, format_err!("not available"))
                    }),
                }),
            )
            .unwrap();

        // Resolve known scheme that returns success.
        let component = registry.resolve("foo://url").await.unwrap();
        assert_eq!("foo://resolved", component.resolved_url.unwrap());

        // Resolve a different scheme that produces an error.
        let expected_res: Result<fsys::Component, ResolverError> =
            Err(ResolverError::component_not_available("bar://url", format_err!("not available")));
        assert_eq!(
            format!("{:?}", expected_res),
            format!("{:?}", registry.resolve("bar://url").await)
        );

        // Resolve an unknown scheme
        let expected_res: Result<fsys::Component, ResolverError> =
            Err(ResolverError::SchemeNotRegistered);
        assert_eq!(
            format!("{:?}", expected_res),
            format!("{:?}", registry.resolve("unknown://url").await),
        );

        // Resolve an URL lacking a scheme.
        let expected_res: Result<fsys::Component, ResolverError> =
            Err(ResolverError::url_parse_error("xxx", url::ParseError::RelativeUrlWithoutBase));
        assert_eq!(format!("{:?}", expected_res), format!("{:?}", registry.resolve("xxx").await),);
    }

    #[test]
    fn test_duplicate_registration() {
        let mut registry = ResolverRegistry::new();
        let resolver_a =
            MockOkResolver { expected_url: "".to_string(), resolved_url: "".to_string() };
        let resolver_b =
            MockOkResolver { expected_url: "".to_string(), resolved_url: "".to_string() };
        assert_matches!(registry.register("fuchsia-pkg".to_string(), Box::new(resolver_a)), Ok(()));
        assert_matches!(registry.register("fuchsia-pkg".to_string(), Box::new(resolver_b)), Err(ResolverRegistrationError::SchemeAlreadyRegistered(scheme)) if scheme == "fuchsia-pkg");
    }

    #[test]
    fn test_multiple_scheme_registration() {
        let mut registry = ResolverRegistry::new();
        let resolver_a =
            MockOkResolver { expected_url: "".to_string(), resolved_url: "".to_string() };
        let resolver_b =
            MockOkResolver { expected_url: "".to_string(), resolved_url: "".to_string() };
        registry.register("fuchsia-pkg".to_string(), Box::new(resolver_a)).unwrap();
        registry.register("fuchsia-boot".to_string(), Box::new(resolver_b)).unwrap();
    }
}
