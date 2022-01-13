// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// A simple program to test the validity of a thermal configuration file.
///
/// This program will attempt to open the thermal configuration file (supplied via the "--input
/// <file_path>" argument), then use the ThermalConfig parser library to parse it to into a valid
/// ThermalConfig. If a config file can be read by the ThermalConfig library, then it is deemed to
/// be valid.
use {
    anyhow::Error,
    argh::FromArgs,
    std::{fs, path::PathBuf},
    thermal_config::ThermalConfig,
};

#[derive(FromArgs)]
#[argh(description = "Input flags for the thermal config validator")]
struct Flags {
    #[argh(option, description = "input path for the thermal configuration JSON file")]
    input: PathBuf,

    #[argh(
        option,
        description = "output path for the stamp file which is written upon successful validation"
    )]
    stamp: PathBuf,
}

fn main_impl(flags: Flags) -> Result<(), Error> {
    ThermalConfig::read(&flags.input).map(|_| ())?;
    fs::write(flags.stamp, "Done!")?;
    Ok(())
}

fn main() -> Result<(), Error> {
    let flags = argh::from_env();
    main_impl(flags)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io;

    fn get_valid_configs() -> Vec<PathBuf> {
        get_configs("valid_test_configs")
    }

    fn get_invalid_configs() -> Vec<PathBuf> {
        get_configs("invalid_test_configs")
    }

    // Gets the test configs located under the given subdirectory
    fn get_configs(sub_dir: &str) -> Vec<PathBuf> {
        get_test_directory()
            .join(sub_dir)
            .read_dir()
            .expect("Failed to read files in subdirectory")
            .map(|dir_entry| dir_entry.unwrap().path())
            .collect()
    }

    // Generates a path that a test can use for the stamp file
    fn stamp_path() -> PathBuf {
        get_test_directory().join("success.stamp")
    }

    // Gets the parent directory for this test executable (example:
    // out/core.nelson-release/host_x64/)
    fn get_test_directory() -> PathBuf {
        std::env::current_exe()
            .expect("Failed to get path of this executable")
            .parent()
            .expect("Failed to get current executable's parent directory")
            .to_path_buf()
    }

    // Deletes the file at the given path, if it exists. Any error (other than a missing file in the
    // first place) causes a panic.
    fn delete_file(file_path: &PathBuf) {
        match fs::remove_file(file_path) {
            Ok(()) => (),
            Err(e) if e.kind() == io::ErrorKind::NotFound => (),
            Err(e) => panic!("Failed to delete file at path {:?} (err = {})", file_path, e),
        }
    }

    /// Tests that valid thermal configuration files pass the validation.
    #[test]
    fn test_valid_thermal_config() {
        let stamp_path = stamp_path();

        let config_paths = get_valid_configs();
        assert!(!config_paths.is_empty());

        for config_path in config_paths {
            // The test checks that the stamp file gets written after successful validation, so
            // ensure the stamp file isn't already present on the filesystem
            delete_file(&stamp_path);

            let flags = Flags { input: config_path.clone(), stamp: stamp_path.clone() };
            match main_impl(flags) {
                Ok(()) => (),
                e => {
                    panic!(
                        "Valid config at path {:?} failed validation (err = {:?})",
                        config_path, e
                    )
                }
            };

            assert!(stamp_path.exists(), "Stamp file not written");
        }
    }

    /// Tests that invalid thermal configuration files fail the validation.
    #[test]
    fn test_invalid_thermal_config() {
        let stamp_path = stamp_path();

        // The test checks that the stamp file is not written if validation fails, so ensure the
        // stamp file isn't already present on the filesystem
        delete_file(&stamp_path);

        let config_paths = get_invalid_configs();
        assert!(!config_paths.is_empty());

        for config_path in config_paths {
            let flags = Flags { input: config_path.clone(), stamp: stamp_path.clone() };
            assert!(
                main_impl(flags).is_err(),
                "Invalid config at path {:?} passed validation",
                config_path
            );

            assert!(!stamp_path.exists(), "Stamp written after failed validation");
        }
    }
}
