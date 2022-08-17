// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        devmgr_config::{DevmgrConfigCollection, DevmgrConfigContents},
        static_pkgs::collection::{StaticPkgsCollection, StaticPkgsError},
    },
    anyhow::{Context, Result},
    fuchsia_archive::Utf8Reader as FarReader,
    fuchsia_hash::Hash,
    fuchsia_merkle::MerkleTree,
    fuchsia_url::{PackageName, PackageVariant},
    maplit::hashset,
    scrutiny::model::{collector::DataCollector, model::DataModel},
    scrutiny_utils::{
        artifact::{ArtifactReader, BlobFsArtifactReader},
        key_value::parse_key_value,
        package::PackageIndexContents,
        url::from_package_name_variant_path,
    },
    std::{
        collections::{HashMap, HashSet},
        path::{Path, PathBuf},
        str::{from_utf8, FromStr},
        sync::Arc,
    },
};

static PKGFS_CMD_DEVMGR_CONFIG_KEY: &str = "zircon.system.pkgfs.cmd";
static PKGFS_BINARY_PATH: &str = "bin/pkgsvr";
static META_FAR_CONTENTS_LISTING_PATH: &str = "meta/contents";
static STATIC_PKGS_LISTING_PATH: &str = "data/static_packages";

#[derive(Debug)]
struct StaticPkgsData {
    deps: HashSet<PathBuf>,
    static_pkgs: PackageIndexContents,
}

#[derive(Debug)]
struct ErrorWithDeps {
    pub deps: HashSet<PathBuf>,
    pub error: StaticPkgsError,
}

