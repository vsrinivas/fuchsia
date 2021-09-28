// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        devmgr_config::{DevmgrConfigCollection, DevmgrConfigContents},
        static_pkgs::collection::{StaticPkgsCollection, StaticPkgsContents, StaticPkgsError},
    },
    anyhow::{anyhow, Context, Result},
    fuchsia_archive::Reader as FarReader,
    fuchsia_hash::Hash,
    fuchsia_merkle::MerkleTree,
    maplit::hashset,
    scrutiny::model::{collector::DataCollector, model::DataModel},
    scrutiny_config::ModelConfig,
    scrutiny_utils::{
        artifact::{ArtifactReader, FileArtifactReader},
        key_value::parse_key_value,
    },
    std::{
        collections::HashSet,
        io::Cursor,
        path::{Path, PathBuf},
        str::{from_utf8, FromStr},
        sync::Arc,
    },
};

static PKGFS_CMD_DEVMGR_CONFIG_KEY: &str = "zircon.system.pkgfs.cmd";
static PKGFS_BINARY_PATH: &str = "bin/pkgsvr";
static META_FAR_CONTENTS_LISTING_PATH: &str = "meta/contents";
static STATIC_PKGS_LISTING_PATH: &str = "data/static_packages";

struct StaticPkgsData {
    deps: HashSet<String>,
    static_pkgs: StaticPkgsContents,
}

struct ErrorWithDeps {
    pub deps: HashSet<String>,
    pub error: StaticPkgsError,
}

fn path_string<P>(path: &P) -> String
where
    P: AsRef<Path>,
{
    format!("{}", path.as_ref().display())
}

fn path_buf_to_str<'a>(path_buf: &'a PathBuf) -> Result<&'a str> {
    path_buf.to_str().ok_or(anyhow!("Failed to convert path to string: {:#?}", path_buf))
}

