// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_syslog::fx_log_err;
use serde::de::DeserializeOwned;
use std::fs::File;
use std::io::BufReader;

// Trait for loading the data from a config file.
pub trait ConfigData: DeserializeOwned {
    const DEFAULT_VALUE: Self;
    const FILE_PATH: &'static str;
}

// Loads and deserializes the config file from T's FILE_PATH. If this fails,
// return the default value.
pub fn load_data_or_default<T: ConfigData>() -> T {
    let file = match File::open(T::FILE_PATH) {
        Ok(f) => f,
        Err(e) => {
            fx_log_err!("unable to open {}, using defaults: {:?}", T::FILE_PATH, e);
            return T::DEFAULT_VALUE;
        }
    };

    match serde_json::from_reader(BufReader::new(file)) {
        Ok(value) => value,
        Err(e) => {
            fx_log_err!("unable to parse config, using defaults: {:?}", e);
            T::DEFAULT_VALUE
        }
    }
}

#[cfg(test)]
pub mod testing {
    use super::*;
    use serde_derive::Deserialize;

    #[derive(Deserialize)]
    struct ValidConfigData {
        value: u32,
    }

    impl ConfigData for ValidConfigData {
        const DEFAULT_VALUE: Self = ValidConfigData { value: 3 };
        const FILE_PATH: &'static str = "/config/data/fake_config_data.json";
    }

    #[derive(Deserialize)]
    struct InvalidConfigData {
        value: u32,
    }

    impl ConfigData for InvalidConfigData {
        const DEFAULT_VALUE: Self = InvalidConfigData { value: 3 };
        const FILE_PATH: &'static str = "/config/data/fake_invalid_config_data.json";
    }

    #[derive(Deserialize)]
    struct InvalidConfigFilePath {
        value: u32,
    }

    impl ConfigData for InvalidConfigFilePath {
        const DEFAULT_VALUE: Self = InvalidConfigFilePath { value: 5 };
        const FILE_PATH: &'static str = "nuthatch";
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_load_valid_config_data() {
        let data = load_data_or_default::<ValidConfigData>();
        assert_eq!(data.value, 10);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_load_invalid_config_data() {
        let data = load_data_or_default::<InvalidConfigData>();
        assert_eq!(data.value, 3);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_load_invalid_config_file_path() {
        let data = load_data_or_default::<InvalidConfigFilePath>();
        assert_eq!(data.value, 5);
    }
}