fn collect_static_pkgs(
    devmgr_config: DevmgrConfigContents,
    mut artifact_reader: Box<dyn ArtifactReader>,
) -> Result<StaticPkgsData, ErrorWithDeps> {
    // Get system image path from ["bin/pkgsvr", <system-image-hash>] cmd.
    let pkgfs_cmd =
        devmgr_config.get(PKGFS_CMD_DEVMGR_CONFIG_KEY).ok_or_else(|| ErrorWithDeps {
            deps: artifact_reader.get_deps(),
            error: StaticPkgsError::MissingPkgfsCmdEntry,
        })?;

    if pkgfs_cmd.len() != 2 {
        return Err(ErrorWithDeps {
            deps: artifact_reader.get_deps(),
            error: StaticPkgsError::UnexpectedPkgfsCmdLen {
                expected_len: 2,
                actual_len: pkgfs_cmd.len(),
            },
        });
    }
    let (pkgfs_binary_path, system_image_merkle_string) = (&pkgfs_cmd[0], &pkgfs_cmd[1]);

    if pkgfs_binary_path != PKGFS_BINARY_PATH {
        return Err(ErrorWithDeps {
            deps: artifact_reader.get_deps(),
            error: StaticPkgsError::UnexpectedPkgfsCmd {
                expected_cmd: PKGFS_BINARY_PATH.to_string(),
                actual_cmd: pkgfs_binary_path.clone(),
            },
        });
    }
    let system_image_merkle =
        Hash::from_str(system_image_merkle_string).map_err(|_| ErrorWithDeps {
            deps: artifact_reader.get_deps(),
            error: StaticPkgsError::MalformedSystemImageHash {
                actual_hash: system_image_merkle_string.clone(),
            },
        })?;

    // Read system image package and verify its merkle.
    let system_image_path = Path::new(system_image_merkle_string);
    let mut system_image_pkg =
        artifact_reader.open(&Path::new(system_image_path)).map_err(|err| ErrorWithDeps {
            deps: artifact_reader.get_deps(),
            error: StaticPkgsError::FailedToReadSystemImage {
                system_image_path: system_image_path.to_path_buf(),
                io_error: err.to_string(),
            },
        })?;
    let computed_system_image_merkle = MerkleTree::from_reader(&mut system_image_pkg)
        .map_err(|err| ErrorWithDeps {
            deps: artifact_reader.get_deps(),
            error: StaticPkgsError::FailedToReadSystemImage {
                system_image_path: system_image_path.to_path_buf(),
                io_error: err.to_string(),
            },
        })?
        .root();
    if computed_system_image_merkle != system_image_merkle {
        return Err(ErrorWithDeps {
            deps: artifact_reader.get_deps(),
            error: StaticPkgsError::FailedToVerifySystemImage {
                expected_merkle_root: system_image_merkle,
                computed_merkle_root: computed_system_image_merkle,
            },
        });
    }

    // Parse system image.
    system_image_pkg.rewind().map_err(|err| ErrorWithDeps {
        deps: artifact_reader.get_deps(),
        error: StaticPkgsError::FailedToParseSystemImage {
            system_image_path: system_image_path.to_path_buf(),
            parse_error: err.to_string(),
        },
    })?;
    let mut system_image_far = FarReader::new(system_image_pkg).map_err(|err| ErrorWithDeps {
        deps: artifact_reader.get_deps(),
        error: StaticPkgsError::FailedToParseSystemImage {
            system_image_path: system_image_path.to_path_buf(),
            parse_error: err.to_string(),
        },
    })?;

    // Extract "data/static_packages" hash from "meta/contents" file.
    let system_image_data_contents = parse_key_value(
        from_utf8(&system_image_far.read_file(META_FAR_CONTENTS_LISTING_PATH).map_err(|err| {
            ErrorWithDeps {
                deps: artifact_reader.get_deps(),
                error: StaticPkgsError::FailedToReadSystemImageMetaFile {
                    system_image_path: system_image_path.to_path_buf(),
                    file_name: META_FAR_CONTENTS_LISTING_PATH.to_string(),
                    far_error: err.to_string(),
                },
            }
        })?)
        .map_err(|err| ErrorWithDeps {
            deps: artifact_reader.get_deps(),
            error: StaticPkgsError::FailedToDecodeSystemImageMetaFile {
                system_image_path: system_image_path.to_path_buf(),
                file_name: META_FAR_CONTENTS_LISTING_PATH.to_string(),
                utf8_error: err.to_string(),
            },
        })?,
    )
    .map_err(|err| ErrorWithDeps {
        deps: artifact_reader.get_deps(),
        error: StaticPkgsError::FailedToParseSystemImageMetaFile {
            system_image_path: system_image_path.to_path_buf(),
            file_name: META_FAR_CONTENTS_LISTING_PATH.to_string(),
            parse_error: err.to_string(),
        },
    })?;
    let static_pkgs_merkle_string =
        system_image_data_contents.get(STATIC_PKGS_LISTING_PATH).ok_or_else(|| ErrorWithDeps {
            deps: artifact_reader.get_deps(),
            error: StaticPkgsError::MissingStaticPkgsEntry {
                system_image_path: system_image_path.to_path_buf(),
                file_name: STATIC_PKGS_LISTING_PATH.to_string(),
            },
        })?;

    // Check static pkgs merkle format; determine path to static pkgs file.
    let static_pkgs_merkle =
        Hash::from_str(static_pkgs_merkle_string).map_err(|_| ErrorWithDeps {
            deps: artifact_reader.get_deps(),
            error: StaticPkgsError::MalformedStaticPkgsHash {
                actual_hash: static_pkgs_merkle_string.clone(),
            },
        })?;
    let static_pkgs_path = Path::new(static_pkgs_merkle_string);

    // Read static packages index, check its merkle, and parse it.
    let mut static_pkgs = artifact_reader.open(static_pkgs_path).map_err(|err| ErrorWithDeps {
        deps: artifact_reader.get_deps(),
        error: StaticPkgsError::FailedToReadStaticPkgs {
            static_pkgs_path: static_pkgs_path.to_path_buf(),
            io_error: err.to_string(),
        },
    })?;
    let computed_static_pkgs_merkle = MerkleTree::from_reader(&mut static_pkgs)
        .map_err(|err| ErrorWithDeps {
            deps: artifact_reader.get_deps(),
            error: StaticPkgsError::FailedToReadStaticPkgs {
                static_pkgs_path: static_pkgs_path.to_path_buf(),
                io_error: err.to_string(),
            },
        })?
        .root();
    if computed_static_pkgs_merkle != static_pkgs_merkle {
        return Err(ErrorWithDeps {
            deps: artifact_reader.get_deps(),
            error: StaticPkgsError::FailedToVerifyStaticPkgs {
                expected_merkle_root: static_pkgs_merkle,
                computed_merkle_root: computed_static_pkgs_merkle,
            },
        });
    }
    static_pkgs.rewind().map_err(|err| ErrorWithDeps {
        deps: artifact_reader.get_deps(),
        error: StaticPkgsError::FailedToParseStaticPkgs {
            static_pkgs_path: static_pkgs_path.to_path_buf(),
            parse_error: err.to_string(),
        },
    })?;
    let mut static_pkgs_contents = String::new();
    static_pkgs.read_to_string(&mut static_pkgs_contents).map_err(|err| ErrorWithDeps {
        deps: artifact_reader.get_deps(),
        error: StaticPkgsError::FailedToParseStaticPkgs {
            static_pkgs_path: static_pkgs_path.to_path_buf(),
            parse_error: err.to_string(),
        },
    })?;

    let static_pkgs = parse_key_value(&static_pkgs_contents).map_err(|err| ErrorWithDeps {
        deps: artifact_reader.get_deps(),
        error: StaticPkgsError::FailedToParseStaticPkgs {
            static_pkgs_path: static_pkgs_path.to_path_buf(),
            parse_error: err.to_string(),
        },
    })?;
    let static_pkgs = static_pkgs
        .into_iter()
        .map(|(name_and_variant, merkle)| {
            let url = from_package_name_variant_path(name_and_variant)?;
            let merkle = Hash::from_str(&merkle)?;
            Ok(((url.name().clone(), url.variant().map(|v| v.clone())), merkle))
        })
        // Handle errors via collect
        // Iter<Result<_, __>> into Result<Vec<_>, __>.
        .collect::<Result<Vec<((PackageName, Option<PackageVariant>), Hash)>>>()
        .map_err(|err| ErrorWithDeps {
            deps: artifact_reader.get_deps(),
            error: StaticPkgsError::FailedToParseStaticPkgs {
                static_pkgs_path: static_pkgs_path.to_path_buf(),
                parse_error: format!(
                    "Failed to parse static packages name/variant=merkle: {:?}",
                    err
                ),
            },
        })?
        // Collect Vec<(_, __)> into HashMap<_, __>.
        .into_iter()
        .collect::<HashMap<(PackageName, Option<PackageVariant>), Hash>>();

    Ok(StaticPkgsData { deps: artifact_reader.get_deps(), static_pkgs })
}

