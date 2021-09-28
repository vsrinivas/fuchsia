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
        artifact::{ArtifactReader, FileArtifactReader},
        bootfs::BootfsReader,
        zbi::{ZbiReader, ZbiType},
    },
    std::{collections::HashMap, str::from_utf8, sync::Arc},
};

fn load_devmgr_config<'a>(
    artifact_reader: &'a mut dyn ArtifactReader,
    zbi_path: &'a str,
    devmgr_config_path: &'a str,
) -> Result<DevmgrConfigContents, DevmgrConfigError> {
    let zbi_buffer =
        artifact_reader.read_raw(zbi_path).map_err(|err| DevmgrConfigError::FailedToReadZbi {
            zbi_path: zbi_path.to_string(),
            io_error: format!("{:?}", err),
        })?;
    let mut reader = ZbiReader::new(zbi_buffer);
    let zbi_sections = reader.parse().map_err(|zbi_error| DevmgrConfigError::FailedToParseZbi {
        zbi_path: zbi_path.to_string(),
        zbi_error: zbi_error.to_string(),
    })?;

    for section in zbi_sections.iter() {
        if section.section_type == ZbiType::StorageBootfs {
            let mut bootfs_reader = BootfsReader::new(section.buffer.clone());
            let bootfs_data = bootfs_reader.parse().map_err(|bootfs_error| {
                DevmgrConfigError::FailedToParseBootfs {
                    zbi_path: zbi_path.to_string(),
                    bootfs_error: bootfs_error.to_string(),
                }
            })?;
            for (file, data) in bootfs_data.iter() {
                if file == &devmgr_config_path {
                    return Ok(parse_devmgr_config_contents(from_utf8(&data).map_err(
                        |utf8_error| DevmgrConfigError::FailedToParseUtf8DevmgrConfig {
                            zbi_path: zbi_path.to_string(),
                            devmgr_config_path: devmgr_config_path.to_string(),
                            utf8_error: utf8_error.to_string(),
                        },
                    )?)
                    .map_err(|parse_error| {
                        DevmgrConfigError::FailedToParseDevmgrConfigFormat {
                            zbi_path: zbi_path.to_string(),
                            devmgr_config_path: devmgr_config_path.to_string(),
                            parse_error,
                        }
                    })?);
                }
            }
        }
    }
    Err(DevmgrConfigError::FailedToLocateDevmgrConfig {
        zbi_path: zbi_path.to_string(),
        devmgr_config_path: devmgr_config_path.to_string(),
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
        let build_path = model.config().build_path();
        let zbi_path_string = model.config().zbi_path();
        let devmgr_config_path = model.config().devmgr_config_path();
        let mut artifact_loader = FileArtifactReader::new(&build_path, &build_path);
        let result =
            load_devmgr_config(&mut artifact_loader, &zbi_path_string, &devmgr_config_path);

        model
            .set(match result {
                Ok(devmgr_config) => DevmgrConfigCollection {
                    devmgr_config: Some(devmgr_config),
                    deps: artifact_loader.get_deps(),
                    errors: vec![],
                },
                Err(err) => DevmgrConfigCollection {
                    devmgr_config: None,
                    deps: artifact_loader.get_deps(),
                    errors: vec![err],
                },
            })
            .context(format!(
                "Failed to collect data from devmgr config bootfs:{} in ZBI at {}",
                devmgr_config_path, zbi_path_string
            ))?;
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
