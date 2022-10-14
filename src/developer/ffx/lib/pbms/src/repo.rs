// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for downloading fuchsia package repository metadata and blobs from a product bundle
//! manifest.

use {
    crate::{
        gcs::{get_boto_path, get_gcs_client_with_auth, get_gcs_client_without_auth},
        pbms::{fetch_bundle_uri, make_remote_url, GS_SCHEME},
    },
    ::gcs::client::{DirectoryProgress, FileProgress, ProgressResponse, ProgressResult, Throttle},
    anyhow::{anyhow, bail, Context, Result},
    camino::Utf8Path,
    chrono::{DateTime, TimeZone, Utc},
    fuchsia_hyper::new_https_client,
    fuchsia_repo::{
        repo_builder::RepoBuilder,
        repo_client::RepoClient,
        repo_keys::RepoKeys,
        repository::{FileSystemRepository, GcsRepository, HttpRepository, RepoProvider},
    },
    futures::{stream::FuturesUnordered, TryStreamExt as _},
    sdk_metadata::PackageBundle,
    serde_json::Value,
    std::path::Path,
    url::Url,
};

// How many package blobs to download at the same time.
const PACKAGE_FETCHER_CONCURRENCY: usize = 5;

/// Fetch the product bundle package repository mirror list.
///
/// This will try to download a package repository from each mirror in the list, stopping on the
/// first success. Otherwise it will return the last error encountered.
pub(crate) async fn fetch_package_repository_from_mirrors<F, I>(
    product_url: &url::Url,
    local_dir: &Path,
    packages: &[PackageBundle],
    progress: &F,
    ui: &I,
) -> Result<()>
where
    F: Fn(DirectoryProgress<'_>, FileProgress<'_>) -> ProgressResult,
    I: structured_ui::Interface + Sync,
{
    // The packages list is a set of mirrors. Try downloading the packages from each one. Only error
    // out if we can't download the packages from any mirror.
    for (i, package) in packages.iter().enumerate() {
        let res = fetch_package_repository(product_url, local_dir, package, progress, ui).await;

        match res {
            Ok(()) => {
                break;
            }
            Err(err) => {
                tracing::warn!("Unable to fetch {:?}: {:?}", package, err);
                // Return the error rather than warn if this was the last packages entry in our
                // list.
                if i + 1 == packages.len() {
                    return Err(err);
                }
            }
        }
    }

    Ok(())
}

/// Fetch packages from this package bundle and write them to `local_dir`.
async fn fetch_package_repository<F, I>(
    product_url: &url::Url,
    local_dir: &Path,
    package: &PackageBundle,
    progress: &F,
    ui: &I,
) -> Result<()>
where
    F: Fn(DirectoryProgress<'_>, FileProgress<'_>) -> ProgressResult,
    I: structured_ui::Interface + Sync,
{
    tracing::debug!(
        "product_url {:?}, local_dir {:?}, package {:?}",
        product_url,
        local_dir,
        package
    );
    let mut repo_metadata_uri =
        make_remote_url(product_url, &package.repo_uri).context("package.repo_uri")?;
    tracing::debug!(
        "package repository: repo_metadata_uri {:?}, repo_uri {}, blob_uri {:?}",
        repo_metadata_uri,
        package.repo_uri,
        package.blob_uri
    );

    match package.format.as_str() {
        "files" => {
            // `Url::join()` treats urls with a trailing slash as a directory, and without as a
            // file. In the latter case, it will strip off the last segment before joining paths.
            // Since the metadata and blob url are directories, make sure they have a trailing
            // slash.
            if !repo_metadata_uri.path().ends_with('/') {
                repo_metadata_uri.set_path(&format!("{}/", repo_metadata_uri.path()));
            }

            let repo_keys_uri = repo_metadata_uri.join("keys/").context("joining keys dir")?;
            repo_metadata_uri =
                repo_metadata_uri.join("repository/").context("joining repository dir")?;

            let repo_blobs_uri = if let Some(blob_repo_uri) = &package.blob_uri {
                make_remote_url(product_url, &blob_repo_uri).context("package.repo_uri")?
            } else {
                // If the blob uri is unspecified, then use `$METADATA_URI/blobs/`.
                repo_metadata_uri.join("blobs/").context("joining blobs dir")?
            };

            fetch_package_repository_from_files(
                local_dir,
                repo_keys_uri,
                repo_metadata_uri,
                repo_blobs_uri,
                progress,
                ui,
            )
            .await
            .context("fetch_package_repository_from_files")
        }
        "tgz" => {
            if package.blob_uri.is_some() {
                // TODO(fxbug.dev/93850): implement pbms.
                unimplemented!();
            }

            fetch_package_repository_from_tgz(local_dir, repo_metadata_uri, progress, ui).await
        }
        _ =>
        // The schema currently defines only "files" or "tgz" (see RFC-100).
        // This error could be a typo in the product bundle or a new image
        // format has been added and this code needs an update.
        {
            bail!(
                "Unexpected image format ({:?}) in product bundle. \
            Supported formats are \"files\" and \"tgz\". \
            Please report as a bug.",
                package.format,
            )
        }
    }
}

