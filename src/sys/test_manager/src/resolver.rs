// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::facet,
    anyhow::Error,
    diagnostics_log as flog,
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_component_resolution as fresolution, fidl_fuchsia_logger as flogger,
    fidl_fuchsia_sys as fv1sys, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::LocalComponentHandles,
    fuchsia_url::{AbsoluteComponentUrl, ComponentUrl, PackageUrl},
    futures::{lock::Mutex, StreamExt, TryStreamExt},
    itertools::Itertools,
    std::{collections::HashSet, sync::Arc},
    tracing::{error, info, warn},
};

// List of non hermetic packages accessed by a test which are logged to make it easy to transition.
#[derive(Debug)]
pub struct NonHermeticPkgList {
    test_url: String,
    list: Mutex<HashSet<String>>,
}

impl NonHermeticPkgList {
    fn new(test_url: String) -> Arc<Self> {
        Arc::new(Self { test_url, list: HashSet::new().into() })
    }

    async fn add_pkg(&self, pkg_name: &str) {
        let mut list = self.list.lock().await;
        list.insert(format!("\"{}\"", pkg_name));
    }
}

impl Drop for NonHermeticPkgList {
    fn drop(&mut self) {
        let list = self.list.get_mut();
        if list.len() > 0 {
            info!(
                "Test '{}' uses non-hermetic packages. \
            Please add below line to facets of your test manifest:
            \"{}\": [ {} ]\
            \nSee https://fuchsia.dev/fuchsia-src/development/testing/components/test_runner_framework?hl=en#hermetic-resolver
            for more information.",
                self.test_url,
                facet::TEST_DEPRECATED_ALLOWED_PACKAGES_FACET_KEY,
                list.drain().join(", ")
            );
        }
    }
}

// Flag to enforce hermetic resolution. If false, default AllowedPackages value would be 'All'.
// This flag helps us easily revert the changes if there is a problem.
pub(crate) const ENFORCE_HERMETIC_RESOLUTION: bool = false;

// Enum donating the list of non-hermetic packages allowed to resolved by a test.
#[derive(Clone, Debug)]
pub enum AllowedPackages {
    // Temporary enum for transition which will allow all packages.
    All(Arc<NonHermeticPkgList>),

    // Strict list of allowed packages.
    List(Arc<HashSet<String>>),
}

impl AllowedPackages {
    pub fn default(test_url: &str) -> Self {
        match ENFORCE_HERMETIC_RESOLUTION {
            false => Self::all(test_url.to_string()),
            true => Self::zero_allowed_pkgs(),
        }
    }

    pub fn all(test_url: String) -> Self {
        Self::All(NonHermeticPkgList::new(test_url))
    }

    pub fn zero_allowed_pkgs() -> Self {
        Self::List(Arc::new(HashSet::new()))
    }

    pub fn from_iter<I>(iter: I) -> Self
    where
        I: IntoIterator<Item = String>,
    {
        Self::List(Arc::new(HashSet::from_iter(iter)))
    }
}

impl PartialEq for AllowedPackages {
    fn eq(&self, other: &Self) -> bool {
        match (self, other) {
            (AllowedPackages::All(_), AllowedPackages::All(_)) => true,
            (AllowedPackages::List(l1), AllowedPackages::List(l2)) => l1.eq(l2),
            _ => false,
        }
    }

    fn ne(&self, other: &Self) -> bool {
        !self.eq(other)
    }
}

async fn validate_hermetic_package(
    component_url_str: &str,
    subscriber: Arc<dyn tracing::Subscriber + Send + Sync + 'static>,
    hermetic_test_package_name: &String,
    other_allowed_packages: &AllowedPackages,
) -> Result<(), fresolution::ResolverError> {
    let component_url = ComponentUrl::parse(component_url_str).map_err(|err| {
        warn!("cannot parse {}, {:?}", component_url_str, err);
        fresolution::ResolverError::InvalidArgs
    })?;

    let allowed_packages = match other_allowed_packages {
        AllowedPackages::All(list) => {
            if let PackageUrl::Absolute(pkg_url) = &component_url.package_url() {
                let package_name = pkg_url.name();
                if package_name.as_ref() != hermetic_test_package_name {
                    list.add_pkg(package_name.as_ref()).await;
                }
            }
            return Ok(());
        }
        AllowedPackages::List(l) => l,
    };

    match component_url.package_url() {
        PackageUrl::Absolute(pkg_url) => {
            let package_name = pkg_url.name();
            if hermetic_test_package_name != package_name.as_ref()
                && !allowed_packages.contains(package_name.as_ref())
            {
                tracing::subscriber::with_default(subscriber, || {
                    warn!(
                    "failed to resolve component {}: package {} is not in the test package allowlist: '{}, {}'
                    \nSee https://fuchsia.dev/fuchsia-src/development/testing/components/test_runner_framework?hl=en#hermetic-resolver
                    for more information.",
                    &component_url_str, package_name, hermetic_test_package_name, allowed_packages.iter().join(", ")
                );
                });
                return Err(fresolution::ResolverError::PackageNotFound);
            }
        }
        PackageUrl::Relative(_url) => {
            // don't do anything as we don't restrict relative urls.
        }
    }
    Ok(())
}