#[derive(Default)]
pub struct StaticPkgsCollector;

impl DataCollector for StaticPkgsCollector {
    fn collect(&self, model: Arc<DataModel>) -> Result<()> {
        let model_config = model.config();
        let devmgr_config_data: Result<Arc<DevmgrConfigCollection>> = model.get();
        if let Err(err) = devmgr_config_data {
            model
                .set(StaticPkgsCollection {
                    static_pkgs: None,
                    deps: hashset! {},
                    errors: vec![StaticPkgsError::FailedToReadDevmgrConfigData {
                        model_error: err.to_string(),
                    }],
                })
                .context("Static packages collector failed to store errors in model")?;
            return Ok(());
        }
        let devmgr_config_data = devmgr_config_data.unwrap();
        let mut deps = devmgr_config_data.deps.clone();

        let mut errors: Vec<StaticPkgsError> = Vec::new();
        if devmgr_config_data.devmgr_config.is_none() {
            errors.push(StaticPkgsError::MissingDevmgrConfigData);
        }
        if devmgr_config_data.errors.len() > 0 {
            errors.push(StaticPkgsError::DevmgrConfigDataContainsErrors {
                devmgr_config_data_errors: devmgr_config_data.errors.clone(),
            });
        }
        if errors.len() > 0 {
            model
                .set(StaticPkgsCollection { static_pkgs: None, deps, errors })
                .context("Static packages collector failed to store errors in model")?;
            return Ok(());
        }
        let devmgr_config = devmgr_config_data.devmgr_config.clone().unwrap();
        let artifact_reader = match BlobFsArtifactReader::try_compound(
            &model_config.build_path(),
            model_config.tmp_dir_path().as_ref(),
            &model_config.blobfs_paths(),
        ) {
            Ok(artifact_reader) => artifact_reader,
            Err(err) => {
                model
                    .set(StaticPkgsCollection {
                        static_pkgs: None,
                        deps,
                        errors: vec![StaticPkgsError::FailedToLoadBlobfs {
                            build_path: model_config.build_path(),
                            blobfs_paths: model_config.blobfs_paths(),
                            blobfs_error: err.to_string(),
                        }],
                    })
                    .context("Static packages collector failed to store errors in model")?;
                return Ok(());
            }
        };

        let data: StaticPkgsCollection =
            match collect_static_pkgs(devmgr_config, Box::new(artifact_reader)) {
                Ok(static_pkgs_data) => {
                    deps.extend(static_pkgs_data.deps.into_iter());
                    StaticPkgsCollection {
                        static_pkgs: Some(static_pkgs_data.static_pkgs),
                        deps,
                        errors: vec![],
                    }
                }
                Err(err) => StaticPkgsCollection {
                    static_pkgs: None,
                    deps: deps.union(&err.deps).map(PathBuf::clone).collect(),
                    errors: vec![err.error],
                },
            };
        model.set(data).context("Static packages collector failed to store result in model")?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{
            collect_static_pkgs, ErrorWithDeps, StaticPkgsCollector,
            META_FAR_CONTENTS_LISTING_PATH, PKGFS_BINARY_PATH, PKGFS_CMD_DEVMGR_CONFIG_KEY,
            STATIC_PKGS_LISTING_PATH,
        },
        crate::{
            devmgr_config::{DevmgrConfigCollection, DevmgrConfigError},
            static_pkgs::collection::{StaticPkgsCollection, StaticPkgsError},
        },
        anyhow::{anyhow, Context, Result},
        fuchsia_archive::write as far_write,
        fuchsia_merkle::{Hash, MerkleTree, HASH_SIZE},
        fuchsia_url::{PackageName, PackageVariant},
        maplit::{btreemap, hashmap, hashset},
        scrutiny::model::collector::DataCollector,
        scrutiny_testing::{artifact::MockArtifactReader, fake::fake_data_model},
        std::{
            collections::{BTreeMap, HashMap},
            io::{BufWriter, Read, Write},
            path::PathBuf,
            str::FromStr,
            sync::Arc,
        },
    };

