// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::new::LocalComponentHandles,
    fuchsia_url::pkg_url::PkgUrl,
    futures::{StreamExt, TryStreamExt},
    maplit::hashset,
    std::collections::HashSet,
    std::sync::Arc,
    tracing::{error, warn},
};

fn get_global_allowlist() -> HashSet<String> {
    hashset! {
        "driver_test_realm".to_string(),
        "regulatory_region".to_string(),
        "archivist-with-feedback-filtering".to_string(),
        "archivist-with-legacy-metrics".to_string(),
        "archivist-with-feedback-filtering-disabled".to_string(),
        "battery-manager".to_string(),
        "fuzz-registry".to_string(),
        "test_manager".to_string(),
    }
}

async fn hermetic_resolve(
    component_url: &str,
    allowed_package_names: &HashSet<String>,
    universe_resolver: Arc<fsys::ComponentResolverProxy>,
    enforce: bool,
) -> Result<fsys::Component, fsys::ResolverError> {
    let package_url = PkgUrl::parse(component_url).map_err(|_| fsys::ResolverError::InvalidArgs)?;
    let package_name = package_url.name();
    if !allowed_package_names.contains(package_name.as_ref()) {
        if enforce {
            error!(
                "failed to resolve component {}: package {} is not in the set of allowed packages: {:?}",
                &component_url, package_name, allowed_package_names
            );
            return Err(fsys::ResolverError::PackageNotFound);
        } else {
            warn!(
                "package {} used by component {} is not in the set of allowed packages: {:?}\n \
                    this will be an error in the near future; see https://fxbug.dev/83130 for more info.",
                package_name, &component_url, allowed_package_names);
        }
    }
    universe_resolver.resolve(component_url).await.map_err(|err| {
        error!("failed to resolve component {}: {:?}", &component_url, err);
        fsys::ResolverError::Internal
    })?
}

