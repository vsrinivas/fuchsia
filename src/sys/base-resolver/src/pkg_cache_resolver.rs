// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{self, Context},
    fidl::endpoints::{ClientEnd, Proxy},
    fidl_fuchsia_component_resolution::{
        self as fresolution, ResolverRequest, ResolverRequestStream,
    },
    fidl_fuchsia_io as fio,
    fuchsia_component::server::ServiceFs,
    fuchsia_pkg_cache_url::{
        fuchsia_pkg_cache_component_url, fuchsia_pkg_cache_manifest_path_str,
        fuchsia_pkg_cache_package_url, pkg_cache_package_path,
    },
    futures::{
        future::TryFutureExt as _,
        stream::{StreamExt as _, TryStreamExt as _},
    },
    tracing::error,
};

pub(crate) async fn main() -> anyhow::Result<()> {
    tracing::info!("started");

    let mut service_fs = ServiceFs::new_local();
    service_fs.dir("svc").add_fidl_service(Services::PkgCacheResolver);
    service_fs.take_and_serve_directory_handle().context("failed to serve outgoing namespace")?;
    let () = service_fs
        .for_each_concurrent(None, |request| async {
            match request {
                Services::PkgCacheResolver(stream) => {
                    serve(stream)
                        .unwrap_or_else(|e| {
                            error!("failed to serve pkg cache resolver request: {:#}", e)
                        })
                        .await
                }
            }
        })
        .await;

    Ok(())
}

enum Services {
    PkgCacheResolver(ResolverRequestStream),
}

async fn serve(stream: ResolverRequestStream) -> anyhow::Result<()> {
    let blobfs =
        blobfs::Client::open_from_namespace_executable().context("failed to open /blob")?;
    let pkg_cache_hash = get_pkg_cache_hash(
        &fuchsia_component::client::connect_to_protocol::<fidl_fuchsia_boot::ArgumentsMarker>()
            .context("failed to connect to fuchsia.boot/Arguments")?,
        blobfs.clone(),
    )
    .await
    .context("failed to get pkg-cache hash")?;
    serve_impl(stream, &blobfs, pkg_cache_hash).await
}

async fn get_pkg_cache_hash(
    boot_args: &fidl_fuchsia_boot::ArgumentsProxy,
    blobfs: blobfs::Client,
) -> Result<fuchsia_hash::Hash, anyhow::Error> {
    system_image::SystemImage::new(blobfs, boot_args)
        .await
        .context("failed to load system_image package")?
        .static_packages()
        .await
        .context("failed to read static packages")?
        .hash_for_package(pkg_cache_package_path())
        .ok_or_else(|| anyhow::anyhow!("failed to find pkg-cache hash in static packages manifest"))
}

async fn serve_impl(
    mut stream: ResolverRequestStream,
    blobfs: &blobfs::Client,
    pkg_cache: fuchsia_hash::Hash,
) -> anyhow::Result<()> {
    while let Some(request) =
        stream.try_next().await.context("failed to read request from FIDL stream")?
    {
        match request {
            ResolverRequest::Resolve { component_url, responder } => {
                if &component_url != fuchsia_pkg_cache_component_url().as_str() {
                    error!(?component_url, "failed to resolve invalid pkg-cache URL");
                    responder
                        .send(&mut Err(fresolution::ResolverError::InvalidArgs))
                        .context("failed sending invalid URL error")?;
                    continue;
                }

                let () = match resolve_pkg_cache(blobfs, pkg_cache).await {
                    Ok(result) => responder.send(&mut Ok(result)),
                    Err(e) => {
                        let fidl_err = (&e).into();
                        error!("failed to resolve pkg-cache: {:#}", anyhow::anyhow!(e));
                        responder.send(&mut Err(fidl_err))
                    }
                }
                .context("failed sending response")?;
            }

            ResolverRequest::ResolveWithContext { component_url, context, responder } => {
                error!(?component_url, ?context, "pkg_cache_resolver does not support ResolveWithContext, and could not resolve URL");
                responder
                    .send(&mut Err(fresolution::ResolverError::InvalidArgs))
                    .context("failed sending response")?;
            }
        }
    }
    Ok(())
}

