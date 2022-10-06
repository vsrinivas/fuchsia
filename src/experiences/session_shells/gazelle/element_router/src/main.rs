// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::{BTreeSet, HashMap};

use anyhow::Context;
use config_lib::Config;
use fidl::endpoints;
use fidl_connector::Connect;
use fidl_fuchsia_element as felement;
use fuchsia_component::server;
use futures::StreamExt;

type Backend = fidl_connector::ServiceReconnector<felement::ManagerMarker>;

struct Router<'a> {
    /// When a `element.Manager/ProposeElement` request comes in, if the URL
    /// matches any of these `rules`, that request is proxied to to the
    /// corresponding `element.Manager` backend.
    rules: Vec<(Matcher, &'a Backend)>,

    /// If the URL doesn't match any of the given `rules`, or the request is
    /// somehow malformed, it gets proxied to the `default` backend.
    default: &'a Backend,
}

impl Router<'_> {
    async fn propose_element(
        &self,
        spec: felement::Spec,
        controller: Option<endpoints::ServerEnd<felement::ControllerMarker>>,
    ) -> anyhow::Result<Result<(), felement::ProposeElementError>> {
        let backend = self
            .select_backend(spec.component_url.as_ref())
            .connect()
            .context("acquiring channel")?;
        backend.propose_element(spec, controller).await.context("calling propose_element")
    }

    fn select_backend(&self, url: Option<&String>) -> &Backend {
        if let Some(url) = url {
            for (matcher, backend) in self.rules.iter() {
                if matcher.matches(url) {
                    return backend;
                }
            }
        }
        self.default
    }

    async fn serve_element_manager_request_stream(&self, stream: felement::ManagerRequestStream) {
        stream
            .for_each_concurrent(None, |request: fidl::Result<felement::ManagerRequest>| async {
                match request {
                    Err(err) => {
                        tracing::error!("FIDL error receiving element.Manager request: {}", err);
                        return;
                    }
                    Ok(felement::ManagerRequest::ProposeElement {
                        spec,
                        controller,
                        responder,
                    }) => {
                        let mut response = match self.propose_element(spec, controller).await {
                            Ok(response) => response,
                            Err(err) => {
                                tracing::error!(
                                    "FIDL error proxying ProposeElement request.
                                    Dropping responder without replying: {}",
                                    err
                                );
                                return;
                            }
                        };
                        if let Err(err) = responder.send(&mut response) {
                            tracing::error!("FIDL error proxying ProposeElement response: {}", err)
                        }
                    }
                }
            })
            .await;
    }
}

/// A Matcher is a predicate over component_urls.
enum Matcher {
    /// An Exact matcher matches a component_url if it exactly matches the given
    /// string.
    Exact(String),

    /// A Scheme matcher matches a component_url if the component_url correctly
    /// parses as a URL, and has a matching scheme.
    Scheme(String),
}

impl Matcher {
    fn matches(&self, component_url: &str) -> bool {
        match self {
            Matcher::Exact(matcher_url) => matcher_url == component_url,
            Matcher::Scheme(matcher_scheme) => {
                // NOTE: Parsing the URL inside `matches` means we may be
                // parsing the same URL multiple times, but it really doesn't
                // matter from a performance perspective, and the logic is
                // easier to understand this way without having to worry about
                // memoization.
                match url::Url::parse(component_url) {
                    Ok(parsed_url) => matcher_scheme == parsed_url.scheme(),
                    Err(err) => {
                        tracing::info!(
                            "URL for ProposeElement ({:?}) request does not parse: {:?}",
                            component_url,
                            err
                        );
                        false
                    }
                }
            }
        }
    }
}

enum IncomingService {
    ElementManager(felement::ManagerRequestStream),
}

#[fuchsia::main(logging = true)]
async fn main() -> anyhow::Result<()> {
    let config = Config::take_from_startup_handle();
    tracing::info!("element_router config: {:?}", config);

    let exact_rules = config.url_to_backend.into_iter().map(|rule: String| -> (Matcher, String) {
        let split: Vec<&str> = rule.split("|").collect();
        assert_eq!(split.len(), 2, "malformed rule: {:?}", rule);
        (Matcher::Exact(split[0].to_owned()), split[1].to_owned())
    });

    let scheme_rules =
        config.scheme_to_backend.into_iter().map(|rule: String| -> (Matcher, String) {
            let split: Vec<&str> = rule.split("|").collect();
            assert_eq!(split.len(), 2, "malformed rule: {:?}", rule);
            (Matcher::Scheme(split[0].to_owned()), split[1].to_owned())
        });

    let all_rules: Vec<(Matcher, String)> = exact_rules.chain(scheme_rules).collect();

    let all_backend_names: BTreeSet<String> = all_rules
        .iter()
        .map(|(_, backend)| backend.to_owned())
        .chain(std::iter::once(config.default_backend.to_owned()))
        .collect();

    let all_backends: HashMap<String, Backend> = all_backend_names
        .into_iter()
        .map(|backend_name| {
            let backend = Backend::with_service_at_path("/svc/".to_owned() + &backend_name);
            (backend_name, backend)
        })
        .collect();

    let router = Router {
        rules: all_rules
            .into_iter()
            .map(|(matcher, backend_name)| {
                let backend = all_backends
                    .get(&backend_name)
                    .expect("somehow we didn't provision this backend?");
                (matcher, backend)
            })
            .collect(),
        default: all_backends
            .get(&config.default_backend)
            .expect("somehow we didn't provision this backend?"),
    };

    let mut fs = server::ServiceFs::new();
    fs.dir("svc").add_fidl_service(IncomingService::ElementManager);
    fs.take_and_serve_directory_handle()?;

    fs.for_each_concurrent(None, |connection_request| async {
        match connection_request {
            IncomingService::ElementManager(stream) => {
                router.serve_element_manager_request_stream(stream).await;
            }
        }
    })
    .await;
    Ok(())
}
