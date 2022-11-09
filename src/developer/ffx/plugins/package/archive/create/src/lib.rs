// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use ffx_core::ffx_plugin;
pub use ffx_package_archive_create_args::CreateCommand;
use fuchsia_pkg::PackageManifest;
use std::fs::{create_dir_all, File};

#[ffx_plugin("ffx_package")]
pub async fn cmd_create(cmd: CreateCommand) -> Result<()> {
    let package_manifest = PackageManifest::try_load_from(&cmd.package_manifest)?;
    let output_dir = cmd.output.parent().expect("output path needs to have a parent").to_path_buf();
    create_dir_all(&output_dir).with_context(|| format!("creating directory {}", output_dir))?;
    let output = File::create(&cmd.output)
        .with_context(|| format!("creating package archive file {}", cmd.output))?;
    package_manifest.archive(cmd.root_dir, output).await?;
    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use camino::{Utf8Path, Utf8PathBuf};
    use fuchsia_pkg::PackageBuilder;
    use std::fs;

    fn create_package_manifest(
        package_manifest_path: Utf8PathBuf,
        meta_far_path: Utf8PathBuf,
        blob_path: Utf8PathBuf,
    ) {
        let tmp = tempfile::TempDir::new().unwrap();

        let mut builder = PackageBuilder::new("my-package-name");

        builder
            .add_file_as_blob("lib/mylib.so", blob_path.into_os_string().into_string().unwrap())
            .unwrap();

        builder
            .add_contents_to_far(
                "meta/my_component.cmx",
                "my_component.cmx contents".as_bytes(),
                tmp.path(),
            )
            .unwrap();

        let package_manifest = builder.build(&tmp.path(), &meta_far_path).unwrap();

        let package_manifest_file = fs::File::create(&package_manifest_path).unwrap();
        serde_json::to_writer_pretty(&package_manifest_file, &package_manifest).unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_archive() {
        let tempdir = tempfile::tempdir().unwrap();
        let root = Utf8Path::from_path(tempdir.path()).unwrap();
        let blobs_dir = root.join("blobs");
        fs::create_dir_all(&blobs_dir).unwrap();

        let package_manifest_path = blobs_dir.join("package_manifest.json");
        let meta_far_path = blobs_dir.join("meta.far");
        let blob = blobs_dir.join("blob");
        fs::File::create(&blob).unwrap();
        create_package_manifest(package_manifest_path.clone(), meta_far_path, blob);

        let result_dir = root.join("results");
        fs::create_dir_all(&result_dir).unwrap();
        let result_far = result_dir.join("test_package.far");

        let cmd = CreateCommand {
            package_manifest: package_manifest_path,
            output: result_far.clone(),
            root_dir: result_dir.clone(),
        };
        cmd_create(cmd).await.unwrap();
        assert!(result_far.exists());
        let archive = File::open(&result_far).unwrap();
        let mut package_archive = fuchsia_archive::Utf8Reader::new(&archive).unwrap();
        assert_eq!(package_archive.read_file("meta.far").unwrap().len(), 20480);
        assert_eq!(
            package_archive
                .read_file("15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b")
                .unwrap()
                .len(),
            0
        );
    }
}