async fn resolve_pkg_cache(
    blobfs: &blobfs::Client,
    pkg_cache: fuchsia_hash::Hash,
) -> Result<fresolution::Component, crate::ResolverError> {
    let (proxy, server) =
        fidl::endpoints::create_proxy().map_err(crate::ResolverError::CreateEndpoints)?;
    let () = package_directory::serve(
        package_directory::ExecutionScope::new(),
        blobfs.clone(),
        pkg_cache,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
        server,
    )
    .await
    .map_err(crate::ResolverError::ServePackageDirectory)?;
    let data = mem_util::open_file_data(&proxy, fuchsia_pkg_cache_manifest_path_str())
        .await
        .map_err(crate::ResolverError::ComponentNotFound)?;
    let client = ClientEnd::new(
        proxy.into_channel().expect("could not convert proxy to channel").into_zx_channel(),
    );
    Ok(fresolution::Component {
        url: Some(fuchsia_pkg_cache_component_url().clone().into_string()),
        decl: Some(data),
        package: Some(fresolution::Package {
            url: Some(fuchsia_pkg_cache_package_url().clone().into_string()),
            directory: Some(client),
            ..fresolution::Package::EMPTY
        }),
        ..fresolution::Component::EMPTY
    })
}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches, fidl_fuchsia_mem as fmem};

    #[fuchsia::test]
    async fn serve_rejects_invalid_url() {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fresolution::ResolverMarker>().unwrap();
        let (blobfs, _) = blobfs::Client::new_mock();
        let server = serve_impl(stream, &blobfs, [0; 32].into());
        let client = async move { proxy.resolve("invalid-url").await };
        let (server, client) = futures::join!(server, client);
        let () = server.unwrap();
        assert_matches!(client, Ok(Err(fresolution::ResolverError::InvalidArgs)));
    }

    #[fuchsia::test]
    async fn serve_sends_error_if_resolve_fails() {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fresolution::ResolverMarker>().unwrap();
        let (blobfs, _) = blobfs::Client::new_mock();
        let server = serve_impl(stream, &blobfs, [0; 32].into());
        let client = async move { proxy.resolve("fuchsia-pkg-cache:///#meta/pkg-cache.cm").await };
        let (server, client) = futures::join!(server, client);
        let () = server.unwrap();
        assert_matches!(client, Ok(Err(fresolution::ResolverError::Io)));
    }

    #[fuchsia::test]
    async fn serve_success() {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fresolution::ResolverMarker>().unwrap();
        let (blobfs, fake) = blobfs::Client::new_temp_dir_fake();
        let package = fuchsia_pkg_testing::PackageBuilder::new("fake-pkg-cache")
            .add_resource_at("meta/pkg-cache.cm", [0u8, 1, 2].as_slice())
            .build()
            .await
            .unwrap();
        package.write_to_blobfs_dir(&fake.backing_dir_as_openat_dir());

        let server = serve_impl(stream, &blobfs, *package.meta_far_merkle_root());
        let client = async move { proxy.resolve("fuchsia-pkg-cache:///#meta/pkg-cache.cm").await };
        let (server, client) = futures::join!(server, client);

        let () = server.unwrap();
        let component = client.unwrap().unwrap();
        assert_eq!(component.url.unwrap(), "fuchsia-pkg-cache:///#meta/pkg-cache.cm");
        assert_matches!(component.decl.unwrap(), fmem::Data::Buffer(_));
        assert_eq!(
            component.package.as_ref().unwrap().url.as_ref().unwrap(),
            "fuchsia-pkg-cache:///"
        );
        let resolved_package = component.package.unwrap().directory.unwrap().into_proxy().unwrap();
        package.verify_contents(&resolved_package).await.unwrap();
    }

    #[fuchsia::test]
    async fn resolve_package_directory_serve_fails() {
        let (blobfs, _) = blobfs::Client::new_mock();
        assert_matches!(
            resolve_pkg_cache(&blobfs, [0; 32].into()).await,
            Err(crate::ResolverError::ServePackageDirectory(_))
        );
    }

    #[fuchsia::test]
    async fn resolve_missing_manifest_data() {
        let (blobfs, fake) = blobfs::Client::new_temp_dir_fake();
        let package =
            fuchsia_pkg_testing::PackageBuilder::new("missing-manifest").build().await.unwrap();
        package.write_to_blobfs_dir(&fake.backing_dir_as_openat_dir());

        assert_matches!(
            resolve_pkg_cache(&blobfs, *package.meta_far_merkle_root()).await,
            Err(crate::ResolverError::ComponentNotFound(_))
        );
    }
}
