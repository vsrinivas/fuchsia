// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_package_download_args::DownloadCommand,
    fuchsia_hyper::new_https_client,
    fuchsia_pkg::PackageManifest,
    fuchsia_repo::{repo_client::RepoClient, repository::HttpRepository, resolve::resolve_package},
    std::fs::File,
    url::Url,
};

const DOWNLOAD_CONCURRENCY: usize = 5;

#[ffx_plugin("ffx_package")]
pub async fn cmd_download(cmd: DownloadCommand) -> Result<()> {
    let client = new_https_client();

    let backend = Box::new(HttpRepository::new(
        client,
        Url::parse(&cmd.tuf_url)?,
        Url::parse(&cmd.blob_url)?,
    ));
    let mut repo = RepoClient::new(backend).await?;
    repo.update().await?;

    let blobs_dir = cmd.output_path.join("blobs");
    std::fs::create_dir_all(&blobs_dir)?;

    // Download the package blobs.
    let meta_far_hash =
        resolve_package(&repo, &cmd.target_path, &blobs_dir, DOWNLOAD_CONCURRENCY).await?;

    // Construct a manifest from the blobs.
    let manifest = PackageManifest::from_blobs_dir(&blobs_dir, meta_far_hash)?;

    // FIXME(http://fxbug.dev/97061): When this function was written, we downloaded the meta.far
    // blob to a toplevel file `meta.far`, rather than writing it into the `blobs/` directory. Lets
    // preserve this behavior for now until we can change downstream users from relying on this
    // functionality.
    std::fs::copy(
        cmd.output_path.join("blobs").join(meta_far_hash.to_string()),
        cmd.output_path.join("meta.far"),
    )?;

    // Write the manifest.
    let manifest_path = cmd.output_path.join("package_manifest.json");
    let mut file = File::create(&manifest_path)?;
    serde_json::to_writer(&mut file, &manifest)?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        camino::Utf8Path,
        fuchsia_async as fasync,
        fuchsia_repo::{
            manager::RepositoryManager, server::RepositoryServer, test_utils::make_pm_repository,
        },
        pretty_assertions::assert_eq,
        std::{collections::BTreeSet, net::Ipv4Addr, sync::Arc},
    };

    #[fuchsia::test]
    async fn test_download() {
        let tmp = tempfile::TempDir::new().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        let repo_path = dir.join("repo");
        let output_path = dir.join("pkg");

        // Create a server.
        let backend = Box::new(make_pm_repository(&repo_path).await);
        let repo = RepoClient::new(backend).await.unwrap();
        let manager = RepositoryManager::new();
        manager.add("tuf", repo);

        let addr = (Ipv4Addr::LOCALHOST, 0).into();
        let (server_fut, _, server) =
            RepositoryServer::builder(addr, Arc::clone(&manager)).start().await.unwrap();

        // Run the server in the background.
        let task = fasync::Task::local(server_fut);

        // Perform the download.
        cmd_download(DownloadCommand {
            tuf_url: server.local_url() + "/tuf",
            blob_url: server.local_url() + "/tuf/blobs",
            target_path: "package1".into(),
            output_path: output_path.as_std_path().to_path_buf(),
        })
        .await
        .unwrap();

        // Check that all the files got downloaded correctly.
        let actual_paths = walkdir::WalkDir::new(&output_path)
            .into_iter()
            .map(|e| Utf8Path::from_path(e.unwrap().path()).unwrap().to_path_buf())
            .collect::<BTreeSet<_>>();

        assert_eq!(
            actual_paths,
            BTreeSet::from([
                output_path.clone(),
                output_path.join("blobs"),
                output_path
                    .join("blobs")
                    .join("2881455493b5870aaea36537d70a2adc635f516ac2092598f4b6056dabc6b25d"),
                output_path
                    .join("blobs")
                    .join("72e1e7a504f32edf4f23e7e8a3542c1d77d12541142261cfe272decfa75f542d"),
                output_path
                    .join("blobs")
                    .join("8a8a5f07f935a4e8e1fd1a1eda39da09bb2438ec0adfb149679ddd6e7e1fbb4f"),
                output_path.join("meta.far"),
                output_path.join("package_manifest.json"),
            ])
        );

        // Check that the produced manifest is parseable.
        let _manifest =
            PackageManifest::try_load_from(output_path.join("package_manifest.json")).unwrap();

        server.stop();
        task.await;
    }
}
