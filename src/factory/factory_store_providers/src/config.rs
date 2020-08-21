// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::validators::{new_validator_context_by_name, Validator},
    anyhow::Error,
    fidl::endpoints::DiscoverableService,
    fuchsia_syslog as syslog,
    serde::Deserialize,
    serde_json::{self, value::Value},
    std::{collections::HashMap, default::Default, io},
};

/// Type that maps a file to a group of arguments passed to a validator.
pub type ValidatorFileArgsMap = HashMap<String, Value>;

#[derive(Debug, Deserialize)]
pub struct FactoryFileSpec {
    pub dest: Option<String>,
    pub path: String,
    #[serde(default)]
    validators: Vec<ValidatorSpec>,
}

#[derive(Debug, Deserialize)]
pub struct ValidatorSpec {
    pub name: String,
    #[serde(default)]
    pub args: Value,
}

#[derive(Debug)]
pub struct ValidatorContext {
    pub name: String,
    pub paths_to_validate: Vec<String>,
    pub validator: Box<dyn Validator>,
}

#[derive(Debug)]
pub struct ConfigContext {
    pub file_path_map: HashMap<String, String>,
    pub validator_contexts: Vec<ValidatorContext>,
}

#[derive(Debug, Default, Deserialize)]
pub struct Config {
    files: Vec<FactoryFileSpec>,
}

impl Config {
    fn load_file(path: &str) -> Result<Self, Error> {
        Ok(serde_json::from_reader(io::BufReader::new(std::fs::File::open(path)?))?)
    }

    pub fn load<T>() -> Result<Self, Error>
    where
        T: DiscoverableService,
    {
        let config_data_file = format!("/config/data/{}.config", &T::SERVICE_NAME);
        syslog::fx_log_info!("Loading {}", &config_data_file);
        Config::load_file(&config_data_file)
    }

    pub fn into_context(self) -> Result<ConfigContext, Error> {
        let mut file_path_map = HashMap::new();
        let mut validator_config_map: HashMap<String, ValidatorFileArgsMap> = HashMap::new();

        // De-dupe validator configurations over the collection of file specs.
        for file in self.files.into_iter() {
            if file.validators.is_empty() {
                syslog::fx_log_warn!(
                    "Entry {:?} must have at least one validator to be processed, skipping",
                    &file.path
                );
                continue;
            }

            for validator in file.validators.into_iter() {
                match validator_config_map.get_mut(&validator.name) {
                    Some(args_map) => {
                        args_map.insert(file.path.clone(), validator.args);
                    }
                    None => {
                        let mut args_map = ValidatorFileArgsMap::new();
                        args_map.insert(file.path.clone(), validator.args);
                        validator_config_map.insert(validator.name, args_map);
                    }
                };
            }

            let dest = file.dest.unwrap_or(file.path.clone());
            match file_path_map.get(&file.path) {
                Some(old_dest) => {
                    syslog::fx_log_warn!(
                        "Entry {:?} already mapped to destination {:?}, ignoring mapping to {:?}",
                        &file.path,
                        old_dest,
                        dest
                    );
                }
                None => {
                    file_path_map.insert(file.path.clone(), dest);
                }
            };
        }

        // Now that validation configurations have been de-duped, the validators can actually be
        // constructed.
        let mut validator_contexts = Vec::new();
        for (name, args_map) in validator_config_map.into_iter() {
            validator_contexts.push(new_validator_context_by_name(&name, args_map)?);
        }
        Ok(ConfigContext { file_path_map, validator_contexts })
    }
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum FactoryConfig {
    FactoryItems,
    Ext4(String),
    FactoryVerity,
}

impl FactoryConfig {
    pub fn load<R>(config: R) -> Result<Self, Error>
    where
        R: io::Read,
    {
        Ok(serde_json::from_reader(config)?)
    }
}

impl Default for FactoryConfig {
    fn default() -> Self {
        FactoryConfig::FactoryItems
    }
}

#[cfg(test)]
mod tests {
    use {super::*, serde_json::json};

