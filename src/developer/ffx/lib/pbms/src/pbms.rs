// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Private functionality for pbms lib.

use {
    crate::{
        gcs::{
            exists_in_gcs, fetch_from_gcs, get_boto_path, get_gcs_client_with_auth,
            get_gcs_client_without_auth,
        },
        repo_info::RepoInfo,
        structured_ui,
    },
    ::gcs::client::{
        DirectoryProgress, FileProgress, ProgressResponse, ProgressResult, ProgressState, Throttle,
    },
    anyhow::{anyhow, bail, Context, Result},
    camino::Utf8Path,
    chrono::{DateTime, NaiveDateTime, Utc},
    ffx_config::sdk::SdkVersion,
    fms::{find_product_bundle, Entries},
    fuchsia_hyper::new_https_client,
    fuchsia_repo::{
        repo_client::RepoClient,
        repo_keys::RepoKeys,
        repository::{FileSystemRepository, GcsRepository, HttpRepository, RepoProvider},
    },
    futures::{stream::FuturesUnordered, TryStreamExt as _},
    sdk_metadata::{Metadata, PackageBundle},
    serde_json::Value,
    std::path::{Path, PathBuf},
    url::Url,
};

pub(crate) const CONFIG_METADATA: &str = "pbms.metadata";
pub(crate) const CONFIG_STORAGE_PATH: &str = "pbms.storage.path";
pub(crate) const GS_SCHEME: &str = "gs";

/// Load FMS Entries.
///
/// Expandable tags (e.g. "{foo}") in `repos` must already be expanded, do not
/// pass in repo URIs with expandable tags.
pub(crate) async fn fetch_product_metadata<F>(
    repo: &url::Url,
    output_dir: &Path,
    progress: &mut F,
) -> Result<()>
where
    F: FnMut(ProgressState<'_>, ProgressState<'_>) -> ProgressResult,
{
    tracing::info!("Getting product metadata for {:?}", repo);
    async_fs::create_dir_all(&output_dir).await.context("create directory")?;

    let mut info = RepoInfo::default();
    info.metadata_url = repo.to_string();
    info.save(&output_dir.join("info"))?;
    tracing::debug!("Wrote info to {:?}", output_dir);

    fetch_bundle_uri(&repo, output_dir, progress).await.context("fetch product bundle by URL")?;
    Ok(())
}

/// Replace the {foo} placeholders in repo paths.
///
/// {version} is replaced with the Fuchsia SDK version string.
/// {sdk.root} is replaced with the SDK directory path.
fn expand_placeholders(uri: &str, version: &str, sdk_root: &str) -> Result<url::Url> {
    let expanded = uri.replace("{version}", version).replace("{sdk.root}", sdk_root);
    if uri.contains(":") {
        Ok(url::Url::parse(&expanded).with_context(|| format!("url parse {:?}", expanded))?)
    } else {
        // If there's no colon, assume it's a local path.
        let base_url = url::Url::parse("file:/").context("parsing minimal file URL")?;
        Ok(url::Url::options()
            .base_url(Some(&base_url))
            .parse(&expanded)
            .with_context(|| format!("url parse {:?}", expanded))?)
    }
}

/// Get a list of the urls in the CONFIG_METADATA config with the placeholders
/// expanded.
///
/// I.e. run expand_placeholders() on each element in CONFIG_METADATA.
pub(crate) async fn pbm_repo_list() -> Result<Vec<url::Url>> {
    let sdk = ffx_config::get_sdk().await.context("PBMS ffx config get sdk")?;
    let version = match sdk.get_version() {
        SdkVersion::Version(version) => version,
        SdkVersion::InTree => "",
        SdkVersion::Unknown => bail!("Unable to determine SDK version vs. in-tree"),
    };
    let sdk_root = sdk.get_path_prefix();
    let repos: Vec<String> = ffx_config::get::<Vec<String>, _>(CONFIG_METADATA)
        .await
        .context("get config CONFIG_METADATA")?;
    let repos: Vec<url::Url> = repos
        .iter()
        .map(|s| {
            expand_placeholders(s, &version, &sdk_root.to_string_lossy())
                .expect(&format!("URL for repo {:?}", s))
        })
        .collect();
    Ok(repos)
}

