// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Configuration data for fgsutil.

use {
    anyhow::{anyhow, bail, Context, Result},
    serde::{Deserialize, Serialize},
    serde_json,
    std::{
        fs::OpenOptions,
        io::{BufReader, ErrorKind},
        path::Path,
    },
};

/// Store the user's configuration data.
pub async fn write_config(config: &Configuration, file_path: &Path) -> Result<()> {
    use std::os::unix::fs::OpenOptionsExt;
    let mut file = OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        // Restrict access to only the user. This is the same permissions that
        // gsutil uses for the ~/.boto file.
        .mode(0o600)
        .open(file_path)
        .context("create config.json")?;
    serde_json::to_writer(&mut file, config)?;
    Ok(())
}

/// Retrieve the user's configuration data.
///
/// If no file is found a default configuration will be returned.
pub async fn read_config(file_path: &Path) -> Result<Configuration> {
    let file = match OpenOptions::new().read(true).write(true).create(false).open(file_path) {
        Ok(f) => f,
        Err(e) => match e.kind() {
            ErrorKind::NotFound => return Ok(Configuration::default()),
            _ => bail!("Trying to open config file: {:?}", e),
        },
    };
    let buf_reader = BufReader::new(file);
    let config = serde_json::from_reader(buf_reader)?;
    Ok(config)
}

/// Configuration settings for the tool or Google Cloud Storage (GCS).
#[derive(Debug, Default, Deserialize, PartialEq, Serialize)]
pub struct Configuration {
    pub gcs: GCS,
}

/// Configuration settings specific to Google Cloud Storage (GCS).
#[derive(Debug, Default, Deserialize, PartialEq, Serialize)]
pub struct GCS {
    pub refresh_token: Option<String>,
}

impl GCS {
    /// Get the refresh token or error with explanation that it's required.
    pub fn require_refresh_token(&self) -> Result<String> {
        let a = self.refresh_token.as_ref().ok_or(anyhow!(
            "Missing OAuth token. Please run `fgsutil config` to set up OAuth and retry."
        ))?;
        Ok(a.to_string())
    }
}

#[cfg(test)]
mod tests {
    use {super::*, std::io::Write, temp_test_env::TempTestEnv};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_read_config() {
        let test_env = TempTestEnv::new().expect("test env");
        let test_config_path = test_env.home.join(".read_config");
        let mut file = std::fs::File::create(&test_config_path).expect("create config");
        file.write(b"{\"gcs\":{\"refresh_token\":\"fake_refresh\"}}").expect("write config");
        let config = read_config(&test_config_path).await.expect("read config");
        let expected =
            Configuration { gcs: GCS { refresh_token: Some("fake_refresh".to_string()) } };
        assert_eq!(config, expected);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_write_config() {
        let test_env = TempTestEnv::new().expect("test env");
        let test_config_path = test_env.home.join(".write_config");
        let config = Configuration { gcs: GCS { refresh_token: Some("fake_refresh".to_string()) } };
        write_config(&config, &test_config_path).await.expect("write config");

        let file = std::fs::File::open(&test_config_path).expect("create config");
        let buf_reader = BufReader::new(file);
        let data: serde_json::Value = serde_json::from_reader(buf_reader).expect("read config");

        assert_eq!("fake_refresh", data["gcs"]["refresh_token"]);
    }
}
