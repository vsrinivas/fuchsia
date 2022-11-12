// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::devmgr_config::collection::{
        DevmgrConfigCollection, DevmgrConfigContents, DevmgrConfigError, DevmgrConfigParseError,
    },
    anyhow::{Context, Result},
    scrutiny::model::{collector::DataCollector, model::DataModel},
    scrutiny_utils::{
        artifact::{ArtifactReader, BlobFsArtifactReader},
        bootfs::BootfsReader,
        package::{open_update_package, read_content_blob},
        zbi::{ZbiReader, ZbiType},
    },
    std::{
        collections::{HashMap, HashSet},
        path::Path,
        str::from_utf8,
        sync::Arc,
    },
};

// Load the devmgr configuration file by following update package -> zbi -> bootfs -> devmgr config
// file. The zbi is assumed to be stored at the file path "zbi.signed" or "zbi" in the update
// package, and `devmgr_config_path` is a path in bootfs embedded in the ZBI.
//
// The purpose of loading the devmgr config is to parse bootstrapping information such as the system
// image merkle root used to bootstrap the software delivery stack.
//
// TODO(fxbug.dev/98030): This function should support the update package -> images package -> ...
// flow.
fn load_devmgr_config<P1: AsRef<Path>, P2: AsRef<Path>>(
    update_package_path: P1,
    artifact_reader: &mut Box<dyn ArtifactReader>,
    devmgr_config_path: P2,
) -> Result<DevmgrConfigContents, DevmgrConfigError> {
    let devmgr_config_path_ref = devmgr_config_path.as_ref();
    let devmgr_config_path_str = devmgr_config_path_ref.to_str().ok_or_else(|| {
        DevmgrConfigError::FailedToParseDevmgrConfigPath {
            devmgr_config_path: devmgr_config_path_ref.to_path_buf(),
        }
    })?;
    let update_package_path_ref = update_package_path.as_ref();
    let mut far_reader =
        open_update_package(update_package_path_ref, artifact_reader).map_err(|err| {
            DevmgrConfigError::FailedToOpenUpdatePackage {
                update_package_path: update_package_path_ref.to_path_buf(),
                io_error: format!("{:?}", err),
            }
        })?;
    let zbi_buffer = read_content_blob(&mut far_reader, artifact_reader, "zbi.signed").or_else(
        |signed_err| {
            read_content_blob(&mut far_reader, artifact_reader, "zbi").map_err(|err| {
                DevmgrConfigError::FailedToReadZbi {
                    update_package_path: update_package_path_ref.to_path_buf(),
                    io_error: format!("{:?}\n{:?}", signed_err, err),
                }
            })
        },
    )?;
    let mut reader = ZbiReader::new(zbi_buffer);
    let zbi_sections = reader.parse().map_err(|zbi_error| DevmgrConfigError::FailedToParseZbi {
        update_package_path: update_package_path_ref.to_path_buf(),
        zbi_error: zbi_error.to_string(),
    })?;

    for section in zbi_sections.iter() {
        if section.section_type == ZbiType::StorageBootfs {
            let mut bootfs_reader = BootfsReader::new(section.buffer.clone());
            let bootfs_data = bootfs_reader.parse().map_err(|bootfs_error| {
                DevmgrConfigError::FailedToParseBootfs {
                    update_package_path: update_package_path_ref.to_path_buf(),
                    bootfs_error: bootfs_error.to_string(),
                }
            })?;
            for (file, data) in bootfs_data.iter() {
                if file == devmgr_config_path_str {
                    return Ok(parse_devmgr_config_contents(from_utf8(&data).map_err(
                        |utf8_error| DevmgrConfigError::FailedToParseUtf8DevmgrConfig {
                            update_package_path: update_package_path_ref.to_path_buf(),
                            devmgr_config_path: devmgr_config_path_ref.to_path_buf(),
                            utf8_error: utf8_error.to_string(),
                        },
                    )?)
                    .map_err(|parse_error| {
                        DevmgrConfigError::FailedToParseDevmgrConfigFormat {
                            update_package_path: update_package_path_ref.to_path_buf(),
                            devmgr_config_path: devmgr_config_path_ref.to_path_buf(),
                            parse_error,
                        }
                    })?);
                }
            }
        }
    }
    Err(DevmgrConfigError::FailedToLocateDevmgrConfig {
        update_package_path: update_package_path_ref.to_path_buf(),
        devmgr_config_path: devmgr_config_path_ref.to_path_buf(),
    })
}

