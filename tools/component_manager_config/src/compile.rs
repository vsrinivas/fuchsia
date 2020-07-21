// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use argh::FromArgs;
use fidl::encoding::encode_persistent;
use fidl_fuchsia_component_internal as component_internal;
use json5;
use serde::Deserialize;
use std::fs;
use std::fs::File;
use std::io::Write;
use std::path::PathBuf;

#[derive(Deserialize, Debug)]
struct Config {
    #[serde(default)]
    debug: bool,
}

impl Into<component_internal::Config> for Config {
    fn into(self) -> component_internal::Config {
        let Config { debug } = self;
        component_internal::Config { debug: Some(debug) }
    }
}

impl Config {
    fn from_json_file(path: PathBuf) -> Result<Self, Error> {
        let data = fs::read_to_string(path)?;
        json5::from_str(&data).map_err(|e| format_err!("failed reading config json: {}", e))
    }
}

#[derive(Debug, Default, FromArgs)]
/// Create a binary config and populate it with data from .json file.
struct Args {
    /// path to a JSON configuration file
    #[argh(option)]
    input: PathBuf,

    /// path to the output binary config file
    #[argh(option)]
    output: PathBuf,
}

pub fn from_args() -> Result<(), Error> {
    compile(argh::from_env())
}

fn compile(args: Args) -> Result<(), Error> {
    let config_json = Config::from_json_file(args.input)?;
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
    fn test() -> Result<(), Error> {
        let tmp_dir = TempDir::new().unwrap();
        let output_path = tmp_dir.path().join("config");
        let input_path = tmp_dir.path().join("foo.json");
        let input = "{\"debug\": true,}";
        File::create(&input_path).unwrap().write_all(input.as_bytes()).unwrap();

        let args = Args { output: output_path.clone(), input: input_path };
        compile(args)?;

        let mut bytes = Vec::new();
        File::open(output_path)?.read_to_end(&mut bytes)?;
        let config: component_internal::Config = decode_persistent(&bytes)?;
        assert_eq!(config.debug, Some(true));
        Ok(())
    }
}