async fn serve_resolver(
    mut stream: fresolution::ResolverRequestStream,
    subscriber: Arc<dyn tracing::Subscriber + Send + Sync + 'static>,
    hermetic_test_package_name: Arc<String>,
    other_allowed_packages: AllowedPackages,
    full_resolver: Arc<fresolution::ResolverProxy>,
) {
    while let Some(request) = stream.try_next().await.expect("failed to serve component resolver") {
        match request {
            fresolution::ResolverRequest::Resolve { component_url, responder } => {
                let mut result = if let Err(err) = validate_hermetic_package(
                    &component_url,
                    subscriber.clone(),
                    &hermetic_test_package_name,
                    &other_allowed_packages,
                )
                .await
                {
                    Err(err)
                } else {
                    let subscriber = subscriber.clone();
                    full_resolver.resolve(&component_url).await.unwrap_or_else(|err| {
                        tracing::subscriber::with_default(subscriber, || {
                            warn!("failed to resolve component {}: {:?}", component_url, err);
                        });
                        Err(fresolution::ResolverError::Internal)
                    })
                };
                if let Err(e) = responder.send(&mut result) {
                    warn!("Failed sending load response for {}: {}", component_url, e);
                }
            }
            fresolution::ResolverRequest::ResolveWithContext {
                component_url,
                mut context,
                responder,
            } => {
                // We don't need to worry about validating context because it should have
                // been produced by Resolve call above.
                let mut result = if let Err(err) = validate_hermetic_package(
                    &component_url,
                    subscriber.clone(),
                    &hermetic_test_package_name,
                    &other_allowed_packages,
                )
                .await
                {
                    Err(err)
                } else {
                    let subscriber = subscriber.clone();
                    full_resolver
                        .resolve_with_context(&component_url, &mut context)
                        .await
                        .unwrap_or_else(|err| {
                            tracing::subscriber::with_default(subscriber, || {
                                warn!(
                                    "failed to resolve component {} with context {:?}: {:?}",
                                    component_url, context, err
                                );
                            });
                            Err(fresolution::ResolverError::Internal)
                        })
                };
                if let Err(e) = responder.send(&mut result) {
                    warn!("Failed sending load response for {}: {}", component_url, e);
                }
            }
        }
    }
}

pub async fn serve_hermetic_resolver(
    handles: LocalComponentHandles,
    hermetic_test_package_name: Arc<String>,
    other_allowed_packages: AllowedPackages,
    full_resolver: Arc<fresolution::ResolverProxy>,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    let mut tasks = vec![];
    let log_proxy = handles
        .connect_to_named_protocol::<flogger::LogSinkMarker>(flogger::LogSinkMarker::DEBUG_NAME)?;
    let tags = ["test_resolver"];
    let (log_publisher, _interest_listener) = flog::Publisher::new_with_proxy(
        log_proxy,
        flog::PublishOptions { interest: flog::interest(tracing::Level::INFO), tags: Some(&tags) },
    )
    .unwrap();
    let log_publisher = Arc::new(log_publisher);
    fs.dir("svc").add_fidl_service(move |stream: fresolution::ResolverRequestStream| {
        let full_resolver = full_resolver.clone();
        let hermetic_test_package_name = hermetic_test_package_name.clone();
        let other_allowed_packages = other_allowed_packages.clone();
        let log_publisher = log_publisher.clone();
        tasks.push(fasync::Task::local(async move {
            serve_resolver(
                stream,
                log_publisher,
                hermetic_test_package_name,
                other_allowed_packages,
                full_resolver,
            )
            .await;
        }));
    });
    fs.serve_connection(handles.outgoing_dir)?;
    fs.collect::<()>().await;
    Ok(())
}

