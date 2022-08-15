// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A tool to:
//! - acquire and display product bundle information (metadata)
//! - acquire related data files, such as disk partition images (data)

use {
    ::gcs::client::ProgressResponse,
    anyhow::{bail, Context, Result},
    errors::ffx_bail,
    ffx_core::ffx_plugin,
    ffx_product_args::{GetCommand, ProductCommand, SubCommand, VerifyCommand},
    fidl_fuchsia_developer_ffx::RepositoryRegistryProxy,
    fidl_fuchsia_developer_ffx_ext::{RepositoryError, RepositorySpec},
    fuchsia_url::RepositoryUrl,
    pbms::{fetch_data_for_product_bundle_v1, update_metadata_from},
    sdk_metadata::{self, ElementType, Envelope, ProductBundleV1},
    std::{
        convert::TryInto,
        fs::{self, File},
        io::{stdout, Write},
        path::Path,
    },
};

/// Provide functionality to show product-bundle metadata, fetch metadata, and
/// pull images and related data.
#[ffx_plugin("product.experimental", RepositoryRegistryProxy = "daemon::protocol")]
pub async fn product(cmd: ProductCommand, repos: RepositoryRegistryProxy) -> Result<()> {
    product_plugin_impl(cmd, &mut stdout(), repos).await
}

/// Dispatch to a sub-command.
pub async fn product_plugin_impl<W>(
    command: ProductCommand,
    writer: &mut W,
    repos: RepositoryRegistryProxy,
) -> Result<()>
where
    W: Write + Sync,
{
    match &command.sub {
        SubCommand::Get(cmd) => pb_get(writer, &cmd, repos).await?,
        SubCommand::Verify(cmd) => pb_verify(&cmd)?,
    }
    Ok(())
}

/// `ffx product get` sub-command.
async fn pb_get<W: Write + Sync>(
    writer: &mut W,
    cmd: &GetCommand,
    repos: RepositoryRegistryProxy,
) -> Result<()> {
    let start = std::time::Instant::now();
    tracing::info!("---------------------- Begin ----------------------------");
    tracing::debug!("product_url Url::parse");
    let product_url = match url::Url::parse(&cmd.product_bundle_url) {
        Ok(p) => p,
        _ => ffx_bail!(
            "The source location must be a URL, failed to parse {:?}",
            cmd.product_bundle_url
        ),
    };
    let local_dir = &cmd.out_dir;
    make_way_for_output(&local_dir, cmd.force).await.context("make_way_for_output")?;
    let pbm_path = local_dir.join("product_bundle.json");
    tracing::debug!("first read_product_bundle_metadata {:?}", pbm_path);
    let product_bundle_metadata =
        match read_product_bundle_metadata(&pbm_path).await.context("reading product metadata") {
            Ok(name) => name,
            _ => {
                // Try updating the metadata and then reading again.
                tracing::debug!("update_metadata_from {:?} {:?}", product_url, local_dir);
                update_metadata_from(&product_url, local_dir, &mut |_d, _f| {
                    write!(writer, ".")?;
                    writer.flush()?;
                    Ok(ProgressResponse::Continue)
                })
                .await
                .context("updating metadata")?;
                read_product_bundle_metadata(&pbm_path)
                    .await
                    .with_context(|| format!("loading product metadata from {:?}", pbm_path))?
            }
        };
    tracing::debug!("fetch_data_for_product_bundle_v1, product_url {:?}", product_url);
    fetch_data_for_product_bundle_v1(
        &product_bundle_metadata,
        &product_url,
        local_dir,
        &mut |_d, _f| {
            write!(writer, ".")?;
            writer.flush()?;
            Ok(ProgressResponse::Continue)
        },
    )
    .await
    .context("getting product data")?;

    let product_name = product_bundle_metadata.name;

    let repo_name = if let Some(repo_name) = &cmd.repository {
        repo_name.clone()
    } else if !product_name.is_empty() {
        // FIXME(103661): Repository names must be a valid domain name, and cannot contain
        // '_'. We might be able to expand our support for [opaque hosts], which supports
        // arbitrary ASCII codepoints. Until then, replace any '_' with '-'.
        //
        // [opaque hosts]: https://url.spec.whatwg.org/#opaque-host
        let repo_name = product_name.replace('_', "-");

        if repo_name != product_name {
            tracing::info!(
                "Repository names cannot contain '_'. Replacing with '-' in {}",
                product_name
            );
        }

        repo_name
    } else {
        // Otherwise use the standard default.
        "devhost".to_string()
    };

    // Make sure the repository name is valid.
    if let Err(err) = RepositoryUrl::parse_host(repo_name.clone()) {
        ffx_bail!("invalid repository name {}: {}", repo_name, err);
    }

    // Register a repository with the daemon if we downloaded any packaging artifacts.
    let repo_path = local_dir.join(&product_name).join("packages");
    if repo_path.exists() {
        let repo_path =
            repo_path.canonicalize().with_context(|| format!("canonicalizing {:?}", repo_path))?;
        tracing::debug!("Register a repository with the daemon at {:?}", repo_path);

        let repo_spec =
            RepositorySpec::Pm { path: repo_path.try_into().context("repo_path.try_into")? };
        repos
            .add_repository(&repo_name, &mut repo_spec.into())
            .await
            .context("communicating with ffx daemon")?
            .map_err(RepositoryError::from)
            .with_context(|| format!("registering repository {}", repo_name))?;

        tracing::debug!("Created repository named '{}'", repo_name);
        writeln!(writer, "\nCreated repository named '{}'", repo_name)?;
    } else {
        tracing::debug!(
            "The repository was not registered with the daemon because path {:?} does not exist.",
            repo_path
        );
    }

    tracing::debug!(
        "Total fx product-bundle get runtime {} seconds.",
        start.elapsed().as_secs_f32()
    );
    tracing::debug!("End");
    Ok(())
}