/// Fetch a package repository using the `files` package bundle format and writes it to
/// `local_dir`.
///
/// This supports the following URL schemes:
/// * `http://`
/// * `https://`
/// * `gs://`
async fn fetch_package_repository_from_files<F, I>(
    local_dir: &Path,
    repo_keys_uri: Url,
    repo_metadata_uri: Url,
    repo_blobs_uri: Url,
    progress: &F,
    ui: &I,
) -> Result<()>
where
    F: Fn(DirectoryProgress<'_>, FileProgress<'_>) -> ProgressResult,
    I: structured_ui::Interface + Sync,
{
    match (repo_metadata_uri.scheme(), repo_blobs_uri.scheme()) {
        (GS_SCHEME, GS_SCHEME) => {
            // FIXME(fxbug.dev/103331): we are reproducing the gcs library's authentication flow,
            // where we will prompt for an oauth token if we get a permission denied error. This
            // was done because the pbms library is written to be used a frontend that can prompt
            // for an oauth token, but the `pkg` library is written to be used on the server side,
            // which cannot do the prompt. We should eventually restructure things such that we can
            // deduplicate this logic.

            // First try to fetch with the public client.
            let client = get_gcs_client_without_auth();
            let backend = Box::new(GcsRepository::new(
                client,
                repo_metadata_uri.clone(),
                repo_blobs_uri.clone(),
            )?) as Box<dyn RepoProvider>;

            let metadata_current_time = Utc::now();
            if let Ok(()) = fetch_package_repository_from_backend(
                local_dir,
                repo_keys_uri.clone(),
                repo_metadata_uri.clone(),
                repo_blobs_uri.clone(),
                backend,
                metadata_current_time,
                progress,
                ui,
            )
            .await
            {
                return Ok(());
            }

            let boto_path = get_boto_path(ui).await?;
            let client =
                get_gcs_client_with_auth(&boto_path).context("get_gcs_client_with_auth")?;

            let backend = Box::new(GcsRepository::new(
                client,
                repo_metadata_uri.clone(),
                repo_blobs_uri.clone(),
            )?) as Box<dyn RepoProvider>;

            fetch_package_repository_from_backend(
                local_dir,
                repo_keys_uri.clone(),
                repo_metadata_uri.clone(),
                repo_blobs_uri.clone(),
                backend,
                metadata_current_time,
                progress,
                ui,
            )
            .await
            .context("fetch_package_repository_from_backend")
        }
        ("http" | "https", "http" | "https") => {
            let client = new_https_client();
            let backend = Box::new(HttpRepository::new(
                client,
                repo_metadata_uri.clone(),
                repo_blobs_uri.clone(),
            )) as Box<dyn RepoProvider>;

            fetch_package_repository_from_backend(
                local_dir,
                repo_keys_uri,
                repo_metadata_uri,
                repo_blobs_uri,
                backend,
                Utc::now(),
                progress,
                ui,
            )
            .await
            .context("fetch_package_repository_from_backend")
        }
        ("file", "file") => {
            // The files are already local, so we don't need to download them.
            Ok(())
        }
        (_, _) => {
            bail!("Unexpected URI scheme in ({}, {})", repo_metadata_uri, repo_blobs_uri);
        }
    }
}

