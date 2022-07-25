// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Result},
    errors::ffx_bail,
    ffx_core::ffx_plugin,
    fuchsia_pkg::{CreationManifest, PackageBuilder},
    std::{
        collections::BTreeSet,
        fs::{create_dir_all, File},
        io::{BufReader, BufWriter, Write as _},
    },
    version_history::AbiRevision,
};

const META_FAR_NAME: &str = "meta.far";
const META_FAR_MERKLE_NAME: &str = "meta.far.merkle";
const META_FAR_DEPFILE_NAME: &str = "meta.far.d";
const PACKAGE_MANIFEST_NAME: &str = "package_manifest.json";
const BLOBS_JSON_NAME: &str = "blobs.json";
const BLOBS_MANIFEST_NAME: &str = "blobs.manifest";

pub use ffx_package_build_args::BuildCommand;

#[ffx_plugin("ffx_package")]
pub async fn cmd_package(cmd: BuildCommand) -> Result<()> {
    cmd_package_build(cmd)?;
    Ok(())
}

pub fn cmd_package_build(cmd: BuildCommand) -> Result<()> {
    let creation_manifest = File::open(&cmd.creation_manifest_path)
        .with_context(|| format!("opening {}", cmd.creation_manifest_path.display()))?;

    let creation_manifest = CreationManifest::from_pm_fini(BufReader::new(creation_manifest))?;

    let mut builder = PackageBuilder::from_creation_manifest(&creation_manifest)?;

    if let Some(abi_revision) = get_abi_revision(&cmd)? {
        builder.abi_revision(abi_revision);
    }

    if let Some(published_name) = &cmd.published_name {
        builder.published_name(published_name);
    }

    if let Some(repository) = &cmd.repository {
        builder.repository(repository);
    }

    if !cmd.out.exists() {
        create_dir_all(&cmd.out)?;
    }

    // Build the package.
    let gendir = tempfile::TempDir::new_in(&cmd.out)?;
    let meta_far_path = cmd.out.join(META_FAR_NAME);
    let package_manifest = builder.build(gendir.path(), &meta_far_path)?;

    // Write out the package manifest to `package_manifest.json`.
    let package_manifest_path = cmd.out.join(PACKAGE_MANIFEST_NAME);
    let file = File::create(&package_manifest_path)
        .with_context(|| format!("creating {}", package_manifest_path.display()))?;
    serde_json::to_writer(BufWriter::new(file), &package_manifest)?;

    // FIXME(fxbug.dev/101306): We're replicating `pm build --depfile` here, and directly expressing
    // that the `meta.far` depends on all the package contents. However, I think this should
    // ultimately be unnecessary, since the build systems should be separately tracking that the
    // creation manifest already depends on the package contents. We should make sure all build
    // systems support this, and remove the `--depfile` here.
    if cmd.depfile {
        let depfile_path = cmd.out.join(META_FAR_DEPFILE_NAME);
        let file = File::create(&depfile_path)
            .with_context(|| format!("creating {}", depfile_path.display()))?;
        let mut file = BufWriter::new(file);

        write!(file, "{}:", meta_far_path.display())?;

        let deps = creation_manifest
            .far_contents()
            .values()
            .chain(creation_manifest.external_contents().values())
            .collect::<BTreeSet<_>>();

        for path in deps {
            // Spaces are separators, so spaces in filenames must be escaped.
            let path = path.replace(' ', "\\ ");

            write!(file, " {}", path)?;
        }
    }

    // FIXME(fxbug.dev/101304): Write out the meta.far.merkle file, that contains the meta.far
    // merkle.
    if cmd.meta_far_merkle {
        if let Some(blob) = package_manifest.blobs().iter().find(|b| b.path == "meta/") {
            let meta_far_merkle_path = cmd.out.join(META_FAR_MERKLE_NAME);
            std::fs::write(meta_far_merkle_path, blob.merkle.to_string().as_bytes())?;
        } else {
            ffx_bail!("Could not find entry for 'meta/'");
        }
    }

    // FIXME(fxbug.dev/101304): Some tools still depend on the legacy `blobs.json` file. We
    // should migrate them over to using `package_manifest.json` so we can stop producing this file.
    if cmd.blobs_json {
        let blobs_json_path = cmd.out.join(BLOBS_JSON_NAME);
        let file = File::create(&blobs_json_path)
            .with_context(|| format!("creating {}", blobs_json_path.display()))?;

        serde_json::to_writer(BufWriter::new(file), package_manifest.blobs())?;
    }

    // FIXME(fxbug.dev/101304): Some tools still depend on the legacy `blobs.manifest` file. We
    // should migrate them over to using `package_manifest.json` so we can stop producing this file.
    if cmd.blobs_manifest {
        let blobs_manifest_path = cmd.out.join(BLOBS_MANIFEST_NAME);
        let file = File::create(&blobs_manifest_path)
            .with_context(|| format!("creating {}", blobs_manifest_path.display()))?;
        let mut file = BufWriter::new(file);

        for entry in package_manifest.blobs() {
            writeln!(file, "{}={}", entry.merkle, entry.source_path)?;
        }
    }

    Ok(())
}