/// Retrieve the path portion of a "file:/" url. Non-file-paths return None.
///
/// If the url has no scheme, the whole string is returned.
/// E.g.
/// - "/foo/bar" -> Some("/foo/bar")
/// - "file://foo/bar" -> Some("/foo/bar")
/// - "http://foo/bar" -> None
pub(crate) fn path_from_file_url(product_url: &url::Url) -> Option<PathBuf> {
    if product_url.scheme() == "file" {
        product_url.to_file_path().ok()
    } else {
        None
    }
}

/// Get a list of product bundle entry names from `path`.
///
/// These are not full product_urls, but just the name that is used in the
/// fragment portion of the URL.
pub(crate) fn pb_names_from_path(path: &Path) -> Result<Vec<String>> {
    let mut entries = Entries::new();
    entries.add_from_path(path).context("adding from path")?;
    Ok(entries
        .iter()
        .filter_map(|entry| match entry {
            Metadata::ProductBundleV1(_) => Some(entry.name().to_string()),
            _ => None,
        })
        .collect::<Vec<String>>())
}

/// Helper function for determining local path.
///
/// if `dir` return a directory path, else may return a glob (file) path.
pub(crate) async fn local_path_helper(
    product_url: &url::Url,
    add_dir: &str,
    dir: bool,
) -> Result<PathBuf> {
    assert!(!product_url.fragment().is_none());
    if let Some(path) = &path_from_file_url(product_url) {
        if dir {
            // TODO(fxbug.dev/98009): Unify the file layout between local and remote
            // product bundles to avoid this hack.
            let sdk = ffx_config::get_sdk().await.context("getting ffx config sdk")?;
            let sdk_root = sdk.get_path_prefix();
            if path.starts_with(&sdk_root) {
                Ok(sdk_root.to_path_buf())
            } else {
                Ok(path.parent().expect("parent of file path").to_path_buf())
            }
        } else {
            Ok(path.to_path_buf())
        }
    } else {
        let url = url_sans_fragment(&product_url)?;
        Ok(get_product_dir(&url).await?.join(add_dir))
    }
}

/// Retrieve the storage directory path from the config.
pub async fn get_storage_dir() -> Result<PathBuf> {
    let storage_path: PathBuf =
        ffx_config::get(CONFIG_STORAGE_PATH).await.context("getting CONFIG_STORAGE_PATH")?;
    Ok(storage_path)
}

/// Retrieve the product directory path from the config.
///
/// This is the storage path plus a hash of the `product_url` provided.
pub async fn get_product_dir(product_url: &url::Url) -> Result<PathBuf> {
    Ok(get_storage_dir().await?.join(pb_dir_name(product_url)))
}

/// Separate the URL on the last "#" character.
///
/// If no "#" is found, use the whole input as the url.
///
/// "file://foo#bar" -> "file://foo"
/// "file://foo" -> "file://foo"
pub(crate) fn url_sans_fragment(product_url: &url::Url) -> Result<url::Url> {
    let mut product_url = product_url.to_owned();
    product_url.set_fragment(None);
    Ok(product_url)
}

