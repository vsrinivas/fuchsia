// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use proc_macro_hack::proc_macro_hack;

#[proc_macro_hack]
pub use config_proc_macros::include_default;

#[macro_export]
macro_rules! get {
    (str, $key:expr, $default:expr) => {{
        ffx_config::get_config_str($key, $default)
    }};
    (str, $key:expr) => {{
        ffx_config::try_get_config_str($key)
    }};
    (bool, $key:expr, $default:expr) => {{
        ffx_config::get_config_bool($key, $default)
    }};
    (file, $key:expr) => {{
        ffx_config::try_get_config_file($key)
    }};
    (file_str, $key:expr) => {{
        ffx_config::try_get_config_file_str($key)
    }};
    (number, $key:expr, $default:expr) => {{
        ffx_config::get_config_number($key, $default)
    }};
    (number, $key:expr) => {{
        ffx_config::try_get_config_number($key)
    }};
    (sub, $key:expr) => {{
        ffx_config::get_config_sub($key)
    }};
    ($key:expr) => {
        ffx_config::get_config($key)
    };
}

#[macro_export]
macro_rules! remove {
    ($level:expr, $key:expr, $build_dir:expr) => {{
        ffx_config::remove_config_with_build_dir($level, $key, $build_dir)
    }};
}

#[macro_export]
macro_rules! set {
    ($level:expr, $key:expr, $value:expr, $build_dir:expr) => {{
        ffx_config::set_config_with_build_dir($level, $key, $value, $build_dir)
    }};
}

#[macro_export]
macro_rules! print {
    ($writer:expr, $build_dir:expr) => {{
        ffx_config::print_config($writer, $build_dir)
    }};
}