fn get_abi_revision(cmd: &BuildCommand) -> Result<Option<u64>> {
    match (cmd.api_level, cmd.abi_revision) {
        (Some(_), Some(_)) => {
            ffx_bail!("--api-level and --abi-revision cannot be specified at the same time")
        }
        (Some(api_level), None) => {
            for version in version_history::VERSION_HISTORY {
                if api_level == version.api_level {
                    return Ok(Some(version.abi_revision.into()));
                }
            }

            ffx_bail!("Unknown API level {}", api_level)
        }
        (None, Some(abi_revision)) => {
            let abi_revision = AbiRevision::new(abi_revision);
            for version in version_history::VERSION_HISTORY {
                if version.abi_revision == abi_revision {
                    return Ok(Some(abi_revision.into()));
                }
            }

            ffx_bail!("Unknown ABI revision {}", abi_revision)
        }
        (None, None) => Ok(None),
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        fuchsia_pkg::MetaPackage,
        pretty_assertions::assert_eq,
        std::{
            collections::BTreeMap,
            convert::TryInto as _,
            fs::{read_dir, read_to_string},
            io::Write,
            path::{Path, PathBuf},
        },
    };

    fn read_meta_far_contents(path: &Path) -> BTreeMap<String, String> {
        let mut metafar = File::open(path).unwrap();
        let mut far_reader = fuchsia_archive::Utf8Reader::new(&mut metafar).unwrap();
        let far_paths = far_reader.list().map(|e| e.path().to_string()).collect::<Vec<_>>();

        let mut far_contents = BTreeMap::new();
        for path in far_paths {
            let contents = far_reader.read_file(&path).unwrap();
            let contents = if path == "meta/fuchsia.abi/abi-revision" {
                AbiRevision::from_bytes(contents.try_into().unwrap()).to_string()
            } else {
                String::from_utf8(contents).unwrap()
            };
            far_contents.insert(path, contents);
        }

        far_contents
    }

    #[test]
    fn test_creation_manifest_not_exist() {
        let tempdir = tempfile::tempdir().unwrap();
        let root = tempdir.path();

        let cmd = BuildCommand {
            creation_manifest_path: root.join("invalid path"),
            out: PathBuf::from("out"),
            api_level: Some(8),
            abi_revision: None,
            repository: None,
            published_name: None,
            depfile: false,
            meta_far_merkle: false,
            blobs_json: false,
            blobs_manifest: false,
        };

        assert!(cmd_package_build(cmd).is_err());
    }

    #[test]
    fn test_package_manifest_not_exist() {
        let tempdir = tempfile::tempdir().unwrap();
        let root = tempdir.path();
        let out = root.join("out");

        let creation_manifest_path = root.join("creation.manifest");
        File::create(&out).unwrap();

        let cmd = BuildCommand {
            creation_manifest_path,
            out: out,
            api_level: Some(8),
            abi_revision: None,
            repository: None,
            published_name: None,
            depfile: false,
            meta_far_merkle: false,
            blobs_json: false,
            blobs_manifest: false,
        };

        assert!(cmd_package_build(cmd).is_err());
    }

    #[test]
    fn test_generate_empty_package_manifest() {
        let tempdir = tempfile::tempdir().unwrap();
        let root = tempdir.path();
        let out = root.join("out");

        let meta_package_path = root.join("package");
        let meta_package_file = File::create(&meta_package_path).unwrap();
        let meta_package = MetaPackage::from_name("my-package".parse().unwrap());
        meta_package.serialize(meta_package_file).unwrap();

        let creation_manifest_path = root.join("creation.manifest");
        let mut creation_manifest = File::create(&creation_manifest_path).unwrap();

        creation_manifest
            .write_all(format!("meta/package={}", meta_package_path.display()).as_bytes())
            .unwrap();

        cmd_package_build(BuildCommand {
            creation_manifest_path,
            out: out.clone(),
            api_level: Some(8),
            abi_revision: None,
            repository: None,
            published_name: None,
            depfile: false,
            meta_far_merkle: false,
            blobs_json: false,
            blobs_manifest: false,
        })
        .unwrap();

        let meta_far_path = out.join(META_FAR_NAME);
        let package_manifest_path = out.join(PACKAGE_MANIFEST_NAME);

        // Make sure we only generated what we expected.
        let mut paths = read_dir(&out).unwrap().map(|e| e.unwrap().path()).collect::<Vec<_>>();
        paths.sort();

        assert_eq!(paths, vec![meta_far_path.clone(), package_manifest_path.clone(),],);

        assert_eq!(
            serde_json::from_reader::<_, serde_json::Value>(
                File::open(&package_manifest_path).unwrap()
            )
            .unwrap(),
            serde_json::json!({
                "version": "1",
                "package": {
                    "name": "my-package",
                    "version": "0",
                },
                "blobs": [
                    {
                        "source_path": meta_far_path,
                        "path": "meta/",
                        "merkle": "436eb4b5943bc74f97d95dc81b2de9acddf6691453a536588a86280cac55222e",
                        "size": 12288,
                    }
                ],
            }),
        );

        assert_eq!(
            read_meta_far_contents(&out.join(META_FAR_NAME)),
            BTreeMap::from([
                ("meta/contents".into(), "".into()),
                ("meta/package".into(), r#"{"name":"my-package","version":"0"}"#.into()),
                (
                    "meta/fuchsia.abi/abi-revision".into(),
                    version_history::VERSION_HISTORY
                        .iter()
                        .find(|v| v.api_level == 8)
                        .unwrap()
                        .abi_revision
                        .to_string(),
                ),
            ]),
        );
    }

    #[test]
    fn test_generate_empty_package_manifest_latest_version() {
        let tempdir = tempfile::tempdir().unwrap();
        let root = tempdir.path();
        let out = root.join("out");

        let meta_package_path = root.join("package");
        let meta_package_file = File::create(&meta_package_path).unwrap();
        let meta_package = MetaPackage::from_name("my-package".parse().unwrap());
        meta_package.serialize(meta_package_file).unwrap();

        let creation_manifest_path = root.join("creation.manifest");
        let mut creation_manifest = File::create(&creation_manifest_path).unwrap();

        creation_manifest
            .write_all(format!("meta/package={}", meta_package_path.display()).as_bytes())
            .unwrap();

        cmd_package_build(BuildCommand {
            creation_manifest_path,
            out: out.clone(),
            api_level: None,
            abi_revision: None,
            repository: None,
            published_name: None,
            depfile: false,
            meta_far_merkle: false,
            blobs_json: false,
            blobs_manifest: false,
        })
        .unwrap();

        let meta_far_path = out.join(META_FAR_NAME);
        let package_manifest_path = out.join(PACKAGE_MANIFEST_NAME);

        // Make sure we only generated what we expected.
        let mut paths = read_dir(&out).unwrap().map(|e| e.unwrap().path()).collect::<Vec<_>>();
        paths.sort();

        assert_eq!(paths, vec![meta_far_path.clone(), package_manifest_path.clone(),],);

        // Since we're generating a file with the latest ABI revision, the meta.far merkle might
        // change when we roll the ABI. So compute the merkle of the file.
        let mut meta_far_merkle_file = File::open(&meta_far_path).unwrap();
        let meta_far_size = meta_far_merkle_file.metadata().unwrap().len();
        let meta_far_merkle = fuchsia_merkle::from_read(&mut meta_far_merkle_file).unwrap().root();

        assert_eq!(
            serde_json::from_reader::<_, serde_json::Value>(
                File::open(&package_manifest_path).unwrap()
            )
            .unwrap(),
            serde_json::json!({
                "version": "1",
                "package": {
                    "name": "my-package",
                    "version": "0",
                },
                "blobs": [
                    {
                        "source_path": meta_far_path,
                        "path": "meta/",
                        "merkle": meta_far_merkle.to_string(),
                        "size": meta_far_size,
                    }
                ],
            }),
        );

        assert_eq!(
            read_meta_far_contents(&out.join(META_FAR_NAME)),
            BTreeMap::from([
                ("meta/contents".into(), "".into()),
                ("meta/package".into(), r#"{"name":"my-package","version":"0"}"#.into()),
                (
                    "meta/fuchsia.abi/abi-revision".into(),
                    version_history::LATEST_VERSION.abi_revision.to_string(),
                ),
            ]),
        );
    }

    #[test]
    fn test_generate_cannot_specify_both_api_and_abi() {
        let tempdir = tempfile::tempdir().unwrap();
        let root = tempdir.path();
        let out = root.join("out");

        let meta_package_path = root.join("package");
        let meta_package_file = File::create(&meta_package_path).unwrap();
        let meta_package = MetaPackage::from_name("my-package".parse().unwrap());
        meta_package.serialize(meta_package_file).unwrap();

        let creation_manifest_path = root.join("creation.manifest");
        let mut creation_manifest = File::create(&creation_manifest_path).unwrap();

        creation_manifest
            .write_all(format!("meta/package={}", meta_package_path.display()).as_bytes())
            .unwrap();

        assert!(cmd_package_build(BuildCommand {
            creation_manifest_path,
            out: out.clone(),
            api_level: Some(8),
            abi_revision: Some(0xA56735A6690E09D8),
            repository: None,
            published_name: None,
            depfile: false,
            meta_far_merkle: false,
            blobs_json: false,
            blobs_manifest: false,
        })
        .is_err(),);
    }

    #[test]
    fn test_build_package_with_contents() {
        let tempdir = tempfile::tempdir().unwrap();
        let root = tempdir.path();

        let out = root.join("out");

        let meta_package_path = root.join("package");
        let meta_package_file = File::create(&meta_package_path).unwrap();
        let meta_package = MetaPackage::from_name("my-package".parse().unwrap());
        meta_package.serialize(meta_package_file).unwrap();

        let creation_manifest_path = root.join("creation.manifest");
        let mut creation_manifest = File::create(&creation_manifest_path).unwrap();

        let empty_file_path = root.join("empty-file");
        File::create(&empty_file_path).unwrap();

        creation_manifest
            .write_all(
                format!(
                    "empty-file={}\nmeta/package={}\n",
                    empty_file_path.display(),
                    meta_package_path.display(),
                )
                .as_bytes(),
            )
            .unwrap();

        cmd_package_build(BuildCommand {
            creation_manifest_path,
            out: out.clone(),
            api_level: Some(8),
            abi_revision: None,
            repository: Some("my-repository".into()),
            published_name: None,
            meta_far_merkle: false,
            depfile: false,
            blobs_json: false,
            blobs_manifest: false,
        })
        .unwrap();

        let meta_far_path = out.join(META_FAR_NAME);
        let package_manifest_path = out.join(PACKAGE_MANIFEST_NAME);

        // Make sure we only generated what we expected.
        let mut paths = read_dir(&out).unwrap().map(|e| e.unwrap().path()).collect::<Vec<_>>();
        paths.sort();

        assert_eq!(paths, vec![meta_far_path.clone(), package_manifest_path.clone(),],);

        assert_eq!(
            serde_json::from_reader::<_, serde_json::Value>(
                File::open(&package_manifest_path).unwrap()
            )
            .unwrap(),
            serde_json::json!({
                "version": "1",
                "package": {
                    "name": "my-package",
                    "version": "0",
                },
                "repository": "my-repository",
                "blobs": [
                    {
                        "source_path": empty_file_path,
                        "path": "empty-file",
                        "merkle": "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b",
                        "size": 0,
                    },
                    {
                        "source_path": meta_far_path,
                        "path": "meta/",
                        "merkle": "36dde5da0ed4a51433a3b45ed9917c98442613f4b12e0f9661519678482ab3e3",
                        "size": 16384,
                    },
                ],
            }),
        );

        assert_eq!(
            read_meta_far_contents(&meta_far_path),
            BTreeMap::from([
                (
                    "meta/contents".into(),
                    "empty-file=15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b\n"
                        .into(),
                ),
                ("meta/package".into(), r#"{"name":"my-package","version":"0"}"#.into()),
                (
                    "meta/fuchsia.abi/abi-revision".into(),
                    version_history::VERSION_HISTORY
                        .iter()
                        .find(|v| v.api_level == 8)
                        .unwrap()
                        .abi_revision
                        .to_string(),
                ),
            ]),
        );
    }

    #[test]
    fn test_build_package_with_everything() {
        let tempdir = tempfile::tempdir().unwrap();
        let root = tempdir.path();

        let out = root.join("out");

        let meta_package_path = root.join("package");
        let meta_package_file = File::create(&meta_package_path).unwrap();
        let meta_package = MetaPackage::from_name("my-package".parse().unwrap());
        meta_package.serialize(meta_package_file).unwrap();

        let creation_manifest_path = root.join("creation.manifest");
        let mut creation_manifest = File::create(&creation_manifest_path).unwrap();

        let empty_file_path = root.join("empty-file");
        File::create(&empty_file_path).unwrap();

        creation_manifest
            .write_all(
                format!(
                    "empty-file={}\nmeta/package={}",
                    empty_file_path.display(),
                    meta_package_path.display(),
                )
                .as_bytes(),
            )
            .unwrap();

        cmd_package_build(BuildCommand {
            creation_manifest_path,
            out: out.clone(),
            api_level: Some(8),
            abi_revision: None,
            repository: None,
            published_name: Some("published-name".into()),
            depfile: true,
            meta_far_merkle: true,
            blobs_json: true,
            blobs_manifest: true,
        })
        .unwrap();

        let meta_far_path = out.join(META_FAR_NAME);
        let meta_far_merkle_path = out.join(META_FAR_MERKLE_NAME);
        let package_manifest_path = out.join(PACKAGE_MANIFEST_NAME);
        let meta_far_depfile_path = out.join(META_FAR_DEPFILE_NAME);
        let blobs_json_path = out.join(BLOBS_JSON_NAME);
        let blobs_manifest_path = out.join(BLOBS_MANIFEST_NAME);

        // Make sure we only generated what we expected.
        let mut paths = read_dir(&out).unwrap().map(|e| e.unwrap().path()).collect::<Vec<_>>();
        paths.sort();

        assert_eq!(
            paths,
            vec![
                blobs_json_path.clone(),
                blobs_manifest_path.clone(),
                meta_far_path.clone(),
                meta_far_depfile_path.clone(),
                meta_far_merkle_path.clone(),
                package_manifest_path.clone(),
            ],
        );

        assert_eq!(
            read_to_string(meta_far_depfile_path).unwrap(),
            format!(
                "{}: {} {}",
                meta_far_path.display(),
                empty_file_path.display(),
                meta_package_path.display(),
            ),
        );

        assert_eq!(
            serde_json::from_reader::<_, serde_json::Value>(File::open(&blobs_json_path).unwrap())
                .unwrap(),
            serde_json::json!([
                    {
                        "source_path": empty_file_path,
                        "path": "empty-file",
                        "merkle": "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b",
                        "size": 0,
                    },
                    {
                        "source_path": meta_far_path,
                        "path": "meta/",
                        "merkle": "36dde5da0ed4a51433a3b45ed9917c98442613f4b12e0f9661519678482ab3e3",
                        "size": 16384,
                    },
                ]
            )
        );

        assert_eq!(
            read_to_string(blobs_manifest_path).unwrap(),
            format!(
                "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b={}\n\
                36dde5da0ed4a51433a3b45ed9917c98442613f4b12e0f9661519678482ab3e3={}\n",
                empty_file_path.display(),
                meta_far_path.display(),
            )
        );
    }
}