/// Helper for `get_product_data()`, see docs there.
pub(crate) async fn get_product_data_from_gcs<I>(
    product_url: &url::Url,
    local_repo_dir: &std::path::Path,
    ui: &mut I,
) -> Result<()>
where
    I: structured_ui::Interface + Sync,
{
    tracing::debug!("get_product_data_from_gcs {:?} to {:?}", product_url, local_repo_dir);
    assert_eq!(product_url.scheme(), GS_SCHEME);
    let product_name = product_url.fragment().expect("URL with trailing product_name fragment.");
    let url = url_sans_fragment(product_url)?;

    fetch_product_metadata(&url, local_repo_dir, &mut |_d, _f| Ok(ProgressResponse::Continue))
        .await
        .context("fetching metadata")?;

    let file_path = local_repo_dir.join("product_bundles.json");
    if !file_path.is_file() {
        bail!("product_bundles.json not found {:?}.", file_path);
    }
    let mut entries = Entries::new();
    entries.add_from_path(&file_path).context("adding entries from gcs")?;
    let product_bundle = find_product_bundle(&entries, &Some(product_name.to_string()))
        .context("finding product bundle")?;
    fetch_data_for_product_bundle_v1(&product_bundle, &url, local_repo_dir, ui).await
}

/// Helper for `get_product_data()`, see docs there.
pub async fn fetch_data_for_product_bundle_v1<I>(
    product_bundle: &sdk_metadata::ProductBundleV1,
    product_url: &url::Url,
    local_repo_dir: &std::path::Path,
    ui: &mut I,
) -> Result<()>
where
    I: structured_ui::Interface + Sync,
{
    // Handy debugging switch to disable images download.
    if true {
        let start = std::time::Instant::now();
        tracing::info!(
            "Getting product data for {:?} to {:?}",
            product_bundle.name,
            local_repo_dir
        );
        let local_dir = local_repo_dir.join(&product_bundle.name).join("images");
        async_fs::create_dir_all(&local_dir).await.context("creating directory")?;

        for image in &product_bundle.images {
            tracing::debug!("image {:?}", image);

            let base_url =
                make_remote_url(product_url, &image.base_uri).context("image.base_uri")?;
            if !exists_in_gcs(&base_url.as_str()).await? {
                tracing::warn!("The base_uri does not exist: {}", base_url);
            }
            fetch_by_format(&image.format, &base_url, &local_dir, &mut |d, f| {
                let mut progress = structured_ui::Progress::builder();
                progress.title(&product_bundle.name);
                progress.entry("Image data", /*at=*/ 1, /*of=*/ 2, "steps");
                progress.entry(&d.url, d.at, d.of, "files");
                progress.entry(&f.url, f.at, f.of, "bytes");
                ui.present(&structured_ui::Presentation::Progress(progress))?;
                Ok(ProgressResponse::Continue)
            })
            .await
            .with_context(|| format!("fetching images for {}.", product_bundle.name))?;
        }
        tracing::debug!("Total fetch images runtime {} seconds.", start.elapsed().as_secs_f32());
    }

    // Handy debugging switch to disable packages download.
    if true {
        let start = std::time::Instant::now();
        let local_dir = local_repo_dir.join(&product_bundle.name).join("packages");
        async_fs::create_dir_all(&local_dir).await.context("creating directory")?;
        tracing::info!(
            "Getting package data for {:?}, local_dir {:?}",
            product_bundle.name,
            local_dir
        );

        fetch_package_repository_from_mirrors(
            product_url,
            &local_dir,
            &product_bundle.packages,
            &mut |d, f| {
                let mut progress = structured_ui::Progress::builder();
                progress.title(&product_bundle.name);
                progress.entry("Package data", /*at=*/ 2, /*at=*/ 2, "steps");
                progress.entry(&d.url, d.at, d.of, "files");
                progress.entry(&f.url, f.at, f.of, "bytes");
                ui.present(&structured_ui::Presentation::Progress(progress))?;
                Ok(ProgressResponse::Continue)
            },
        )
        .await
        .with_context(|| {
            format!(
                "fetch_package_repository_from_mirrors {:?}, local_dir {:?}",
                product_url, local_dir
            )
        })?;

        tracing::debug!("Total fetch packages runtime {} seconds.", start.elapsed().as_secs_f32());
    }

    tracing::info!("Download of product data for {:?} is complete.", product_bundle.name);
    tracing::info!("Data written to \"{}\".", local_repo_dir.display());
    Ok(())
}

