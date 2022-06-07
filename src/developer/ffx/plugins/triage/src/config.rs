// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    std::{ffi::OsStr, fs::read_dir, path::PathBuf},
};

/// The default configs paths used if 0 paths are given.
/// These will be prefixed by ${FUCHSIA_DIR} to get the full config file path.
const DEFAULT_INTREE_RELATIVE_CONFIG_FILES: [&str; 2] =
    ["src/diagnostics/config/triage/", "src/diagnostics/config/triage/detect/"];

/// The default config paths variable in OOT invocation.
const DEFAULT_CONFIG_PATHS_VARIABLE: &str = "triage.config_paths";

/// Gets the closest enclosing fuchsia checkout.
/// We look for .jiri_root to determine whether a directory
/// is a fuchsia checkout.
/// Returns None if not working in tree.
fn get_closed_enclosing_fuchsia_dir(current_dir: PathBuf) -> Result<Option<PathBuf>> {
    let mut current_dir = current_dir;
    loop {
        let jiri_root_path = current_dir.join(".jiri_root/");
        if jiri_root_path.exists() {
            return Ok(Some(current_dir));
        }
        if !current_dir.pop() {
            break;
        }
    }
    Ok(None)
}

/// Returns the config files passed via cmdline arguments.
/// If 0 paths are given returns default paths.
pub async fn get_or_default_config_files(
    config: Vec<String>,
    current_dir: PathBuf,
) -> Result<Vec<PathBuf>> {
    let config_paths: Vec<PathBuf> = if config.is_empty() {
        if let Some(fuchsia_dir) = get_closed_enclosing_fuchsia_dir(current_dir)? {
            DEFAULT_INTREE_RELATIVE_CONFIG_FILES
                .iter()
                .map(|default_file| {
                    let mut dir = fuchsia_dir.clone();
                    dir.push(default_file);
                    dir
                })
                .collect()
        } else {
            // OOT default configs are expected to be set in triage.config_path variable.
            ffx_config::get(DEFAULT_CONFIG_PATHS_VARIABLE).await.context(format!(
                "Please set the default config using `ffx config set {} \"{}\"`.",
                DEFAULT_CONFIG_PATHS_VARIABLE, r#"[\"config1.triage\",\"default/config2.triage\"]"#
            ))?
        }
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
        fs,
        path::{Path, PathBuf},
    };
    use tempfile::{tempdir, NamedTempFile};

    fn create_empty_file(root: &Path, filename: &'static str) -> PathBuf {
        let file = root.join(filename);
        fs::File::create(&file).expect("Unable to create new file.");
        file
    }

    #[fuchsia::test]
    async fn innermost_fuchsia_dir_default_config() {
        let tempdir = tempdir().expect("Unable to create tempdir for testing.");
        let root = tempdir.path();

        let outer_jiri_root = root.join(".jiri_root");
        let inner_dir = root.join("fake");
        let innermost_checkout = inner_dir.join("fuchsia");
        let inner_jiri_root = innermost_checkout.join(".jiri_root");
        let inner_subdir = innermost_checkout.join("subdir");

        fs::create_dir_all(&outer_jiri_root).expect("Unable to create outer jiri root directory.");
        fs::create_dir_all(&inner_jiri_root).expect("Unable to create inner jiri root directory.");
        fs::create_dir_all(&inner_subdir).expect("Unable to create inner subdir root directory.");

        let canonical_inner_subdir = fs::canonicalize(&inner_subdir)
            .expect("Unable to get canonical path to fuchsia checkout.");

        // Directory structure is
        // $root/.jiri_root
        // $root/fake/fuchsia/
        // $root/fake/fuchsia/.jiri_root
        // We invoke the plugin from $root/fake/fuchsia/subdir/

        let config_files = get_or_default_config_files(vec![], canonical_inner_subdir)
            .await
            .expect("Unable to get config files.");

        // Get absolute path as $TMPDIR might contain symlinks.
        // Eg. on macOS /var@ -> /private/var
        // Hence for tempdir, env::current_dir()? will return /private/var/...
        // but tempdir() will return /var/... .
        let canonical_innermost_path = fs::canonicalize(&innermost_checkout)
            .expect("Unable to get canonical path to fuchsia checkout.");

        assert_eq!(
            config_files,
            &[
                canonical_innermost_path.join("src/diagnostics/config/triage/"),
                canonical_innermost_path.join("src/diagnostics/config/triage/detect/")
            ]
        );
    }

    #[fuchsia::test]
    async fn oot_default_config() {
        ffx_config::test_init().expect("Unable to initialize ffx_config.");

        let oot_test_default_configs =
            [Path::new("default/configs/a.triage"), Path::new("configs/b.triage")];

        let tempdir = tempdir().expect("Unable to create tempdir for testing.");
        let root = tempdir.path();

        ffx_config::set(
            (DEFAULT_CONFIG_PATHS_VARIABLE, ffx_config::ConfigLevel::User),
            serde_json::json!(oot_test_default_configs),
        )
        .await
        .expect("Unable to set oot default config variable.");

        let config_files = get_or_default_config_files(vec![], root.to_path_buf())
            .await
            .expect("Unable to get config files.");

        // One reason of failure might be the existence of .jiri_root/ in the $TEMPDIR parent
        // path due to which OOT configs are not picked up.
        assert_eq!(config_files, oot_test_default_configs);
    }

    #[fuchsia::test]
    async fn directory_passed_to_config() {
        let fake_cwd = tempdir().expect("Unable to create tempdir for testing.");

        let tempdir = tempdir().expect("Unable to create tempdir for testing.");
        let root = tempdir.path();
        let config_file1 = create_empty_file(root, "config1.triage");
        let _txt_file = create_empty_file(root, "not_config.txt");
        let _jpg_file = create_empty_file(root, "random_picture.jpg");

        let config_file2 =
            NamedTempFile::new().expect("Unable to create namedtempfile for testing.");

        let config_files = get_or_default_config_files(
            vec![root.to_string_lossy().into(), config_file2.path().to_string_lossy().into()],
            fake_cwd.path().to_path_buf(),
        )
        .await
        .expect("Unable to get config files.");

        assert_eq!(config_files, &[&config_file1, config_file2.path()]);
    }
}