    fn create_system_image_far(static_pkgs_merkle: Option<Hash>) -> Vec<u8> {
        let mut system_image_far = BufWriter::new(Vec::new());
        let meta_contents = match static_pkgs_merkle {
            Some(static_pkgs_merkle) => {
                format!("{}={}\n", STATIC_PKGS_LISTING_PATH, static_pkgs_merkle)
            }
            None => "".to_string(),
        };
        let meta_contents_bytes = meta_contents.as_bytes();
        let meta_contents_reader: Box<dyn Read> = Box::new(meta_contents_bytes);
        let path_content_map: BTreeMap<&str, (u64, Box<dyn Read>)> = btreemap! {
            META_FAR_CONTENTS_LISTING_PATH =>
                (meta_contents_bytes.len() as u64, meta_contents_reader),
        };
        far_write(&mut system_image_far, path_content_map).unwrap();
        system_image_far.into_inner().unwrap()
    }

    fn create_static_pkgs_listing(
        mapping: HashMap<(PackageName, Option<PackageVariant>), Hash>,
    ) -> Vec<u8> {
        let mut static_pkgs_listing = BufWriter::new(Vec::new());
        // let iter: <HashMap<(PackageName, Option<PackageVariant>), Hash> as std::iter::IntoIterator>::IntoIter =
        //     mapping.into_iter();
        for ((name, variant), merkle) in mapping {
            match variant {
                Some(variant) => {
                    write!(static_pkgs_listing, "{}/{}={}\n", name, variant, merkle).unwrap()
                }
                None => write!(static_pkgs_listing, "{}={}\n", name, merkle).unwrap(),
            };
        }
        static_pkgs_listing.into_inner().unwrap()
    }

