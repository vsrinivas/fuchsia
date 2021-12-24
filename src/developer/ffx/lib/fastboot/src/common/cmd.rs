// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Error, Result},
    serde::{Deserialize, Serialize},
    std::default::Default,
    std::path::{Path, PathBuf},
};

pub(crate) const OEM_FILE_ERROR_MSG: &str =
    "Unrecognized OEM staged file. Expected comma-separated pair: \"<OEM_COMMAND>,<PATH_TO_FILE>\"";

#[derive()]
pub struct ManifestParams {
    pub manifest: Option<PathBuf>,
    pub product: String,
    pub product_bundle: Option<String>,
    pub oem_stage: Vec<OemFile>,
    pub no_bootloader_reboot: bool,
    pub skip_verify: bool,
    pub op: Command,
}

impl Default for ManifestParams {
    fn default() -> Self {
        Self {
            manifest: None,
            product: "fuchsia".to_string(),
            product_bundle: None,
            oem_stage: vec![],
            no_bootloader_reboot: false,
            skip_verify: false,
            op: Command::Flash,
        }
    }
}

pub enum Command {
    Flash,
    Unlock(UnlockParams),
    Boot(BootParams),
}

pub struct UnlockParams {
    pub cred: Option<String>,
    pub force: bool,
}

pub struct BootParams {
    pub zbi: Option<String>,
    pub vbmeta: Option<String>,
    pub slot: String,
}

#[derive(Default, Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct OemFile(String, String);

impl OemFile {
    pub fn new(command: String, path: String) -> Self {
        Self(command, path)
    }

    pub fn command(&self) -> &str {
        self.0.as_str()
    }

    pub fn file(&self) -> &str {
        self.1.as_str()
    }
}

impl std::str::FromStr for OemFile {
    type Err = Error;

    fn from_str(s: &str) -> Result<Self> {
        if s.len() == 0 {
            bail!(OEM_FILE_ERROR_MSG);
        }

        let splits: Vec<&str> = s.split(",").collect();

        if splits.len() != 2 {
            bail!(OEM_FILE_ERROR_MSG);
        }

        let file = Path::new(splits[1]);
        if !file.exists() {
            bail!("File does not exist: {}", splits[1]);
        }

        Ok(Self(splits[0].to_string(), file.to_string_lossy().to_string()))
    }
}