/// Generate a (likely) unique name for the URL.
///
/// URLs don't always make good file paths.
pub(crate) fn pb_dir_name(gcs_url: &url::Url) -> String {
    let mut gcs_url = gcs_url.to_owned();
    gcs_url.set_fragment(None);

    use std::collections::hash_map::DefaultHasher;
    use std::hash::Hash;
    use std::hash::Hasher;
    let mut s = DefaultHasher::new();
    gcs_url.as_str().hash(&mut s);
    let out = s.finish();
    tracing::debug!("pb_dir_name {:?}, hash {:?}", gcs_url, out);
    format!("{}", out)
}

/// Fetch the product bundle package repository mirror list.
///
/// This will try to download a package repository from each mirror in the list, stopping on the
/// first success. Otherwise it will return the last error encountered.
async fn fetch_package_repository_from_mirrors<F>(
    product_url: &url::Url,
    local_dir: &Path,
    packages: &[PackageBundle],
    progress: &mut F,
) -> Result<()>
where
    F: FnMut(DirectoryProgress<'_>, FileProgress<'_>) -> ProgressResult,
{
    // The packages list is a set of mirrors. Try downloading the packages from each one. Only error
    // out if we can't download the packages from any mirror.
    for (i, package) in packages.iter().enumerate() {
        let res = fetch_package_repository(product_url, local_dir, package, progress).await;

        match res {
            Ok(()) => {
                break;
            }
            Err(err) => {
                tracing::warn!("Unable to fetch {:?}: {:?}", package, err);
                if i + 1 == packages.len() {
                    return Err(err);
                }
            }
        }
    }

    Ok(())
}