    #[fuchsia::test]
    fn test_missing_all_data() -> Result<()> {
        // Model contains no data (in particular, no devmgr config data).
        let model = fake_data_model();
        let collector = StaticPkgsCollector::default();
        collector
            .collect(model.clone())
            .context("Failed to return cleanly when data missing from model")?;
        let result: Arc<StaticPkgsCollection> =
            model.get().context("Failed to get static pkgs data put to model")?;
        assert!(result.static_pkgs.is_none());
        assert_eq!(result.errors.len(), 1);
        match &result.errors[0] {
            StaticPkgsError::FailedToReadDevmgrConfigData { .. } => Ok(()),
            err => Err(anyhow!("Unexpected error: {}", err.to_string())),
        }
    }

    #[fuchsia::test]
    fn test_missing_config_data() -> Result<()> {
        let model = fake_data_model();
        // Result from devmgr config contains no config.
        let devmgr_config_result =
            DevmgrConfigCollection { deps: hashset! {}, devmgr_config: None, errors: vec![] };
        model.clone().set(devmgr_config_result).context("Failed to store devmgr config result")?;
        let collector = StaticPkgsCollector::default();
        collector
            .collect(model.clone())
            .context("Failed to return cleanly when data missing from model")?;
        let result: Arc<StaticPkgsCollection> =
            model.get().context("Failed to get static pkgs data put to model")?;
        assert!(result.static_pkgs.is_none());
        assert_eq!(result.errors.len(), 1);
        match &result.errors[0] {
            StaticPkgsError::MissingDevmgrConfigData => Ok(()),
            err => Err(anyhow!("Unexpected error: {}", err.to_string())),
        }
    }

    #[fuchsia::test]
    fn test_err_from_devmgr_config() -> Result<()> {
        let model = fake_data_model();
        // Result from devmgr config contains no config and an error.
        let devmgr_config_result = DevmgrConfigCollection {
            deps: hashset! {},
            devmgr_config: None,
            errors: vec![DevmgrConfigError::FailedToReadZbi {
                update_package_path: "update.far".into(),
                io_error: "Failed to read file at update.far".to_string(),
            }],
        };
        model.clone().set(devmgr_config_result).context("Failed to store devmgr config result")?;
        let collector = StaticPkgsCollector::default();
        collector
            .collect(model.clone())
            .context("Failed to return cleanly when data missing from model")?;
        let result: Arc<StaticPkgsCollection> =
            model.get().context("Failed to get static pkgs data put to model")?;
        assert!(result.static_pkgs.is_none());
        assert_eq!(result.errors.len(), 2);
        match (&result.errors[0], &result.errors[1]) {
            (
                StaticPkgsError::MissingDevmgrConfigData,
                StaticPkgsError::DevmgrConfigDataContainsErrors { .. },
            )
            | (
                StaticPkgsError::DevmgrConfigDataContainsErrors { .. },
                StaticPkgsError::MissingDevmgrConfigData,
            ) => Ok(()),
            (err1, err2) => {
                Err(anyhow!("Unexpected errors: {} and {}", err1.to_string(), err2.to_string()))
            }
        }
    }

    #[fuchsia::test]
    fn test_missing_pkgfs_cmd_entry() {
        let mock_artifact_reader = MockArtifactReader::new();
        let devmgr_config = hashmap! {};
        let result = collect_static_pkgs(devmgr_config, Box::new(mock_artifact_reader));
        match result {
            Err(ErrorWithDeps { error: StaticPkgsError::MissingPkgfsCmdEntry { .. }, .. }) => {
                return
            }
            _ => panic!("Unexpected result: {:?}", result),
        };
    }