/// Remove prior output directory, if necessary.
async fn make_way_for_output(local_dir: &Path, force: bool) -> Result<()> {
    tracing::debug!("make_way_for_output {:?}, force {}", local_dir, force);
    if local_dir.exists() {
        tracing::debug!("local_dir.exists {:?}", local_dir);
        if std::fs::read_dir(&local_dir).expect("reading dir").next().is_none() {
            tracing::debug!("The dir is empty (which is okay) {:?}", local_dir);
        } else if force {
            if !(local_dir.join("info").exists() && local_dir.join("product_bundle.json").exists())
            {
                // Let's avoid `rm -rf /` or similar.
                ffx_bail!(
                    "The directory does not resemble an old product \
                    bundle. For caution's sake, please remove the output \
                    directory {:?} by hand and try again.",
                    local_dir
                );
            }
            std::fs::remove_dir_all(&local_dir)
                .with_context(|| format!("removing output dir {:?}", local_dir))?;
            assert!(!local_dir.exists());
            tracing::debug!("Removed all of {:?}", local_dir);
        } else {
            ffx_bail!(
                "The output directory already exists. Please provide \
                another directory to write to, or use --force to overwrite the \
                contents of {:?}.",
                local_dir
            );
        }
    }
    Ok(())
}

/// Read the product bundle metadata from the PBM file at 'pbm_path'.
///
/// pbm_path must point to a single pbm. If more than one is found, an error
/// will be returned.
async fn read_product_bundle_metadata(pbm_path: &Path) -> Result<sdk_metadata::ProductBundleV1> {
    use fms::{find_product_bundle, Entries};
    tracing::debug!("read_product_bundle_metadata {:?}", pbm_path);
    let entries =
        Entries::from_path_list(&[pbm_path.to_path_buf()]).await.context("loading fms entries")?;
    let pbm =
        find_product_bundle(&entries, /*fms_name=*/ &None).context("finding pbm in entries")?;
    Ok(pbm.to_owned())
}