async fn fetch_package_repository_from_backend<'a, F, I>(
    local_dir: &'a Path,
    repo_keys_uri: Url,
    repo_metadata_uri: Url,
    repo_blobs_uri: Url,
    backend: Box<dyn RepoProvider>,
    metadata_current_time: DateTime<Utc>,
    progress: &'a F,
    ui: &'a I,
) -> Result<()>
where
    F: Fn(DirectoryProgress<'_>, FileProgress<'_>) -> ProgressResult,
    I: structured_ui::Interface + Sync,
{
    tracing::debug!(
        "creating package repository, repo_metadata_uri {}, repo_blobs_uri {}",
        repo_metadata_uri,
        repo_blobs_uri
    );

    let mut repo_client = RepoClient::from_trusted_remote(backend).await.with_context(|| {
        format!(
            "creating package repository, repo_metadata_uri {}, repo_blobs_uri {}",
            repo_metadata_uri, repo_blobs_uri
        )
    })?;

    // TUF metadata may be expired, so pretend we're updating relative to the Unix Epoch so the
    // metadata won't expired.
    let start_time = Utc.timestamp(0, 0);

    repo_client
        .update_with_start_time(&start_time)
        .await
        .with_context(|| format!("downloading TUF metadata from {}", repo_metadata_uri))?;

    let local_dir =
        Utf8Path::from_path(local_dir).ok_or_else(|| anyhow!("local dir must be UTF-8 safe"))?;
    let keys_dir = local_dir.join("keys");
    let metadata_dir = local_dir.join("repository");
    let blobs_dir = metadata_dir.join("blobs");

    // Download the repository private keys.
    fetch_bundle_uri(&repo_keys_uri.join("targets.json")?, keys_dir.as_std_path(), progress, ui)
        .await
        .context("fetch_bundle_uri targets.json")?;

    fetch_bundle_uri(&repo_keys_uri.join("snapshot.json")?, keys_dir.as_std_path(), progress, ui)
        .await
        .context("fetch_bundle_uri snapshot.json")?;

    fetch_bundle_uri(&repo_keys_uri.join("timestamp.json")?, keys_dir.as_std_path(), progress, ui)
        .await
        .context("fetch_bundle_uri timestamp.json")?;

    let trusted_targets = fuchsia_repo::resolve::resolve_repository_metadata_with_start_time(
        &repo_client,
        &metadata_dir,
        &start_time,
    )
    .await
    .with_context(|| format!("downloading repository {} {}", repo_metadata_uri, repo_blobs_uri))?;

    let mut count = 0;
    // Exit early if there are no targets.
    if let Some(trusted_targets) = trusted_targets {
        // Download all the packages.
        let fetcher = fuchsia_repo::resolve::PackageFetcher::new(
            &repo_client,
            blobs_dir.as_std_path(),
            PACKAGE_FETCHER_CONCURRENCY,
        )
        .await?;

        let mut futures = FuturesUnordered::new();

        let mut throttle = Throttle::from_duration(std::time::Duration::from_millis(500));
        for (package_name, desc) in trusted_targets.targets().iter() {
            let merkle = desc.custom().get("merkle").context("missing merkle")?;
            let merkle = if let Value::String(hash) = merkle {
                hash.parse()?
            } else {
                bail!("Merkle field is not a String. {:#?}", desc)
            };

            count += 1;
            if throttle.is_ready() {
                match progress(
                    DirectoryProgress { url: repo_blobs_uri.as_ref(), at: 0, of: 1 },
                    FileProgress { url: "Packages", at: 0, of: count },
                )
                .context("rendering progress")?
                {
                    ProgressResponse::Cancel => break,
                    _ => (),
                }
            }
            tracing::debug!("package: {}", package_name.as_str());

            futures.push(fetcher.fetch_package(merkle));
        }

        while let Some(()) = futures.try_next().await? {}
        progress(
            DirectoryProgress { url: repo_blobs_uri.as_ref(), at: 1, of: 1 },
            FileProgress { url: "Packages", at: count, of: count },
        )
        .context("rendering progress")?;
    };

    // Refresh the local metadata to make sure it hasn't expired.
    let repo_keys = RepoKeys::from_dir(local_dir.join("keys").as_std_path())?;
    let repo = FileSystemRepository::new(metadata_dir, blobs_dir);
    let mut repo_client =
        RepoClient::from_trusted_remote(repo).await.context("fetching TUF metadata")?;

    // RepoClient doesn't automatically fetch the latest metadata. Fetch it now to make sure we
    // generate the correct metadata version.
    repo_client.update_with_start_time(&start_time).await?;

    // Refresh the metadata and generate all metadata expirations relative to the
    // `metadata_current_time`.
    RepoBuilder::from_client(&repo_client, &repo_keys)
        .current_time(metadata_current_time)
        .time_versioning(true)
        .refresh_metadata(true)
        .commit()
        .await
        .context("committing metadata")?;

    Ok(())
}

