// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use proc_macro_hack::proc_macro_hack;

#[proc_macro_hack]
pub use config_proc_macros::include_default;

#[macro_export]
macro_rules! ffx_cmd {
    () => {{
        #[cfg(test)]
        {
            ffx_args::Ffx {
                config: None,
                subcommand: ffx_args::Subcommand::Daemon(ffx_args::DaemonCommand {}),
            }
        }
        #[cfg(not(test))]
        {
            argh::from_env()
        }
    }};
}

#[macro_export]
macro_rules! get {
    (str, $key:expr, $default:expr) => {{
        ffx_config::get_config_str($key, $default, ffx_config::ffx_cmd!())
    }};
    (bool, $key:expr, $default:expr) => {{
        ffx_config::get_config_bool($key, $default, ffx_config::ffx_cmd!())
    }};
    ($key:expr) => {
        ffx_config::get_config($key, ffx_config::ffx_cmd!())
    };
}

#[macro_export]
macro_rules! remove {
    ($level:expr, $key:expr, $build_dir:expr) => {{
        ffx_config::remove_config_with_build_dir($level, $key, $build_dir, ffx_config::ffx_cmd!())
    }};
}

#[macro_export]
macro_rules! set {
    ($level:expr, $key:expr, $value:expr, $build_dir:expr) => {{
        ffx_config::set_config_with_build_dir(
            $level,
            $key,
            $value,
            $build_dir,
            ffx_config::ffx_cmd!(),
        )
    }};
}
