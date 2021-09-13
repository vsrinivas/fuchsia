// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use errors::ffx_bail;
use ffx_core::ffx_plugin;
pub use ffx_packaging_build_args::BuildCommand;
use fuchsia_pkg::{build, CreationManifest};
use std::fs::{create_dir_all, File};
use std::io::{BufReader, Write};

const DEFAULT_PACKAGE_MANIFEST_PATH: &str = "package_manifest.json";

#[ffx_plugin("ffx_package")]
pub async fn cmd_package(cmd: BuildCommand) -> Result<()> {
    cmd_package_build(cmd)?;
    Ok(())
}

pub fn cmd_package_build(cmd: BuildCommand) -> Result<()> {
    if !cmd.build_manifest_path.metadata()?.is_file() {
        ffx_bail!(
            "Build manifest path point to a directory: {}",
            cmd.build_manifest_path.display()
        );
    }

    let package_manifest_path = if let Some(path) = cmd.package_manifest_path {
        path
    } else {
        cmd.out.join(DEFAULT_PACKAGE_MANIFEST_PATH)
    };
    if !package_manifest_path.metadata()?.is_file() {
        ffx_bail!(
            "Package manifest path point to a directory: {}",
            package_manifest_path.display()
        );
    }
    let out_dir = cmd.out;

    let build_manifest = File::open(cmd.build_manifest_path)?;

    let creation_manifest = CreationManifest::from_pm_fini(BufReader::new(build_manifest))?;
    create_dir_all(&out_dir)?;
    let meta_far_path = out_dir.join("meta.far");
    let package_manifest = build(&creation_manifest, &meta_far_path, cmd.published_name)?;
    let mut file = File::create(package_manifest_path)?;
    file.write_all(serde_json::to_string(&package_manifest)?.as_bytes())?;
    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_pkg::MetaPackage;
    use std::fs::read_to_string;
    use std::path::PathBuf;

    #[test]
    fn test_build_manifest_not_exist() -> Result<()> {
        let cmd = BuildCommand {
            build_manifest_path: PathBuf::from("invalid path"),
            package_manifest_path: Some(PathBuf::from("./package_manifest.json")),
            out: PathBuf::from("./out"),
            published_name: String::from("package"),
        };
        assert!(cmd_package_build(cmd).is_err());
        Ok(())
    }

    #[test]
    fn test_package_manifest_not_exist() -> Result<()> {
        let tempdir = tempfile::tempdir().unwrap();
        let root = tempdir.path();
        let build_manifest_path = root.join("build.manifest");
        let package_manifest_path = Some(root.join("./out"));
        File::create(&build_manifest_path).unwrap();

        let cmd = BuildCommand {
            build_manifest_path,
            package_manifest_path,
            out: PathBuf::from("./out"),
            published_name: String::from("package"),
        };
        assert!(cmd_package_build(cmd).is_err());
        Ok(())
    }

    #[test]
    fn test_generate_empty_package_manifest() -> Result<()> {
        let tempdir = tempfile::tempdir().unwrap();
        let root = tempdir.path();
        let build_manifest_path = root.join("build.manifest");
        let meta_package_path = root.join("package");
        let package_manifest_path = root.join("package_manifest.json");

        let mut build_manifest = File::create(&build_manifest_path).unwrap();
        let _package_manifest = File::create(&package_manifest_path).unwrap();
        let meta_package_file = File::create(&meta_package_path).unwrap();

        build_manifest
            .write_all(format!("meta/package={}", meta_package_path.display()).as_bytes())?;
        let meta_package = MetaPackage::from_name_and_variant("my-package", "0").unwrap();
        meta_package.serialize(meta_package_file).unwrap();

        let cmd = BuildCommand {
            build_manifest_path,
            package_manifest_path: Some(package_manifest_path.clone()),
            out: PathBuf::from("./out"),
            published_name: String::from("package"),
        };
        cmd_package_build(cmd).unwrap();
        assert_eq!("{\"version\":\"1\",\"package\":{\"name\":\"package\",\"version\":\"0\"},\"blobs\":[{\"source_path\":\"./out/meta.far\",\"path\":\"meta/\",\"merkle\":\"421f08d453e25059d5908923901582e683dbce82fe697e40656bd74e143a96a5\",\"size\":8192}]}".to_owned(), read_to_string(package_manifest_path).unwrap());
        Ok(())
    }

    #[test]
    fn test_build_package_with_contents() -> Result<()> {
        let tempdir = tempfile::tempdir().unwrap();
        let root = tempdir.path();
        let build_manifest_path = root.join("build.manifest");
        let meta_package_path = root.join("package");
        let emtpy_file_path = root.join("file");
        let package_manifest_path = root.join("package_manifest.json");

        let mut build_manifest = File::create(&build_manifest_path).unwrap();
        let _package_manifest = File::create(&package_manifest_path).unwrap();
        let meta_package_file = File::create(&meta_package_path).unwrap();
        File::create(&emtpy_file_path).unwrap();

        build_manifest.write_all(
            format!(
                "meta/package={}\nfile={}",
                meta_package_path.display(),
                emtpy_file_path.display()
            )
            .as_bytes(),
        )?;
        let meta_package = MetaPackage::from_name_and_variant("my-package", "0").unwrap();
        meta_package.serialize(meta_package_file).unwrap();

        let cmd = BuildCommand {
            build_manifest_path,
            package_manifest_path: Some(package_manifest_path.clone()),
            out: PathBuf::from("./out"),
            published_name: String::from("package"),
        };
        let emtpy_file_path_string = emtpy_file_path.display().to_string();
        cmd_package_build(cmd).unwrap();
        assert_eq!("{\"version\":\"1\",\"package\":{\"name\":\"package\",\"version\":\"0\"},\"blobs\":[{\"source_path\":\"\",\"path\":\"file\",\"merkle\":\"15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b\",\"size\":0},{\"source_path\":\"./out/meta.far\",\"path\":\"meta/\",\"merkle\":\"d14b2158d0a08f826c5359a7c8af79432cdd70490f0366c6ffa832421444078f\",\"size\":12288}]}".to_owned(), read_to_string(package_manifest_path).unwrap().as_str().replace(&emtpy_file_path_string, ""));
        Ok(())
    }
}