    #[fuchsia::test]
    fn test_pkgfs_cmd_too_short() {
        let mock_artifact_reader = MockArtifactReader::new();
        let devmgr_config = hashmap! {
            PKGFS_CMD_DEVMGR_CONFIG_KEY.to_string() => vec![PKGFS_BINARY_PATH.to_string()],
        };
        let result = collect_static_pkgs(devmgr_config, Box::new(mock_artifact_reader));
        match result {
            Err(ErrorWithDeps { error: StaticPkgsError::UnexpectedPkgfsCmdLen { .. }, .. }) => {
                return
            }
            _ => panic!("Unexpected result: {:?}", result),
        };
    }

    #[fuchsia::test]
    fn test_pkgfs_cmd_too_long() {
        let mock_artifact_reader = MockArtifactReader::new();
        let devmgr_config = hashmap! {
            PKGFS_CMD_DEVMGR_CONFIG_KEY.to_string() => vec![
                PKGFS_BINARY_PATH.to_string(),
                "param1".to_string(),
                "param2".to_string(),
            ],
        };
        let result = collect_static_pkgs(devmgr_config, Box::new(mock_artifact_reader));
        match result {
            Err(ErrorWithDeps { error: StaticPkgsError::UnexpectedPkgfsCmdLen { .. }, .. }) => {
                return
            }
            _ => panic!("Unexpected result: {:?}", result),
        };
    }

    #[fuchsia::test]
    fn test_bad_pkgfs_cmd() {
        let mock_artifact_reader = MockArtifactReader::new();
        let bad_cmd_name = "unexpected/pkgsvr/path";
        let devmgr_config = hashmap! {
            PKGFS_CMD_DEVMGR_CONFIG_KEY.to_string() => vec![
                bad_cmd_name.to_string(),
                Hash::from([0; HASH_SIZE]).to_string(),
            ],
        };
        let result = collect_static_pkgs(devmgr_config, Box::new(mock_artifact_reader));
        match result {
            Err(ErrorWithDeps { error: StaticPkgsError::UnexpectedPkgfsCmd { .. }, .. }) => return,
            _ => panic!("Unexpected result: {:?}", result),
        };
    }

    #[fuchsia::test]
    fn test_invalid_system_image_merkle() {
        let mock_artifact_reader = MockArtifactReader::new();
        let bad_merkle_root = "I am not a merkle root";
        let devmgr_config = hashmap! {
            PKGFS_CMD_DEVMGR_CONFIG_KEY.to_string() => vec![
                PKGFS_BINARY_PATH.to_string(),
                bad_merkle_root.to_string(),
            ],
        };
        let result = collect_static_pkgs(devmgr_config, Box::new(mock_artifact_reader));
        match result {
            Err(ErrorWithDeps {
                error: StaticPkgsError::MalformedSystemImageHash { .. }, ..
            }) => return,
            _ => panic!("Unexpected result: {:?}", result),
        };
    }

    #[fuchsia::test]
    fn test_missing_system_image() {
        let mock_artifact_reader = MockArtifactReader::new();
        let designated_system_image_hash = Hash::from([0; HASH_SIZE]);
        let devmgr_config = hashmap! {
            PKGFS_CMD_DEVMGR_CONFIG_KEY.to_string() => vec![
                PKGFS_BINARY_PATH.to_string(),
                designated_system_image_hash.to_string(),
            ],
        };
        let result = collect_static_pkgs(devmgr_config, Box::new(mock_artifact_reader));
        match result {
            Err(ErrorWithDeps {
                error: StaticPkgsError::FailedToReadSystemImage { .. }, ..
            }) => return,
            _ => panic!("Unexpected result: {:?}", result),
        };
    }

