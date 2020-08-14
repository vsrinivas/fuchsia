// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use argh::FromArgs;
use fidl::encoding::encode_persistent;
use fidl_fuchsia_component_internal as component_internal;
use serde::Deserialize;
use serde_json5;
use std::fs;
use std::fs::File;
use std::io::Write;
use std::path::PathBuf;

#[derive(Deserialize, Debug, Default)]
struct Config {
    debug: Option<bool>,
    list_children_batch_size: Option<u32>,
    security_policy: Option<SecurityPolicy>,
}

#[derive(Deserialize, Debug, Default)]
pub struct SecurityPolicy {
    job_policy: Option<JobPolicyAllowlists>,
}

#[derive(Deserialize, Debug, Default)]
pub struct JobPolicyAllowlists {
    ambient_mark_vmo_exec: Option<Vec<String>>,

    main_process_critical: Option<Vec<String>>,
}

impl Into<component_internal::Config> for Config {
    fn into(self) -> component_internal::Config {
        let (ambient_mark_vmo_exec, main_process_critical) = if let Some(SecurityPolicy {
            job_policy: Some(JobPolicyAllowlists { ambient_mark_vmo_exec, main_process_critical }),
        }) = self.security_policy
        {
            (ambient_mark_vmo_exec, main_process_critical)
        } else {
            (None, None)
        };

        component_internal::Config {
            debug: self.debug,
            list_children_batch_size: self.list_children_batch_size,
            security_policy: Some(component_internal::SecurityPolicy {
                job_policy: Some(component_internal::JobPolicyAllowlists {
                    ambient_mark_vmo_exec,
                    main_process_critical,
                }),
            }),
        }
    }
}

macro_rules! extend_if_unset {
    ( $($target:expr, $value:expr, $field:ident)+ ) => {
        $(
            $target.$field = match (&$target.$field, &$value.$field) {
                (Some(_), Some(_)) => {
                    return Err(format_err!("Conflicting field found: {:?}", stringify!($field)))
                }
                (None, Some(_)) => $value.$field,
                _ => $target.$field,
            };
        )+
    };
}

impl Config {
    fn from_json_file(path: &PathBuf) -> Result<Self, Error> {
        let data = fs::read_to_string(path)?;
        serde_json5::from_str(&data).map_err(|e| format_err!("failed reading config json: {}", e))
    }

    fn extend(mut self, another: Config) -> Result<Self, Error> {
        extend_if_unset!(self, another, debug);
        extend_if_unset!(self, another, list_children_batch_size);
        extend_if_unset!(self, another, security_policy);
        Ok(self)
    }
}

#[derive(Debug, Default, FromArgs)]
/// Create a binary config and populate it with data from .json file.
struct Args {
    /// path to a JSON configuration file
    #[argh(option)]
    input: Vec<PathBuf>,

    /// path to the output binary config file
    #[argh(option)]
    output: PathBuf,
}

pub fn from_args() -> Result<(), Error> {
    compile(argh::from_env())
}

fn compile(args: Args) -> Result<(), Error> {
    let config_json = args
        .input
        .iter()
        .map(Config::from_json_file)
        .take_while(Result::is_ok)
        .map(Result::unwrap)
        .try_fold(Config::default(), |acc, next| acc.extend(next))?;
    let mut config_fidl: component_internal::Config = config_json.into();
    let bytes = encode_persistent(&mut config_fidl)?;
    let mut file = File::create(args.output)?;
    file.write_all(&bytes)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::encoding::decode_persistent;
    use std::io::Read;
    use tempfile::TempDir;

    #[test]
    fn test_debug() -> Result<(), Error> {
        let tmp_dir = TempDir::new().unwrap();
        let output_path = tmp_dir.path().join("config");
        let input_path = tmp_dir.path().join("foo.json");
        let input = r#"{
            debug: true,
            list_children_batch_size: 123,
            security_policy: {
                job_policy: {
                    main_process_critical: [ "/", "/bar" ],
                    ambient_mark_vmo_exec: ["/foo"],
                }
            }
        }"#;
        File::create(&input_path).unwrap().write_all(input.as_bytes()).unwrap();

        let args = Args { output: output_path.clone(), input: vec![input_path] };
        compile(args)?;

        let mut bytes = Vec::new();
        File::open(output_path)?.read_to_end(&mut bytes)?;
        let config: component_internal::Config = decode_persistent(&bytes)?;
        assert_eq!(
            config,
            component_internal::Config {
                debug: Some(true),
                list_children_batch_size: Some(123),
                security_policy: Some(component_internal::SecurityPolicy {
                    job_policy: Some(component_internal::JobPolicyAllowlists {
                        main_process_critical: Some(vec!["/".to_string(), "/bar".to_string()]),
                        ambient_mark_vmo_exec: Some(vec!["/foo".to_string()]),
                    }),
                }),
            }
        );
        Ok(())
    }

    #[test]
    fn test_conflict() -> Result<(), Error> {
        let tmp_dir = TempDir::new().unwrap();
        let output_path = tmp_dir.path().join("config");

        let input_path = tmp_dir.path().join("foo.json");
        let input = "{\"debug\": true,}";
        File::create(&input_path).unwrap().write_all(input.as_bytes()).unwrap();

        let another_input_path = tmp_dir.path().join("bar.json");
        let another_input = "{\"debug\": false,}";
        File::create(&another_input_path).unwrap().write_all(another_input.as_bytes()).unwrap();

        let args =
            Args { output: output_path.clone(), input: vec![input_path, another_input_path] };
        assert!(compile(args).is_err());
        Ok(())
    }
}
