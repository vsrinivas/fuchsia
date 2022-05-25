// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use ffx_core::ffx_plugin;
pub use ffx_package_archive_args::ArchiveCommand;
use fuchsia_pkg::PackageManifest;
use std::fs::{create_dir_all, File};

#[ffx_plugin("ffx_package")]
pub async fn cmd_archive(cmd: ArchiveCommand) -> Result<()> {
    let package_manifest = PackageManifest::try_load_from(&cmd.package_manifest)?;
    let output_dir = cmd.output.parent().expect("output path needs to have a parent").to_path_buf();
    create_dir_all(&output_dir)
        .with_context(|| format!("creating directory {}", output_dir.display()))?;
    let output = File::create(&cmd.output)
        .with_context(|| format!("creating package archive file {}", cmd.output.display()))?;
    package_manifest.archive(cmd.root_dir, output).await?;
    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_pkg::{build_with_file_system, CreationManifest, FileSystem, MetaPackage};
    use maplit::{btreemap, hashmap};
    use std::collections::HashMap;
    use std::fs;
    use std::io;
    use std::path::PathBuf;

    struct FakeFileSystem {
        content_map: HashMap<String, Vec<u8>>,
    }

    impl<'a> FileSystem<'a> for FakeFileSystem {
        type File = &'a [u8];
        fn open(&'a self, path: &str) -> Result<Self::File, io::Error> {
            Ok(self.content_map.get(path).unwrap().as_slice())
        }
        fn len(&self, path: &str) -> Result<u64, io::Error> {
            Ok(self.content_map.get(path).unwrap().len() as u64)
        }
        fn read(&self, path: &str) -> Result<Vec<u8>, io::Error> {
            Ok(self.content_map.get(path).unwrap().clone())
        }
    }

    fn create_package_manifest(
        package_manifest_path: PathBuf,
        meta_far_path: PathBuf,
        blob_path: PathBuf,
    ) {
        let blob_path_string = blob_path.into_os_string().into_string().unwrap();
        let creation_manifest = CreationManifest::from_external_and_far_contents(
            btreemap! {
                "lib/mylib.so".to_string() => blob_path_string.clone()
            },
            btreemap! {
                "meta/my_component.cmx".to_string() => "host/my_component.cmx".to_string(),
                "meta/package".to_string() => "host/meta/package".to_string()
            },
        )
        .unwrap();
        let component_manifest_contents = "my_component.cmx contents";
        let mut v = vec![];
        let meta_package = MetaPackage::from_name("my-package-name".parse().unwrap());
        meta_package.serialize(&mut v).unwrap();
        let file_system = FakeFileSystem {
            content_map: hashmap! {
                blob_path_string.clone() => Vec::new(),
                "host/my_component.cmx".to_string() => component_manifest_contents.as_bytes().to_vec(),
                "host/meta/package".to_string() => v
            },
        };

        let package_manifest = build_with_file_system(
            &creation_manifest,
            &meta_far_path,
            "test_package",
            None,
            &file_system,
        )
        .unwrap();
        let package_manifest_file = fs::File::create(&package_manifest_path).unwrap();
        serde_json::to_writer_pretty(&package_manifest_file, &package_manifest).unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_archive() {
        let tempdir = tempfile::tempdir().unwrap();
        let root = tempdir.path().join("blobs");
        fs::create_dir_all(&root).unwrap();

        let package_manifest_path = root.join("package_manifest.json");
        let meta_far_path = root.join("meta.far");
        let blob = root.join("blob");
        fs::File::create(&blob).unwrap();
        create_package_manifest(package_manifest_path.clone(), meta_far_path, blob);

        let result_dir = tempdir.path().join("results");
        fs::create_dir_all(&result_dir).unwrap();
        let result_far = result_dir.join("test_package.far");

        let cmd = ArchiveCommand {
            package_manifest: package_manifest_path,
            output: result_far.clone(),
            root_dir: result_dir.clone(),
        };
        cmd_archive(cmd).await.unwrap();
        assert!(result_far.exists());
        let archive = File::open(&result_far).unwrap();
        let mut package_archive = fuchsia_archive::Reader::new(&archive).unwrap();
        assert_eq!(package_archive.read_file("meta.far").unwrap().len(), 16384);
        assert_eq!(
            package_archive
                .read_file("15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b")
                .unwrap()
                .len(),
            0
        );
    }
}