    #[test]
    fn test_simple_configs() {
        {
            let config_json = json!({
                "files": [
                    {
                        "path": "file1",
                        "validators": [
                            {
                                "name": "pass"
                            }
                        ]
                    }
                ]
            });

            let config: Config = serde_json::from_value(config_json).unwrap();
            assert_eq!("file1", config.files[0].path);
            assert_eq!(None, config.files[0].dest);
            let context = config.into_context().unwrap();
            assert_eq!("file1", context.file_path_map.get("file1").unwrap());
            assert_eq!("pass", context.validator_contexts[0].name);
            assert_eq!(vec!["file1"], context.validator_contexts[0].paths_to_validate);
        }

        {
            let config_json = json!({
                "files": [
                    {
                        "path": "file2",
                        "dest": "destfile2",
                        "validators": [
                            {
                                "name": "pass"
                            }
                        ]
                    }
                ]
            });

            let config: Config = serde_json::from_value(config_json).unwrap();
            assert_eq!("file2", config.files[0].path);
            assert_eq!(Some("destfile2".to_string()), config.files[0].dest);
            let context = config.into_context().unwrap();
            assert_eq!("destfile2", context.file_path_map.get("file2").unwrap());
            assert_eq!("pass", context.validator_contexts[0].name);
            assert_eq!(vec!["file2"], context.validator_contexts[0].paths_to_validate);
        }
    }

    #[test]
    fn test_into_validation_contexts_dedupes_validators() {
        let config_json = json!({
            "files": [
                {
                    "path": "file1",
                    "validators": [
                        {
                            "name": "pass"
                        }
                    ]
                },
                {
                    "path": "file2",
                    "validators": [
                        {
                            "name": "pass",
                        }
                    ]
                },
                {
                    "path": "file3",
                    "validators": [
                        {
                            "name": "text"
                        }
                    ]
                }
            ]
        });

        let config: Config = serde_json::from_value(config_json).unwrap();
        let context = config.into_context().unwrap();
        assert_eq!(2, context.validator_contexts.len());

        for mut validator_context in context.validator_contexts {
            match validator_context.name.as_ref() {
                "pass" => {
                    validator_context.paths_to_validate.sort_unstable();
                    assert_eq!(vec!["file1", "file2"], validator_context.paths_to_validate)
                }
                "text" => assert_eq!(vec!["file3"], validator_context.paths_to_validate),
                _ => panic!("Unexpected validator: {}", validator_context.name),
            }
        }
    }

    #[test]
    fn test_config_context_skips_unvalidated_files() {
        let config_json = json!({
            "files": [
                {
                    "path": "file1",
                    "validators": [
                        {
                            "name": "pass"
                        }
                    ]
                },
                {
                    "path": "file2",
                }
            ],
        });

        let config: Config = serde_json::from_value(config_json).unwrap();
        let context = config.into_context().unwrap();
        assert_eq!(1, context.file_path_map.len());
        assert_eq!(1, context.validator_contexts.len());
        assert_eq!("pass", context.validator_contexts[0].name);
        assert_eq!(vec!["file1"], context.validator_contexts[0].paths_to_validate);
    }

    #[test]
    fn test_load_configs() {
        let ext4_config_json = json!({
            "ext4": "/pkg/data/factory_ext4.img"
        });
        let mut buf = std::io::Cursor::new(Vec::new());
        serde_json::to_writer(&mut buf, &ext4_config_json).unwrap();
        buf.set_position(0);
        let ext4_config = FactoryConfig::load(buf).unwrap();
        match ext4_config {
            FactoryConfig::Ext4(_) => {}
            _ => panic!("expected ext4 factory config"),
        }

        let verity_config_json = json!("factory_verity");
        buf = std::io::Cursor::new(Vec::new());
        serde_json::to_writer(&mut buf, &verity_config_json).unwrap();
        buf.set_position(0);
        let verity_config = FactoryConfig::load(buf).unwrap();
        match verity_config {
            FactoryConfig::FactoryVerity => {}
            _ => panic!("expected factory verity config"),
        }
    }
}
