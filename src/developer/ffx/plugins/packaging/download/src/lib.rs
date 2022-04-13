// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result, ffx_core::ffx_plugin, ffx_packaging_download_args::DownloadCommand,
    fuchsia_hyper::new_https_client, pkg::repository::package_download,
};

#[ffx_plugin("ffx_package")]
pub async fn cmd_download(cmd: DownloadCommand) -> Result<()> {
    let client = new_https_client();
    package_download(client, cmd.tuf_url, cmd.blob_url, cmd.target_path, cmd.output_path).await?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        camino::Utf8Path,
        fuchsia_async as fasync,
        fuchsia_pkg::PackageManifest,
        pkg::{
            manager::RepositoryManager, server::RepositoryServer, test_utils::make_pm_repository,
        },
        pretty_assertions::assert_eq,
        std::{collections::BTreeSet, fs::File, net::Ipv4Addr, sync::Arc},
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_download() {
        let tmp = tempfile::TempDir::new().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        let repo_path = dir.join("repo");
        let output_path = dir.join("pkg");

        // Create a server.
        let repo = make_pm_repository("tuf", &repo_path).await;
        let manager = RepositoryManager::new();
        manager.add(Arc::new(repo));

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

        // Check that the manifest was correctly written.
        let f = File::open(output_path.join("package_manifest.json")).unwrap();
        let _manifest: PackageManifest = serde_json::from_reader(f).unwrap();

        server.stop();
        task.await;
    }
}