    #[fuchsia::test]
    fn test_incorrect_system_image_merkle() {
        let mut mock_artifact_reader = MockArtifactReader::new();

        // Code under test designates `designated_system_image_hash` as "where to find system image
        // blob". This value is mapped to a valid system image file according to
        // `mock_artifact_reader`, but is not the correct hash for the file.
        let designated_system_image_hash = Hash::from([0; HASH_SIZE]);
        let system_image_contents = create_system_image_far(None);
        let system_image_hash =
            MerkleTree::from_reader(system_image_contents.as_slice()).unwrap().root();
        assert!(designated_system_image_hash != system_image_hash);

        // Incorrectly map `designated_system_image_hash` to `system_image_contents` (that's not its
        // content hash!).
        mock_artifact_reader.append_artifact(
            &PathBuf::from(designated_system_image_hash.to_string()),
            system_image_contents,
        );

        let devmgr_config = hashmap! {
            PKGFS_CMD_DEVMGR_CONFIG_KEY.to_string() => vec![
                PKGFS_BINARY_PATH.to_string(),
                designated_system_image_hash.to_string(),
            ],
        };
        let result = collect_static_pkgs(devmgr_config, Box::new(mock_artifact_reader));
        match result {
            Err(ErrorWithDeps {
                error: StaticPkgsError::FailedToVerifySystemImage { .. }, ..
            }) => return,
            _ => panic!("Unexpected result: {:?}", result),
        };
    }

    #[fuchsia::test]
    fn test_missing_static_pkgs() {
        let mut mock_artifact_reader = MockArtifactReader::new();

        // `None` below implies no static packages entry in system image package.
        let system_image_contents = create_system_image_far(None);
        let system_image_hash =
            MerkleTree::from_reader(system_image_contents.as_slice()).unwrap().root();

        mock_artifact_reader
            .append_artifact(&PathBuf::from(system_image_hash.to_string()), system_image_contents);

        let devmgr_config = hashmap! {
            PKGFS_CMD_DEVMGR_CONFIG_KEY.to_string() => vec![
                PKGFS_BINARY_PATH.to_string(),
                system_image_hash.to_string(),
                ],
        };
        let result = collect_static_pkgs(devmgr_config, Box::new(mock_artifact_reader));
        match result {
            Err(ErrorWithDeps {
                error: StaticPkgsError::MissingStaticPkgsEntry { .. }, ..
            }) => return,
            _ => panic!("Unexpected result: {:?}", result),
        };
    }

    #[fuchsia::test]
    fn test_static_pkgs_not_found() {
        let mut mock_artifact_reader = MockArtifactReader::new();

        let static_pkgs = hashmap! {};
        let static_pkgs_contents = create_static_pkgs_listing(static_pkgs.clone());
        let static_pkgs_hash =
            MerkleTree::from_reader(static_pkgs_contents.as_slice()).unwrap().root();

        let system_image_contents = create_system_image_far(Some(static_pkgs_hash.clone()));
        let system_image_hash =
            MerkleTree::from_reader(system_image_contents.as_slice()).unwrap().root();

        // Note: `mock_artifact_reader does not have static packages manifest added, so it will
        // yield an error when code under test attempts to said manifest.

        mock_artifact_reader
            .append_artifact(&PathBuf::from(system_image_hash.to_string()), system_image_contents);

        let result = collect_static_pkgs(
            hashmap! {
                PKGFS_CMD_DEVMGR_CONFIG_KEY.to_string() => vec![
                    PKGFS_BINARY_PATH.to_string(),
                    system_image_hash.to_string(),
                ],
            },
            Box::new(mock_artifact_reader),
        );
        match result {
            Err(ErrorWithDeps {
                error: StaticPkgsError::FailedToReadStaticPkgs { .. }, ..
            }) => return,
            _ => panic!("Unexpected result: {:?}", result),
        };
    }

