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
    use tempfile::TempDir;

    fn get_valid_configs() -> Vec<PathBuf> {
        get_configs("valid_thermal_test_configs")
    }

    fn get_invalid_configs() -> Vec<PathBuf> {
        get_configs("invalid_thermal_test_configs")
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

    // Gets the parent directory for this test executable (example:
    // out/core.nelson-release/host_x64/)
    fn get_test_directory() -> PathBuf {
        std::env::current_exe()
            .expect("Failed to get path of this executable")
            .parent()
            .expect("Failed to get current executable's parent directory")
            .to_path_buf()
    }

    // Helper for managing the temporary directory / path of the test-only stamp file
    struct TestStamp {
        // Keep ownership of the TempDir. Otherwise, when the TempDir is dropped the directory gets
        // deleted on the filesystem.
        _temp_dir: TempDir,

        // A valid path located inside the TempDir that we can use for the test-only stamp file
        path: PathBuf,
    }

    impl TestStamp {
        fn new() -> Self {
            let temp_dir = TempDir::new().expect("Failed to create TempDir");
            let path = temp_dir.path().to_path_buf().join("success.validated");
            Self { _temp_dir: temp_dir, path }
        }
    }

    /// Tests that valid thermal configuration files pass the validation.
    #[test]
    fn test_valid_thermal_config() {
        let config_paths = get_valid_configs();
        assert!(!config_paths.is_empty());

        for config_path in config_paths {
            let stamp = TestStamp::new();
            let flags = Flags { input: config_path.clone(), stamp: stamp.path.clone() };
            match main_impl(flags) {
                Ok(()) => (),
                e => {
                    panic!(
                        "Valid config at path {:?} failed validation (err = {:?})",
                        config_path, e
                    )
                }
            };

            assert!(stamp.path.exists(), "Stamp file not written");
        }
    }

    /// Tests that invalid thermal configuration files fail the validation.
    #[test]
    fn test_invalid_thermal_config() {
        let config_paths = get_invalid_configs();
        assert!(!config_paths.is_empty());

        for config_path in config_paths {
            let stamp = TestStamp::new();
            let flags = Flags { input: config_path.clone(), stamp: stamp.path.clone() };
            assert!(
                main_impl(flags).is_err(),
                "Invalid config at path {:?} passed validation",
                config_path
            );

            assert!(!stamp.path.exists(), "Stamp written after failed validation");
        }
    }
}