/// Fetch a package repository using the `tgz` package bundle format, and automatically expand the
/// tarball into the `local_dir` directory.
async fn fetch_package_repository_from_tgz<F, I>(
    local_dir: &Path,
    repo_uri: Url,
    progress: &F,
    ui: &I,
) -> Result<()>
where
    F: Fn(DirectoryProgress<'_>, FileProgress<'_>) -> ProgressResult,
    I: structured_ui::Interface + Sync,
{
    fetch_bundle_uri(&repo_uri, &local_dir, progress, ui)
        .await
        .with_context(|| format!("downloading repo URI {}", repo_uri))
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        async_net::{Ipv4Addr, TcpListener},
        camino::{Utf8Path, Utf8PathBuf},
        fuchsia_async::Task,
        fuchsia_hyper::TcpStream,
        fuchsia_repo::{
            repository::{HttpRepository, PmRepository},
            test_utils,
        },
        futures::{FutureExt as _, Stream as _},
        hyper::{service::service_fn, Body, Request, Response, StatusCode},
        std::{
            convert::Infallible,
            pin::Pin,
            sync::Arc,
            task::{Context, Poll},
        },
        tuf::metadata::Metadata as _,
    };

    struct HyperListener(TcpListener);

    impl hyper::server::accept::Accept for HyperListener {
        type Conn = TcpStream;
        type Error = std::io::Error;

        fn poll_accept(
            self: Pin<&mut Self>,
            cx: &mut Context<'_>,
        ) -> Poll<Option<Result<Self::Conn, Self::Error>>> {
            match Pin::new(&mut self.0.incoming()).poll_next(cx) {
                Poll::Pending => Poll::Pending,
                Poll::Ready(res) => Poll::Ready(
                    res.map(|res| res.map(|stream| fuchsia_hyper::TcpStream { stream })),
                ),
            }
        }
    }

    async fn file_server(listener: TcpListener, dir: Arc<Utf8PathBuf>) {
        let mut incoming = listener.incoming();
        while let Some(stream) = incoming.try_next().await.unwrap() {
            let dir = Arc::clone(&dir);

            let conn = hyper::server::conn::Http::new()
                .with_executor(fuchsia_hyper::Executor)
                .serve_connection(
                    fuchsia_hyper::TcpStream { stream },
                    service_fn(move |req| {
                        serve_file(Arc::clone(&dir), req).map(Ok::<_, Infallible>)
                    }),
                );

            Task::local(async move {
                conn.await.unwrap();
            })
            .detach();
        }
    }

    async fn serve_file(dir: Arc<Utf8PathBuf>, req: Request<Body>) -> Response<Body> {
        let path = dir.join(&req.uri().path()[1..]);

        let response = match async_fs::read(path).await {
            Ok(bytes) => Response::new(Body::from(bytes)),
            Err(err) => {
                if err.kind() == std::io::ErrorKind::NotFound {
                    Response::builder().status(StatusCode::NOT_FOUND).body(Body::empty()).unwrap()
                } else {
                    panic!("unexpected error {}", err)
                }
            }
        };

        response
    }

    async fn create_expired_repo(repo_dir: &Utf8Path) {
        // Create a normal repository.
        let repo = test_utils::make_pm_repository(&repo_dir).await;
        let repo_keys = repo.repo_keys().unwrap();

        // Then commit on top of it expired metadata.
        let mut repo_client = RepoClient::from_trusted_remote(&repo).await.unwrap();
        repo_client.update().await.unwrap();

        RepoBuilder::from_client(&repo_client, &repo_keys)
            .current_time(Utc.timestamp(0, 0))
            .refresh_non_root_metadata(true)
            .commit()
            .await
            .unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_fetch_package_repository_from_backend() {
        let tmp = tempfile::tempdir().unwrap();
        let root = Utf8Path::from_path(tmp.path()).unwrap();
        let remote_repo_dir = root.join("remote");
        std::fs::create_dir(&remote_repo_dir).unwrap();

        let local_repo_dir = root.join("local");
        let local_blob_dir = local_repo_dir.join("repository").join("blobs");

        // Create a web server that serves an remote repository with expired metadata.
        create_expired_repo(&remote_repo_dir).await;
        let listener = TcpListener::bind((Ipv4Addr::LOCALHOST, 0)).await.unwrap();
        let local_addr = listener.local_addr().unwrap();
        let _server_task = Task::local(file_server(listener, Arc::new(remote_repo_dir)));

        // Create a client to connect to the repository.
        let keys_url = Url::parse(&format!("http://{}/keys/", local_addr)).unwrap();
        let metadata_url = Url::parse(&format!("http://{}/repository/", local_addr)).unwrap();
        let blobs_url = Url::parse(&format!("http://{}/repository/blobs/", local_addr)).unwrap();

        let remote_repo = HttpRepository::new(
            fuchsia_hyper::new_client(),
            metadata_url.clone(),
            blobs_url.clone(),
        );

        // Use a fixed metadata current time so we can validate we correctly generated the timestamp
        // version.
        let metadata_timestamp_version = 100;
        let metadata_current_time = Utc.timestamp(metadata_timestamp_version as i64, 0);

        // Download the repository metadata.
        let ui = structured_ui::MockUi::new();
        fetch_package_repository_from_backend(
            local_repo_dir.as_std_path(),
            keys_url,
            metadata_url,
            blobs_url,
            Box::new(remote_repo),
            metadata_current_time,
            &|_, _| Ok(ProgressResponse::Continue),
            &ui,
        )
        .await
        .unwrap();

        // Make sure we can resolve the metadata.
        let local_repo = PmRepository::new(local_repo_dir);
        let mut repo_client = RepoClient::from_trusted_remote(&local_repo).await.unwrap();
        repo_client.update_with_start_time(&metadata_current_time).await.unwrap();

        // The metadata should have been refreshed.
        assert_eq!(repo_client.database().trusted_root().version(), 1);
        assert_eq!(
            repo_client.database().trusted_targets().unwrap().version(),
            metadata_timestamp_version
        );
        assert_eq!(
            repo_client.database().trusted_snapshot().unwrap().version(),
            metadata_timestamp_version
        );
        assert_eq!(
            repo_client.database().trusted_timestamp().unwrap().version(),
            metadata_timestamp_version
        );

        // Make sure we downloaded all the blobs.
        for blob in [
            "050907f009ff634f9aa57bff541fb9e9c2c62b587c23578e77637cda3bd69458",
            "2881455493b5870aaea36537d70a2adc635f516ac2092598f4b6056dabc6b25d",
            "ecc11f7f4b763c5a21be2b4159c9818bbe22ca7e6d8100a72f6a41d3d7b827a9",
            "548981eb310ddc4098fb5c63692e19ac4ae287b13d0e911fbd9f7819ac22491c",
            "8a8a5f07f935a4e8e1fd1a1eda39da09bb2438ec0adfb149679ddd6e7e1fbb4f",
            "72e1e7a504f32edf4f23e7e8a3542c1d77d12541142261cfe272decfa75f542d",
        ] {
            assert!(local_blob_dir.join(blob).exists(), "blob {} does not exist", blob);
        }

        // Make sure we downloaded the repo keys.
        let repo_keys = local_repo.repo_keys().unwrap();
        assert!(repo_keys.root_keys().is_empty());
        assert!(!repo_keys.targets_keys().is_empty());
        assert!(!repo_keys.snapshot_keys().is_empty());
        assert!(!repo_keys.timestamp_keys().is_empty());
    }
}