fn collect_static_pkgs(
    devmgr_config: DevmgrConfigContents,
    model_config: &ModelConfig,
) -> Result<StaticPkgsData, ErrorWithDeps> {
    let build_path = model_config.build_path();
    let mut artifact_loader = FileArtifactReader::new(&build_path, &build_path);
    // Get system image path from ["bin/pkgsvr", <system-image-hash>] cmd.
    let pkgfs_cmd = devmgr_config.get(PKGFS_CMD_DEVMGR_CONFIG_KEY).ok_or(ErrorWithDeps {
        deps: artifact_loader.get_deps(),
        error: StaticPkgsError::MissingPkgfsCmdEntry,
    })?;
    if pkgfs_cmd.len() != 2 {
        return Err(ErrorWithDeps {
            deps: artifact_loader.get_deps(),
            error: StaticPkgsError::UnexpectedPkgfsCmdLen {
                expected_len: 2,
                actual_len: pkgfs_cmd.len(),
            },
        });
    }
    if &pkgfs_cmd[0] != PKGFS_BINARY_PATH {
        return Err(ErrorWithDeps {
            deps: artifact_loader.get_deps(),
            error: StaticPkgsError::UnexpectedPkgfsCmd {
                expected_cmd: PKGFS_BINARY_PATH.to_string(),
                actual_cmd: pkgfs_cmd[0].clone(),
            },
        });
    }
    if Hash::from_str(&pkgfs_cmd[1]).is_err() {
        return Err(ErrorWithDeps {
            deps: artifact_loader.get_deps(),
            error: StaticPkgsError::MalformedSystemImageHash { actual_hash: pkgfs_cmd[1].clone() },
        });
    }

    // Load blob manifest for subsequent blob-loading operations.
    let blob_manifest_path = model_config.blob_manifest_path();
    let blob_manifest_buffer = artifact_loader
        .read_raw(path_buf_to_str(&blob_manifest_path).map_err(|_| ErrorWithDeps {
            deps: artifact_loader.get_deps(),
            error: StaticPkgsError::BlobManifestPathInvalid {
                blob_manifest_path: path_string(&blob_manifest_path),
            },
        })?)
        .map_err(|err| ErrorWithDeps {
            deps: artifact_loader.get_deps(),
            error: StaticPkgsError::FailedToReadBlobManifest {
                blob_manifest_path: path_string(&blob_manifest_path),
                io_error: err.to_string(),
            },
        })?;
    let blob_manifest_contents = from_utf8(&blob_manifest_buffer).map_err(|err| ErrorWithDeps {
        deps: artifact_loader.get_deps(),
        error: StaticPkgsError::FailedToParseBlobManifest {
            blob_manifest_path: path_string(&blob_manifest_path),
            parse_error: err.to_string(),
        },
    })?;
    let blob_manifest =
        parse_key_value(blob_manifest_contents.to_string()).map_err(|err| ErrorWithDeps {
            deps: artifact_loader.get_deps(),
            error: StaticPkgsError::FailedToParseBlobManifest {
                blob_manifest_path: path_string(&blob_manifest_path),
                parse_error: err.to_string(),
            },
        })?;
    let blob_directory = Path::new(&blob_manifest_path).parent().ok_or_else(|| ErrorWithDeps {
        deps: artifact_loader.get_deps(),
        error: StaticPkgsError::BlobManifestNotInDirectory {
            blob_manifest_path: path_string(&blob_manifest_path),
        },
    })?;

    // Locate system image package.
    let system_image_merkle = &pkgfs_cmd[1];
    let system_image_file =
        blob_manifest.get(system_image_merkle).ok_or_else(|| ErrorWithDeps {
            deps: artifact_loader.get_deps(),
            error: StaticPkgsError::SystemImageNotFoundInManifest {
                system_image_merkle: system_image_merkle.clone(),
                blob_manifest_path: path_string(&blob_manifest_path),
            },
        })?;
    let system_image_path = blob_directory.join(system_image_file);

    // Read system image package and verify its merkle.
    let system_image_pkg_buffer = artifact_loader
        .read_raw(path_buf_to_str(&system_image_path).map_err(|_| ErrorWithDeps {
            deps: artifact_loader.get_deps(),
            error: StaticPkgsError::SystemImagePathInvalid {
                system_image_path: path_string(&system_image_path),
            },
        })?)
        .map_err(|err| ErrorWithDeps {
            deps: artifact_loader.get_deps(),
            error: StaticPkgsError::FailedToReadSystemImage {
                system_image_path: path_string(&system_image_path),
                io_error: err.to_string(),
            },
        })?;
    let computed_system_image_merkle =
        MerkleTree::from_reader(Cursor::new(&system_image_pkg_buffer))
            .map_err(|err| ErrorWithDeps {
                deps: artifact_loader.get_deps(),
                error: StaticPkgsError::FailedToReadSystemImage {
                    system_image_path: path_string(&system_image_path),
                    io_error: err.to_string(),
                },
            })?
            .root()
            .to_string();
    if &computed_system_image_merkle != system_image_merkle {
        return Err(ErrorWithDeps {
            deps: artifact_loader.get_deps(),
            error: StaticPkgsError::FailedToVerifySystemImage {
                expected_merkle_root: system_image_merkle.clone(),
                computed_merkle_root: computed_system_image_merkle,
            },
        });
    }

    // Parse system image.
    let mut system_image_far =
        FarReader::new(Cursor::new(&system_image_pkg_buffer)).map_err(|err| ErrorWithDeps {
            deps: artifact_loader.get_deps(),
            error: StaticPkgsError::FailedToParseSystemImage {
                system_image_path: path_string(&system_image_path),
                parse_error: err.to_string(),
            },
        })?;

    // Extract "data/static_packages" hash from "meta/contents" file.
    let system_image_data_contents = parse_key_value(
        from_utf8(&system_image_far.read_file(META_FAR_CONTENTS_LISTING_PATH).map_err(|err| {
            ErrorWithDeps {
                deps: artifact_loader.get_deps(),
                error: StaticPkgsError::FailedToReadSystemImageMetaFile {
                    system_image_path: path_string(&system_image_path),
                    file_name: META_FAR_CONTENTS_LISTING_PATH.to_string(),
                    far_error: err.to_string(),
                },
            }
        })?)
        .map_err(|err| ErrorWithDeps {
            deps: artifact_loader.get_deps(),
            error: StaticPkgsError::FailedToDecodeSystemImageMetaFile {
                system_image_path: path_string(&system_image_path),
                file_name: META_FAR_CONTENTS_LISTING_PATH.to_string(),
                utf8_error: err.to_string(),
            },
        })?
        .to_string(),
    )
    .map_err(|err| ErrorWithDeps {
        deps: artifact_loader.get_deps(),
        error: StaticPkgsError::FailedToParseSystemImageMetaFile {
            system_image_path: path_string(&system_image_path),
            file_name: META_FAR_CONTENTS_LISTING_PATH.to_string(),
            parse_error: err.to_string(),
        },
    })?;
    let static_pkgs_merkle =
        system_image_data_contents.get(STATIC_PKGS_LISTING_PATH).ok_or_else(|| ErrorWithDeps {
            deps: artifact_loader.get_deps(),
            error: StaticPkgsError::MissingStaticPkgsEntry {
                system_image_path: path_string(&system_image_path),
                file_name: STATIC_PKGS_LISTING_PATH.to_string(),
            },
        })?;

    // Check static pkgs merkle format; determine path to static pkgs file.
    if Hash::from_str(static_pkgs_merkle).is_err() {
        return Err(ErrorWithDeps {
            deps: artifact_loader.get_deps(),
            error: StaticPkgsError::MalformedStaticPkgsHash {
                actual_hash: static_pkgs_merkle.clone(),
            },
        });
    }
    let static_pkgs_path =
        blob_directory.join(&blob_manifest.get(static_pkgs_merkle).ok_or_else(|| {
            ErrorWithDeps {
                deps: artifact_loader.get_deps(),
                error: StaticPkgsError::StaticPkgsNotFoundInManifest {
                    static_pkgs_merkle: static_pkgs_merkle.clone(),
                    blob_manifest_path: path_string(&blob_manifest_path),
                },
            }
        })?);

    // Read static packages index, check its merkle, and parse it.
    let static_pkgs_buffer = artifact_loader
        .read_raw(path_buf_to_str(&static_pkgs_path).map_err(|_| ErrorWithDeps {
            deps: artifact_loader.get_deps(),
            error: StaticPkgsError::StaticPkgsPathInvalid {
                static_pkgs_path: path_string(&static_pkgs_path),
            },
        })?)
        .map_err(|err| ErrorWithDeps {
            deps: artifact_loader.get_deps(),
            error: StaticPkgsError::FailedToReadStaticPkgs {
                static_pkgs_path: path_string(&static_pkgs_path),
                io_error: err.to_string(),
            },
        })?;
    let computed_static_pkgs_merkle = MerkleTree::from_reader(Cursor::new(&static_pkgs_buffer))
        .map_err(|err| ErrorWithDeps {
            deps: artifact_loader.get_deps(),
            error: StaticPkgsError::FailedToReadStaticPkgs {
                static_pkgs_path: path_string(&static_pkgs_path),
                io_error: err.to_string(),
            },
        })?
        .root()
        .to_string();
    if &computed_static_pkgs_merkle != static_pkgs_merkle {
        return Err(ErrorWithDeps {
            deps: artifact_loader.get_deps(),
            error: StaticPkgsError::FailedToVerifyStaticPkgs {
                expected_merkle_root: path_string(&static_pkgs_path),
                computed_merkle_root: computed_static_pkgs_merkle,
            },
        });
    }
    let static_pkgs_contents = from_utf8(&static_pkgs_buffer).map_err(|err| ErrorWithDeps {
        deps: artifact_loader.get_deps(),
        error: StaticPkgsError::FailedToParseStaticPkgs {
            static_pkgs_path: path_string(&static_pkgs_path),
            parse_error: err.to_string(),
        },
    })?;
    let static_pkgs =
        parse_key_value(static_pkgs_contents.to_string()).map_err(|err| ErrorWithDeps {
            deps: artifact_loader.get_deps(),
            error: StaticPkgsError::FailedToParseStaticPkgs {
                static_pkgs_path: path_string(&static_pkgs_path),
                parse_error: err.to_string(),
            },
        })?;

    Ok(StaticPkgsData { deps: artifact_loader.get_deps(), static_pkgs })
}