/// Verify that the product bundle has the correct format and is ready for use.
fn pb_verify(cmd: &VerifyCommand) -> Result<()> {
    let file = File::open(&cmd.product_bundle).context("opening product bundle")?;
    let envelope: Envelope<ProductBundleV1> =
        serde_json::from_reader(file).context("parsing product bundle")?;
    if let Some(verified_path) = &cmd.verified_file {
        fs::write(verified_path, "verified").context("writing verified file")?;
    }
    pb_verify_product_bundle(envelope.data)
}

fn pb_verify_product_bundle(product_bundle: ProductBundleV1) -> Result<()> {
    // TODO(https://fxbug.dev/82728): Add path validation.
    if product_bundle.kind != ElementType::ProductBundle {
        bail!("File type is not ProductBundle");
    }
    if product_bundle.device_refs.is_empty() {
        bail!("At least one 'device_ref' must be supplied");
    }
    if product_bundle.images.is_empty() {
        bail!("At least one entry in 'images' must be supplied");
    }
    for image in product_bundle.images {
        if image.format != "files" && image.format != "tgz" {
            bail!("Only images with format 'files' or 'tgz' are supported");
        }
        if !image.base_uri.starts_with("file:") {
            bail!("Image 'base_uri' paths must start with 'file:'");
        }
    }
    if product_bundle.packages.is_empty() {
        bail!("At least one entry in 'packages' must be supplied");
    }
    for package in product_bundle.packages {
        if package.format != "files" && package.format != "tgz" {
            bail!("Only packages with format 'files' or 'tgz' are supported");
        }
        if let Some(blob_uri) = package.blob_uri {
            if !blob_uri.starts_with("file:") {
                bail!("Package 'blob_uri' paths must start with 'file:'");
            }
        }
        if !package.repo_uri.starts_with("file:") {
            bail!("Package 'repo_uri' paths must start with 'file:'");
        }
    }
    if let Some(emu) = product_bundle.manifests.emu {
        if emu.disk_images.is_empty() {
            bail!("At least one entry in the emulator 'disk_images' must be supplied");
        }
    }
    if let Some(flash) = product_bundle.manifests.flash {
        if flash.products.is_empty() {
            bail!("At least one entry in the flash manifest 'products' must be supplied");
        }
    }
    Ok(())
}

