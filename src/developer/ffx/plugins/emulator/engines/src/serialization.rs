// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{FemuEngine, QemuEngine, SERIALIZE_FILE_NAME};
use anyhow::{anyhow, Context, Result};
use ffx_emulator_config::EmulatorEngine;
use serde::{Deserialize, Serialize};
use std::{fs::File, path::PathBuf};

pub fn read_from_disk(instance_directory: &PathBuf) -> Result<Box<dyn EmulatorEngine>> {
    let value = read_from_disk_untyped(instance_directory)
        .context("Failed to read engine configuration from disk.")?;

    let filepath = instance_directory.join(SERIALIZE_FILE_NAME);
    if let Some(engine_type) = value.get("engine_type") {
        match engine_type.as_str() {
            Some("femu") => Ok(Box::new(<FemuEngine as Deserialize>::deserialize(value).context(
                format!("Expected a FEMU engine in {:?}, but deserialization failed.", filepath),
            )?) as Box<dyn EmulatorEngine>),
            Some("qemu") => Ok(Box::new(<QemuEngine as Deserialize>::deserialize(value).context(
                format!("Expected a QEMU engine in {:?}, but deserialization failed.", filepath),
            )?) as Box<dyn EmulatorEngine>),
            _ => Err(anyhow!("Not a valid engine type.")),
        }
    } else {
        Err(anyhow!("Deserialized data doesn't contain an engine type value."))
    }
}

pub fn read_from_disk_untyped(instance_directory: &PathBuf) -> Result<serde_json::Value> {
    // Get the engine's location, which is in the instance directory.
    let filepath = instance_directory.join(SERIALIZE_FILE_NAME);

    // Read the engine.json file and deserialize it from disk into a new TypedEngine instance
    if filepath.exists() {
        let file = File::open(&filepath)
            .context(format!("Unable to open file {:?} for deserialization", filepath))?;
        let value: serde_json::Value = serde_json::from_reader(file)
            .context(format!("Invalid JSON syntax in {:?}", filepath))?;
        Ok(value)
    } else {
        Err(anyhow!("Engine file doesn't exist at {:?}", filepath))
    }
}

pub trait SerializingEngine: Serialize {
    fn write_to_disk(&self, instance_directory: &PathBuf) -> Result<()> {
        // The engine's serialized form will be saved in ${EMU_INSTANCE_ROOT_DIR}/${runtime.name}.
        // This is the path set up by the EngineBuilder, so it's expected to already exist.
        let filepath = instance_directory.join(SERIALIZE_FILE_NAME);

        // Create the engine.json file to hold the serialized data, and write it out to disk,
        let file = File::create(&filepath)
            .context(format!("Unable to create file {:?} for serialization", filepath))?;
        tracing::debug!("Writing serialized engine out to {:?}", filepath);
        match serde_json::to_writer(file, self) {
            Ok(_) => Ok(()),
            Err(e) => Err(anyhow!(e)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::qemu_based::qemu::QemuEngine;
    use ffx_emulator_config::EngineType;
    use regex::Regex;
    use std::{
        fs::{create_dir_all, remove_file},
        io::{Read, Write},
    };
    use tempfile::tempdir;

    #[test]
    fn test_write_then_read() -> Result<()> {
        // Create a test directory in TempFile::tempdir.
        let name = "test_write_then_read";
        let temp_path = PathBuf::from(tempdir().unwrap().path()).join(name);
        let file_path = temp_path.join(SERIALIZE_FILE_NAME);
        create_dir_all(&temp_path)?;

        // Set up some test data.
        let q_engine = QemuEngine { engine_type: EngineType::Qemu, ..Default::default() };
        let f_engine = FemuEngine { engine_type: EngineType::Femu, ..Default::default() };

        // Serialize the QEMU engine to disk.
        q_engine.write_to_disk(&temp_path).expect("Problem serializing QEMU engine to disk.");

        // Deserialize it from the expected file location.
        let file = File::open(&file_path)
            .expect(&format!("Unable to open file {:?} for deserialization.", file_path));
        let engine_copy: QemuEngine = serde_json::from_reader(file)?;
        assert_eq!(engine_copy, q_engine);

        // Also deserialize with read_from_disk, to make sure that succeeds.
        let box_engine = read_from_disk(&temp_path);
        assert!(box_engine.is_ok(), "Read from disk failed for QEMU: {:?}", box_engine.err());

        // Clean up for the next test.
        remove_file(&file_path).expect("Problem removing serialized file during test.");

        // Serialize the FEMU engine to disk.
        f_engine.write_to_disk(&temp_path).expect("Problem serializing FEMU engine to disk.");
        let box_engine = read_from_disk(&temp_path);
        assert!(box_engine.is_ok(), "Read from disk failed for FEMU: {:?}", box_engine.err());

        // Now that we know the file is ok, we intentionally corrupt the FEMU engine file.
        let mut file = File::open(&file_path)
            .expect(&format!("Unable to open file {:?} for manual read.", &file_path));
        let mut text = String::new();
        file.read_to_string(&mut text)
            .expect(&format!("Couldn't read contents of {:?} to memory.", &file_path));
        let re = Regex::new("femu")?;
        let unsupported = re.replace_all(&text, "unsupported").into_owned();

        // Reset the file with the corrupted contents, then attempt a read_from_disk,
        // expecting it to fail because the engine type is invalid.
        remove_file(&file_path).expect("Problem removing serialized file during test.");
        let mut file = File::create(&file_path)?;
        write!(file, "{}", &unsupported)?;
        let box_engine = read_from_disk(&temp_path);
        assert!(box_engine.is_err());

        // Go back to a known engine type, but corrupt one of the data fields.
        let re = Regex::new("memory")?;
        let unsupported = re.replace_all(&text, "unsupported").into_owned();
        remove_file(&file_path).expect("Problem removing serialized file during test.");
        let mut file = File::create(&file_path)?;
        write!(file, "{}", &unsupported)?;
        let box_engine = read_from_disk(&temp_path);
        assert!(box_engine.is_err());

        Ok(())
    }

    #[test]
    fn test_broken_reads() -> Result<()> {
        // Create a test directory in TempFile::tempdir.
        let name = "test_write_then_read";
        let temp_path = PathBuf::from(tempdir().unwrap().path()).join(name);
        let file_path = temp_path.join(SERIALIZE_FILE_NAME);
        create_dir_all(&temp_path)?;

        let bad_json = "This is not valid JSON";
        let no_pid = r#"{ "engine_type":"femu" }"#;
        let bad_pid = r#"{ "engine_type":"femu","pid":"string" }"#;
        let has_pid = r#"{ "engine_type":"femu","pid":123456 }"#;

        // Note: This string is a currently valid and complete instance of a FEMU config as it
        // would be serialized to disk. The test on this string should fail if a change (to the
        // EmulatorConfiguration data structure, for example) would break deserialization of
        // existing emulation instances. If your change causes this test to fail, consider wrapping
        // the fields you changed in Option<foo>, or providing a default value for the field to
        // deserialize with. Do not simply update this text to match your change, or users will
        // see [Broken] emulators on their next update. Wait until the field has had time to "bake"
        // before updating this text for your changes.
        let valid_femu = r#"{"emulator_configuration":{"device":{"audio":{"model":"hda"},"cpu":{
            "architecture":"x64","count":0},"memory":{"quantity":8192,"units":"megabytes"},
            "pointing_device":"mouse","screen":{"height":800,"width":1280,"units":"pixels"},
            "storage":{"quantity":2,"units":"gigabytes"}},"flags":{"args":[],"envs":{},"features":[],
            "kernel_args":[],"options":[]},"guest":{"fvm_image":"/path/to/fvm.blk","kernel_image":
            "/path/to/multiboot.bin","zbi_image":"/path/to/fuchsia.zbi"},"host":{"acceleration":
            "hyper","architecture":"x64","gpu":"auto","log":"/path/to/emulator.log","networking"
            :"tap","os":"linux","port_map":{}},"runtime":{"console":"none","debugger":false,
            "dry_run":false,"headless":true,"hidpi_scaling":false,"instance_directory":"/some/dir",
            "log_level":"info","mac_address":"52:54:47:5e:82:ef","name":"fuchsia-emulator",
            "startup_timeout":{"secs":60,"nanos":0},"template":"/path/to/config","upscript":null}},
            "pid":657042,"engine_type":"femu"}"#;

