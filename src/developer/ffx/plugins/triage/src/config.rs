// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Context, Result},
    std::{env, ffi::OsStr, fs::read_dir, path::PathBuf},
};

/// The default configs paths used if 0 paths are given.
/// These will be prefixed by ${FUCHSIA_DIR} to get the full config file path.
// TODO(fxbug.dev/95665): load default config path from ffx config.
const DEFAULT_RELATIVE_CONFIG_FILES: [&str; 2] =
    [("src/diagnostics/config/triage/"), ("src/diagnostics/config/triage/detect/")];

/// Gets $FUCHSIA_DIR variable from the environment.
/// Returns an error if variable is not set or contains invalid Unicode.
fn get_fuchsia_dir_from_env() -> Result<PathBuf> {
    match env::var_os("FUCHSIA_DIR") {
        Some(os_str) => os_str
            .into_string()
            .map(PathBuf::from)
            .map_err(|_| anyhow!("$FUCHSIA_DIR environment var contained invalid Unicode.")),
        None => bail!("Could not find $FUCHSIA_DIR variable set in the environment."),
    }
}

/// Returns the config files passed via cmdline arguments.
/// If 0 paths are given returns default paths.
pub fn get_or_default_config_files(config: Vec<String>) -> Result<Vec<PathBuf>> {
    let config_paths: Vec<PathBuf> = if config.is_empty() {
        let fuchsia_dir = get_fuchsia_dir_from_env()?;

        DEFAULT_RELATIVE_CONFIG_FILES
            .iter()
            .map(|default_file| {
                let mut dir = fuchsia_dir.clone();
                dir.push(default_file);
                dir
            })
            .collect()
    } else {
        config.into_iter().map(PathBuf::from).collect()
    };

    let mut config_files = Vec::new();

    for config_path in config_paths {
        // If path is a directory, all files in that directory
        // with an extension of "triage" are taken.
        if config_path.is_dir() {
            let dir_entries = read_dir(&config_path)
                .context(format!("Could not read directory {}.", config_path.display()))?;

            config_files.extend(dir_entries.into_iter().filter_map(|dir_entry| {
                if let Some(path) = dir_entry.ok().map(|entry| entry.path()) {
                    if path.extension().and_then(OsStr::to_str) == Some("triage") {
                        return Some(path);
                    }
                }
                None
            }));
        } else {
            config_files.push(config_path);
        }
    }

    Ok(config_files)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::{
        env, fs,
        path::{Path, PathBuf},
    };
    use tempfile::{tempdir, NamedTempFile};

    fn create_empty_file(root: &Path, filename: &'static str) -> PathBuf {
        let file = root.join(filename);
        fs::File::create(&file).expect("Unable to create new file.");
        file
    }

    #[fuchsia::test]
    fn default_config() {
        env::set_var("FUCHSIA_DIR", "/fake/fuchsia/");
        let config_files =
            get_or_default_config_files(vec![]).expect("Unable to get config files.");

        assert_eq!(
            config_files,
            &[
                Path::new("/fake/fuchsia/src/diagnostics/config/triage/"),
                Path::new("/fake/fuchsia/src/diagnostics/config/triage/detect/")
            ]
        );
    }

    #[fuchsia::test]
    fn directory_passed_to_config() {
        let tempdir = tempdir().expect("Unable to create tempdir for testing.");
        let root = tempdir.path();
        let config_file1 = create_empty_file(root, "config1.triage");
        let _txt_file = create_empty_file(root, "not_config.txt");
        let _jpg_file = create_empty_file(root, "random_picture.jpg");

        let config_file2 =
            NamedTempFile::new().expect("Unable to create namedtempfile for testing.");

        let config_files = get_or_default_config_files(vec![
            root.to_string_lossy().into(),
            config_file2.path().to_string_lossy().into(),
        ])
        .expect("Unable to get config files.");

        assert_eq!(config_files, &[&config_file1, config_file2.path()]);
    }
}
