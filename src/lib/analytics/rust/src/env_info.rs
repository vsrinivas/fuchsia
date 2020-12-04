// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use home::home_dir;
use nix::sys::utsname::uname;
use std::path::Path;
use std::path::PathBuf;

pub fn os_and_release_desc() -> String {
    if (cfg!(unix)) {
        let uname = uname();
        format!("{} {}", uname.sysname(), uname.machine())
    } else if cfg!(windows) {
        // TODO implement windows uname
        "Windows".to_string()
    } else {
        "unknown".to_string()
    }
}

pub fn path_for_analytics_file(status_file_name: &str) -> String {
    let analytics_dir = analytics_folder();
    let status_file_path = analytics_dir.to_owned() + status_file_name;
    status_file_path
}

pub fn analytics_folder() -> String {
    let mut metrics_path = get_home_dir();
    metrics_path.push(".fuchsia/metrics/");
    let path_str = metrics_path.to_str();
    match path_str {
        Some(v) => String::from(v),
        None => String::from("/tmp/.fuchsia/metrics/"),
    }
}

fn get_home_dir() -> PathBuf {
    match home_dir() {
        Some(dir) => dir,
        None => PathBuf::from("/tmp"),
    }
}
