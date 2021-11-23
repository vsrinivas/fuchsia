// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{FemuEngine, QemuEngine};
use anyhow::{anyhow, Result};
use async_trait::async_trait;
use ffx_emulator_config::EmulatorEngine;
use serde::{Deserialize, Serialize};
use std::{fs::File, path::PathBuf};

pub(crate) const SERIALIZE_FILE_NAME: &str = "engine.json";

pub async fn read_from_disk(instance_directory: &PathBuf) -> Result<Box<dyn EmulatorEngine>> {
    // Get the engine's location, which is in the instance directory.
    let filepath = instance_directory.join(SERIALIZE_FILE_NAME);

    // Read the engine.json file and deserialize it from disk into a new TypedEngine instance
    if filepath.exists() {
        let file = File::open(&filepath)
            .expect(&format!("Unable to open file {:?} for deserialization", filepath));
        let value: serde_json::Value = serde_json::from_reader(file)?;
        if let Some(engine_type) = value.get("engine_type") {
            match engine_type.as_str() {
                Some("femu") => Ok(Box::new(<FemuEngine as Deserialize>::deserialize(value)?)
                    as Box<dyn EmulatorEngine>),
                Some("qemu") => Ok(Box::new(<QemuEngine as Deserialize>::deserialize(value)?)
                    as Box<dyn EmulatorEngine>),
                _ => Err(anyhow!("Not a valid engine type.")),
            }
        } else {
            Err(anyhow!("Deserialized data doesn't contain an engine type value."))
        }
    } else {
        Err(anyhow!("Engine file doesn't exist at {:?}", filepath))
    }
}

#[async_trait]
pub trait SerializingEngine: Serialize {
    async fn write_to_disk(&self, instance_directory: &PathBuf) -> Result<()> {
        // The engine's serialized form will be saved in ${EMU_INSTANCE_ROOT_DIR}/${runtime.name}.
        // This is the path set up by the EngineBuilder, so it's expected to already exist.
        let filepath = instance_directory.join(SERIALIZE_FILE_NAME);

        // Create the engine.json file to hold the serialized data, and write it out to disk,
        let file = File::create(&filepath)
            .expect(&format!("Unable to create file {:?} for serialization", filepath));
        log::debug!("Writing serialized engine out to {:?}", filepath);
        match serde_json::to_writer(file, self) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::qemu::QemuEngine;
    use ffx_emulator_config::EngineType;
    use futures::executor::block_on;
    use std::fs::{create_dir_all, remove_file};
    use tempfile::tempdir;

    #[test]
    fn test_write_then_read() -> Result<()> {
        // Create a test directory in TempFile::tempdir
        let name = "test_write_then_read";
        let mut temp_path = PathBuf::from(tempdir().unwrap().path()).join(name);
        create_dir_all(&temp_path)?;

        // Set up some test data.
        let engine = QemuEngine { engine_type: EngineType::Qemu, ..Default::default() };

        // Serialize the engine to disk.
        block_on(engine.write_to_disk(&temp_path)).expect("Problem serializing engine to disk.");

        // Deserialize it from the expected file location.
        temp_path.push(SERIALIZE_FILE_NAME);
        let file = File::open(&temp_path)
            .expect(&format!("Unable to open file {:?} for deserialization", temp_path));
        let engine_copy: QemuEngine = serde_json::from_reader(file)?;
        assert_eq!(engine_copy, engine);

        remove_file(&temp_path).expect("Problem removing serialized file during test.");
        Ok(())
    }
}