async fn hermetic_loader(
    component_url_str: &str,
    hermetic_test_package_name: &String,
    other_allowed_packages: &AllowedPackages,
    loader_service: fv1sys::LoaderProxy,
) -> Option<Box<fv1sys::Package>> {
    let component_url = match AbsoluteComponentUrl::parse(component_url_str) {
        Ok(u) => u,
        Err(e) => {
            warn!("Invalid component url {}: {}", component_url_str, e);
            return None;
        }
    };
    let package_name = component_url.package_url().name();

    match other_allowed_packages {
        AllowedPackages::All(list) => {
            if hermetic_test_package_name != package_name.as_ref() {
                list.add_pkg(package_name.as_ref()).await;
            }
        }
        AllowedPackages::List(allowed_packages) => {
            if hermetic_test_package_name != package_name.as_ref()
                && !allowed_packages.contains(package_name.as_ref())
            {
                error!(
                "failed to resolve component {}: package {} is not in the test package allowlist: '{}, {}'
                \nSee https://fuchsia.dev/fuchsia-src/development/testing/components/test_runner_framework?hl=en#hermetic-resolver
                for more information.",
                &component_url_str, package_name, hermetic_test_package_name, allowed_packages.iter().join(", ")
            );
                return None;
            }
        }
    }

    match loader_service.load_url(component_url_str).await {
        Ok(r) => r,
        Err(e) => {
            warn!("can't communicate with global loader: {}", e);
            None
        }
    }
}