    #[fuchsia::test]
    fn test_incorrect_static_pkgs_merkle() {
        let mut mock_artifact_reader = MockArtifactReader::new();

        let static_pkgs = hashmap! {};
        let static_pkgs_contents = create_static_pkgs_listing(static_pkgs.clone());
        let static_pkgs_hash =
            MerkleTree::from_reader(static_pkgs_contents.as_slice()).unwrap().root();

        // System image designates `designated_static_pkgs_hash` as "where to find static pkgs
        // listing". This value is mapped to the static pkgs file according to
        // `mock_artifact_reader`, but is not the correct hash for the file.
        let designated_static_pkgs_hash = Hash::from([0; HASH_SIZE]);
        let system_image_contents =
            create_system_image_far(Some(designated_static_pkgs_hash.clone()));
        let system_image_hash =
            MerkleTree::from_reader(system_image_contents.as_slice()).unwrap().root();
        assert!(designated_static_pkgs_hash != static_pkgs_hash);

        // Incorrectly map `designated_static_pkgs_hash` to `static_pkgs_contents` (that's not its
        // content hash!).
        mock_artifact_reader.append_artifact(
            &PathBuf::from(designated_static_pkgs_hash.to_string()),
            static_pkgs_contents,
        );

        mock_artifact_reader
            .append_artifact(&PathBuf::from(system_image_hash.to_string()), system_image_contents);

        let devmgr_config = hashmap! {
            PKGFS_CMD_DEVMGR_CONFIG_KEY.to_string() => vec![
                PKGFS_BINARY_PATH.to_string(),
                system_image_hash.to_string(),
            ],
        };
        let result = collect_static_pkgs(devmgr_config, Box::new(mock_artifact_reader));
        match result {
            Err(ErrorWithDeps {
                error: StaticPkgsError::FailedToVerifyStaticPkgs { .. }, ..
            }) => return,
            _ => panic!("Unexpected result: {:?}", result),
        };
    }

    #[fuchsia::test]
    fn test_empty_static_pkgs() {
        let mut mock_artifact_reader = MockArtifactReader::new();

        let static_pkgs = hashmap! {};
        let static_pkgs_contents = create_static_pkgs_listing(static_pkgs.clone());
        let static_pkgs_hash =
            MerkleTree::from_reader(static_pkgs_contents.as_slice()).unwrap().root();

        let system_image_contents = create_system_image_far(Some(static_pkgs_hash.clone()));
        let system_image_hash =
            MerkleTree::from_reader(system_image_contents.as_slice()).unwrap().root();

        mock_artifact_reader
            .append_artifact(&PathBuf::from(static_pkgs_hash.to_string()), static_pkgs_contents);
        mock_artifact_reader
            .append_artifact(&PathBuf::from(system_image_hash.to_string()), system_image_contents);

        let result = collect_static_pkgs(
            hashmap! {
                PKGFS_CMD_DEVMGR_CONFIG_KEY.to_string() => vec![
                    PKGFS_BINARY_PATH.to_string(),
                    system_image_hash.to_string(),
                ],
            },
            Box::new(mock_artifact_reader),
        )
        .unwrap();

        assert_eq!(result.static_pkgs, static_pkgs);
    }

    #[fuchsia::test]
    fn test_some_static_pkgs() {
        let mut mock_artifact_reader = MockArtifactReader::new();

        let alpha_hash = Hash::from([0; HASH_SIZE]);
        let beta_hash = Hash::from([1; HASH_SIZE]);
        let static_pkgs = hashmap! {
            (PackageName::from_str("alpha").unwrap(), Some(PackageVariant::zero())) => alpha_hash.clone(),
            (PackageName::from_str("beta").unwrap(), Some(PackageVariant::zero())) => beta_hash.clone(),
        };
        let static_pkgs_contents = create_static_pkgs_listing(static_pkgs.clone());
        let static_pkgs_hash =
            MerkleTree::from_reader(static_pkgs_contents.as_slice()).unwrap().root();

        let system_image_contents = create_system_image_far(Some(static_pkgs_hash.clone()));
        let system_image_hash =
            MerkleTree::from_reader(system_image_contents.as_slice()).unwrap().root();

        mock_artifact_reader
            .append_artifact(&PathBuf::from(static_pkgs_hash.to_string()), static_pkgs_contents);
        mock_artifact_reader
            .append_artifact(&PathBuf::from(system_image_hash.to_string()), system_image_contents);

        let result = collect_static_pkgs(
            hashmap! {
                PKGFS_CMD_DEVMGR_CONFIG_KEY.to_string() => vec![
                    PKGFS_BINARY_PATH.to_string(),
                    system_image_hash.to_string(),
                ],
            },
            Box::new(mock_artifact_reader),
        )
        .unwrap();

        assert_eq!(result.static_pkgs, static_pkgs);
    }
}