#[derive(Default)]
pub struct StaticPkgsCollector;

impl DataCollector for StaticPkgsCollector {
    fn collect(&self, model: Arc<DataModel>) -> Result<()> {
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

        let data: StaticPkgsCollection = match collect_static_pkgs(devmgr_config, model.config()) {
            Ok(static_pkgs_data) => {
                deps.extend(static_pkgs_data.deps.into_iter());
                StaticPkgsCollection {
                    static_pkgs: Some(static_pkgs_data.static_pkgs),
                    deps,
                    errors: vec![],
                }
            }
            Err(err) => {
                StaticPkgsCollection { static_pkgs: None, deps: err.deps, errors: vec![err.error] }
            }
        };
        model.set(data).context("Static packages collector failed to store result in model")
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{
            StaticPkgsCollector, META_FAR_CONTENTS_LISTING_PATH, PKGFS_BINARY_PATH,
            PKGFS_CMD_DEVMGR_CONFIG_KEY, STATIC_PKGS_LISTING_PATH,
        },
        crate::{
            devmgr_config::{DevmgrConfigCollection, DevmgrConfigError},
            static_pkgs::collection::{StaticPkgsCollection, StaticPkgsError},
        },
        anyhow::{anyhow, Context, Result},
        fuchsia_archive::write as far_write,
        fuchsia_merkle::MerkleTree,
        maplit::{btreemap, hashmap, hashset},
        scrutiny::model::collector::DataCollector,
        scrutiny_testing::fake::*,
        std::{
            collections::{BTreeMap, HashMap},
            fs::File,
            io::{Read, Write},
            path::{Path, PathBuf},
            sync::Arc,
        },
    };

