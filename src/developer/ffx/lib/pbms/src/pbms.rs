// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Private functionality for pbms lib.

use {
    crate::{gcs::fetch_from_gcs, repo_info::RepoInfo},
    anyhow::{bail, Context, Result},
    fms::{find_product_bundle, Entries},
    sdk_metadata::Metadata,
    std::{
        io::Write,
        path::{Path, PathBuf},
    },
};

pub(crate) const CONFIG_METADATA: &str = "pbms.metadata";
pub(crate) const CONFIG_STORAGE_PATH: &str = "pbms.storage.path";
pub(crate) const GS_SCHEME: &str = "gs";

/// Load FMS Entries for a given SDK `version`.
///
/// Expandable tags (e.g. "{foo}") in `repos` must already be expanded, do not
/// pass in repo URIs with expandable tags.
pub(crate) async fn fetch_product_metadata<W>(
    repos: &Vec<url::Url>,
    verbose: bool,
    writer: &mut W,
) -> Result<()>
where
    W: Write + Sync,
{
    writeln!(writer, "Getting product metadata.")?;
    let storage_path: PathBuf =
        ffx_config::get(CONFIG_STORAGE_PATH).await.context("get CONFIG_STORAGE_PATH")?;
    async_fs::create_dir_all(&storage_path).await.context("create directory")?;
    for repo_url in repos {
        if repo_url.scheme() != GS_SCHEME {
            // There's no need to fetch local files or unrecognized schemes.
            continue;
        }
        let hash = pb_dir_name(&repo_url);
        let local_path = storage_path.join(hash).join("product_bundles.json");
        if let Some(local_dir) = local_path.parent() {
            async_fs::create_dir_all(&local_dir).await.context("create directory")?;

            let mut info = RepoInfo::default();
            info.metadata_url = repo_url.to_string();
            info.save(&local_dir.join("info"))?;

            let temp_dir = tempfile::tempdir_in(&local_dir).context("create temp dir")?;
            fetch_bundle_uri(&repo_url, &temp_dir.path(), verbose, writer)
                .await
                .context("fetch product bundle by URL")?;
            let the_file = temp_dir.path().join("product_bundles.json");
            if the_file.is_file() {
                async_fs::rename(&the_file, &local_path).await.context("move temp file")?;
            }
        }
    }
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
    use ffx_config::sdk::SdkVersion;
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
    entries.add_from_path(path)?;
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
            let sdk = ffx_config::get_sdk().await.context("PBMS ffx config get sdk")?;
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
        let storage_path: PathBuf =
            ffx_config::get(CONFIG_STORAGE_PATH).await.context("get CONFIG_STORAGE_PATH")?;
        Ok(storage_path.join(pb_dir_name(&url)).join(add_dir))
    }
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
pub(crate) async fn get_product_data_from_gcs<W>(
    product_url: &url::Url,
    verbose: bool,
    writer: &mut W,
) -> Result<()>
where
    W: Write + Sync,
{
    assert_eq!(product_url.scheme(), GS_SCHEME);
    let product_name = product_url.fragment().expect("URL with trailing product_name fragment.");
    let url = url_sans_fragment(product_url)?;

    fetch_product_metadata(&vec![url.to_owned()], verbose, writer)
        .await
        .context("fetch metadata")?;

    let storage_path: PathBuf =
        ffx_config::get(CONFIG_STORAGE_PATH).await.context("get CONFIG_STORAGE_PATH")?;
    let local_repo_dir = storage_path.join(pb_dir_name(&url));
    let file_path = local_repo_dir.join("product_bundles.json");
    if !file_path.is_file() {
        bail!("Failed to download metadata.");
    }
    let mut entries = Entries::new();
    entries.add_from_path(&file_path).context("add entries from gcs")?;
    let product_bundle = find_product_bundle(&entries, &Some(product_name.to_string()))
        .context("find product bundle")?;

    writeln!(writer, "Getting product data for {:?}", product_bundle.name)?;
    let local_dir = local_repo_dir.join(&product_bundle.name).join("images");
    async_fs::create_dir_all(&local_dir).await.context("create directory")?;
    for image in &product_bundle.images {
        if verbose {
            writeln!(writer, "    image: {:?}", image)?;
        } else {
            write!(writer, ".")?;
        }
        let base_url = url::Url::parse(&image.base_uri)
            .with_context(|| format!("parse image.base_uri {:?}", image.base_uri))?;
        fetch_by_format(&image.format, &base_url, &local_dir, verbose, writer)
            .await
            .with_context(|| format!("Images for {}.", product_bundle.name))?;
    }
    writeln!(writer, "Getting package data for {:?}", product_bundle.name)?;
    let local_dir = local_repo_dir.join(&product_bundle.name).join("packages");
    async_fs::create_dir_all(&local_dir).await.context("create directory")?;
    for package in &product_bundle.packages {
        if verbose {
            writeln!(writer, "    package: {:?}", package.repo_uri)?;
        } else {
            write!(writer, ".")?;
            writer.flush()?;
        }
        let repo_url = url::Url::parse(&package.repo_uri)
            .with_context(|| format!("parse package.repo_uri {:?}", package.repo_uri))?;
        fetch_by_format(&package.format, &repo_url, &local_dir, verbose, writer)
            .await
            .with_context(|| format!("Packages for {}.", product_bundle.name))?;
    }
    writeln!(writer, "Download of product data for {:?} is complete.", product_bundle.name)?;
    if verbose {
        if let Some(parent) = local_dir.parent() {
            writeln!(writer, "Data written to \"{}\".", parent.display())?;
        }
    }
    Ok(())
}