fn parse_devmgr_config_contents(
    str_contents: &str,
) -> Result<DevmgrConfigContents, DevmgrConfigParseError> {
    let mut line_nos: HashMap<&str, (usize, &str)> = HashMap::new();
    let mut contents: DevmgrConfigContents = HashMap::new();
    let lines: Vec<&str> = str_contents.trim_matches(|ch| ch == '\n').split("\n").collect();
    for line_no in 0..lines.len() {
        let line_contents = lines[line_no];
        let kv: Vec<&str> = line_contents.split("=").collect();
        if kv.len() != 2 {
            return Err(DevmgrConfigParseError::FailedToParseKeyValue {
                line_no: line_no + 1,
                line_contents: line_contents.to_string(),
            });
        }
        if let Some((previous_line_no, previous_line_contents)) = line_nos.get(&kv[0]) {
            return Err(DevmgrConfigParseError::RepeatedKey {
                line_no: line_no + 1,
                line_contents: line_contents.to_string(),
                previous_line_no: previous_line_no + 1,
                previous_line_contents: previous_line_contents.to_string(),
            });
        }
        line_nos.insert(kv[0], (line_no + 1, line_contents));
        contents.insert(kv[0].to_string(), kv[1].trim().split("+").map(String::from).collect());
    }
    Ok(contents)
}

#[derive(Default)]
pub struct DevmgrConfigCollector;