    static VALID_MERKLE_HASH: &str =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

    fn create_blob_manifest(
        blob_manifest_path: &PathBuf,
        merkles_to_paths: HashMap<String, String>,
    ) -> Result<()> {
        let mut blob_manifest =
            File::create(blob_manifest_path).context("Failed to create blob manifest")?;
        for (merkle, path) in merkles_to_paths.into_iter() {
            write!(blob_manifest, "{}={}\n", merkle, path)
                .context("Failed to write to blob manifest")?;
        }
        blob_manifest.flush().context("Failed to flush writes to blob manifest")?;
        Ok(())
    }

    fn create_system_image_far<P>(
        system_image_path: &P,
        static_pkgs_merkle: Option<String>,
    ) -> Result<String>
    where
        P: AsRef<Path>,
    {
        let system_image_far =
            File::create(system_image_path).context("Failed to create system image far")?;
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
        far_write(system_image_far, path_content_map)
            .context("Failed to write system image far file")?;
        let system_image_far = File::open(system_image_path)
            .context("Failed to reopen system image far after write")?;
        let system_image_merkle_str: String = MerkleTree::from_reader(system_image_far)
            .context("Failed to read back system image far for merkle tree construction")?
            .root()
            .to_string();
        Ok(system_image_merkle_str)
    }

