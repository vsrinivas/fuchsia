// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    glob::glob,
    log::{info, warn},
    std::fs,
};

const CURRENT_PATH: &str = "/cache/current";
const PREVIOUS_PATH: &str = "/cache/previous";

// Throw away stuff from two boots ago. Move stuff in the "current"
// directory to the "previous" directory.
pub fn shuffle_at_boot() {
    // These may fail if /cache was wiped. This is WAI and should not signal an error.
    fs::remove_dir_all(PREVIOUS_PATH)
        .map_err(|e| info!("Could not delete {}: {:?}", PREVIOUS_PATH, e))
        .ok();
    fs::rename(CURRENT_PATH, PREVIOUS_PATH)
        .map_err(|e| info!("Could not move {} to {}: {:?}", CURRENT_PATH, PREVIOUS_PATH, e))
        .ok();
}

// Write a VMO's contents to the appropriate file.
pub fn write(service_name: &str, tag: &str, data: &str) {
    // /cache/ may be deleted any time. It's OK to try to create CURRENT_PATH if it alreay exists.
    let path = format!("{}/{}", CURRENT_PATH, service_name);
    fs::create_dir_all(&path)
        .map_err(|e| warn!("Could not create directory {}: {:?}", path, e))
        .ok();
    fs::write(&format!("{}/{}", path, tag), data)
        .map_err(|e| warn!("Could not write file {}/{}: {:?}", path, tag, e))
        .ok();
}

// All the names in the previous-boot directory.
// TODO(fxbug.dev/71350): If this gets big, use Lazy Inspect.
pub fn remembered_data() -> Result<Vec<(String, Vec<(String, String)>)>, Error> {
    // Counter for number of tags successfully retrieved. If no persisted tags were
    // retrieved, this method returns an error.
    let mut tags_retrieved = 0;

    let mut service_entries = Vec::new();
    // Create an iterator over all subdirectories of /cache/previous
    // which contains persisted data from the last boot.
    for service_path in glob(&format!("{}/*", PREVIOUS_PATH))
        .map_err(|e| format_err!("Failed to read previous-path glob pattern: {:?}", e))?
    {
        match service_path {
            Err(e) => {
                // If our glob pattern was valid, but we encountered glob errors while iterating, just warn
                // since there may still be some persisted metrics.
                warn!(
                    "Encountered GlobError; contents could not be read to determine if glob pattern was matched: {:?}",
                    e
                )
            }
            Ok(path) => {
                if let Some(name) = path.file_name() {
                    let service_name = name.to_string_lossy().to_string();
                    let mut tag_entries = Vec::new();
                    for tag_path in
                        glob(&format!("{}/{}/*", PREVIOUS_PATH, service_name)).map_err(|e| {
                            format_err!(
                                "Failed to read previous service persistence pattern: {:?}",
                                e
                            )
                        })?
                    {
                        match tag_path {
                            Ok(path) => {
                                if let Some(tag_name) = path.file_name() {
                                    let tag_name = tag_name.to_string_lossy().to_string();
                                    match fs::read(path.clone()) {
                                        Ok(text) => {
                                            // TODO(cphoenix): We want to encode failures at retrieving persisted
                                            // metrics in the inspect hierarchy so clients know why their data is
                                            // missing.
                                            match std::str::from_utf8(&text) {
                                                Ok(contents) => {
                                                    tags_retrieved += 1;

                                                    tag_entries
                                                        .push((tag_name, contents.to_owned()));
                                                }
                                                Err(e) => {
                                                    warn!(
                                                        "Failed to parse persisted bytes at path: {:?} into text: {:?}",
                                                        path, e
                                                    );
                                                }
                                            }
                                        }
                                        Err(e) => {
                                            warn!(
                                            "Failed to retrieve text persisted at path: {:?}: {:?}",
                                            path, e
                                        );
                                        }
                                    }
                                }
                            }
                            Err(e) => {
                                // If our glob pattern was valid, but we encountered glob errors while iterating, just warn
                                // since there may still be some persisted metrics.
                                warn!(
                                        "Encountered GlobError; contents could not be read to determine if glob pattern was matched: {:?}",
                                        e
                                    )
                            }
                        }
                    }
                    service_entries.push((service_name, tag_entries));
                }
            }
        };
    }

    if tags_retrieved == 0 {
        info!("No persisted data was successfully retrieved.");
    }

    Ok(service_entries)
}
