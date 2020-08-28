// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{env::var, fs::create_dir_all, path::PathBuf};

pub use core_macros::{ffx_command, ffx_plugin};

pub fn get_base_path() -> PathBuf {
    let mut path = var("XDG_CONFIG_HOME").map(PathBuf::from).unwrap_or_else(|_| {
        let mut home = home::home_dir().expect("unknown home directory");
        home.push(".local");
        home.push("share");
        home
    });
    path.push("Fuchsia");
    path.push("ffx");
    create_dir_all(&path).expect("unable to create ffx directory");
    path
}