/// Generate a (likely) unique name for the URL.
///
/// URLs don't always make good file paths.
fn pb_dir_name(gcs_url: &url::Url) -> String {
    use std::collections::hash_map::DefaultHasher;
    use std::hash::Hash;
    use std::hash::Hasher;
    let mut s = DefaultHasher::new();
    gcs_url.as_str().hash(&mut s);
    format!("{}", s.finish())
}

/// Download and expand data.
///
/// For a directory, all files in the directory are downloaded.
/// For a .tgz file, the file is downloaded and expanded.
async fn fetch_by_format<W>(
    format: &str,
    uri: &url::Url,
    local_dir: &Path,
    verbose: bool,
    writer: &mut W,
) -> Result<()>
where
    W: Write + Sync,
{
    match format {
        "files" | "tgz" => fetch_bundle_uri(uri, &local_dir, verbose, writer).await,
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
async fn fetch_bundle_uri<W>(
    product_url: &url::Url,
    local_dir: &Path,
    verbose: bool,
    writer: &mut W,
) -> Result<()>
where
    W: Write + Sync,
{
    if product_url.scheme() == GS_SCHEME {
        fetch_from_gcs(product_url.as_str(), local_dir, verbose, writer)
            .await
            .context("Download from GCS.")?;
    } else if product_url.scheme() == "http" || product_url.scheme() == "https" {
        fetch_from_web(product_url, local_dir).await.context("fetch from http(s)")?;
    } else if let Some(_) = &path_from_file_url(product_url) {
        // Since the file is already local, no fetch is necessary.
    } else {
        bail!("Unexpected URI scheme in ({:?})", product_url);
    }
    Ok(())
}

async fn fetch_from_web(_product_uri: &url::Url, _local_dir: &Path) -> Result<()> {
    // TODO(fxbug.dev/93850): implement pbms.
    unimplemented!();
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
        let mut writer = Box::new(std::io::stdout());
        fetch_by_format("bad", &url, &Path::new("unused"), /*verbose=*/ false, &mut writer)
            .await
            .expect("bad fetch");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    #[should_panic(expected = "Unexpected URI scheme")]
    async fn test_fetch_bundle_uri() {
        let url = url::Url::parse("fake://foo").expect("url");
        let mut writer = Box::new(std::io::stdout());
        fetch_bundle_uri(&url, &Path::new("unused"), /*verbose=*/ false, &mut writer)
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
}