pub async fn serve_hermetic_loader(
    mut stream: fv1sys::LoaderRequestStream,
    hermetic_test_package_name: Arc<String>,
    other_allowed_packages: AllowedPackages,
    loader_service: fv1sys::LoaderProxy,
) {
    while let Some(fv1sys::LoaderRequest::LoadUrl { url, responder }) =
        stream.try_next().await.expect("failed to serve loader")
    {
        let mut result = hermetic_loader(
            &url,
            &hermetic_test_package_name,
            &other_allowed_packages,
            loader_service.clone(),
        )
        .await
        .map(|p| *p);

        if let Err(e) = responder.send(result.as_mut()) {
            warn!("Failed sending load response for {}: {}", url, e);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;
    use fidl::endpoints::create_proxy_and_stream;
    use maplit::hashset;

    async fn respond_to_resolve_requests(mut stream: fresolution::ResolverRequestStream) {
        while let Some(request) =
            stream.try_next().await.expect("failed to serve component mock resolver")
        {
            match request {
                fresolution::ResolverRequest::Resolve { component_url, responder } => {
                    match component_url.as_str() {
                        "fuchsia-pkg://fuchsia.com/package-one#meta/comp.cm"
                        | "fuchsia-pkg://fuchsia.com/package-three#meta/comp.cm"
                        | "fuchsia-pkg://fuchsia.com/package-four#meta/comp.cm" => {
                            responder.send(&mut Ok(fresolution::Component::EMPTY))
                        }
                        "fuchsia-pkg://fuchsia.com/package-two#meta/comp.cm" => responder
                            .send(&mut Err(fresolution::ResolverError::ResourceUnavailable)),
                        _ => responder.send(&mut Err(fresolution::ResolverError::Internal)),
                    }
                    .expect("failed sending response");
                }
                fresolution::ResolverRequest::ResolveWithContext {
                    component_url,
                    context: _,
                    responder,
                } => {
                    match component_url.as_str() {
                        "fuchsia-pkg://fuchsia.com/package-one#meta/comp.cm" | "name#resource" => {
                            responder.send(&mut Ok(fresolution::Component::EMPTY))
                        }
                        _ => responder.send(&mut Err(fresolution::ResolverError::PackageNotFound)),
                    }
                    .expect("failed sending response");
                }
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
                    "fuchsia-pkg://fuchsia.com/package-one#meta/comp.cm"
                    | "fuchsia-pkg://fuchsia.com/package-three#meta/comp.cm"
                    | "fuchsia-pkg://fuchsia.com/package-four#meta/comp.cm"
                    | "fuchsia-pkg://fuchsia.com/package-five#meta/comp.cm" => {
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

    // Run hermetic resolver
    fn run_resolver(
        hermetic_test_package_name: Arc<String>,
        other_allowed_packages: AllowedPackages,
        mock_full_resolver: Arc<fresolution::ResolverProxy>,
    ) -> (fasync::Task<()>, fresolution::ResolverProxy) {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fresolution::ResolverMarker>().unwrap();
        let subscriber = tracing::subscriber::NoSubscriber::default();
        let task = fasync::Task::local(async move {
            serve_resolver(
                stream,
                Arc::new(subscriber),
                hermetic_test_package_name,
                other_allowed_packages,
                mock_full_resolver,
            )
            .await;
        });
        (task, proxy)
    }

    #[fuchsia::test]
    async fn test_successful_resolve() {
        let pkg_name = "package-one".to_string();

        let (resolver_proxy, resolver_request_stream) =
            create_proxy_and_stream::<fresolution::ResolverMarker>()
                .expect("failed to create mock full resolver proxy");
        let _full_resolver_task = fasync::Task::spawn(async move {
            respond_to_resolve_requests(resolver_request_stream).await;
        });

        let (_task, hermetic_resolver_proxy) = run_resolver(
            pkg_name.into(),
            AllowedPackages::zero_allowed_pkgs(),
            Arc::new(resolver_proxy),
        );

        assert_eq!(
            hermetic_resolver_proxy
                .resolve("fuchsia-pkg://fuchsia.com/package-one#meta/comp.cm")
                .await
                .unwrap(),
            Ok(fresolution::Component::EMPTY)
        );
        let mut mock_context = fresolution::Context { bytes: vec![0] };
        assert_eq!(
            hermetic_resolver_proxy
                .resolve_with_context("name#resource", &mut mock_context)
                .await
                .unwrap(),
            Ok(fresolution::Component::EMPTY)
        );
        assert_eq!(
            hermetic_resolver_proxy
                .resolve_with_context("name#not_found", &mut mock_context)
                .await
                .unwrap(),
            Err(fresolution::ResolverError::PackageNotFound)
        );
        assert_eq!(
            hermetic_resolver_proxy
                .resolve_with_context(
                    "fuchsia-pkg://fuchsia.com/package-one#meta/comp.cm",
                    &mut mock_context
                )
                .await
                .unwrap(),
            Ok(fresolution::Component::EMPTY)
        );
    }

    #[fuchsia::test]
    async fn drop_connection_on_resolve() {
        let pkg_name = "package-one".to_string();

        let (resolver_proxy, resolver_request_stream) =
            create_proxy_and_stream::<fresolution::ResolverMarker>()
                .expect("failed to create mock full resolver proxy");
        let _full_resolver_task = fasync::Task::spawn(async move {
            respond_to_resolve_requests(resolver_request_stream).await;
        });

        let (_task, hermetic_resolver_proxy) = run_resolver(
            pkg_name.into(),
            AllowedPackages::zero_allowed_pkgs(),
            Arc::new(resolver_proxy),
        );

        let _ =
            hermetic_resolver_proxy.resolve("fuchsia-pkg://fuchsia.com/package-one#meta/comp.cm");
        drop(hermetic_resolver_proxy); // code should not crash
    }

    #[fuchsia::test]
    async fn test_package_not_allowed() {
        let (resolver_proxy, _) = create_proxy_and_stream::<fresolution::ResolverMarker>()
            .expect("failed to create mock full resolver proxy");

        let (_task, hermetic_resolver_proxy) = run_resolver(
            "package-two".to_string().into(),
            AllowedPackages::zero_allowed_pkgs(),
            Arc::new(resolver_proxy),
        );

        assert_eq!(
            hermetic_resolver_proxy
                .resolve("fuchsia-pkg://fuchsia.com/package-one#meta/comp.cm")
                .await
                .unwrap(),
            Err(fresolution::ResolverError::PackageNotFound)
        );
        let mut mock_context = fresolution::Context { bytes: vec![0] };
        assert_eq!(
            hermetic_resolver_proxy
                .resolve_with_context(
                    "fuchsia-pkg://fuchsia.com/package-one#meta/comp.cm",
                    &mut mock_context
                )
                .await
                .unwrap(),
            Err(fresolution::ResolverError::PackageNotFound)
        );
    }

    #[fuchsia::test]
    async fn other_packages_allowed() {
        let (resolver_proxy, resolver_request_stream) =
            create_proxy_and_stream::<fresolution::ResolverMarker>()
                .expect("failed to create mock full resolver proxy");

        let list = hashset!("package-three".to_string(), "package-four".to_string());

        let _full_resolver_task = fasync::Task::spawn(async move {
            respond_to_resolve_requests(resolver_request_stream).await;
        });

        let (_task, hermetic_resolver_proxy) = run_resolver(
            "package-two".to_string().into(),
            AllowedPackages::List(list.into()),
            Arc::new(resolver_proxy),
        );

        assert_eq!(
            hermetic_resolver_proxy
                .resolve("fuchsia-pkg://fuchsia.com/package-one#meta/comp.cm")
                .await
                .unwrap(),
            Err(fresolution::ResolverError::PackageNotFound)
        );

        assert_eq!(
            hermetic_resolver_proxy
                .resolve("fuchsia-pkg://fuchsia.com/package-three#meta/comp.cm")
                .await
                .unwrap(),
            Ok(fresolution::Component::EMPTY)
        );

        assert_eq!(
            hermetic_resolver_proxy
                .resolve("fuchsia-pkg://fuchsia.com/package-four#meta/comp.cm")
                .await
                .unwrap(),
            Ok(fresolution::Component::EMPTY)
        );

        assert_eq!(
            hermetic_resolver_proxy
                .resolve("fuchsia-pkg://fuchsia.com/package-two#meta/comp.cm")
                .await
                .unwrap(),
            // we return this error from our mock resolver for package-two.
            Err(fresolution::ResolverError::ResourceUnavailable)
        );
    }

    #[fuchsia::test]
    async fn test_failed_resolve() {
        let (resolver_proxy, resolver_request_stream) =
            create_proxy_and_stream::<fresolution::ResolverMarker>()
                .expect("failed to create mock full resolver proxy");
        let _full_resolver_task = fasync::Task::spawn(async move {
            respond_to_resolve_requests(resolver_request_stream).await;
        });

        let pkg_name = "package-two".to_string();
        let (_task, hermetic_resolver_proxy) = run_resolver(
            pkg_name.into(),
            AllowedPackages::zero_allowed_pkgs(),
            Arc::new(resolver_proxy),
        );

        assert_eq!(
            hermetic_resolver_proxy
                .resolve("fuchsia-pkg://fuchsia.com/package-two#meta/comp.cm")
                .await
                .unwrap(),
            Err(fresolution::ResolverError::ResourceUnavailable)
        );
    }

    #[fuchsia::test]
    async fn test_invalid_url() {
        let (resolver_proxy, _) = create_proxy_and_stream::<fresolution::ResolverMarker>()
            .expect("failed to create mock full resolver proxy");

        let pkg_name = "package-two".to_string();
        let (_task, hermetic_resolver_proxy) = run_resolver(
            pkg_name.into(),
            AllowedPackages::zero_allowed_pkgs(),
            Arc::new(resolver_proxy),
        );

        assert_eq!(
            hermetic_resolver_proxy.resolve("invalid_url").await.unwrap(),
            Err(fresolution::ResolverError::InvalidArgs)
        );
    }

    mod loader {
        use super::*;

        #[fuchsia::test]
        async fn test_successful_loader() {
            let pkg_name = "package-one".to_string();

            let (loader_proxy, mut loader_request_stream) =
                create_proxy_and_stream::<fv1sys::LoaderMarker>()
                    .expect("failed to create mock loader proxy");
            let (proxy, stream) = create_proxy_and_stream::<fv1sys::LoaderMarker>().unwrap();

            let _loader_task = fasync::Task::spawn(async move {
                respond_to_loader_requests(&mut loader_request_stream).await;
                drop(loader_request_stream);
            });

            let _serve_task = fasync::Task::spawn(serve_hermetic_loader(
                stream,
                pkg_name.into(),
                AllowedPackages::zero_allowed_pkgs(),
                loader_proxy,
            ));

            assert_matches!(
                proxy.load_url("fuchsia-pkg://fuchsia.com/package-one#meta/comp.cm").await.unwrap(),
                Some(_)
            );
        }

        #[fuchsia::test]
        async fn drop_connection_on_load() {
            let pkg_name = "package-one".to_string();

            let (loader_proxy, mut loader_request_stream) =
                create_proxy_and_stream::<fv1sys::LoaderMarker>()
                    .expect("failed to create mock loader proxy");
            let (proxy, stream) = create_proxy_and_stream::<fv1sys::LoaderMarker>().unwrap();

            let loader_task = fasync::Task::spawn(async move {
                respond_to_loader_requests(&mut loader_request_stream).await;
                drop(loader_request_stream);
            });

            let _serve_task = fasync::Task::spawn(serve_hermetic_loader(
                stream,
                pkg_name.into(),
                AllowedPackages::zero_allowed_pkgs(),
                loader_proxy,
            ));

            let _ = proxy.load_url("fuchsia-pkg://fuchsia.com/package-one#meta/comp.cm");
            drop(proxy);
            loader_task.await;
        }

        // Logging disabled as this outputs ERROR log, which will fail the test.
        #[fuchsia::test(logging = false)]
        async fn test_package_not_allowed() {
            let (loader_proxy, _) = create_proxy_and_stream::<fv1sys::LoaderMarker>()
                .expect("failed to create mock loader proxy");

            let (proxy, stream) = create_proxy_and_stream::<fv1sys::LoaderMarker>().unwrap();

            let _serve_task = fasync::Task::spawn(serve_hermetic_loader(
                stream,
                "package-two".to_string().into(),
                AllowedPackages::zero_allowed_pkgs(),
                loader_proxy,
            ));

            assert_eq!(
                proxy.load_url("fuchsia-pkg://fuchsia.com/package-one#meta/comp.cm").await.unwrap(),
                None
            );
        }

        #[fuchsia::test]
        async fn test_failed_loader() {
            let (loader_proxy, mut loader_request_stream) =
                create_proxy_and_stream::<fv1sys::LoaderMarker>()
                    .expect("failed to create mock loader proxy");
            let _loader_task = fasync::Task::spawn(async move {
                respond_to_loader_requests(&mut loader_request_stream).await;
                drop(loader_request_stream);
            });

            let pkg_name = "package-two".to_string();

            let (proxy, stream) = create_proxy_and_stream::<fv1sys::LoaderMarker>().unwrap();

            let _serve_task = fasync::Task::spawn(serve_hermetic_loader(
                stream,
                pkg_name.into(),
                AllowedPackages::zero_allowed_pkgs(),
                loader_proxy,
            ));

            assert_eq!(
                proxy.load_url("fuchsia-pkg://fuchsia.com/package-two#meta/comp.cm").await.unwrap(),
                None
            );
        }

        // Logging disabled as this outputs ERROR log, which will fail the test.
        #[fuchsia::test(logging = false)]
        async fn other_packages_allowed() {
            let list = hashset!("package-three".to_string(), "package-four".to_string());
            let (loader_proxy, mut loader_request_stream) =
                create_proxy_and_stream::<fv1sys::LoaderMarker>()
                    .expect("failed to create mock loader proxy");
            let _loader_task = fasync::Task::spawn(async move {
                for _ in 0..4 {
                    respond_to_loader_requests(&mut loader_request_stream).await;
                }
                drop(loader_request_stream);
            });

            let pkg_name = "package-five".to_string();

            let (proxy, stream) = create_proxy_and_stream::<fv1sys::LoaderMarker>().unwrap();

            let _serve_task = fasync::Task::spawn(serve_hermetic_loader(
                stream,
                pkg_name.into(),
                AllowedPackages::List(list.into()),
                loader_proxy,
            ));

            assert_eq!(
                proxy.load_url("fuchsia-pkg://fuchsia.com/package-one#meta/comp.cm").await.unwrap(),
                None
            );

            assert_matches!(
                proxy
                    .load_url("fuchsia-pkg://fuchsia.com/package-five#meta/comp.cm")
                    .await
                    .unwrap(),
                Some(_)
            );
            assert_matches!(
                proxy
                    .load_url("fuchsia-pkg://fuchsia.com/package-three#meta/comp.cm")
                    .await
                    .unwrap(),
                Some(_)
            );
            assert_matches!(
                proxy
                    .load_url("fuchsia-pkg://fuchsia.com/package-four#meta/comp.cm")
                    .await
                    .unwrap(),
                Some(_)
            );
        }

        #[fuchsia::test]
        async fn test_invalid_url_loader() {
            let (loader_proxy, _) = create_proxy_and_stream::<fv1sys::LoaderMarker>()
                .expect("failed to create mock loader proxy");

            let pkg_name = "package-two".to_string();

            let (proxy, stream) = create_proxy_and_stream::<fv1sys::LoaderMarker>().unwrap();

            let _serve_task = fasync::Task::spawn(serve_hermetic_loader(
                stream,
                pkg_name.into(),
                AllowedPackages::zero_allowed_pkgs(),
                loader_proxy,
            ));

            assert_eq!(proxy.load_url("invalid_url").await.unwrap(), None);
        }
    }
}