pub async fn serve_hermetic_resolver(
    handles: LocalComponentHandles,
    allowed_package_names: HashSet<String>,
    universe_resolver: Arc<fsys::ComponentResolverProxy>,
    enforce: bool,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    let mut tasks = vec![];

    fs.dir("svc").add_fidl_service(move |mut stream: fsys::ComponentResolverRequestStream| {
        let allowed_package_names =
            allowed_package_names.union(&get_global_allowlist()).cloned().collect();
        let universe_resolver = universe_resolver.clone();
        tasks.push(fasync::Task::local(async move {
            while let Some(fsys::ComponentResolverRequest::Resolve { component_url, responder }) =
                stream.try_next().await.expect("failed to serve component resolver")
            {
                match hermetic_resolve(
                    &component_url,
                    &allowed_package_names,
                    universe_resolver.clone(),
                    enforce,
                )
                .await
                {
                    Ok(result) => responder.send(&mut Ok(result)),
                    Err(err) => responder.send(&mut Err(err.into())),
                }
                .expect("failed sending response");
            }
        }));
    });
    fs.serve_connection(handles.outgoing_dir.into_channel())?;
    fs.collect::<()>().await;
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_proxy_and_stream,
        fuchsia_component_test::error::Error as RealmBuilderError,
        fuchsia_component_test::new::{
            Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route,
        },
    };

    async fn respond_to_resolve_requests(stream: &mut fsys::ComponentResolverRequestStream) {
        let request = stream
            .next()
            .await
            .expect("did not get next request")
            .expect("error getting next request");
        match request {
            fsys::ComponentResolverRequest::Resolve { component_url, responder } => {
                match component_url.as_str() {
                    "fuchsia-pkg://fuchsia.com/package-one#meta/comp.cm" => {
                        responder.send(&mut Ok(fsys::Component::EMPTY))
                    }
                    "fuchsia-pkg://fuchsia.com/package-two#meta/comp.cm" => {
                        responder.send(&mut Err(fsys::ResolverError::ResourceUnavailable))
                    }
                    _ => responder.send(&mut Err(fsys::ResolverError::Internal)),
                }
                .expect("failed sending response");
            }
        }
    }

    // Constructs a test realm that contains a local system resolver that we
    // route to our hermetic resolver.
    async fn construct_test_realm(
        allowed_package_names: HashSet<String>,
        mock_universe_resolver: Arc<fsys::ComponentResolverProxy>,
    ) -> Result<RealmInstance, RealmBuilderError> {
        // Set up a realm to test the hermetic resolver.
        let builder = RealmBuilder::new().await?;

        let hermetic_resolver = builder
            .add_local_child(
                "hermetic_resolver",
                move |handles| {
                    Box::pin(serve_hermetic_resolver(
                        handles,
                        allowed_package_names.clone(),
                        mock_universe_resolver.clone(),
                        true,
                    ))
                },
                ChildOptions::new(),
            )
            .await?;
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<fsys::ComponentResolverMarker>())
                    .from(&hermetic_resolver)
                    .to(Ref::parent()),
            )
            .await?;

        builder.build().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_successful_resolve() {
        let mut allowed = HashSet::new();
        allowed.insert("package-one".to_string());

        let (resolver_proxy, mut resolver_request_stream) =
            create_proxy_and_stream::<fsys::ComponentResolverMarker>()
                .expect("failed to create mock universe resolver proxy");
        let universe_resolver_task = fasync::Task::spawn(async move {
            respond_to_resolve_requests(&mut resolver_request_stream).await;
            drop(resolver_request_stream);
        });

        let realm = construct_test_realm(allowed, Arc::new(resolver_proxy))
            .await
            .expect("failed to construct test realm");
        let hermetic_resolver_proxy = realm
            .root
            .connect_to_protocol_at_exposed_dir::<fsys::ComponentResolverMarker>()
            .unwrap();

        assert_eq!(
            hermetic_resolver_proxy
                .resolve("fuchsia-pkg://fuchsia.com/package-one#meta/comp.cm")
                .await
                .unwrap(),
            Ok(fsys::Component::EMPTY)
        );
        universe_resolver_task.await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_package_not_allowed() {
        let (resolver_proxy, _) = create_proxy_and_stream::<fsys::ComponentResolverMarker>()
            .expect("failed to create mock universe resolver proxy");

        let realm = construct_test_realm(HashSet::new(), Arc::new(resolver_proxy))
            .await
            .expect("failed to construct test realm");
        let hermetic_resolver_proxy = realm
            .root
            .connect_to_protocol_at_exposed_dir::<fsys::ComponentResolverMarker>()
            .unwrap();

        assert_eq!(
            hermetic_resolver_proxy
                .resolve("fuchsia-pkg://fuchsia.com/package-one#meta/comp.cm")
                .await
                .unwrap(),
            Err(fsys::ResolverError::PackageNotFound)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_failed_resolve() {
        let (resolver_proxy, mut resolver_request_stream) =
            create_proxy_and_stream::<fsys::ComponentResolverMarker>()
                .expect("failed to create mock universe resolver proxy");
        let universe_resolver_task = fasync::Task::spawn(async move {
            respond_to_resolve_requests(&mut resolver_request_stream).await;
            drop(resolver_request_stream);
        });

        let mut allowed = HashSet::new();
        allowed.insert("package-two".to_string());

        let realm = construct_test_realm(allowed, Arc::new(resolver_proxy))
            .await
            .expect("failed to construct test realm");
        let hermetic_resolver_proxy = realm
            .root
            .connect_to_protocol_at_exposed_dir::<fsys::ComponentResolverMarker>()
            .unwrap();

        assert_eq!(
            hermetic_resolver_proxy
                .resolve("fuchsia-pkg://fuchsia.com/package-two#meta/comp.cm")
                .await
                .unwrap(),
            Err(fsys::ResolverError::ResourceUnavailable)
        );
        universe_resolver_task.await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_invalid_url() {
        let (resolver_proxy, _) = create_proxy_and_stream::<fsys::ComponentResolverMarker>()
            .expect("failed to create mock universe resolver proxy");

        let mut allowed = HashSet::new();
        allowed.insert("package-two".to_string());

        let realm = construct_test_realm(allowed, Arc::new(resolver_proxy))
            .await
            .expect("failed to construct test realm");
        let hermetic_resolver_proxy = realm
            .root
            .connect_to_protocol_at_exposed_dir::<fsys::ComponentResolverMarker>()
            .unwrap();

        assert_eq!(
            hermetic_resolver_proxy.resolve("invalid_url").await.unwrap(),
            Err(fsys::ResolverError::InvalidArgs)
        );
    }
}