        let mut file = File::create(&file_path)?;
        write!(file, "{}", &bad_json)?;
        let box_engine = read_from_disk(&temp_path);
        assert!(box_engine.is_err());
        let value = read_from_disk_untyped(&temp_path);
        assert!(value.is_err());

        remove_file(&file_path).expect("Problem removing serialized file during test.");
        let mut file = File::create(&file_path)?;
        write!(file, "{}", &no_pid)?;
        let box_engine = read_from_disk(&temp_path);
        assert!(box_engine.is_err());
        let value = read_from_disk_untyped(&temp_path);
        assert!(value.is_ok(), "{:?}", value);
        assert!(value.unwrap().get("pid").is_none());

        remove_file(&file_path).expect("Problem removing serialized file during test.");
        let mut file = File::create(&file_path)?;
        write!(file, "{}", &bad_pid)?;
        let box_engine = read_from_disk(&temp_path);
        assert!(box_engine.is_err());
        let value = read_from_disk_untyped(&temp_path);
        assert!(value.is_ok(), "{:?}", value);
        assert!(value.as_ref().unwrap().get("pid").is_some());
        assert!(value.unwrap().get("pid").unwrap().as_i64().is_none());

        remove_file(&file_path).expect("Problem removing serialized file during test.");
        let mut file = File::create(&file_path)?;
        write!(file, "{}", &has_pid)?;
        let box_engine = read_from_disk(&temp_path);
        assert!(box_engine.is_err());
        let value = read_from_disk_untyped(&temp_path);
        assert!(value.is_ok(), "{:?}", value);
        assert!(value.as_ref().unwrap().get("pid").is_some());
        assert!(value.as_ref().unwrap().get("pid").unwrap().as_i64().is_some());
        assert_eq!(value.unwrap().get("pid").unwrap().as_i64().unwrap(), 123456);

        remove_file(&file_path).expect("Problem removing serialized file during test.");
        let mut file = File::create(&file_path)?;
        write!(file, "{}", &valid_femu)?;
        let box_engine = read_from_disk(&temp_path);
        assert!(box_engine.is_ok(), "{:?}", box_engine.err());
        assert_eq!(box_engine.unwrap().engine_type(), EngineType::Femu);

        Ok(())
    }
}