    fn create_static_pkgs_listing<P>(
        static_pkgs_path: &P,
        static_pkgs_path_to_merkle: HashMap<String, String>,
    ) -> Result<String>
    where
        P: AsRef<Path>,
    {
        let mut static_pkgs_listing =
            File::create(static_pkgs_path).context("Failed to create static packages listing")?;
        for (path, merkle) in static_pkgs_path_to_merkle.into_iter() {
            write!(static_pkgs_listing, "{}={}\n", path, merkle)
                .context("Failed to write to static packages listing")?;
        }
        static_pkgs_listing.flush().context("Failed to flush writes to static packages listing")?;
        let static_pkgs_listing = File::open(static_pkgs_path)
            .context("Failed to reopen static packages listing after write")?;
        let static_pkgs_merkle_str: String = MerkleTree::from_reader(static_pkgs_listing)
            .context("Failed to read back static packages listing for merkle tree construction")?
            .root()
            .to_string();
        Ok(static_pkgs_merkle_str)
    }

    #[test]
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

    #[test]
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

    #[test]
    fn test_err_from_devmgr_config() -> Result<()> {
        let model = fake_data_model();
        // Result from devmgr config contains no config and an error.
        let devmgr_config_result = DevmgrConfigCollection {
            deps: hashset! {},
            devmgr_config: None,
            errors: vec![DevmgrConfigError::FailedToOpenZbi {
                zbi_path: "fuchsia.zbi".to_string(),
                io_error: "Failed to open file at fuchsia.zbi".to_string(),
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

    #[test]
    fn test_missing_pkgfs_cmd_entry() -> Result<()> {
        let model = fake_data_model();
        let devmgr_config_result = DevmgrConfigCollection {
            deps: hashset! {},
            // Empty devmgr config contains no pkgfs cmd entry.
            devmgr_config: Some(HashMap::new()),
            errors: vec![],
        };
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
            StaticPkgsError::MissingPkgfsCmdEntry => Ok(()),
            err => Err(anyhow!("Unexpected error: {}", err.to_string())),
        }
    }

    #[test]
    fn test_pkgfs_cmd_too_short() -> Result<()> {
        let model = fake_data_model();
        let devmgr_config_result = DevmgrConfigCollection {
            deps: hashset! {},
            // Too few arguments for launching pkgsvr.
            devmgr_config: Some(hashmap! {
                PKGFS_CMD_DEVMGR_CONFIG_KEY.to_string() => vec![PKGFS_BINARY_PATH.to_string()]
            }),
            errors: vec![],
        };
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
            StaticPkgsError::UnexpectedPkgfsCmdLen { expected_len: 2, actual_len: 1 } => Ok(()),
            err => Err(anyhow!("Unexpected error: {}", err.to_string())),
        }
    }

    #[test]
    fn test_pkgfs_cmd_too_long() -> Result<()> {
        let model = fake_data_model();
        let devmgr_config_result = DevmgrConfigCollection {
            deps: hashset! {},
            // Too many arguments for launching pkgsvr.
            devmgr_config: Some(hashmap! {
                PKGFS_CMD_DEVMGR_CONFIG_KEY.to_string() => vec![
                    PKGFS_BINARY_PATH.to_string(), "param1".to_string(), "param2".to_string(),
                ]
            }),
            errors: vec![],
        };
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
            StaticPkgsError::UnexpectedPkgfsCmdLen { expected_len: 2, actual_len: 3 } => Ok(()),
            err => Err(anyhow!("Unexpected error: {}", err.to_string())),
        }
    }

    #[test]
    fn test_bad_pkgfs_cmd() -> Result<()> {
        let bad_cmd_name = "unexpected/pkgsvr/path";
        let model = fake_data_model();
        let devmgr_config_result = DevmgrConfigCollection {
            deps: hashset! {},
            // Unexpected path to pkgsvr.
            devmgr_config: Some(hashmap! {
                PKGFS_CMD_DEVMGR_CONFIG_KEY.to_string() => vec![
                    bad_cmd_name.to_string(), VALID_MERKLE_HASH.to_string(),
                ]
            }),
            errors: vec![],
        };
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
            StaticPkgsError::UnexpectedPkgfsCmd { .. } => Ok(()),
            err => Err(anyhow!("Unexpected error: {}", err.to_string())),
        }
    }