impl DataCollector for DevmgrConfigCollector {
    fn collect(&self, model: Arc<DataModel>) -> Result<()> {
        let model_config = model.config();
        let build_path = model_config.build_path();
        let update_package_path = model_config.update_package_path();
        let blobfs_paths = model_config.blobfs_paths();
        let devmgr_config_path = model_config.devmgr_config_path();

        // Initialize artifact reader; early exit on initialization failure.
        let mut artifact_reader: Box<dyn ArtifactReader> = match BlobFsArtifactReader::try_compound(
            &build_path,
            model_config.tmp_dir_path().as_ref(),
            &blobfs_paths,
        )
        .map_err(|err| DevmgrConfigError::FailedToOpenBlobfs {
            build_path: build_path.clone(),
            blobfs_paths: blobfs_paths.clone(),
            blobfs_error: format!("{:?}", err),
        }) {
            Ok(compound_artifact_reader) => Box::new(compound_artifact_reader),
            Err(err) => {
                model.set(DevmgrConfigCollection {
                        devmgr_config: None,
                        deps: HashSet::new(),
                        errors: vec![err],
                    })
                    .with_context(|| { format!(
                        "Failed to initialize artifact reader for devmgr config collector with build path {:?}, blobfs paths {:?}",
                        build_path, blobfs_paths,
                    )})?;
                return Ok(());
            }
        };

        // Execute query using deps-tracking artifact reader.
        let result =
            load_devmgr_config(&update_package_path, &mut artifact_reader, &devmgr_config_path);

        // Store result in model.
        model
            .set(match result {
                Ok(devmgr_config) => DevmgrConfigCollection {
                    devmgr_config: Some(devmgr_config),
                    deps: artifact_reader.get_deps(),
                    errors: vec![],
                },
                Err(err) => DevmgrConfigCollection {
                    devmgr_config: None,
                    deps: artifact_reader.get_deps(),
                    errors: vec![err],
                },
            })
            .with_context(|| { format!(
                "Failed to collect data from devmgr config bootfs:{:?} in ZBI from update package at {:?}",
                devmgr_config_path, update_package_path,
            )})?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {super::parse_devmgr_config_contents, maplit::hashmap};

    #[test]
    fn test_empty() {
        assert!(parse_devmgr_config_contents("").is_err());
    }

    #[test]
    fn test_one() {
        assert_eq!(
            parse_devmgr_config_contents("a=a").unwrap(),
            hashmap! {"a".to_string() => vec!["a".to_string()]}
        );
    }

    #[test]
    fn test_multiple_keys() {
        assert_eq!(
            parse_devmgr_config_contents(
                "a=a
b=a"
            )
            .unwrap(),
            hashmap! {
                "a".to_string() => vec!["a".to_string()], "b".to_string() => vec!["a".to_string()]
            }
        );
    }

    #[test]
    fn test_duplicate_keys() {
        assert!(parse_devmgr_config_contents(
            "a=a
a=a"
        )
        .is_err());
    }

    #[test]
    fn test_multiple_values() {
        assert_eq!(
            parse_devmgr_config_contents("a=a+b+c+d").unwrap(),
            hashmap! {
                "a".to_string() => vec![
                    "a".to_string(), "b".to_string(), "c".to_string(), "d".to_string()
                ]
            }
        );
    }

    #[test]
    fn test_plus_containing_key() {
        assert_eq!(
            parse_devmgr_config_contents("c++=u+a+f").unwrap(),
            hashmap! {"c++".to_string() => vec!["u".to_string(), "a".to_string(), "f".to_string()]}
        );
    }

    #[test]
    fn test_value_double_plus() {
        assert_eq!(
            parse_devmgr_config_contents("a=a++a").unwrap(),
            hashmap! {"a".to_string() => vec!["a".to_string(), "".to_string(), "a".to_string()]}
        );
    }

    #[test]
    fn test_value_whitespace() {
        assert_eq!(
            parse_devmgr_config_contents("a=a+ a +a").unwrap(),
            hashmap! {"a".to_string() => vec!["a".to_string(), " a ".to_string(), "a".to_string()]}
        );
    }

    #[test]
    fn test_too_many_eq() {
        assert!(parse_devmgr_config_contents("a=b=c").is_err());
    }

    #[test]
    fn test_too_few_eq() {
        assert!(parse_devmgr_config_contents("a").is_err());
    }

    #[test]
    fn test_leading_newlines() {
        assert_eq!(
            parse_devmgr_config_contents(
                "

a=a"
            )
            .unwrap(),
            hashmap! {"a".to_string() => vec!["a".to_string()]}
        );
    }

    #[test]
    fn test_trailing_newlines() {
        assert_eq!(
            parse_devmgr_config_contents(
                "a=a

"
            )
            .unwrap(),
            hashmap! {"a".to_string() => vec!["a".to_string()]}
        );
    }

    #[test]
    fn test_leading_trailing_newlines() {
        assert_eq!(
            parse_devmgr_config_contents(
                "

a=a

"
            )
            .unwrap(),
            hashmap! {"a".to_string() => vec!["a".to_string()]}
        );
    }

    #[test]
    fn test_leading_whitespace() {
        assert_eq!(
            parse_devmgr_config_contents(
                "

  a=a

"
            )
            .unwrap(),
            hashmap! {"  a".to_string() => vec!["a".to_string()]}
        );
    }

    #[test]
    fn test_unicode() {
        assert_eq!(
            parse_devmgr_config_contents("🙂=🍞+≈+∔+幸せ").unwrap(),
            hashmap! {
                "🙂".to_string() => vec![
                    "🍞".to_string(), "≈".to_string(), "∔".to_string(), "幸せ".to_string()
                ]
            }
        );
    }
}
