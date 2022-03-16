// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_sys as fv1sys, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::new::LocalComponentHandles,
    fuchsia_url::pkg_url::PkgUrl,
    futures::{StreamExt, TryStreamExt},
    std::collections::HashSet,
    std::sync::Arc,
    tracing::{error, warn},
};

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
                "failed to resolve component {}: package {} is not in the set of allowed packages: {:?}
                \nSee https://fuchsia.dev/fuchsia-src/development/testing/components/test_runner_framework?hl=en#hermetic-resolver
                for more information.",
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
    allowed_package_names: Arc<HashSet<String>>,
    universe_resolver: Arc<fsys::ComponentResolverProxy>,
    enforce: bool,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    let mut tasks = vec![];

    fs.dir("svc").add_fidl_service(move |mut stream: fsys::ComponentResolverRequestStream| {
        let universe_resolver = universe_resolver.clone();
        let allowed_package_names = allowed_package_names.clone();
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

async fn hermetic_loader(
    component_url: &str,
    allowed_package_names: &HashSet<String>,
    loader_service: fv1sys::LoaderProxy,
) -> Option<Box<fv1sys::Package>> {
    let package_url = match PkgUrl::parse(component_url) {
        Ok(u) => u,
        Err(e) => {
            warn!("Invalid component url {}: {}", component_url, e);
            return None;
        }
    };
    let package_name = package_url.name();
    if !allowed_package_names.contains(package_name.as_ref()) {
        error!(
                "failed to resolve component {}: package {} is not in the set of allowed packages: {:?}
                \nSee https://fuchsia.dev/fuchsia-src/development/testing/components/test_runner_framework?hl=en#hermetic-resolver
                for more information.",
                &component_url, package_name, allowed_package_names
            );
        return None;
    }
    match loader_service.load_url(component_url).await {
        Ok(r) => r,
        Err(e) => {
            warn!("can't communicate with global loader: {}", e);
            None
        }
    }
}

pub async fn serve_hermetic_loader(
    mut stream: fv1sys::LoaderRequestStream,
    allowed_package_names: Arc<HashSet<String>>,
    loader_service: fv1sys::LoaderProxy,
) {
    while let Some(fv1sys::LoaderRequest::LoadUrl { url, responder }) =
        stream.try_next().await.expect("failed to serve loader")
    {
        let mut result =
            hermetic_loader(&url, &allowed_package_names, loader_service.clone()).await.map(|p| *p);

        responder.send(result.as_mut()).expect("failed sending response");
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
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

    async fn respond_to_loader_requests(stream: &mut fv1sys::LoaderRequestStream) {
        let request = stream
            .next()
            .await
            .expect("did not get next request")
            .expect("error getting next request");
        match request {
            fv1sys::LoaderRequest::LoadUrl { url, responder } => {
                match url.as_str() {
                    "fuchsia-pkg://fuchsia.com/package-one#meta/comp.cm" => {
                        responder.send(Some(&mut fv1sys::Package {
                            data: None,
                            directory: None,
                            resolved_url: url,
                        }))
                    }
                    "fuchsia-pkg://fuchsia.com/package-two#meta/comp.cm" => responder.send(None),
                    _ => responder.send(None),
                }
                .expect("failed sending response");
            }
        }
    }

    // Constructs a test realm that contains a local system resolver that we
    // route to our hermetic resolver.
    async fn construct_test_realm(
        allowed_package_names: Arc<HashSet<String>>,
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

        let realm = construct_test_realm(allowed.into(), Arc::new(resolver_proxy))
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

        let realm = construct_test_realm(HashSet::new().into(), Arc::new(resolver_proxy))
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

        let realm = construct_test_realm(allowed.into(), Arc::new(resolver_proxy))
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

        let realm = construct_test_realm(allowed.into(), Arc::new(resolver_proxy))
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

    mod loader {
        use super::*;

        #[fasync::run_singlethreaded(test)]
        async fn test_successful_loader() {
            let mut allowed = HashSet::new();
            allowed.insert("package-one".to_string());

            let (loader_proxy, mut loader_request_stream) =
                create_proxy_and_stream::<fv1sys::LoaderMarker>()
                    .expect("failed to create mock loader proxy");
            let (proxy, stream) = create_proxy_and_stream::<fv1sys::LoaderMarker>().unwrap();

            let _loader_task = fasync::Task::spawn(async move {
                respond_to_loader_requests(&mut loader_request_stream).await;
                drop(loader_request_stream);
            });

            let _serve_task =
                fasync::Task::spawn(serve_hermetic_loader(stream, allowed.into(), loader_proxy));

            assert_matches!(
                proxy.load_url("fuchsia-pkg://fuchsia.com/package-one#meta/comp.cm").await.unwrap(),
                Some(_)
            );
        }

        #[fasync::run_singlethreaded(test)]
        async fn test_package_not_allowed() {
            let (loader_proxy, _) = create_proxy_and_stream::<fv1sys::LoaderMarker>()
                .expect("failed to create mock loader proxy");

            let (proxy, stream) = create_proxy_and_stream::<fv1sys::LoaderMarker>().unwrap();

            let _serve_task = fasync::Task::spawn(serve_hermetic_loader(
                stream,
                HashSet::new().into(),
                loader_proxy,
            ));

            assert_eq!(
                proxy.load_url("fuchsia-pkg://fuchsia.com/package-one#meta/comp.cm").await.unwrap(),
                None
            );
        }

        #[fasync::run_singlethreaded(test)]
        async fn test_failed_loader() {
            let (loader_proxy, mut loader_request_stream) =
                create_proxy_and_stream::<fv1sys::LoaderMarker>()
                    .expect("failed to create mock loader proxy");
            let _loader_task = fasync::Task::spawn(async move {
                respond_to_loader_requests(&mut loader_request_stream).await;
                drop(loader_request_stream);
            });

            let mut allowed = HashSet::new();
            allowed.insert("package-two".to_string());

            let (proxy, stream) = create_proxy_and_stream::<fv1sys::LoaderMarker>().unwrap();

            let _serve_task =
                fasync::Task::spawn(serve_hermetic_loader(stream, allowed.into(), loader_proxy));

            assert_eq!(
                proxy.load_url("fuchsia-pkg://fuchsia.com/package-two#meta/comp.cm").await.unwrap(),
                None
            );
        }

        #[fasync::run_singlethreaded(test)]
        async fn test_invalid_url_loader() {
            let (loader_proxy, _) = create_proxy_and_stream::<fv1sys::LoaderMarker>()
                .expect("failed to create mock loader proxy");

            let mut allowed = HashSet::new();
            allowed.insert("package-two".to_string());

            let (proxy, stream) = create_proxy_and_stream::<fv1sys::LoaderMarker>().unwrap();

            let _serve_task =
                fasync::Task::spawn(serve_hermetic_loader(stream, allowed.into(), loader_proxy));

            assert_eq!(proxy.load_url("invalid_url").await.unwrap(), None);
        }
    }
}
