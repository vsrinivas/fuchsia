// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Private functionality for pbms lib.

use {
    crate::{gcs::fetch_from_gcs, repo_info::RepoInfo},
    anyhow::{bail, Context, Result},
    ffx_config::sdk::SdkVersion,
    fms::{find_product_bundle, Entries},
    sdk_metadata::{Metadata, MetadataValue, ProductBundleV1},
    std::{
        collections::HashMap,
        io::Write,
        path::{Path, PathBuf},
    },
    url::Url,
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
    log::info!("Getting product metadata.");
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
        let storage_path: PathBuf =
            ffx_config::get(CONFIG_STORAGE_PATH).await.context("getting CONFIG_STORAGE_PATH")?;
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
        .context("fetching metadata")?;

    let storage_path: PathBuf =
        ffx_config::get(CONFIG_STORAGE_PATH).await.context("getting CONFIG_STORAGE_PATH")?;
    let local_repo_dir = storage_path.join(pb_dir_name(&url));
    let file_path = local_repo_dir.join("product_bundles.json");
    if !file_path.is_file() {
        bail!("Failed to download metadata.");
    }
    let mut entries = Entries::new();
    entries.add_from_path(&file_path).context("adding entries from gcs")?;
    let product_bundle = find_product_bundle(&entries, &Some(product_name.to_string()))
        .context("finding product bundle")?;

    let start = std::time::Instant::now();
    log::debug!("Getting product data for {:?}", product_bundle.name);
    let local_dir = local_repo_dir.join(&product_bundle.name).join("images");
    async_fs::create_dir_all(&local_dir).await.context("creating directory")?;
    for image in &product_bundle.images {
        if verbose {
            writeln!(writer, "    image: {:?}", image)?;
        } else {
            write!(writer, ".")?;
        }
        let base_url = url::Url::parse(&image.base_uri)
            .with_context(|| format!("parsing image.base_uri {:?}", image.base_uri))?;
        fetch_by_format(&image.format, &base_url, &local_dir, verbose, writer)
            .await
            .with_context(|| format!("fetching images for {}.", product_bundle.name))?;
    }
    log::debug!("Total fetch images runtime {} seconds.", start.elapsed().as_secs_f32());

    let start = std::time::Instant::now();
    writeln!(writer, "Getting package data for {:?}", product_bundle.name)?;
    let local_dir = local_repo_dir.join(&product_bundle.name).join("packages");
    async_fs::create_dir_all(&local_dir).await.context("creating directory")?;

    // TODO(fxbug.dev/89775): Replace this with the library from fxbug.dev/89775.
    fetch_package_tgz(verbose, writer, &local_dir, &product_bundle).await?;

    /*
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
    */
    log::debug!("Total fetch packages runtime {} seconds.", start.elapsed().as_secs_f32());

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

/// Returns the build_info_version from the metadata if present.
async fn get_version_for_packages(
    metadata_collection: &Option<Vec<(String, MetadataValue)>>,
) -> Result<Option<String>> {
    let mut version: Option<String> = None;
    if let Some(metadata) = metadata_collection {
        if let Some((_, version_value)) =
            metadata.iter().find(|(key, _)| key == "build_info_version")
        {
            match version_value {
                MetadataValue::StringValue(value) => version = Some(value.to_string()),
                _ => {}
            }
        }
    }
    if version.is_none() {
        // If there is no metadata, or it did not contain build_info_version, use the
        // SDK version.
        let sdk = ffx_config::get_sdk().await.context("getting ffx config sdk")?;
        version = match sdk.get_version() {
            SdkVersion::Version(version) => Some(version.to_string()),
            SdkVersion::InTree => None,
            SdkVersion::Unknown => bail!("Unable to determine SDK version vs. in-tree"),
        };
    }
    Ok(version)
}

/// Download and extract the packages tarball.
async fn fetch_package_tgz<W>(
    verbose: bool,
    writer: &mut W,
    local_dir: &Path,
    product_bundle: &ProductBundleV1,
) -> Result<()>
where
    W: Write + Sync,
{
    let version = get_version_for_packages(&product_bundle.metadata)
        .await
        .context("determining version for package fetching")?;

    // TODO(fxbug.dev/98529): Handle the non-standard name for terminal.*.
    let file_name = match product_bundle.name.as_str() {
        "terminal.qemu-arm64" => String::from("qemu-arm64.tar.gz"),
        "terminal.qemu-x64" => String::from("qemu-x64.tar.gz"),
        "terminal.x64" => String::from("generic-x64.tar.gz"),
        "terminal.arm64" => String::from("generic-arm64.tar.gz"),
        _ => format!("{}-release.tar.gz", product_bundle.name),
    };
    let package_url = match version {
        Some(value) => format!("gs://fuchsia/development/{}/packages/{}", value, file_name),
        None => format!("gs://fuchsia/development/packages/{}", file_name),
    };
    let url = Url::parse(&package_url)?;

    // Download the package tgz into a temp directory.
    let temp_dir = tempfile::TempDir::new_in(&local_dir).context("creating temp dir")?;
    match fetch_by_format(&"tgz", &url, &temp_dir.path(), verbose, writer).await {
        Ok(()) => {}
        Err(err) => {
            // Since we're hardcoding the bucket to download artifacts from, it's possible the
            // actual SDK bucket might not match the hardcoded one. If this happens, emit a warning,
            // rather than error out.
            writeln!(
                writer,
                "WARNING: Unable to fetch {}: {:?}\n\
                 Maybe package artifacts are stored in a different bucket?",
                url, err
            )?;
            return Ok(());
        }
    }

    // Open the archive. This is typically compressed with parallel gzip streams, so we need to use
    // the multiple-stream-aware gzip decoder.
    let archive_path = temp_dir.path().join(&file_name);
    let file = std::fs::File::open(&archive_path)
        .with_context(|| format!("opening archive {:?}", archive_path))?;
    let file = flate2::read::MultiGzDecoder::new(file);
    let mut archive = tar::Archive::new(file);

    // Extract all the "amber-files/" entries into the temp directory.
    let archive_dir = temp_dir.path().join("archive");
    for entry in archive
        .entries()
        .with_context(|| format!("reading package archive entries from {}", file_name))?
    {
        let mut entry =
            entry.with_context(|| format!("reading package entry from {}", file_name))?;

        let path =
            entry.path().with_context(|| format!("reading entry path from {}", file_name))?;

        if path.starts_with("amber-files") {
            let path = path.to_path_buf();

            if verbose {
                writeln!(writer, "    Extract {}:{}", file_name, path.display())?;
            } else {
                write!(writer, ".")?;
                writer.flush()?;
            }

            entry
                .unpack_in(&archive_dir)
                .with_context(|| format!("unpacking {}", path.display()))?;
        }
    }

    // Now that we've fully extracted the archive, we need to move the files into the destination.
    // This is easy if the destination does not exist.
    let amber_files_dir = archive_dir.join("amber-files");
    if !local_dir.exists() {
        std::fs::rename(&amber_files_dir, &local_dir)?;
        return Ok(());
    }

    // FIXME(http://fxbug.dev/81098): It's a little more complicated if the directory already
    // exists. We could rename the old directory out of the way, then move the new directory in
    // place, but that would confuse the package server. The package server uses a system file
    // watcher to watch for changes to metadata, but unfortunately it follows the directory inode
    // if it's moved to another location. So it would not serve any updated artifacts to a device.
    //
    // To avoid this, we'll instead move each file over to the new location.
    let src_entries = get_directory_entries(&amber_files_dir)?;
    let mut dst_entries = get_directory_entries(&local_dir)?;

    for (rel_path, src_path) in src_entries {
        let dst_path = if let Some(dst_path) = dst_entries.remove(&rel_path) {
            dst_path
        } else {
            let dst_path = local_dir.join(rel_path);
            if let Some(parent) = dst_path.parent() {
                async_fs::create_dir_all(&parent)
                    .await
                    .with_context(|| format!("creating directory {}", parent.display()))?;
            }
            dst_path
        };

        if verbose {
            writeln!(writer, "    Move {} to {}", src_path.display(), dst_path.display())?;
        } else {
            write!(writer, ".")?;
            writer.flush()?;
        }

        std::fs::rename(&src_path, &dst_path)
            .with_context(|| format!("moving {} to {}", src_path.display(), dst_path.display()))?;
    }

    // Remove any old files left in the destination.
    for dst_path in dst_entries.values() {
        // The temp directory is in the `local_dir`, so ignore those paths.
        if dst_path.starts_with(&temp_dir.path()) {
            continue;
        }

        if verbose {
            writeln!(writer, "    Remove {}", dst_path.display())?;
        } else {
            write!(writer, ".")?;
            writer.flush()?;
        }
        std::fs::remove_file(&dst_path)
            .with_context(|| format!("removing {}", dst_path.display()))?;
    }

    Ok(())
}

fn get_directory_entries(root: &Path) -> Result<HashMap<PathBuf, PathBuf>> {
    let mut entries = HashMap::new();
    for entry in walkdir::WalkDir::new(&root) {
        let entry = entry.context("walking directory")?;
        let path = entry.path();

        if path.is_file() {
            let rel_path = path
                .strip_prefix(&root)
                .with_context(|| format!("stripping {} from {}", root.display(), path.display()))?;

            entries.insert(rel_path.to_path_buf(), path.to_path_buf());
        }
    }
    Ok(entries)
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
            .context("Downloading from GCS.")?;
    } else if product_url.scheme() == "http" || product_url.scheme() == "https" {
        fetch_from_web(product_url, local_dir).await.context("fetching from http(s)")?;
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
