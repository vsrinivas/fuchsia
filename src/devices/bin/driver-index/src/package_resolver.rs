// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    fidl_fuchsia_io as fio, fidl_fuchsia_pkg as fpkg,
    fuchsia_hash::Hash,
    fuchsia_url::{PackageVariant, UnpinnedAbsolutePackageUrl},
    futures::{StreamExt, TryStreamExt},
    std::{path::PathBuf, str::FromStr},
};

async fn get_hash(
    package_url: &fpkg::PackageUrl,
    packages: &fio::DirectoryProxy,
) -> Result<fpkg::BlobId> {
    let package_url = UnpinnedAbsolutePackageUrl::parse(&package_url.url)?;
    let package_meta_path: PathBuf = format!(
        "{}/{}/meta",
        package_url.name(),
        package_url.variant().cloned().unwrap_or_else(|| PackageVariant::zero())
    )
    .into();
    let package_meta_file =
        fuchsia_fs::open_file(packages, &package_meta_path, fio::OpenFlags::RIGHT_READABLE)
            .with_context(|| format!("Failed to open {:?}", &package_meta_path))?;
    let merkle_root_str = fuchsia_fs::read_file(&package_meta_file)
        .await
        .context("Failed to read package meta file")?;
    let merkle_root_hash =
        Hash::from_str(&merkle_root_str).context("Failed to parse package metal file as a hash")?;
    let merkle_root_bytes = merkle_root_hash.as_bytes();
    if merkle_root_bytes.len() != 32 {
        anyhow::bail!(
            "Expected merkle root hash byte length to be 32 but was {} instead",
            merkle_root_bytes.len()
        );
    }
    let mut merkle_root = [0; 32];
    merkle_root.clone_from_slice(merkle_root_bytes);
    Ok(fpkg::BlobId { merkle_root })
}

pub async fn serve(stream: fpkg::PackageResolverRequestStream) -> Result<()> {
    stream
        .map(|result| result.context("failed request"))
        .try_for_each(|request| async {
            match request {
                fpkg::PackageResolverRequest::Resolve { package_url, dir, responder } => {
                    let package_url = UnpinnedAbsolutePackageUrl::parse(&package_url)?;
                    let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DIRECTORY;
                    fuchsia_fs::directory::open_channel_in_namespace(
                        &format!(
                            "/pkgfs/packages/{}/{}",
                            package_url.name(),
                            package_url
                                .variant()
                                .cloned()
                                .unwrap_or_else(|| PackageVariant::zero())
                        ),
                        flags,
                        dir,
                    )?;
                    responder
                        .send(&mut Ok(fpkg::ResolutionContext { bytes: vec![] }))
                        .context("error sending response")?;
                }
                fidl_fuchsia_pkg::PackageResolverRequest::ResolveWithContext {
                    package_url: _,
                    context: _,
                    dir: _,
                    responder,
                } => {
                    // Not implemented for driver-index PackageResolver
                    responder
                        .send(&mut Err(fidl_fuchsia_pkg::ResolveError::Internal))
                        .context("error sending response")?;
                }
                fpkg::PackageResolverRequest::GetHash { package_url, responder } => {
                    let packages = fuchsia_fs::directory::open_in_namespace(
                        "/pkgfs/packages/",
                        fio::OpenFlags::RIGHT_READABLE,
                    )
                    .context("Failed to open \"/pkgfs/packages/\"")?;
                    let hash = get_hash(&package_url, &packages).await?;
                    responder
                        .send(&mut Ok(hash))
                        .context("Failed to send response to GetHash request")?;
                }
            }
            Ok(())
        })
        .await?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_async as fasync,
        fuchsia_hash::Hash,
        futures::{future::FutureExt, select},
        vfs::directory::entry::DirectoryEntry,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_get_hash() {
        const PACKAGE_NAME: &str = "foo";
        const MERKLE_ROOT: [u8; 32] = [
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
            25, 26, 27, 28, 29, 30, 31, 32,
        ];

        let merkle_root: Hash = MERKLE_ROOT.into();
        let merkle_root_str = merkle_root.to_string();
        let meta_file =
            vfs::file::vmo::asynchronous::read_only_static(merkle_root_str.into_bytes());
        let packages = vfs::pseudo_directory! {
                PACKAGE_NAME => vfs::pseudo_directory! {
                    "0" => vfs::pseudo_directory! {
                        "meta" => meta_file.clone(),
                    }
                },
        };
        let fs_scope = vfs::execution_scope::ExecutionScope::new();
        let (client, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        packages.open(
            fs_scope.clone(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
            0,
            vfs::path::Path::dot(),
            fidl::endpoints::ServerEnd::new(server.into_channel()),
        );

        let package_url =
            fpkg::PackageUrl { url: format!("fuchsia-pkg://fuchsia.com/{}", PACKAGE_NAME) };
        select! {
            res = get_hash(
                &package_url,
                &client,
            ).fuse() => {
                assert_eq!(res.unwrap(), fpkg::BlobId { merkle_root: MERKLE_ROOT });
            },
            _ = fs_scope.wait().fuse() => {
                panic!("packages directory server prematurely finished");
            }
        }
    }
}
