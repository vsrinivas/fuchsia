// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {glob::glob, log::warn, std::fs};

const CURRENT_PATH: &str = "/cache/current";
const PREVIOUS_PATH: &str = "/cache/previous";

// Throw away stuff from two boots ago. Move stuff in the "current"
// directory to the "previous" directory.
pub fn shuffle_at_boot() {
    // These may fail if /cache was wiped. This is WAI and should not signal an error.
    fs::remove_dir_all(PREVIOUS_PATH).ok();
    fs::rename(CURRENT_PATH, PREVIOUS_PATH).ok();
}

// Write a VMO's contents to the appropriate file.
pub fn write(service_name: &str, tag: &str, data: &str) {
    // /cache/ may be deleted any time. It's OK to try to create CURRENT_PATH if it alreay exists.
    let path = format!("{}/{}", CURRENT_PATH, service_name);
    fs::create_dir_all(&path).ok();
    fs::write(&format!("{}/{}", path, tag), data).ok();
}

// All the names in the previous-boot directory.
// TODO(fxbug.dev/71350): If this gets big, use Lazy Inspect.
pub fn remembered_data() -> Vec<(String, Vec<(String, String)>)> {
    let mut service_entries = Vec::new();
    for service_path in glob(&format!("{}/*", PREVIOUS_PATH)).unwrap() {
        if let Ok(service_path) = service_path {
            let service_name = service_path.file_name().unwrap().to_string_lossy().to_string();
            let mut tag_entries = Vec::new();
            for tag_path in glob(&format!("{}/{}/*", PREVIOUS_PATH, service_name)).unwrap() {
                if let Ok(tag_path) = tag_path {
                    let tag_name = tag_path.file_name().unwrap().to_string_lossy().to_string();
                    match fs::read(tag_path.clone()) {
                        Ok(text) => {
                            // TODO(lukenicholson): We want to encode failures at retrieving persisted
                            // metrics in the elephant inspect hierarchy so clients know why their data is
                            // missing.
                            match std::str::from_utf8(&text) {
                                Ok(contents) => {
                                    tag_entries.push((tag_name, contents.to_owned()));
                                }
                                Err(e) => {
                                    warn!(
                                        "Failed to parse persisted bytes at path: {:?} into text: {:?}",
                                        tag_path, e
                                    );
                                }
                            }
                        }
                        Err(e) => {
                            warn!(
                                "Failed to retrieve text persisted at path: {:?}: {:?}",
                                tag_path, e
                            );
                        }
                    }
                }
            }
            service_entries.push((service_name, tag_entries));
        }
    }
    service_entries
}
