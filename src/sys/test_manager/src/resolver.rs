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
    tracing::error,
};

async fn hermetic_resolve(
    component_url: &str,
    allowed_package_names: &Vec<String>,
    universe_resolver: &fsys::ComponentResolverProxy,
) -> Result<fsys::Component, fsys::ResolverError> {
    let package_url = PkgUrl::parse(component_url).map_err(|_| fsys::ResolverError::InvalidArgs)?;
    let package_name = package_url.name().to_string();
    if !allowed_package_names.contains(&package_name) {
        error!(
            "failed to resolve component {}: package {} is not in the set of allowed packages {:?}",
            &component_url, &package_name, allowed_package_names
        );
        return Err(fsys::ResolverError::PackageNotFound);
    }
    universe_resolver.resolve(component_url).await.map_err(|err| {
        error!("failed to resolve component {}: {:?}", &component_url, err);
        fsys::ResolverError::Internal
    })?
}

pub async fn serve_hermetic_resolver(
    handles: LocalComponentHandles,
    allowed_package_names: Vec<String>,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    let mut tasks = vec![];
    let universe_resolver = handles
        .connect_to_protocol::<fsys::ComponentResolverMarker>()
        .expect("failed to connect to system resolver");

    fs.dir("svc").add_fidl_service(move |mut stream: fsys::ComponentResolverRequestStream| {
        let allowed_package_names = allowed_package_names.clone();
        let universe_resolver = universe_resolver.clone();
        tasks.push(fasync::Task::local(async move {
            while let Some(fsys::ComponentResolverRequest::Resolve { component_url, responder }) =
                stream.try_next().await.expect("failed to serve component resolver")
            {
                match hermetic_resolve(&component_url, &allowed_package_names, &universe_resolver)
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
        fuchsia_component_test::error::Error as RealmBuilderError,
        fuchsia_component_test::new::{
            Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route,
        },
    };

    async fn serve_local_universe_resolver(handles: LocalComponentHandles) -> Result<(), Error> {
        let mut fs = ServiceFs::new();
        let mut tasks = vec![];

        fs.dir("svc").add_fidl_service(move |mut stream: fsys::ComponentResolverRequestStream| {
            tasks.push(fasync::Task::local(async move {
                while let Some(fsys::ComponentResolverRequest::Resolve {
                    component_url,
                    responder,
                }) = stream.try_next().await.expect("failed to serve component resolver")
                {
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
            }));
        });
        fs.serve_connection(handles.outgoing_dir.into_channel())?;
        fs.collect::<()>().await;
        Ok(())
    }

    // Constructs a test realm that contains a local system resolver that we
    // route to our hermetic resolver.
    async fn construct_test_realm(
        allowed_package_names: Vec<String>,
    ) -> Result<RealmInstance, RealmBuilderError> {
        let builder = RealmBuilder::new().await?;

        let local_universe_resolver = builder
            .add_local_child(
                "local_universe_resolver",
                move |handles| Box::pin(serve_local_universe_resolver(handles)),
                ChildOptions::new(),
            )
            .await?;
        let hermetic_resolver = builder
            .add_local_child(
                "hermetic_resolver",
                move |handles| {
                    Box::pin(serve_hermetic_resolver(handles, allowed_package_names.clone()))
                },
                ChildOptions::new(),
            )
            .await?;

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.sys2.ComponentResolver"))
                    .from(&local_universe_resolver)
                    .to(&hermetic_resolver),
            )
            .await?;
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.sys2.ComponentResolver"))
                    .from(&hermetic_resolver)
                    .to(Ref::parent()),
            )
            .await?;

        builder.build().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_successful_resolve() {
        let realm = construct_test_realm(vec![String::from("package-one")])
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
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_package_not_allowed() {
        let realm = construct_test_realm(vec![]).await.expect("failed to construct test realm");
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
        let realm = construct_test_realm(vec![String::from("package-two")])
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
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_invalid_url() {
        let realm = construct_test_realm(vec![String::from("package-two")])
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