    #[test]
    fn test_invalid_system_image_merkle() -> Result<()> {
        let bad_merkle_root = "I am not a merkle root";
        let model = fake_data_model();
        let devmgr_config_result = DevmgrConfigCollection {
            deps: hashset! {},
            // System image merkle root parameter is not a valid merkle hash.
            devmgr_config: Some(hashmap! {
                PKGFS_CMD_DEVMGR_CONFIG_KEY.to_string() => vec![
                    PKGFS_BINARY_PATH.to_string(), bad_merkle_root.to_string(),
                ]
            }),
            errors: vec![],
        };
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
            StaticPkgsError::MalformedSystemImageHash { .. } => Ok(()),
            err => Err(anyhow!("Unexpected error: {}", err.to_string())),
        }
    }

    #[test]
    fn test_missing_system_image() -> Result<()> {
        let model = fake_data_model();
        // Empty blob manifest: System image will not be found.
        create_blob_manifest(&model.config().blob_manifest_path(), HashMap::new())?;
        let devmgr_config_result = DevmgrConfigCollection {
            deps: hashset! {},
            devmgr_config: Some(hashmap! {
                PKGFS_CMD_DEVMGR_CONFIG_KEY.to_string() => vec![
                    PKGFS_BINARY_PATH.to_string(), VALID_MERKLE_HASH.to_string(),
                ]
            }),
            errors: vec![],
        };
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
            StaticPkgsError::SystemImageNotFoundInManifest { .. } => Ok(()),
            err => Err(anyhow!("Unexpected error: {}", err.to_string())),
        }
    }

    #[test]
    fn test_incorrect_system_image_merkle() -> Result<()> {
        let model = fake_data_model();
        let system_image_name = "system_image.far";
        let system_image_path = model
            .config()
            .blob_manifest_path()
            .parent()
            .ok_or(anyhow!("Blob manifest path is not in a directory"))?
            .join(system_image_name);
        let system_image_merkle = create_system_image_far(&system_image_path, None)?;
        // System image filed under incorrect hash: `VALID_MERKLE_HASH`.
        assert!(&system_image_merkle != VALID_MERKLE_HASH);
        create_blob_manifest(
            &model.config().blob_manifest_path(),
            hashmap! {
                VALID_MERKLE_HASH.to_string() => system_image_name.to_string(),
            },
        )?;
        let devmgr_config_result = DevmgrConfigCollection {
            deps: hashset! {},
            // Config designates `VALID_MERKLE_HASH`, which maps to a system
            // image, but is not the correct hash for the system image.
            devmgr_config: Some(hashmap! {
                PKGFS_CMD_DEVMGR_CONFIG_KEY.to_string() => vec![
                    PKGFS_BINARY_PATH.to_string(), VALID_MERKLE_HASH.to_string(),
                ]
            }),
            errors: vec![],
        };
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
            StaticPkgsError::FailedToVerifySystemImage { .. } => Ok(()),
            err => Err(anyhow!("Unexpected error: {}", err.to_string())),
        }
    }

    #[test]
    fn test_missing_static_pkgs() -> Result<()> {
        let model = fake_data_model();
        let system_image_name = "system_image.far";
        let system_image_path = model
            .config()
            .blob_manifest_path()
            .parent()
            .ok_or(anyhow!("Blob manifest path is not in a directory"))?
            .join(system_image_name);
        // `None` below: Do not include static pkgs listing in system image far.
        let system_image_merkle = create_system_image_far(&system_image_path, None)?;
        create_blob_manifest(
            &model.config().blob_manifest_path(),
            hashmap! {
                system_image_merkle.clone() => system_image_name.to_string(),
            },
        )?;
        let devmgr_config_result = DevmgrConfigCollection {
            deps: hashset! {},
            devmgr_config: Some(hashmap! {
                PKGFS_CMD_DEVMGR_CONFIG_KEY.to_string() => vec![
                    PKGFS_BINARY_PATH.to_string(), system_image_merkle,
                ]
            }),
            errors: vec![],
        };
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
            StaticPkgsError::MissingStaticPkgsEntry { .. } => Ok(()),
            err => Err(anyhow!("Unexpected error: {}", err.to_string())),
        }
    }

    #[test]
    fn test_static_pkgs_not_found() -> Result<()> {
        let model = fake_data_model();
        let system_image_name = "system_image.far";
        let system_image_path = model
            .config()
            .blob_manifest_path()
            .parent()
            .ok_or(anyhow!("Blob manifest path is not in a directory"))?
            .join(system_image_name);
        // Provide valid well-formed merkle that does not appear in blob
        // manifest as static packages merkle.
        let system_image_merkle =
            create_system_image_far(&system_image_path, Some(VALID_MERKLE_HASH.to_string()))?;
        create_blob_manifest(
            &model.config().blob_manifest_path(),
            hashmap! {
                system_image_merkle.clone() => system_image_name.to_string(),
            },
        )?;
        let devmgr_config_result = DevmgrConfigCollection {
            deps: hashset! {},
            devmgr_config: Some(hashmap! {
                PKGFS_CMD_DEVMGR_CONFIG_KEY.to_string() => vec![
                    PKGFS_BINARY_PATH.to_string(), system_image_merkle,
                ]
            }),
            errors: vec![],
        };
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
            StaticPkgsError::StaticPkgsNotFoundInManifest { .. } => Ok(()),
            err => Err(anyhow!("Unexpected error: {}", err.to_string())),
        }
    }

    #[test]
    fn test_incorrect_static_pkgs_merkle() -> Result<()> {
        let model = fake_data_model();
        let blob_manifest_path = model.config().blob_manifest_path();
        let blob_dir = blob_manifest_path
            .parent()
            .ok_or(anyhow!("Blob manifest path is not in a directory"))?;
        let static_pkgs_name = "static_packages";
        let static_pkgs_path = blob_dir.clone().join(static_pkgs_name);
        let static_pkgs_merkle = create_static_pkgs_listing(&static_pkgs_path, hashmap! {})?;
        let system_image_name = "system_image.far";
        let system_image_path = blob_dir.clone().join(system_image_name);
        // System image designates `VALID_MERKLE_HASH` as "where to find static
        // pkgs listing". This value maps to static pkgs in the blob manifest,
        // but is not the correct hash for the static pkgs blob.
        let system_image_merkle =
            create_system_image_far(&system_image_path, Some(VALID_MERKLE_HASH.to_string()))?;
        // Static pkgs filed under incorrect hash: `VALID_MERKLE_HASH`.
        assert!(&static_pkgs_merkle != VALID_MERKLE_HASH);
        create_blob_manifest(
            &model.config().blob_manifest_path(),
            hashmap! {
                VALID_MERKLE_HASH.to_string() => static_pkgs_name.to_string(),
                system_image_merkle.clone() => system_image_name.to_string(),
            },
        )?;
        let devmgr_config_result = DevmgrConfigCollection {
            deps: hashset! {},
            devmgr_config: Some(hashmap! {
                PKGFS_CMD_DEVMGR_CONFIG_KEY.to_string() => vec![
                    PKGFS_BINARY_PATH.to_string(), system_image_merkle,
                ]
            }),
            errors: vec![],
        };
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
            StaticPkgsError::FailedToVerifyStaticPkgs { .. } => Ok(()),
            err => Err(anyhow!("Unexpected error: {}", err.to_string())),
        }
    }

    #[test]
    fn test_empty_static_pkgs() -> Result<()> {
        let model = fake_data_model();
        let blob_manifest_path = model.config().blob_manifest_path();
        let blob_dir = blob_manifest_path
            .parent()
            .ok_or(anyhow!("Blob manifest path is not in a directory"))?;
        let static_pkgs_name = "static_packages";
        let static_pkgs_path = blob_dir.clone().join(static_pkgs_name);
        let static_pkgs_merkle = create_static_pkgs_listing(&static_pkgs_path, hashmap! {})?;
        let system_image_name = "system_image.far";
        let system_image_path = blob_dir.clone().join(system_image_name);
        let system_image_merkle =
            create_system_image_far(&system_image_path, Some(static_pkgs_merkle.clone()))
                .context("Failed to create system image far")?;
        create_blob_manifest(
            &model.config().blob_manifest_path(),
            hashmap! {
                static_pkgs_merkle.clone() => static_pkgs_name.to_string(),
                system_image_merkle.clone() => system_image_name.to_string(),
            },
        )?;
        let devmgr_config_result = DevmgrConfigCollection {
            deps: hashset! {},
            devmgr_config: Some(hashmap! {
                PKGFS_CMD_DEVMGR_CONFIG_KEY.to_string() => vec![
                    PKGFS_BINARY_PATH.to_string(), system_image_merkle,
                ]
            }),
            errors: vec![],
        };
        model.clone().set(devmgr_config_result).context("Failed to store devmgr config result")?;
        let collector = StaticPkgsCollector::default();
        collector
            .collect(model.clone())
            .context("Failed to return cleanly when data missing from model")?;
        let result: Arc<StaticPkgsCollection> =
            model.get().context("Failed to get static pkgs data put to model")?;
        assert_eq!(result.static_pkgs, Some(hashmap! {}));
        assert_eq!(result.errors.len(), 0);
        Ok(())
    }

    #[test]
    fn test_some_static_pkgs() -> Result<()> {
        let model = fake_data_model();
        let blob_manifest_path = model.config().blob_manifest_path();
        let blob_dir = blob_manifest_path
            .parent()
            .ok_or(anyhow!("Blob manifest path is not in a directory"))?;
        let static_pkgs_name = "static_packages";
        let static_pkgs_path = blob_dir.clone().join(static_pkgs_name);
        let static_pkgs = hashmap! {
            "alpha/0".to_string() => VALID_MERKLE_HASH.to_string(),
            "beta/0".to_string() => VALID_MERKLE_HASH.to_string()
        };
        let static_pkgs_merkle =
            create_static_pkgs_listing(&static_pkgs_path, static_pkgs.clone())?;
        let system_image_name = "system_image.far";
        let system_image_path = blob_dir.clone().join(system_image_name);
        let system_image_merkle =
            create_system_image_far(&system_image_path, Some(static_pkgs_merkle.clone()))?;
        create_blob_manifest(
            &model.config().blob_manifest_path(),
            hashmap! {
                static_pkgs_merkle.clone() => static_pkgs_name.to_string(),
                system_image_merkle.clone() => system_image_name.to_string(),
            },
        )?;
        let devmgr_config_result = DevmgrConfigCollection {
            deps: hashset! {},
            devmgr_config: Some(hashmap! {
                PKGFS_CMD_DEVMGR_CONFIG_KEY.to_string() => vec![
                    PKGFS_BINARY_PATH.to_string(), system_image_merkle,
                ]
            }),
            errors: vec![],
        };
        model.clone().set(devmgr_config_result).context("Failed to store devmgr config result")?;
        let collector = StaticPkgsCollector::default();
        collector
            .collect(model.clone())
            .context("Failed to return cleanly when data missing from model")?;
        let result: Arc<StaticPkgsCollection> =
            model.get().context("Failed to get static pkgs data put to model")?;
        assert_eq!(result.static_pkgs, Some(static_pkgs));
        assert_eq!(result.errors.len(), 0);
        Ok(())
    }
}
