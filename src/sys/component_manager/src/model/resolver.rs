// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, Fail},
    fidl_fuchsia_sys2 as fsys,
    futures::future,
    futures::future::FutureObj,
    std::collections::HashMap,
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
    resolvers: HashMap<String, Box<dyn Resolver + Send + Sync + 'static>>,
}

impl ResolverRegistry {
    pub fn new() -> ResolverRegistry {
        ResolverRegistry { resolvers: HashMap::new() }
    }

    pub fn register(
        &mut self,
        scheme: String,
        resolver: Box<dyn Resolver + Send + Sync + 'static>,
    ) {
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
            Err(e) => FutureObj::new(Box::new(future::err(ResolverError::uri_parse_error(
                component_uri,
                e,
            )))),
        }
    }
}

/// Errors produced by `Resolver`.
#[derive(Debug, Fail)]
pub enum ResolverError {
    #[fail(display = "component not available with uri \"{}\": {}", uri, err)]
    ComponentNotAvailable {
        uri: String,
        #[fail(cause)]
        err: Error,
    },
    #[fail(display = "component manifest not available for uri \"{}\": {}", uri, err)]
    ManifestNotAvailable {
        uri: String,
        #[fail(cause)]
        err: Error,
    },
    #[fail(display = "component manifest invalid for uri \"{}\": {}", uri, err)]
    ManifestInvalid {
        uri: String,
        #[fail(cause)]
        err: Error,
    },
    #[fail(display = "scheme not registered")]
    SchemeNotRegistered,
    #[fail(display = "failed to parse uri \"{}\": {}", uri, err)]
    UriParseError {
        uri: String,
        #[fail(cause)]
        err: Error,
    },
    #[fail(display = "uri missing resource \"{}\"", uri)]
    UriMissingResourceError { uri: String },
}

impl ResolverError {
    pub fn component_not_available(uri: impl Into<String>, err: impl Into<Error>) -> ResolverError {
        ResolverError::ComponentNotAvailable { uri: uri.into(), err: err.into() }
    }

    pub fn manifest_not_available(uri: impl Into<String>, err: impl Into<Error>) -> ResolverError {
        ResolverError::ManifestNotAvailable { uri: uri.into(), err: err.into() }
    }

    pub fn manifest_invalid(uri: impl Into<String>, err: impl Into<Error>) -> ResolverError {
        ResolverError::ManifestInvalid { uri: uri.into(), err: err.into() }
    }

    pub fn uri_parse_error(uri: impl Into<String>, err: impl Into<Error>) -> ResolverError {
        ResolverError::UriParseError { uri: uri.into(), err: err.into() }
    }

    pub fn uri_missing_resource_error(uri: impl Into<String>) -> ResolverError {
        ResolverError::UriMissingResourceError { uri: uri.into() }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, failure::format_err};

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
                    storage: None,
                }),
                package: None,
            })))
        }
    }

    struct MockErrorResolver {
        pub expected_uri: String,
        pub error: Box<dyn Fn(&str) -> ResolverError + Send + Sync + 'static>,
    }

    impl Resolver for MockErrorResolver {
        fn resolve(
            &self,
            component_uri: &str,
        ) -> FutureObj<Result<fsys::Component, ResolverError>> {
            assert_eq!(self.expected_uri, component_uri);
            FutureObj::new(Box::new(future::err((self.error)(component_uri))))
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
                error: Box::new(|uri| {
                    ResolverError::component_not_available(uri, format_err!("not available"))
                }),
            }),
        );

        // Resolve known scheme that returns success.
        let component = await!(registry.resolve("foo://uri")).unwrap();
        assert_eq!("foo://resolved", component.resolved_uri.unwrap());

        // Resolve a different scheme that produces an error.
        let expected_res: Result<fsys::Component, ResolverError> =
            Err(ResolverError::component_not_available("bar://uri", format_err!("not available")));
        assert_eq!(
            format!("{:?}", expected_res),
            format!("{:?}", await!(registry.resolve("bar://uri")))
        );

        // Resolve an unknown scheme
        let expected_res: Result<fsys::Component, ResolverError> =
            Err(ResolverError::SchemeNotRegistered);
        assert_eq!(
            format!("{:?}", expected_res),
            format!("{:?}", await!(registry.resolve("unknown://uri"))),
        );

        // Resolve an URL lacking a scheme.
        let expected_res: Result<fsys::Component, ResolverError> =
            Err(ResolverError::uri_parse_error("xxx", url::ParseError::RelativeUrlWithoutBase));
        assert_eq!(format!("{:?}", expected_res), format!("{:?}", await!(registry.resolve("xxx"))),);
    }
}
