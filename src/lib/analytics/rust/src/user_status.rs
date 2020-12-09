// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use std::fs;
use std::fs::File;
use std::io::{BufRead, BufReader, Error, Write};
use std::ops::Deref;
use std::path::Path;
use uuid::Uuid;

pub use crate::env_info::*;

// TODO(fxbug.dev/66008): use a more explicit variable for the purpose rather
// than the output dir
const TEST_ENV_VAR: &'static str = "FUCHSIA_TEST_OUTDIR";

pub fn is_test_env() -> bool {
    std::env::var(TEST_ENV_VAR).is_ok()
}

pub fn is_new_user() -> bool {
    !analytics_status_file_exists()
}

pub fn is_opted_in() -> bool {
    return read_analytics_status();
}

pub fn opt_in_status(status: &bool) -> Result<()> {
    write_analytics_status(status);
    if !status {
        remove_uuid_file();
    }
    Ok(())
}

fn analytics_status_file_exists() -> bool {
    fs::metadata(Path::new(&analytics_status_path())).is_ok()
}

fn read_analytics_status() -> bool {
    read_boolean_from_file(analytics_status_path())
}

fn analytics_status_path() -> String {
    path_for_analytics_file("analytics-status")
}

fn read_boolean_from_file(status_file_path: String) -> bool {
    return match std::fs::read_to_string(status_file_path) {
        Ok(contents) => match contents.trim().parse::<u8>() {
            Ok(bool_val) => match bool_val {
                0 => false,
                1 => true,
                _ => false,
            },
            Err(e) => {
                // eprintln!("file value was not parseable as a boolean");
                false
            }
        },
        Err(e) => {
            // eprintln!("Could not read status file");
            false
        }
    };
}

fn write_analytics_status(status: &bool) {
    write_boolean_to_file(status, analytics_status_path())
}

fn write_boolean_to_file(status: &bool, status_file_path: String) {
    match fs::create_dir_all(analytics_folder()) {
        Ok(x) => match File::create(status_file_path) {
            Ok(mut output) => {
                let state = match status {
                    true => 1,
                    false => 0,
                };
                writeln!(output, "{}", state);
            }
            Err(e) => eprintln!("Could not open status file for writing. {:}", e),
        },
        Err(e) => eprintln!("Could not create directory for status files. {:}", e),
    }
}

pub fn uuid() -> String {
    match read_uuid() {
        Some(uuid) => uuid.to_string().to_owned(),
        None => {
            let uuid = Uuid::new_v4();
            write_uuid(uuid);
            uuid.to_string().to_owned()
        }
    }
}

fn read_uuid() -> Option<Uuid> {
    read_uuid_file(uuid_path())
}

fn write_uuid(uuid: Uuid) {
    write_uuid_file(uuid, uuid_path())
}

fn uuid_path() -> String {
    path_for_analytics_file("uuid")
}

fn remove_uuid_file() {
    let path = uuid_path();
    let uuid_file = Path::new(&path);
    if fs::metadata(&uuid_file).is_ok() {
        std::fs::remove_file(uuid_file);
    }
}

fn read_uuid_file(status_file_path: String) -> Option<Uuid> {
    return match std::fs::read_to_string(status_file_path) {
        Ok(contents) => match Uuid::parse_str(&contents.trim()) {
            Ok(uuid) => Some(uuid),
            Err(e) => None,
        },
        Err(e) => None,
    };
}

fn write_uuid_file(uuid: Uuid, status_file_path: String) {
    match fs::create_dir_all(analytics_folder()) {
        Ok(x) => match File::create(status_file_path) {
            Ok(mut output) => {
                writeln!(output, "{}", uuid);
            }
            Err(e) => eprintln!("Could not open status file for writing"),
        },
        Err(e) => eprintln!("Could not create directory for status files"),
    }
}

pub fn has_opened_ffx_previously() -> bool {
    return read_ffx_analytics_status();
}

pub fn set_has_opened_ffx() {
    write_ffx_analytics_status();
}

fn read_ffx_analytics_status() -> bool {
    read_boolean_from_file(ffx_analytics_status_path())
}

fn ffx_analytics_status_path() -> String {
    path_for_analytics_file("ffx")
}

fn write_ffx_analytics_status() {
    write_boolean_to_file(&true, ffx_analytics_status_path())
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    // Rust tests are run in parallel in threads, which means that this test is
    // disruptive to other tests. There's little ROI to doing some kind of fork
    // dance here, so the test is included, but not run by default.
    #[ignore]
    pub fn test_is_test_env() {
        std::env::set(TEST_ENV_VAR, "somepath");
        assert_eq!(true, is_test_env());
        std::env::remove_var(TEST_ENV_VAR);
        assert_eq!(false, is_test_env());
    }
}
