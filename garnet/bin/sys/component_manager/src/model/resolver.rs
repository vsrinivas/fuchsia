// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_sys2 as fsys,
    futures::future,
    futures::future::FutureObj,
    std::{collections::HashMap, error, fmt},
    url::Url,
};

/// Resolves a component URI to its content.
/// TODO: Consider defining an internal representation for `fsys::Component` so as to
/// further isolate the `Model` from FIDL interfacting concerns.
pub trait Resolver {
    fn resolve<'a>(
        &'a self,
        component_uri: &'a str,
    ) -> FutureObj<'a, Result<fsys::Component, ResolverError>>;
}

/// Resolves a component URI using a resolver selected based on the URI's scheme.
pub struct ResolverRegistry {
    resolvers: HashMap<String, Box<dyn Resolver>>,
}

impl ResolverRegistry {
    pub fn new() -> ResolverRegistry {
        ResolverRegistry { resolvers: HashMap::new() }
    }

    pub fn register(&mut self, scheme: String, resolver: Box<dyn Resolver>) {
        self.resolvers.insert(scheme, resolver);
    }
}

impl Resolver for ResolverRegistry {
    fn resolve<'a>(
        &'a self,
        component_uri: &'a str,
    ) -> FutureObj<'a, Result<fsys::Component, ResolverError>> {
        match Url::parse(component_uri) {
            Ok(parsed_uri) => {
                if let Some(ref resolver) = self.resolvers.get(parsed_uri.scheme()) {
                    resolver.resolve(component_uri)
                } else {
                    FutureObj::new(Box::new(future::err(ResolverError::SchemeNotRegistered)))
                }
            }
            Err(err) => FutureObj::new(Box::new(future::err(ResolverError::from(err)))),
        }
    }
}

// TODO: Allow variants other than InternalError to take strings.
/// Errors produced by `Resolver`.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ResolverError {
    ComponentNotAvailable,
    SchemeNotRegistered,
    UrlParseError(url::ParseError),
    InternalError(String),
}

impl ResolverError {
    pub fn internal_error(err: impl Into<String>) -> Self {
        ResolverError::InternalError(err.into())
    }
}

impl fmt::Display for ResolverError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match &self {
            ResolverError::ComponentNotAvailable => write!(f, "component not available"),
            ResolverError::SchemeNotRegistered => write!(f, "component uri scheme not registered"),
            ResolverError::UrlParseError(err) => err.fmt(f),
            ResolverError::InternalError(err) => write!(f, "internal error: {}", err),
        }
    }
}

impl error::Error for ResolverError {
    fn source(&self) -> Option<&(dyn error::Error + 'static)> {
        match *self {
            ResolverError::ComponentNotAvailable => None,
            ResolverError::SchemeNotRegistered => None,
            ResolverError::UrlParseError(ref err) => err.source(),
            ResolverError::InternalError(_) => None,
        }
    }
}

impl From<url::ParseError> for ResolverError {
    fn from(err: url::ParseError) -> Self {
        ResolverError::UrlParseError(err)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    struct MockOkResolver {
        pub expected_uri: String,
        pub resolved_uri: String,
    }

    impl Resolver for MockOkResolver {
        fn resolve(
            &self,
            component_uri: &str,
        ) -> FutureObj<Result<fsys::Component, ResolverError>> {
            assert_eq!(self.expected_uri, component_uri);
            FutureObj::new(Box::new(future::ok(fsys::Component {
                resolved_uri: Some(self.resolved_uri.clone()),
                decl: Some(fsys::ComponentDecl {
                    program: None,
                    uses: None,
                    exposes: None,
                    offers: None,
                    facets: None,
                    children: None,
                }),
                package: None,
            })))
        }
    }

    struct MockErrorResolver {
        pub expected_uri: String,
        pub error: ResolverError,
    }

    impl Resolver for MockErrorResolver {
        fn resolve(
            &self,
            component_uri: &str,
        ) -> FutureObj<Result<fsys::Component, ResolverError>> {
            assert_eq!(self.expected_uri, component_uri);
            FutureObj::new(Box::new(future::err(self.error.clone())))
        }
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn register_and_resolve() {
        let mut registry = ResolverRegistry::new();
        registry.register(
            "foo".to_string(),
            Box::new(MockOkResolver {
                expected_uri: "foo://uri".to_string(),
                resolved_uri: "foo://resolved".to_string(),
            }),
        );
        registry.register(
            "bar".to_string(),
            Box::new(MockErrorResolver {
                expected_uri: "bar://uri".to_string(),
                error: ResolverError::ComponentNotAvailable,
            }),
        );

        // Resolve known scheme that returns success.
        let component = await!(registry.resolve("foo://uri")).unwrap();
        assert_eq!("foo://resolved", component.resolved_uri.unwrap());

        // Resolve a different scheme that produces an error.
        assert_eq!(
            Err(ResolverError::ComponentNotAvailable),
            await!(registry.resolve("bar://uri"))
        );

        // Resolve an unknown scheme.
        assert_eq!(
            Err(ResolverError::SchemeNotRegistered),
            await!(registry.resolve("unknown://uri")),
        );

        // Resolve an URL lacking a scheme.
        assert_eq!(
            Err(ResolverError::UrlParseError(url::ParseError::RelativeUrlWithoutBase)),
            await!(registry.resolve("xxx")),
        );
    }
}