/// Fetch packages from this package bundle and write them to `local_dir`.
async fn fetch_package_repository<F>(
    product_url: &url::Url,
    local_dir: &Path,
    package: &PackageBundle,
    progress: &mut F,
) -> Result<()>
where
    F: FnMut(DirectoryProgress<'_>, FileProgress<'_>) -> ProgressResult,
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
            )
            .await
            .context("fetch_package_repository_from_files")
        }
        "tgz" => {
            if package.blob_uri.is_some() {
                // TODO(fxbug.dev/93850): implement pbms.
                unimplemented!();
            }

            fetch_package_repository_from_tgz(local_dir, repo_metadata_uri, progress).await
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
async fn fetch_package_repository_from_files<F>(
    local_dir: &Path,
    repo_keys_uri: Url,
    repo_metadata_uri: Url,
    repo_blobs_uri: Url,
    progress: &mut F,
) -> Result<()>
where
    F: FnMut(DirectoryProgress<'_>, FileProgress<'_>) -> ProgressResult,
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

            if let Ok(()) = fetch_package_repository_from_backend(
                local_dir,
                repo_keys_uri.clone(),
                repo_metadata_uri.clone(),
                repo_blobs_uri.clone(),
                backend,
                progress,
            )
            .await
            {
                return Ok(());
            }

            let boto_path = get_boto_path().await?;
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
                progress,
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
                progress,
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

async fn fetch_package_repository_from_backend<'a, F>(
    local_dir: &'a Path,
    repo_keys_uri: Url,
    repo_metadata_uri: Url,
    repo_blobs_uri: Url,
    backend: Box<dyn RepoProvider>,
    progress: &'a mut F,
) -> Result<()>
where
    F: FnMut(DirectoryProgress<'_>, FileProgress<'_>) -> ProgressResult,
{
    tracing::debug!(
        "creating package repository, repo_metadata_uri {}, repo_blobs_uri {}",
        repo_metadata_uri,
        repo_blobs_uri
    );
    let repo = RepoClient::from_trusted_remote(backend).await.with_context(|| {
        format!(
            "creating package repository, repo_metadata_uri {}, repo_blobs_uri {}",
            repo_metadata_uri, repo_blobs_uri
        )
    })?;

    let local_dir =
        Utf8Path::from_path(local_dir).ok_or_else(|| anyhow!("local dir must be UTF-8 safe"))?;
    let keys_dir = local_dir.join("keys");
    let metadata_dir = local_dir.join("repository");
    let blobs_dir = metadata_dir.join("blobs");

    // Download the repository private keys.
    fetch_bundle_uri(&repo_keys_uri.join("timestamp.json")?, keys_dir.as_std_path(), progress)
        .await
        .context("fetch_bundle_uri timestamp.json")?;

    fetch_bundle_uri(&repo_keys_uri.join("snapshot.json")?, keys_dir.as_std_path(), progress)
        .await
        .context("fetch_bundle_uri snapshot.json")?;

    fetch_bundle_uri(&repo_keys_uri.join("targets.json")?, keys_dir.as_std_path(), progress)
        .await
        .context("fetch_bundle_uri targets.json")?;

    // TUF metadata may be expired, so pretend we're updating relative to the Unix Epoch so the
    // metadata won't expired.
    let start_time = DateTime::<Utc>::from_utc(NaiveDateTime::from_timestamp(0, 0), Utc);

    let trusted_targets = fuchsia_repo::resolve::resolve_repository_metadata_with_start_time(
        &repo,
        &metadata_dir,
        &start_time,
    )
    .await
    .with_context(|| format!("downloading repository {} {}", repo_metadata_uri, repo_blobs_uri))?;

    let mut count = 0;
    // Exit early if there are no targets.
    if let Some(trusted_targets) = trusted_targets {
        // Download all the packages.
        let fetcher =
            fuchsia_repo::resolve::PackageFetcher::new(&repo, blobs_dir.as_std_path(), 5).await?;

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

    let repo_storage = FileSystemRepository::new(metadata_dir, blobs_dir);
    let keys_dir = local_dir.join("keys");
    let repo_keys = RepoKeys::from_dir(keys_dir.as_std_path())?;

    fuchsia_repo::repo_storage::refresh_repository(&repo_storage, &repo_keys).await?;

    Ok(())
}

/// Fetch a package repository using the `tgz` package bundle format, and automatically expand the
/// tarball into the `local_dir` directory.
async fn fetch_package_repository_from_tgz<F>(
    local_dir: &Path,
    repo_uri: Url,
    progress: &mut F,
) -> Result<()>
where
    F: FnMut(DirectoryProgress<'_>, FileProgress<'_>) -> ProgressResult,
{
    fetch_bundle_uri(&repo_uri, &local_dir, progress)
        .await
        .with_context(|| format!("downloading repo URI {}", repo_uri))
}

/// Download and expand data.
///
/// For a directory, all files in the directory are downloaded.
/// For a .tgz file, the file is downloaded and expanded.
async fn fetch_by_format<F>(
    format: &str,
    uri: &url::Url,
    local_dir: &Path,
    progress: &mut F,
) -> Result<()>
where
    F: FnMut(DirectoryProgress<'_>, FileProgress<'_>) -> ProgressResult,
{
    tracing::debug!("fetch_by_format");
    match format {
        "files" | "tgz" => fetch_bundle_uri(uri, &local_dir, progress).await,
        _ =>
        // The schema currently defines only "files" or "tgz" (see RFC-100).
        // This error could be a typo in the product bundle or a new image
        // format has been added and this code needs an update.
        {
            bail!(
                "Unexpected image format ({:?}) in product bundle. \
            Supported formats are \"files\" and \"tgz\". \
            Please report as a bug.",
                format,
            )
        }
    }
}

/// Download data from any of the supported schemes listed in RFC-100, Product
/// Bundle, "bundle_uri".
///
/// Currently: "pattern": "^(?:http|https|gs|file):\/\/"
async fn fetch_bundle_uri<F>(
    product_url: &url::Url,
    local_dir: &Path,
    progress: &mut F,
) -> Result<()>
where
    F: FnMut(DirectoryProgress<'_>, FileProgress<'_>) -> ProgressResult,
{
    tracing::debug!("fetch_bundle_uri");
    if product_url.scheme() == GS_SCHEME {
        fetch_from_gcs(product_url.as_str(), local_dir, progress)
            .await
            .context("Downloading from GCS.")?;
    } else if product_url.scheme() == "http" || product_url.scheme() == "https" {
        fetch_from_web(product_url, local_dir, progress).await.context("fetching from http(s)")?;
    } else if let Some(_) = &path_from_file_url(product_url) {
        // Since the file is already local, no fetch is necessary.
        tracing::debug!("Found local file path {:?}", product_url);
    } else {
        bail!("Unexpected URI scheme in ({:?})", product_url);
    }
    Ok(())
}

async fn fetch_from_web<F>(
    _product_uri: &url::Url,
    _local_dir: &Path,
    _progress: &mut F,
) -> Result<()>
where
    F: FnMut(DirectoryProgress<'_>, FileProgress<'_>) -> ProgressResult,
{
    // TODO(fxbug.dev/93850): implement pbms.
    unimplemented!();
}

/// If internal_url is a file scheme, join `product_url` and `internal_url`.
/// Otherwise, return `internal_url`.
fn make_remote_url(product_url: &url::Url, internal_url: &str) -> Result<url::Url> {
    let result = if let Some(remainder) = internal_url.strip_prefix("file:/") {
        // Note: The product_url must either be a path to the product_bundle.json file or to the
        // parent directory (with a trailing slash).
        product_url.join(remainder)?
    } else {
        url::Url::parse(&internal_url).with_context(|| format!("parsing url {:?}", internal_url))?
    };
    tracing::debug!(
        "make_remote_url product_url {:?}, internal_url {:?}, result  {:?}",
        product_url,
        internal_url,
        result
    );
    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_path_from_file_url() {
        let input = url::Url::parse("fake://foo#bar").expect("url");
        let output = path_from_file_url(&input);
        assert!(output.is_none());

        let input = url::Url::parse("file:///../../foo#bar").expect("url");
        let output = path_from_file_url(&input);
        assert_eq!(output, Some(Path::new("/foo").to_path_buf()));

        let input = url::Url::parse("file://foo#bar").expect("url");
        let output = path_from_file_url(&input);
        assert!(output.is_none());

        let input = url::Url::parse("file:///foo#bar").expect("url");
        let output = path_from_file_url(&input);
        assert_eq!(output, Some(Path::new("/foo").to_path_buf()));

        let temp_dir = tempfile::TempDir::new().expect("temp dir");
        let base_url = url::Url::from_directory_path(temp_dir.path().join("a/b/c/d")).expect("url");
        let input =
            url::Url::options().base_url(Some(&base_url)).parse("../../foo#bar").expect("url");
        let output = path_from_file_url(&input);
        assert_eq!(output, Some(temp_dir.path().join("a/b/foo").to_path_buf()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_url_sans_fragment() {
        let input = url::Url::parse("fake://foo#bar").expect("url");
        let output = url_sans_fragment(&input).expect("sans fragment");
        assert_eq!(output, url::Url::parse("fake://foo").expect("check url"));

        let input = url::Url::parse("fake://foo").expect("url");
        let output = url_sans_fragment(&input).expect("sans fragment");
        assert_eq!(output, url::Url::parse("fake://foo").expect("check url"));
    }

    // Disabling this test until a test config can be modified without altering
    // the local user's config.
    #[ignore]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_local_path_helper() {
        let url = url::Url::parse("fake://foo#bar").expect("url");
        let path = local_path_helper(&url, "foo", /*dir=*/ true).await.expect("dir helper");
        assert!(path.to_string_lossy().ends_with("ffx/pbms/951333825719265977/foo"));

        // Note that the hash will be the same even though the fragment is
        // different.
        let url = url::Url::parse("fake://foo#blah").expect("url");
        let path = local_path_helper(&url, "foo", /*dir=*/ true).await.expect("dir helper");
        assert!(path.to_string_lossy().ends_with("ffx/pbms/951333825719265977/foo"));

        let url = url::Url::parse("gs://foo/blah/*.json#bar").expect("url");
        let path = local_path_helper(&url, "foo", /*dir=*/ true).await.expect("dir helper");
        assert!(path.to_string_lossy().ends_with("ffx/pbms/16042545670964745983/foo"));

        let url = url::Url::parse("file:///foo/blah/*.json#bar").expect("url");
        let path = local_path_helper(&url, "foo", /*dir=*/ true).await.expect("dir helper");
        assert_eq!(path.to_string_lossy(), "/foo/blah");

        let url = url::Url::parse("file:///foo/blah/*.json#bar").expect("url");
        let path = local_path_helper(&url, "foo", /*dir=*/ false).await.expect("dir helper");
        assert_eq!(path.to_string_lossy(), "/foo/blah/*.json");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    #[should_panic(expected = "Unexpected image format")]
    async fn test_fetch_by_format() {
        let url = url::Url::parse("fake://foo").expect("url");
        fetch_by_format("bad", &url, &Path::new("unused"), &mut |_d, _f| {
            Ok(ProgressResponse::Continue)
        })
        .await
        .expect("bad fetch");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    #[should_panic(expected = "Unexpected URI scheme")]
    async fn test_fetch_bundle_uri() {
        let url = url::Url::parse("fake://foo").expect("url");
        fetch_bundle_uri(&url, &Path::new("unused"), &mut |_d, _f| Ok(ProgressResponse::Continue))
            .await
            .expect("bad fetch");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_pb_dir_name() {
        let url = url::Url::parse("fake://foo").expect("url");
        let hash = pb_dir_name(&url);
        assert!(url.as_str() != hash);
        assert!(!hash.contains("/"));
        assert!(!hash.contains(" "));
    }

    #[test]
    fn test_make_remote_url() {
        let base = url::Url::parse("gs://foo/bar/cat/blah.txt").expect("url");
        let base_directory = url::Url::parse("gs://foo/bar/cat/").expect("url directory");
        assert_eq!(
            make_remote_url(&base, &"gs://a/b").expect("make_remote_url").as_str(),
            "gs://a/b"
        );
        assert_eq!(
            make_remote_url(&base, &"file:/../../c").expect("make_remote_url").as_str(),
            "gs://foo/c"
        );
        assert_eq!(
            make_remote_url(&base_directory, &"file:/../../c").expect("make_remote_url").as_str(),
            "gs://foo/c"
        );
        assert_eq!(
            make_remote_url(&base, &"file:/../c").expect("make_remote_url").as_str(),
            "gs://foo/bar/c"
        );
        assert_eq!(
            make_remote_url(&base_directory, &"file:/../c").expect("make_remote_url").as_str(),
            "gs://foo/bar/c"
        );
        assert_eq!(
            make_remote_url(&base, &"file:/../c/").expect("make_remote_url").as_str(),
            "gs://foo/bar/c/"
        );
        assert_eq!(
            make_remote_url(&base_directory, &"file:/../c/").expect("make_remote_url").as_str(),
            "gs://foo/bar/c/"
        );
        assert_eq!(
            make_remote_url(&base, &"file:/c/").expect("make_remote_url").as_str(),
            "gs://foo/bar/cat/c/"
        );
        assert_eq!(
            make_remote_url(&base_directory, &"file:/c/").expect("make_remote_url").as_str(),
            "gs://foo/bar/cat/c/"
        );
        assert_eq!(
            make_remote_url(&base, &"file:/..").expect("make_remote_url").as_str(),
            "gs://foo/bar/"
        );
        assert_eq!(
            make_remote_url(&base_directory, &"file:/..").expect("make_remote_url").as_str(),
            "gs://foo/bar/"
        );
    }
}