#[cfg(test)]
mod test {
    use sdk_metadata::{
        ElementType, EmuManifest, FlashManifest, ImageBundle, Manifests, PackageBundle, Product,
        ProductBundleV1,
    };
    use {super::*, tempfile};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_make_way_for_output() {
        let test_dir = tempfile::TempDir::new().expect("temp dir");

        make_way_for_output(&test_dir.path(), /*force=*/ false).await.expect("empty dir is okay");

        std::fs::create_dir(&test_dir.path().join("foo")).expect("make_dir foo");
        std::fs::File::create(test_dir.path().join("info")).expect("create info");
        std::fs::File::create(test_dir.path().join("product_bundle.json"))
            .expect("create product_bundle.json");
        make_way_for_output(&test_dir.path(), /*force=*/ true).await.expect("rm dir is okay");

        let test_dir = tempfile::TempDir::new().expect("temp dir");
        std::fs::create_dir(&test_dir.path().join("foo")).expect("make_dir foo");
        assert!(make_way_for_output(&test_dir.path(), /*force=*/ false).await.is_err());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_read_product_bundle_metadata() {
        let test_dir = tempfile::TempDir::new().expect("temp dir");

        assert!(read_product_bundle_metadata(&test_dir.path()).await.is_err());

        let pbm_path = test_dir.path().join("product_bundle.json");
        std::fs::File::create(&pbm_path).expect("create pbm_path");
        assert!(read_product_bundle_metadata(&pbm_path).await.is_err());
    }

    #[test]
    fn verify_valid() {
        let pb = default_valid_pb();
        assert!(pb_verify_product_bundle(pb).is_ok());
    }

    #[test]
    fn verify_invalid_type() {
        let mut pb = default_valid_pb();
        pb.kind = ElementType::PhysicalDevice;
        assert!(pb_verify_product_bundle(pb).is_err());
    }

    #[test]
    fn verify_missing_device_ref() {
        let mut pb = default_valid_pb();
        pb.device_refs = vec![];
        assert!(pb_verify_product_bundle(pb).is_err());
    }

    #[test]
    fn verify_missing_images() {
        let mut pb = default_valid_pb();
        pb.images = vec![];
        assert!(pb_verify_product_bundle(pb).is_err());
    }

    #[test]
    fn verify_image_invalid_format() {
        let mut pb = default_valid_pb();
        pb.images[0].format = "invalid".into();
        assert!(pb_verify_product_bundle(pb).is_err());
    }

    #[test]
    fn verify_image_invalid_base_uri() {
        let mut pb = default_valid_pb();
        pb.images[0].base_uri = "gs://path/to/file".into();
        assert!(pb_verify_product_bundle(pb).is_err());
    }

    #[test]
    fn verify_missing_packages() {
        let mut pb = default_valid_pb();
        pb.packages = vec![];
        assert!(pb_verify_product_bundle(pb).is_err());
    }

    #[test]
    fn verify_package_invalid_format() {
        let mut pb = default_valid_pb();
        pb.packages[0].format = "invalid".into();
        assert!(pb_verify_product_bundle(pb).is_err());
    }

    #[test]
    fn verify_package_invalid_blob_uri() {
        let mut pb = default_valid_pb();
        pb.packages[0].blob_uri = Some("gs://path/to/file".into());
        assert!(pb_verify_product_bundle(pb).is_err());
    }

    #[test]
    fn verify_package_invalid_repo_uri() {
        let mut pb = default_valid_pb();
        pb.packages[0].repo_uri = "gs://path/to/file".into();
        assert!(pb_verify_product_bundle(pb).is_err());
    }

    #[test]
    fn verify_missing_emu_disk_images() {
        let mut pb = default_valid_pb();
        pb.manifests.emu = Some(EmuManifest {
            disk_images: vec![],
            initial_ramdisk: "ramdisk".into(),
            kernel: "kernel".into(),
        });
        assert!(pb_verify_product_bundle(pb).is_err());
    }

    #[test]
    fn verify_missing_flash_products() {
        let mut pb = default_valid_pb();
        pb.manifests.flash = Some(FlashManifest {
            hw_revision: "board".into(),
            products: vec![],
            credentials: vec![],
        });
        assert!(pb_verify_product_bundle(pb).is_err());
    }

    fn default_valid_pb() -> ProductBundleV1 {
        ProductBundleV1 {
            description: None,
            device_refs: vec!["device".into()],
            images: vec![ImageBundle {
                base_uri: "file://path/to/images".into(),
                format: "files".into(),
            }],
            manifests: Manifests {
                emu: Some(EmuManifest {
                    disk_images: vec!["file://path/to/images".into()],
                    initial_ramdisk: "ramdisk".into(),
                    kernel: "kernel".into(),
                }),
                flash: Some(FlashManifest {
                    hw_revision: "board".into(),
                    products: vec![Product {
                        bootloader_partitions: vec![],
                        name: "product".into(),
                        oem_files: vec![],
                        partitions: vec![],
                        requires_unlock: false,
                    }],
                    credentials: vec![],
                }),
            },
            metadata: None,
            packages: vec![PackageBundle {
                blob_uri: Some("file://path/to/blobs".into()),
                format: "files".into(),
                repo_uri: "file://path/to/repo".into(),
            }],
            name: "default_pb".into(),
            kind: ElementType::ProductBundle,
        }
    }
}
