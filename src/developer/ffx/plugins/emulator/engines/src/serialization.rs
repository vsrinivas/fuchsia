// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Result};
use async_trait::async_trait;
use ffx_emulator_config::LogLevel;
use serde::Serialize;
use std::{fs::File, path::PathBuf};

const SERIALIZE_FILE_NAME: &str = "engine.json";

#[async_trait]
pub trait SerializingEngine: Serialize {
    async fn write_to_disk(
        &self,
        instance_directory: &PathBuf,
        log_level: &LogLevel,
    ) -> Result<()> {
        // The engine's serialized form will be saved in ${EMU_INSTANCE_ROOT_DIR}/${runtime.name}.
        // This is the path set up by the EngineBuilder, so it's expected to already exist.
        let mut filepath = instance_directory.clone();

        // Create the engine.json file to hold the serialized data, and write it out to disk,
        filepath.push(SERIALIZE_FILE_NAME);
        if log_level == &LogLevel::Verbose {
            println!("Writing serialized engine out to {}", filepath.to_string_lossy());
        }
        let file = File::create(&filepath)
            .expect(&format!("Unable to create file {:?} for serialization", filepath));
        match serde_json::to_writer(file, self) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures::executor::block_on;
    use serde::Deserialize;
    use std::{
        fs::{create_dir_all, remove_file},
        io::BufReader,
    };
    use tempfile::tempdir;

    #[derive(Debug, Deserialize, Serialize, PartialEq)]
    struct FakeEngine {
        data: String,
        number: usize,
    }
    impl SerializingEngine for FakeEngine {}

    #[test]
    fn test_write_to_disk() -> Result<()> {
        // Create a test directory in TempFile::tempdir
        let mut temp_dir = PathBuf::from(tempdir().unwrap().path());
        temp_dir.push("test_to_disk");
        create_dir_all(&temp_dir)?;

        // Set up some test data.
        let engine = FakeEngine { data: "Here's some stuff to serialize".to_string(), number: 42 };

        // Serialize the FakeEngine to disk.
        block_on(engine.write_to_disk(&temp_dir, &LogLevel::Info))?;

        // Deserialize it from the expected file location.
        let mut filename = temp_dir;
        filename.push(SERIALIZE_FILE_NAME);
        let engine_copy: FakeEngine =
            serde_json::from_reader(BufReader::new(File::open(&filename)?))?;

        // Verify the two copies are identical and clean up the file.
        assert_eq!(engine_copy, engine);
        remove_file(&filename)?;
        Ok(())
    }
}
